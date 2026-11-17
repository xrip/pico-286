// Host test for the Sound Blaster DSP. Build and run from the project root:
//   gcc -std=gnu17 -fms-extensions -O2 -Isrc -Isrc/emu8950 tests/sb_test.c -o tests/sb_test.exe && tests/sb_test.exe
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t guest_memory[0x10000];
#define I8237_MEMORY_READ(address) guest_memory[(address) & 0xFFFF]
#define I8237_MEMORY_WRITE(address, value) (guest_memory[(address) & 0xFFFF] = (value))

#include "../src/emulator/emulator.h"
#include "../src/emulator/audio/sound_blaster.c.inl"

dma_channel_s dma_channels[DMA_CHANNELS];
uint8_t i8237_byte_flipflop;
i8259_s i8259;

#define SB_BASE 0x220

static void dsp_write(const uint8_t value) { blaster_write(SB_BASE + DSP_WRITE, value); }
static uint8_t dsp_read(void) { return blaster_read(SB_BASE + DSP_READ); }

static void dsp_write_length(const uint8_t command, const uint16_t length) {
    dsp_write(command);
    dsp_write((uint8_t) (length - 1));
    dsp_write((uint8_t) ((length - 1) >> 8));
}

static bool take_irq(void) {
    const bool raised = i8259.interrupt_request_register & (1u << 5); // SB Pro: IRQ 5
    i8259.interrupt_request_register = 0;
    return raised;
}

// Fresh card, speaker on, DMA channel open over the whole test memory.
static void setup(void) {
    memset(guest_memory, 0, sizeof(guest_memory));
    memset(dma_channels, 0, sizeof(dma_channels));
    dma_channels[SB_DMA_CHANNEL].address_increase = 1;
    dma_channels[SB_DMA_CHANNEL].count = 0xFFFF;
    blaster_write(SB_BASE + DSP_RESET, 1);
    assert(dsp_read() == 0xAA);
    dsp_write(DSP_ENABLE_SPEAKER);
    take_irq();
}

// Takes samples till the SB raises its IRQ; gives back how many it took.
static int samples_till_irq(const int limit) {
    for (int count = 1; count <= limit; count++) {
        blaster_sample();
        if (take_irq()) return count;
    }
    return -1;
}

static void test_version(void) {
    setup();
    dsp_write(DSP_VERSION);
    assert(dsp_read() == 3);
    assert(dsp_read() == 2);
}

static void test_time_constant(void) {
    setup();
    dsp_write(DSP_SET_TIME_CONSTANT);
    dsp_write(211); // about 22 kHz
    assert(timeconst == 45);
}

static void test_single_cycle_dma(void) {
    setup();
    guest_memory[0] = 0x80 + 1;
    dsp_write_length(DSP_DMA_SINGLE, 4);
    assert(blaster_sample() == 1 << 6);
    assert(samples_till_irq(10) == 3);
    assert(!sound_blaster.dma_transfer_enabled);
}

static void test_auto_init_dma_uses_block_size(void) {
    setup();
    dsp_write_length(DSP_DMA_BLOCK_SIZE, 5);
    dsp_write_length(DSP_DMA_SINGLE, 2); // must not change the block size
    assert(samples_till_irq(10) == 2);
    dsp_write(DSP_DMA_AUTO);
    assert(samples_till_irq(10) == 5);
    assert(samples_till_irq(10) == 5);
    dsp_write(DSP_DMA_EXIT_AUTO);
    assert(samples_till_irq(10) == -1);
}

static void test_high_speed_dma(void) {
    setup();
    dsp_write_length(DSP_DMA_BLOCK_SIZE, 3);
    dsp_write(DSP_DMA_HS_SINGLE); // no length bytes: the next byte is a new command
    dsp_write(DSP_VERSION);
    assert(dsp_read() == 3);
    assert(samples_till_irq(10) == 3);
    assert(!sound_blaster.dma_transfer_enabled);

    setup();
    dsp_write_length(DSP_DMA_BLOCK_SIZE, 3);
    dsp_write(DSP_DMA_HS_AUTO);
    assert(samples_till_irq(10) == 3);
    assert(samples_till_irq(10) == 3);
}

static void test_record_writes_silence(void) {
    setup();
    dsp_write_length(DSP_DMA_ADC_SINGLE, 2);
    assert(samples_till_irq(10) == 2);
    assert(guest_memory[0] == 128 && guest_memory[1] == 128);
}

static void mixer_write(const uint8_t index, const uint8_t value) {
    blaster_write(SB_BASE + MIXER_INDEX, index);
    blaster_write(SB_BASE + MIXER_DATA, value);
}

static uint8_t mixer_read(const uint8_t index) {
    blaster_write(SB_BASE + MIXER_INDEX, index);
    return blaster_read(SB_BASE + MIXER_DATA);
}

static void test_mixer(void) {
    setup();
    mixer_write(0x22, 0x55);
    assert(mixer_read(0x22) == 0x55);
    assert(blaster_read(SB_BASE + MIXER_INDEX) == 0x22);

    blaster_write(SB_BASE + DSP_RESET, 1); // a DSP reset keeps the mixer
    assert(mixer_read(0x22) == 0x55);

    mixer_write(MIXER_RESET, 0);
    assert(mixer_read(0x22) == 0x99);
    assert(mixer_read(0x04) == 0x99);
    assert(mixer_read(0x0E) == 0x00);
}

int main(void) {
    test_version();
    test_time_constant();
    test_single_cycle_dma();
    test_auto_init_dma_uses_block_size();
    test_high_speed_dma();
    test_record_writes_silence();
    test_mixer();
    puts("sb_test: all passed");
    return 0;
}
