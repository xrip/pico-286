#include <assert.h>
#include <stdint.h>
#include <string.h>

#define PICO_ON_DEVICE 0

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
int timer_period;

static uint64_t now_us = 1000000;

uint64_t i8253_now_us(void) {
    return now_us;
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

    // fully nested mode: an IRQ waits while the same or a higher-priority IRQ is in service
    i8259_interrupt(3);
    assert(i8259_nextirq() == 0x0B);
    i8259_interrupt(3);
    i8259_interrupt(5);
    assert(i8259_get_pending_irqs() == 0);
    i8259_interrupt(1);
    assert(i8259_get_pending_irqs() == (1u << 1));
    assert(i8259_nextirq() == 0x09);
    i8259_write(0x20, 0x20); // EOI ends IRQ 1; IRQ 3 is still in service
    assert(i8259.in_service_register == (1u << 3));
    assert(i8259_get_pending_irqs() == 0);
    i8259_write(0x20, 0x20);
    assert(i8259_get_pending_irqs() == ((1u << 3) | (1u << 5)));

    // ICW1 clears pending and in-service IRQs
    assert(i8259_nextirq() == 0x0B);
    i8259_write(0x20, 0x11);
    assert(i8259.interrupt_request_register == 0);
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

    // mode 3 counts down by two in each half period
    i8253_write(0x43, 0x36);
    i8253_write(0x40, 0x00);
    i8253_write(0x40, 0x10); // divisor 0x1000 starts now
    const uint64_t start = now_us;
    now_us = start + 100; // 119 PIT ticks: high half
    assert(i8253_read(0x40) == (uint8_t) (0x1000 - 2 * 119));
    assert(i8253_read(0x40) == (uint8_t) ((0x1000 - 2 * 119) >> 8));
    now_us = start + 1725; // 2058 PIT ticks: 10 ticks into the low half
    assert(i8253_read(0x40) == (uint8_t) (0x1000 - 2 * 10));
    assert(i8253_read(0x40) == (uint8_t) ((0x1000 - 2 * 10) >> 8));

    // a latched count is kept until both of its bytes are read
    i8253_write(0x43, 0x34); // channel 0, lo/hi, mode 2
    i8253_write(0x40, 0x00);
    i8253_write(0x40, 0x10);
    const uint64_t latch_start = now_us;
    now_us = latch_start + 100; // 119 ticks
    i8253_write(0x43, 0x00);    // latch channel 0
    const uint8_t latched_low = i8253_read(0x40);
    now_us = latch_start + 1100; // 1312 ticks: the live count has moved on
    const uint16_t latched = latched_low | (uint16_t) (i8253_read(0x40) << 8);
    assert(latched == 0x1000 - 1 - 119);
    const uint8_t live_low = i8253_read(0x40);
    const uint16_t live = live_low | (uint16_t) (i8253_read(0x40) << 8);
    assert(live == 0x1000 - 1 - 1312);
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
    assert(i8237_readport(DMA_STATUS_REGISTER) == (1u << 2)); // TC of channel 2
    assert(i8237_readport(DMA_STATUS_REGISTER) == 0);

    i8237_writeport(DMA_COMMAND_REGISTER, 0x00);
    assert(dma_channels[2].auto_init == 1);

    i8237_writepage(0x87, 0x03);
    i8237_writepage(0x83, 0x02);
    assert(dma_channels[0].page == 0x30000);
    assert(dma_channels[1].page == 0x20000);
    assert(i8237_readpage(0x83) == 2);
}

int main(void) {
    test_i8259();
    test_i8253();
    test_i8237();
    return 0;
}
