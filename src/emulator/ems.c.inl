// The Lo-tech EMS board driver is hardcoded to 2MB.
#pragma once
#define EMS_PSRAM_OFFSET (2048 << 10)

static uint8_t ems_pages[4] = {0};
uint8_t __attribute__((section(".psram"))) EMS[EMS_MEMORY_SIZE] = {0};

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
    return EMS[phys_addr];
}

// TODO: Overlap?
static INLINE uint16_t ems_readw(const uint32_t address) {
    const uint32_t phys_addr = physical_address(address);
    return *(uint16_t *) &EMS[phys_addr];
}

static INLINE uint32_t ems_readdw(const uint32_t address) {
    const uint32_t phys_addr = physical_address(address);
    return *(uint32_t *) &EMS[phys_addr];
}

static INLINE void ems_write(const uint32_t address, const uint8_t data) {
    const uint32_t phys_addr = physical_address(address);
    EMS[phys_addr] = data;
}


static INLINE void ems_writew(const uint32_t address, const uint16_t data) {
    const uint32_t phys_addr = physical_address(address);
    *(uint16_t *) &EMS[phys_addr] = data;
}

static INLINE void ems_writedw(const uint32_t address, const uint32_t data) {
    const uint32_t phys_addr = physical_address(address);
    *(uint32_t *) &EMS[phys_addr] = data;
}
