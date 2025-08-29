// The Lo-tech EMS board driver is hardcoded to 2MB.
#define EMS_PSRAM_OFFSET (2048 << 10)

static uint8_t ems_pages[4] = {0};

void out_ems(uint16_t port, uint8_t data) {
    ems_pages[port & 3] = data;
}

static INLINE uint32_t physical_address(uint32_t address) {
    uint32_t page_addr = address & 0x3FFF;
    uint8_t selector = ems_pages[(address >> 14) & 3];
    return selector * 0x4000 + page_addr;
}

uint8_t __attribute__((section(".psram"))) EMS[EMS_MEMORY_SIZE] = {0};

static INLINE uint8_t ems_read(uint32_t addr) {
    uint32_t phys_addr = physical_address(addr);
    return EMS[phys_addr];
}

// TODO: Overlap?
static INLINE uint16_t ems_readw(uint32_t addr) {
    uint32_t phys_addr = physical_address(addr);
    return *(uint16_t *) &EMS[phys_addr];
}

static INLINE uint32_t ems_readdw(uint32_t addr) {
    uint32_t phys_addr = physical_address(addr);
    return *(uint32_t *) &EMS[phys_addr];
}

static INLINE void ems_write(uint32_t addr, uint8_t data) {
    uint32_t phys_addr = physical_address(addr);
    EMS[phys_addr] = data;
}


static INLINE void ems_writew(uint32_t addr, uint16_t data) {
    uint32_t phys_addr = physical_address(addr);
    *(uint16_t *) &EMS[phys_addr] = data;
}

static INLINE void ems_writedw(uint32_t addr, uint32_t data) {
    uint32_t phys_addr = physical_address(addr);
    *(uint32_t *) &EMS[phys_addr] = data;
}
