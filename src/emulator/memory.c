#pragma GCC optimize("Ofast")

#include "includes/bios.h"
#include "emulator.h"
#include "ems.c.inl"
uint32_t __attribute__((aligned (4)))  VIDEORAM[VIDEORAM_SIZE] = {0};
uint8_t __attribute__((aligned (4), section(".psram"))) RAM[RAM_SIZE] = {0};
uint8_t __attribute__((aligned (4), section(".psram"))) UMB[UMB_END - UMB_START] = {0};
uint8_t __attribute__((aligned (4), section(".psram"))) EXTENDED[EXTENDED_END - HMA_START] = {0};
#define A20 0x100000 // with the A20 gate shut, address bit 20 is 0

#define VIDEORAM_MASK 0xFFFF
read86_t read86;
read86w_t readw86;
read86dw_t readdw86;
write86_t write86;
write86w_t writew86;
write86dw_t writedw86;

// Writes a byte to the virtual memory
__not_in_flash() void write86_ob(const uint32_t address, const uint8_t value) {
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
    } else if (!a20_enabled && (address & A20)) {
        write86(address & ~A20, value);
    } else if (address >= HMA_START && address < EXTENDED_END) {
        EXTENDED[address - HMA_START] = value;
    }
}

// Writes a word to the virtual memory
__not_in_flash() void writew86_ob(const uint32_t address, const uint16_t value) {
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
        } else if (!a20_enabled && (address & A20)) {
            writew86(address & ~A20, value);
        } else if (address >= HMA_START && address < EXTENDED_END) {
            *(uint16_t *) &EXTENDED[address - HMA_START] = value;
        }
    }
}

__not_in_flash() void writedw86_ob(const uint32_t address, const uint32_t value) {
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
        } else if (!a20_enabled && (address & A20)) {
            writedw86(address & ~A20, value);
        } else if (address >= HMA_START && address <= EXTENDED_END - 4) {
            *(uint32_t *) &EXTENDED[address - HMA_START] = value;
        }
    }
}

// Reads a byte from the virtual memory
__not_in_flash() uint8_t read86_ob(const uint32_t address) {
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
    if (!a20_enabled && (address & A20)) {
        return read86(address & ~A20);
    }
    if (address >= HMA_START && address < EXTENDED_END) {
        return EXTENDED[address - HMA_START];
    }
    return 0xFF;
}

// Reads a word from the virtual memory
__not_in_flash() uint16_t readw86_ob(const uint32_t address) {
    if (address & 1) {
        return (uint16_t) read86(address) | ((uint16_t) read86(address + 1) << 8);
    }
    if (address < RAM_SIZE) {
        return *(uint16_t *) &RAM[address];
    }
    if (address >= VIDEORAM_START && address < VIDEORAM_END) {
        return  vga_mem_read16(address);
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
    if (!a20_enabled && (address & A20)) {
        return readw86(address & ~A20);
    }
    if (address >= HMA_START && address < EXTENDED_END) {
        return *(uint16_t *) &EXTENDED[address - HMA_START];
    }
    return 0xFFFF;
}

__not_in_flash() uint32_t readdw86_ob(const uint32_t address) {
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
    if (!a20_enabled && (address & A20)) {
        return readdw86(address & ~A20);
    }
    if (address >= HMA_START && address <= EXTENDED_END - 4) {
        return *(uint32_t *) &EXTENDED[address - HMA_START];
    }
    return 0xFFFFFFFF;
}

