// The CPU alone on the host: flat 16 MB memory and stubs for the rest of the emulator.
// For the CPU test runners (sst_test.c, cpu_bench.c); include it once, before anything that uses printf.
#pragma once
#include "emulator.h"
// The CPU prints through printf_ (src/printf); keep it quiet, and print our results with stdio.
#undef printf
#undef snprintf
void _putchar(char character) {}

// Flat 16 MB memory, as the tests expect, in place of the PC memory map.
static uint8_t memory[1 << 24];
#define PHYSICAL(address) ((address) & 0xFFFFFF)
// Every written address is kept, so clear_memory() can put memory back to zeros quickly.
enum { WRITE_LOG_SIZE = 4096 };
static uint32_t write_log[WRITE_LOG_SIZE];
static int write_count; // stops at WRITE_LOG_SIZE + 1: the log is full

static uint8_t flat_read(uint32_t address) { return memory[PHYSICAL(address)]; }
static uint16_t flat_readw(uint32_t address) { return flat_read(address) | flat_read(address + 1) << 8; }
static uint32_t flat_readdw(uint32_t address) { return flat_readw(address) | (uint32_t) flat_readw(address + 2) << 16; }
static void flat_write(uint32_t address, uint8_t value) {
    memory[PHYSICAL(address)] = value;
    if (write_count < WRITE_LOG_SIZE) write_log[write_count] = PHYSICAL(address);
    if (write_count <= WRITE_LOG_SIZE) write_count++;
}
static void clear_memory(void) {
    if (write_count > WRITE_LOG_SIZE) memset(memory, 0, sizeof memory);
    else for (int i = 0; i < write_count; i++) memory[write_log[i]] = 0;
    write_count = 0;
}
static void flat_writew(uint32_t address, uint16_t value) { flat_write(address, value); flat_write(address + 1, value >> 8); }
static void flat_writedw(uint32_t address, uint32_t value) { flat_writew(address, value); flat_writew(address + 2, value >> 16); }
read86_t read86 = flat_read;
read86w_t readw86 = flat_readw;
read86dw_t readdw86 = flat_readdw;
write86_t write86 = flat_write;
write86w_t writew86 = flat_writew;
write86dw_t writedw86 = flat_writedw;
void move_linear(uint32_t destination, uint32_t source, uint32_t length) {}

// What cpu.c needs from the rest of the emulator. The tests expect FFh from every port.
uint16_t portin(uint16_t port) { return 0xFF; }
uint16_t portin16(uint16_t port) { return 0xFFFF; }
uint16_t last_out_port, last_out_value; // for the test386 runner (POST codes)
void portout(uint16_t port, uint16_t value) {
    last_out_port = port;
    last_out_value = value;
}
void portout16(uint16_t port, uint16_t value) {}
void keyboard_pump(void) {}
uint8_t xms_handler() { return 0xCB; }
void xms_reset() {}
void vga_init(void) {}
void vga_set_cpu_page_offset(uint32_t offset) {}
void tga_portout(uint16_t port, uint16_t value) {}
void tga_draw_char(uint8_t ch, int x, int y, uint8_t color) {}
int a20_enabled;
uint8_t cga_blinking, cga_blinking_lock, crt_controller[32], tga_palette_map[16], vga_planar_mode, i8237_byte_flipflop;
uint32_t tga_offset, vga_palette[256], VIDEORAM[VIDEORAM_SIZE];
uint8_t RAM[RAM_SIZE], UMB[UMB_END - UMB_START], EXTENDED[EXTENDED_END - HMA_START];
dma_channel_s dma_channels[DMA_CHANNELS];
i8259_s i8259;
