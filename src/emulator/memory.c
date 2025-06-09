#pragma GCC optimize("Ofast")

#include "includes/bios.h"
#include "emulator.h"


#include "emulator/ems.c.inl"

#if PICO_ON_DEVICE
#include "pico.h"
uint8_t * PSRAM_DATA = (uint8_t*)0x11000000;
uint8_t ALIGN(4, RAM[RAM_SIZE + 4]) = {0};
uint8_t ALIGN(4, VIDEORAM[VIDEORAM_SIZE + 4]) = {0};


// Writes a byte to the virtual memory
void __time_critical_func() write86(uint32_t address, uint8_t value) {
    if (address < RAM_SIZE) {
        RAM[address] = value;
    } else if (address < VIDEORAM_START) {
        write8psram(address, value);
    } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        // Check for EGA/VGA planar mode
        if (vga_planar_mode == 2 || vga_planar_mode == 4) { // Or other conditions indicating planar mode
            uint8_t map_mask = vga_sequencer[2];
            uint8_t write_mode = vga_graphics_control[5] & 0x03;
            uint8_t data_rotate_count = vga_graphics_control[3] & 0x07;
            // uint8_t rotate_function = (vga_graphics_control[3] >> 3) & 0x03; // Ignoring for now
            uint8_t bit_mask = vga_graphics_control[8];
            uint8_t set_reset_data = vga_graphics_control[0];
            uint8_t enable_set_reset = vga_graphics_control[1];
            // uint8_t read_mode = (vga_graphics_control[5] >> 3) & 0x01; // Ignoring color compare for writes for now

            uint32_t vram_offset = address - VIDEORAM_START;
            uint8_t cpu_data = value;
            uint8_t rotated_data = (cpu_data >> data_rotate_count) | (cpu_data << (8 - data_rotate_count));

            for (int plane = 0; plane < 4; ++plane) {
                if (map_mask & (1 << plane)) {
                    uint32_t current_plane_base_addr = plane * vga_plane_size;
                    // Ensure the offset calculation is within the bounds of a single plane when added to current_plane_base_addr
                    // The vram_offset should be relative to the start of the linear VGA buffer (0xA0000)
                    // and then be used to address into the specific plane.
                    // The modulo VIDEORAM_SIZE should be applied to the final plane address if planes wrap around within VIDEORAM total size,
                    // or vga_plane_size if each plane is a distinct block.
                    // Given vga_plane_offset is used in the original code, it implies a linear mapping that might be adjusted by sequencer.
                    // For planar modes, vram_offset is the direct offset in the 64k bank.
                    uint32_t current_plane_addr = current_plane_base_addr + vram_offset;

                    // Make sure current_plane_addr is within the total VIDEORAM bounds
                    // This check might need refinement based on how planes are mapped in VIDEORAM
                    if (current_plane_addr >= VIDEORAM_SIZE) {
                        // This case should ideally not happen if vram_offset and vga_plane_size are correct
                        // and mem_address is within 0xA0000-0xAFFFF (for typical 64k segment)
                        continue;
                    }

                    uint8_t vram_byte = VIDEORAM[current_plane_addr]; // Read current byte from VRAM for logical ops
                    uint8_t plane_data_to_write;

                    if (enable_set_reset & (1 << plane)) {
                        plane_data_to_write = (set_reset_data & (1 << plane)) ? 0xFF : 0x00;
                    } else {
                        uint8_t data_for_op = rotated_data;
                        switch (write_mode) {
                            case 0:
                                plane_data_to_write = data_for_op;
                                break;
                            case 1: // VRAM data is latched, then ANDed with rotated_data
                                plane_data_to_write = vram_byte & data_for_op;
                                break;
                            case 2: // Write rotated_data ORed with latched VRAM data
                                plane_data_to_write = vram_byte | data_for_op;
                                break;
                            case 3: // Write rotated_data XORed with latched VRAM data
                                plane_data_to_write = vram_byte ^ data_for_op;
                                break;
                            default: // Should not happen
                                plane_data_to_write = data_for_op;
                                break;
                        }
                    }
                    VIDEORAM[current_plane_addr] = (plane_data_to_write & bit_mask) | (vram_byte & ~bit_mask);
                }
            }
        } else {
            // Default VGA write (linear or simple map, uses vga_plane_offset from sequencer)
            // The original calculation was (vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE
            // vga_plane_offset is usually 0 for graphics modes, but can be set by sequencer for specific mappings.
            // For standard VGA modes like 13h (linear), vga_plane_offset would be 0.
            // For planar modes, direct addressing into planes is handled above.
            // This path should handle non-planar modes or modes where vga_planar_mode is not set.
            VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE] = value;
        }
    } else if (address >= EMS_START && address < EMS_END) {
        ems_write(address - EMS_START, value);
    } else if (address >= UMB_START && address < UMB_END) {
        write8psram(address, value);
    } else if (address >= HMA_START && address < HMA_END) {
        write8psram(address, value);
    }
}

// Writes a word to the virtual memory
void __time_critical_func() writew86(uint32_t address, uint16_t value) {
    if (address & 1) {
        write86(address, value);
        write86(address + 1, value >> 8);
    } else {
        if (address < RAM_SIZE) {
            *(uint16_t *) &RAM[address] = value;
        } else if (address < VIDEORAM_START) {
            write16psram(address, value);
        } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
            *(uint16_t *) &VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE] = value;
        } else if (address >= EMS_START && address < EMS_END) {
            ems_writew(address - EMS_START, value);
        } else if (address >= UMB_START && address < UMB_END) {
            write16psram(address, value);
        } else if (address >= HMA_START && address < HMA_END) {
            write16psram(address, value);
        }
    }
}

// Writes a dword to the virtual memory
void __time_critical_func() writedw86(uint32_t address, uint32_t value) {
    if (address & 3) {
        write86(address, value);
        write86(address + 1, value >> 8);
        write86(address + 2, value >> 16);
        write86(address + 3, value >> 24);
    } else {
        if (address < RAM_SIZE) {
            *(uint32_t *) &RAM[address] = value;
        } else if (address < VIDEORAM_START) {
            write32psram(address, value);
        } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
            *(uint32_t *) &VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE] = value;
        } else if (address >= EMS_START && address < EMS_END) {
            ems_writedw(address - EMS_START, value);
        } else if (address >= UMB_START && address < UMB_END) {
            write32psram(address, value);
        } else if (address >= HMA_START && address < HMA_END) {
            write32psram(address, value);
        }
    }
}

// Reads a byte from the virtual memory
uint8_t __time_critical_func() read86(uint32_t address) {
    if (address < RAM_SIZE) {
        return RAM[address];
    }
    if (address < VIDEORAM_START) {
        return read8psram(address);
    }
    if (unlikely(address >= VIDEORAM_START && address < VIDEORAM_END)) {
        return VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
    }
    if (address >= EMS_START && address < EMS_END) {
        return ems_read(address - EMS_START);
    }
    if (address >= UMB_START && address < UMB_END) {
        return read8psram(address);
    }
    if (unlikely(address == 0xFC000)) {
        return 0x21;
    }
    if (unlikely(address >= BIOS_START && address < HMA_START)) {
        return BIOS[address - BIOS_START];
    }
    if (address >= HMA_START && address < HMA_END) {
        return read8psram(address);
    }
    return 0xFF;
}

// Reads a word from the virtual memory
uint16_t __time_critical_func() readw86(uint32_t address) {
    if (address & 1) {
        return (uint16_t) read86(address) | ((uint16_t) read86(address + 1) << 8);
    }
    if (address < RAM_SIZE) {
        return *(uint16_t *) &RAM[address];
    }
    if (address < VIDEORAM_START) {
        return read16psram(address);
    }
    if (unlikely(address >= VIDEORAM_START && address < VIDEORAM_END)) {
        return *(uint16_t *) &VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
    }
    if (address >= EMS_START && address < EMS_END) {
        return ems_readw(address - EMS_START);
    }
    if (address >= UMB_START && address < UMB_END) {
        return read16psram(address);
    }
    if (unlikely(address >= BIOS_START && address < HMA_START)) {
        return *(uint16_t *) &BIOS[address - BIOS_START];
    }
    if (address >= HMA_START && address < HMA_END) {
        return read16psram(address);
    }
    return 0xFFFF;
}

// Reads a dword from the virtual memory
uint32_t __time_critical_func() readdw86(uint32_t address) {
    if (address & 3) {
        return (uint32_t) read86(address) | ((uint32_t) read86(address + 1) << 8) |
               ((uint32_t) read86(address + 2) << 16) | ((uint32_t) read86(address + 3) << 24);
    }
    if (address < RAM_SIZE) {
        return *(uint32_t *) &RAM[address];
    }
    if (address < VIDEORAM_START) {
        return read32psram(address);
    }
    if (unlikely(address >= VIDEORAM_START && address < VIDEORAM_END)) {
        return *(uint32_t *) &VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
    }
    if (address >= EMS_START && address < EMS_END) {
        return ems_readdw(address - EMS_START);
    }
    if (address >= UMB_START && address < UMB_END) {
        return read32psram(address);
    }
    if (unlikely(address >= BIOS_START && address < HMA_START)) {
        return *(uint32_t *) &BIOS[address - BIOS_START];
    }
    if (address >= HMA_START && address < HMA_END) {
        return read32psram(address);
    }
    return 0xFFFFFFFF;
}

#else
uint8_t ALIGN(4, VIDEORAM[VIDEORAM_SIZE + 4]) = {0 };
uint8_t ALIGN(4, RAM[RAM_SIZE + 4]) = {0 };
uint8_t ALIGN(4, UMB[(UMB_END - UMB_START) + 4]) = {0 };
uint8_t ALIGN(4, HMA[(HMA_END - HMA_START) + 4]) = {0 };

// Writes a byte to the virtual memory
void write86(uint32_t address, uint8_t value) {
    if (address < RAM_SIZE) {
        RAM[address] = value;
    } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        if (vga_planar_mode == 2 || vga_planar_mode == 4) { // Or other conditions indicating planar mode
            uint8_t map_mask = vga_sequencer[2];
            uint8_t write_mode = vga_graphics_control[5] & 0x03;
            uint8_t data_rotate_count = vga_graphics_control[3] & 0x07;
            uint8_t bit_mask = vga_graphics_control[8];
            uint8_t set_reset_data = vga_graphics_control[0];
            uint8_t enable_set_reset = vga_graphics_control[1];

            uint32_t vram_offset = address - VIDEORAM_START;
            uint8_t cpu_data = value;
            uint8_t rotated_data = (cpu_data >> data_rotate_count) | (cpu_data << (8 - data_rotate_count));

            for (int plane = 0; plane < 4; ++plane) {
                if (map_mask & (1 << plane)) {
                    uint32_t current_plane_base_addr = plane * vga_plane_size;
                    uint32_t current_plane_addr = current_plane_base_addr + vram_offset;

                    if (current_plane_addr >= VIDEORAM_SIZE) {
                         continue;
                    }

                    uint8_t vram_byte = VIDEORAM[current_plane_addr];
                    uint8_t plane_data_to_write;

                    if (enable_set_reset & (1 << plane)) {
                        plane_data_to_write = (set_reset_data & (1 << plane)) ? 0xFF : 0x00;
                    } else {
                        uint8_t data_for_op = rotated_data;
                        switch (write_mode) {
                            case 0:
                                plane_data_to_write = data_for_op;
                                break;
                            case 1:
                                plane_data_to_write = vram_byte & data_for_op;
                                break;
                            case 2:
                                plane_data_to_write = vram_byte | data_for_op;
                                break;
                            case 3:
                                plane_data_to_write = vram_byte ^ data_for_op;
                                break;
                            default:
                                plane_data_to_write = data_for_op;
                                break;
                        }
                    }
                    VIDEORAM[current_plane_addr] = (plane_data_to_write & bit_mask) | (vram_byte & ~bit_mask);
                }
            }
        } else {
            if (log_debug)
                printf("Writing %04X %02x\n", (vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE, value);
            VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE] = value;
        }
    } else if (address >= EMS_START && address < EMS_END) {
        ems_write(address - EMS_START, value);
    } else if (address >= UMB_START && address < UMB_END) {
        UMB[address - UMB_START] = value;
    } else if (address >= HMA_START && address < HMA_END) {
        HMA[address - HMA_START] = value;
    }
}

// Writes a word to the virtual memory
void writew86(uint32_t address, uint16_t value) {
    if (address & 1) {
        write86(address, (uint8_t) (value & 0xFF));
        write86(address + 1, (uint8_t) ((value >> 8) & 0xFF));
    } else {
        if (address < RAM_SIZE) {
            *(uint16_t *) &RAM[address] = value;
        } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
            if (log_debug)
                printf("WritingW %04X %04x\n", (vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE, value);
            *(uint16_t *) &VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE] = value;
        } else if (address >= EMS_START && address < EMS_END) {
            ems_writew(address - EMS_START, value);
        } else if (address >= UMB_START && address < UMB_END) {
            *(uint16_t *) &UMB[address - UMB_START] = value;
        } else if (address >= HMA_START && address < HMA_END) {
            *(uint16_t *) &HMA[address - HMA_START] = value;
        }
    }
}

void writedw86(uint32_t address, uint32_t value) {
    if (address & 1) {
        write86(address, (uint8_t) (value & 0xFF));
        write86(address + 1, (uint8_t) ((value >> 8) & 0xFF));
        write86(address + 2, (uint8_t) ((value >> 16) & 0xFF));
        write86(address + 3, (uint8_t) ((value >> 24) & 0xFF));
    } else {
        if (address < RAM_SIZE) {
            *(uint32_t *) &RAM[address] = value;
        } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
            *(uint32_t *) &VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE] = value;
        } else if (address >= EMS_START && address < EMS_END) {
            ems_writedw(address - EMS_START, value);
        } else if (address >= UMB_START && address < UMB_END) {
            *(uint32_t *) &UMB[address - UMB_START] = value;
        } else if (address >= HMA_START && address < HMA_END) {
            *(uint32_t *) &HMA[address - HMA_START] = value;
        }
    }
}

// Reads a byte from the virtual memory
uint8_t read86(uint32_t address) {
    if (address < RAM_SIZE) {
        return RAM[address];
    }
    if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        return VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
    }
    if (address >= EMS_START && address < EMS_END) {
        return ems_read(address - EMS_START);
    }
    if (address >= UMB_START && address < UMB_END) {
        return UMB[address - UMB_START];
    }
    if (address == 0xFC000) {
        return 0x21;
    }
    if (address >= BIOS_START && address < HMA_START) {
        return BIOS[address - BIOS_START];
    }
    if (address >= HMA_START && address < HMA_END) {
        return HMA[address - HMA_START];
    }
    return 0xFF;
}

// Reads a word from the virtual memory
uint16_t readw86(uint32_t address) {
    if (address & 1) {
        return (uint16_t) read86(address) | ((uint16_t) read86(address + 1) << 8);
    }
    if (address < RAM_SIZE) {
        return *(uint16_t *) &RAM[address];
    }
    if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        return *(uint16_t *) &VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
    }
    if (address >= EMS_START && address < EMS_END) {
        return ems_readw(address - EMS_START);
    }
    if (address >= UMB_START && address < UMB_END) {
        return *(uint16_t *) &UMB[address - UMB_START];
    }
    if (address >= BIOS_START && address < HMA_START) {
        return *(uint16_t *) &BIOS[address - BIOS_START];
    }
    if (address >= HMA_START && address < HMA_END) {
        return *(uint16_t *) &HMA[address - HMA_START];
    }
    return 0xFFFF;
}

#endif