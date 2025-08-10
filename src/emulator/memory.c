#pragma GCC optimize("Ofast")

#include "includes/bios.h"
#include "emulator.h"


#include "ems.c.inl"

#if PICO_ON_DEVICE
#include "pico.h"
uint8_t * PSRAM_DATA = (uint8_t*)0x11000000;
uint8_t ALIGN(4, RAM[RAM_SIZE + 4]) = {0};
uint8_t ALIGN(4, VIDEORAM[VIDEORAM_SIZE + 4]) = {0};
uint8_t ALIGN(4, VIDEORAM1[VIDEORAM_SIZE + 4]) = {0};
uint8_t ALIGN(4, VIDEORAM2[VIDEORAM_SIZE + 4]) = {0};
uint8_t ALIGN(4, VIDEORAM3[VIDEORAM_SIZE + 4]) = {0};
uint8_t VIDEORAM_write_mask = 0xF;
uint8_t* VIDEORAM_PLANES[] = { VIDEORAM, VIDEORAM1, VIDEORAM2, VIDEORAM3 };

// Writes a byte to the virtual memory
void __time_critical_func() write86(uint32_t address, uint8_t value) {
    if (address < RAM_SIZE) {
        RAM[address] = value;
    } else if (address < VIDEORAM_START) {
        write8psram(address, value);
    } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        register uint32_t off = (vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE;
        if (VIDEORAM_write_mask & 0b0001)
            VIDEORAM[off] = value;
        if (VIDEORAM_write_mask & 0b0010)
            VIDEORAM1[off] = value;
        if (VIDEORAM_write_mask & 0b0100)
            VIDEORAM2[off] = value;
        if (VIDEORAM_write_mask & 0b1000)
            VIDEORAM3[off] = value;
    } else if (address >= EMS_START && address < EMS_END) {
        ems_write(address - EMS_START, value);
    } else if (address >= UMB_START && address < UMB_END) {
        write8psram(address, value);
    } else if (address >= HMA_START && address < HMA_END) {
        if (a20_enabled)
            write8psram(address, value);
        else
            write86(address - HMA_START, value);
    } else if (!a20_enabled && address >= HMA_END) {
        write86(address - HMA_START, value);
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
            register uint32_t off = (vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE;
            if (VIDEORAM_write_mask & 0b0001)
                *(uint16_t *) &VIDEORAM[off] = value;
            if (VIDEORAM_write_mask & 0b0010)
                *(uint16_t *) &VIDEORAM1[off] = value;
            if (VIDEORAM_write_mask & 0b0100)
                *(uint16_t *) &VIDEORAM2[off] = value;
            if (VIDEORAM_write_mask & 0b1000)
                *(uint16_t *) &VIDEORAM3[off] = value;
        } else if (address >= EMS_START && address < EMS_END) {
            ems_writew(address - EMS_START, value);
        } else if (address >= UMB_START && address < UMB_END) {
            write16psram(address, value);
        } else if (address >= HMA_START && address < HMA_END) {
            if (a20_enabled)
                write16psram(address, value);
            else
                writew86(address - HMA_START, value);
        } else if (!a20_enabled && address >= HMA_END) {
            writew86(address - HMA_START, value);
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
            register uint32_t off = (vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE;
            if (VIDEORAM_write_mask & 0b0001)
                *(uint32_t *) &VIDEORAM[off] = value;
            if (VIDEORAM_write_mask & 0b0010)
                *(uint32_t *) &VIDEORAM1[off] = value;
            if (VIDEORAM_write_mask & 0b0100)
                *(uint32_t *) &VIDEORAM2[off] = value;
            if (VIDEORAM_write_mask & 0b1000)
                *(uint32_t *) &VIDEORAM3[off] = value;
        } else if (address >= EMS_START && address < EMS_END) {
            ems_writedw(address - EMS_START, value);
        } else if (address >= UMB_START && address < UMB_END) {
            write32psram(address, value);
        } else if (address >= HMA_START && address < HMA_END) {
            if (a20_enabled)
                write32psram(address, value);
            else
                writedw86(address - HMA_START, value);
        } else if (!a20_enabled && address >= HMA_END) {
            writedw86(address - HMA_START, value);
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
        return VIDEORAM_PLANES[vga_graphics_control[4] & 3][(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
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
        if (a20_enabled)
            return read8psram(address);
        return read86(address - HMA_START);
    }
    if (!a20_enabled && address >= HMA_END) {
        return read86(address - HMA_START);
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
        return *(uint16_t *) &VIDEORAM_PLANES[vga_graphics_control[4] & 3][(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
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
        if (a20_enabled)
            return read16psram(address);
        return readw86(address - HMA_START);
    }
    if (!a20_enabled && address >= HMA_END) {
        return readw86(address - HMA_START);
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
        return *(uint32_t *) &VIDEORAM_PLANES[vga_graphics_control[4] & 3][(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE];
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
        if (a20_enabled)
            return read32psram(address);
        return readdw86(address - HMA_START);
    }
    if (!a20_enabled && address >= HMA_END) {
        return readdw86(address - HMA_START);
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
        if (log_debug)
            printf("Writing %04X %02x\n", (vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE, value);
        VIDEORAM[(vga_plane_offset + address - VIDEORAM_START) % VIDEORAM_SIZE] = value;
    } else if (address >= EMS_START && address < EMS_END) {
        ems_write(address - EMS_START, value);
    } else if (address >= UMB_START && address < UMB_END) {
        UMB[address - UMB_START] = value;
    } else if (address >= HMA_START && address < HMA_END) {
        if (a20_enabled) {
            HMA[address - HMA_START] = value;
        } else {
            RAM[address - HMA_START] = value;
        }
    } else if (!a20_enabled && address >= HMA_END) {
        write86(address - HMA_START, value);
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
            if (a20_enabled) {
                *(uint16_t *) &HMA[address - HMA_START] = value;
            } else {
                *(uint16_t *) &RAM[address - HMA_START] = value;
            }
        } else if (!a20_enabled && address >= HMA_END) {
            writew86(address - HMA_START, value);
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
            if (a20_enabled) {
                *(uint32_t *) &HMA[address - HMA_START] = value;
            } else {
                *(uint32_t *) &RAM[address - HMA_START] = value;
            }
        } else if (!a20_enabled && address >= HMA_END) {
            writedw86(address - HMA_START, value);
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
        if (a20_enabled) {
            return HMA[address - HMA_START];
        }
        return RAM[address - HMA_START];
    }
    if (!a20_enabled && address >= HMA_END) {
        return read86(address - HMA_START);
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
        if (a20_enabled) {
            return *(uint16_t *) &HMA[address - HMA_START];
        }
        return *(uint16_t *) &RAM[address - HMA_START];
    }
    if (!a20_enabled && address >= HMA_END) {
        return readw86(address - HMA_START);
    }
    return 0xFFFF;
}

#endif