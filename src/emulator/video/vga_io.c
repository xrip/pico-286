#include "emulator/emulator.h"
#include "emulator/video/vga_io.h"

// Defined in vga.c
extern uint8_t vga_sequencer[5];
extern uint8_t vga_graphics_control[9];

// The 4 VGA planes, each 64KB
uint8_t vga_planes[4][65536];
uint8_t vga_latch[4];

uint8_t vga_mem_read(uint32_t address) {
    address -= 0xA0000;

    // Latch the data from all planes
    for (int i = 0; i < 4; i++) {
        vga_latch[i] = vga_planes[i][address];
    }

    if ((vga_graphics_control[5] & 0x08) == 0) { // Read Mode 0
        uint8_t plane = vga_graphics_control[4] & 0x03;
        return vga_latch[plane];
    } else { // Read Mode 1
        uint8_t color_compare = vga_graphics_control[2];
        uint8_t color_dont_care = vga_graphics_control[7];
        uint8_t result = 0;

        for (int i = 0; i < 8; i++) {
            uint8_t pixel_color = 0;
            if ((vga_latch[0] >> i) & 1) pixel_color |= 1;
            if ((vga_latch[1] >> i) & 1) pixel_color |= 2;
            if ((vga_latch[2] >> i) & 1) pixel_color |= 4;
            if ((vga_latch[3] >> i) & 1) pixel_color |= 8;

            if (((pixel_color ^ color_compare) & ~color_dont_care & 0x0F) == 0) {
                result |= (1 << i);
            }
        }
        return result;
    }
}

void vga_mem_write(uint32_t address, uint8_t value) {
    address -= 0xA0000;
    uint8_t write_mode = vga_graphics_control[5] & 0x03;
    uint8_t plane_mask = vga_sequencer[2];
    uint8_t bit_mask = vga_graphics_control[8];

    // Perform a read to fill the latches
    vga_mem_read(address + 0xA0000);

    uint8_t rotated_val = value;
    if (write_mode == 0 || write_mode == 3) {
        uint8_t rotate_count = vga_graphics_control[3] & 0x07;
        if (rotate_count > 0) {
            rotated_val = (value >> rotate_count) | (value << (8 - rotate_count));
        }
    }

    for (int i = 0; i < 4; i++) {
        if (!((plane_mask >> i) & 1)) {
            continue;
        }

        uint8_t new_data = 0;
        switch (write_mode) {
            case 0: {
                uint8_t enable_set_reset = vga_graphics_control[1];
                if ((enable_set_reset >> i) & 1) {
                    uint8_t set_reset_bit = (vga_graphics_control[0] >> i) & 1;
                    new_data = (set_reset_bit) ? 0xFF : 0x00;
                } else {
                    new_data = rotated_val;
                }
                break;
            }
            case 1:
                new_data = vga_latch[i];
                break;
            case 2:
                new_data = ((value >> i) & 1) ? 0xFF : 0x00;
                break;
            case 3:
                new_data = (vga_graphics_control[3] & 0x18) ? (rotated_val & bit_mask) : bit_mask;
                break;
        }

        uint8_t alu_func = (vga_graphics_control[3] >> 3) & 0x03;
        uint8_t alu_result;
        switch(alu_func) {
            case 0: // Unchanged
                alu_result = new_data;
                break;
            case 1: // AND
                alu_result = new_data & vga_latch[i];
                break;
            case 2: // OR
                alu_result = new_data | vga_latch[i];
                break;
            case 3: // XOR
                alu_result = new_data ^ vga_latch[i];
                break;
        }

        vga_planes[i][address] = (vga_latch[i] & ~bit_mask) | (alu_result & bit_mask);
    }
}
