#pragma GCC optimize("Ofast")

#include "includes/bios.h"
#include "emulator.h"
#include "ems.c.inl"

uint32_t __attribute__((aligned (4)))  VIDEORAM[VIDEORAM_SIZE] = {0};
uint8_t __attribute__((aligned (4), section(".psram"))) RAM[RAM_SIZE] = {0};
uint8_t __attribute__((aligned (4))) UMB[UMB_END - UMB_START] = {0}; // shared used block (should NOT be in .psram)
uint8_t __attribute__((aligned (4), section(".psram"))) HMA[HMA_END - HMA_START] = {0};

#define VIDEORAM_MASK 0xFFFF

// Writes a byte to the virtual memory
void write86(const uint32_t address, const uint8_t value) {
    if (address < RAM_SIZE) {
        RAM[address] = value;
    } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        // printf("video write %x traddr %x =  %x\n", address, (address - VIDEORAM_START) & VIDEORAM_MASK, value);
        vga_mem_write(address, value);
        // VIDEORAM[(address - VIDEORAM_START) & VIDEORAM_MASK] = value;
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
void writew86(const uint32_t address, const uint16_t value) {
    if (address & 1) {
        write86(address, (uint8_t) (value & 0xFF));
        write86(address + 1, (uint8_t) ((value >> 8) & 0xFF));
    } else {
        if (address < RAM_SIZE) {
            *(uint16_t *) &RAM[address] = value;
        } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
            vga_mem_write16(address, value);
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

void writedw86(const uint32_t address, const uint32_t value) {
    if (address & 1) {
        write86(address, (uint8_t) (value & 0xFF));
        write86(address + 1, (uint8_t) ((value >> 8) & 0xFF));
        write86(address + 2, (uint8_t) ((value >> 16) & 0xFF));
        write86(address + 3, (uint8_t) ((value >> 24) & 0xFF));
    } else {
        if (address < RAM_SIZE) {
            *(uint32_t *) &RAM[address] = value;
        } else if (address >= VIDEORAM_START && address < VIDEORAM_END) {
            write86(address, (uint8_t) (value & 0xFF));
            write86(address + 1, (uint8_t) ((value >> 8) & 0xFF));
            write86(address + 2, (uint8_t) ((value >> 16) & 0xFF));
            write86(address + 3, (uint8_t) ((value >> 24) & 0xFF));
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
uint8_t read86(const uint32_t address) {
    if (address < RAM_SIZE) {
        return RAM[address];
    }
    if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        return vga_mem_read(address);
    }
     // if (address >= VBIOS_START && address < VBIOS_END) {
         // return VGABIOS[address - VBIOS_START];
     // }
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
uint16_t readw86(const uint32_t address) {
    if (address & 1) {
        return (uint16_t) read86(address) | ((uint16_t) read86(address + 1) << 8);
    }
    if (address < RAM_SIZE) {
        return *(uint16_t *) &RAM[address];
    }
    if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        return (uint16_t) read86(address) | ((uint16_t) read86(address + 1) << 8);
    }
    // if (address >= VBIOS_START && address < VBIOS_END) {
        // return *(uint16_t *) &VGABIOS[address - VBIOS_START];
    // }
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

uint32_t readdw86(const uint32_t address) {
    if (address & 3) {
        return (uint32_t) read86(address)
               | ((uint32_t) read86(address + 1) << 8)
               | ((uint32_t) read86(address + 2) << 16)
               | ((uint32_t) read86(address + 3) << 24);
    }
    if (address < RAM_SIZE) {
        return *(uint32_t *) &RAM[address];
    }
    if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        return (uint32_t) read86(address)
               | ((uint32_t) read86(address + 1) << 8)
               | ((uint32_t) read86(address + 2) << 16)
               | ((uint32_t) read86(address + 3) << 24);
    }
    if (address >= EMS_START && address < EMS_END) {
        return ems_readdw(address - EMS_START);
    }
    if (address >= UMB_START && address < UMB_END) {
        return *(uint32_t *) &UMB[address - UMB_START];
    }
    if (address >= BIOS_START && address < HMA_START) {
        return *(uint32_t *) &BIOS[address - BIOS_START];
    }
    if (address >= HMA_START && address < HMA_END) {
        if (a20_enabled) {
            return *(uint32_t *) &HMA[address - HMA_START];
        }
        return *(uint32_t *) &RAM[address - HMA_START];
    }
    if (!a20_enabled && address >= HMA_END) {
        return readdw86(address - HMA_START);
    }
    return 0xFFFFFFFF;
}
