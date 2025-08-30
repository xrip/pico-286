#pragma once
#include <stdint.h>

extern uint8_t vga_planes[4][65536];

uint8_t vga_mem_read(uint32_t address);
void vga_mem_write(uint32_t address, uint8_t value);
