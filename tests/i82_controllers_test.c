#include <assert.h>
#include <stdint.h>
#include <string.h>

#define PICO_ON_DEVICE 0
#define I2S_SOUND 1
#define HARDWARE_SOUND 0

static uint8_t test_memory[1u << 20];
#define I8237_MEMORY_READ(address) test_memory[(address) & 0xFFFFFu]
#define I8237_MEMORY_WRITE(address, value) (test_memory[(address) & 0xFFFFFu] = (value))

#include "../src/emulator/i8259.h"
#include "../src/emulator/i8253.h"
#include "../src/emulator/i8237.h"

i8259_s i8259;
i8253_s i8253;
dma_channel_s dma_channels[DMA_CHANNELS];
uint8_t i8237_byte_flipflop;
uint8_t port61;
int speakerenabled;
int timer_period;

uint64_t i8253_now_us(void) {
    return 1000000;
}

static void test_i8259(void) {
    memset(&i8259, 0, sizeof(i8259));
    i8259.interrupt_mask_register = 0xFF;
    i8259.interrupt_vector_offset = 0x08;

    i8259_write(0x20, 0x11);
    i8259_write(0x21, 0x08);
    i8259_write(0x21, 0x00);
    i8259_write(0x21, 0x01);
    i8259_write(0x21, 0x00);

    i8259_interrupt(3);
    assert(i8259_get_pending_irqs() == (1u << 3));
    assert(i8259_nextirq() == 0x0B);
    assert(i8259.in_service_register == (1u << 3));

    i8259_write(0x20, 0x20);
    assert(i8259.in_service_register == 0);
}

static void test_i8253(void) {
    memset(&i8253, 0, sizeof(i8253));

    i8253_write(0x43, 0x36);
    i8253_write(0x40, 0x34);
    assert(i8253.channels[0].active == 0);
    i8253_write(0x40, 0x12);

    assert(i8253.channels[0].active == 1);
    assert(i8253.channels[0].reload_value == 0x1234);
    assert(i8253.channels[0].operating_mode == 3);
    assert(timer_period == PIT_FREQUENCY / 0x1234);
}

static void test_i8237(void) {
    memset(test_memory, 0, sizeof(test_memory));
    i8237_reset();
    for (unsigned channel = 0; channel < DMA_CHANNELS; ++channel) {
        assert(dma_channels[channel].masked == 1);
    }

    i8237_writeport(DMA_CLEAR_FF, 0);
    i8237_writeport(0x04, 0x34);
    i8237_writeport(0x04, 0x12);
    i8237_writeport(DMA_CLEAR_FF, 0);
    i8237_writeport(0x05, 0x01);
    i8237_writeport(0x05, 0x00);
    i8237_writepage(0x81, 0x01);
    i8237_writeport(DMA_MODE_REGISTER, 0x1A);
    i8237_writeport(DMA_CHANNEL_MASK_REGISTER, 0x02);

    assert(dma_channels[2].address == 0x1234);
    assert(dma_channels[2].reload_address == 0x1234);
    assert(dma_channels[2].count == 1);
    assert(dma_channels[2].reload_count == 1);
    assert(i8237_readpage(0x81) == 1);

    test_memory[0x11234] = 0xA5;
    assert(i8237_read(2) == 0xA5);
    assert(dma_channels[2].address == 0x1235);
    assert(dma_channels[2].count == 0);

    test_memory[0x11235] = 0x5A;
    assert(i8237_read(2) == 0x5A);
    assert(dma_channels[2].address == 0x1234);
    assert(dma_channels[2].count == 1);
    assert(i8237_readport(DMA_STATUS_REGISTER) == (1u << 6));
    assert(i8237_readport(DMA_STATUS_REGISTER) == 0);
}

int main(void) {
    test_i8259();
    test_i8253();
    test_i8237();
    return 0;
}
