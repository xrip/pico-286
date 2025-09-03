// The Lo-tech EMS board driver is hardcoded to 2MB.
#pragma once
#include "psram_spi.h"

#define EMS_PSRAM_OFFSET ((1024 + 64) << 10)
extern uint32_t butter_psram_size;

static uint8_t ems_pages[4] = {0};
uint8_t __attribute__((aligned (4), section(".psram"))) EMS[EMS_MEMORY_SIZE] = {0};

inline void out_ems(const uint16_t port, const uint8_t data) {
    ems_pages[port & 3] = data;
}

static INLINE uint32_t physical_address(const uint32_t address) {
    const uint32_t page_addr = address & 0x3FFF;
    const uint8_t selector = ems_pages[(address >> 14) & 3];
    return selector * 0x4000 + page_addr;
}

static INLINE uint8_t ems_read(const uint32_t address) {
    const uint32_t phys_addr = physical_address(address);
    return butter_psram_size ? EMS[phys_addr] : read8psram(phys_addr + EMS_PSRAM_OFFSET);
}

// TODO: Overlap?
static INLINE uint16_t ems_readw(const uint32_t address) {
    const uint32_t phys_addr = physical_address(address);
    return butter_psram_size ? (*(uint16_t *) &EMS[phys_addr]) : read16psram(phys_addr + EMS_PSRAM_OFFSET);
}

static INLINE uint32_t ems_readdw(const uint32_t address) {
    const uint32_t phys_addr = physical_address(address);
    return butter_psram_size ? (*(uint32_t *) &EMS[phys_addr]) : read32psram(phys_addr + EMS_PSRAM_OFFSET);
}

static INLINE void ems_write(const uint32_t address, const uint8_t data) {
    const uint32_t phys_addr = physical_address(address);
    if (butter_psram_size)
        EMS[phys_addr] = data;
    else
        write8psram(phys_addr + EMS_PSRAM_OFFSET, data);
}


static INLINE void ems_writew(const uint32_t address, const uint16_t data) {
    const uint32_t phys_addr = physical_address(address);
    if (butter_psram_size)
        *(uint16_t *) &EMS[phys_addr] = data;
    else
        write16psram(phys_addr + EMS_PSRAM_OFFSET, data);
}

static INLINE void ems_writedw(const uint32_t address, const uint32_t data) {
    const uint32_t phys_addr = physical_address(address);
    if (butter_psram_size)
        *(uint32_t *) &EMS[phys_addr] = data;
    else
        write32psram(phys_addr + EMS_PSRAM_OFFSET, data);
}
