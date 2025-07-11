#pragma once

#define SWAP_PAGE_SIZE (2048)
#define RAM_IN_PAGE_ADDR_MASK (0x000007FF)
#define SHIFT_AS_DIV (11)

#define SWAP_BLOCKS (RAM_SIZE / SWAP_PAGE_SIZE)



bool init_swap();
uint8_t swap_read(uint32_t address);
uint16_t swap_read16(uint32_t addr32);
uint32_t swap_read32(uint32_t addr32);
void swap_write(uint32_t addr32, uint8_t value);
void swap_write16(uint32_t addr32, uint16_t value);
void swap_write32(uint32_t addr32, uint32_t value);
void swap_file_read_block(uint8_t * dst, uint32_t file_offset, uint32_t size);
void swap_file_flush_block(const uint8_t* src, uint32_t file_offset, uint32_t sz);

