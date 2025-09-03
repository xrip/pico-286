#pragma once

#if PICO_ON_DEVICE

bool init_swap();
uint8_t swap_read(uint32_t address);
uint16_t swap_read16(uint32_t addr32);
uint32_t swap_read32(uint32_t addr32);
void swap_write(uint32_t addr32, uint8_t value);
void swap_write16(uint32_t addr32, uint16_t value);
void swap_write32(uint32_t addr32, uint32_t value);
void swap_file_read_block(uint8_t * dst, uint32_t file_offset, uint32_t size);
void swap_file_flush_block(const uint8_t* src, uint32_t file_offset, uint32_t sz);

#else
/// placeholders
inline static bool init_swap() {}
inline static uint8_t swap_read(uint32_t address) { return 0xFF; }
inline static uint16_t swap_read16(uint32_t addr32)  { return 0xFFFF; }
inline static uint32_t swap_read32(uint32_t addr32)  { return 0xFFFFFFFF; }
inline static void swap_write(uint32_t addr32, uint8_t value) {}
inline static void swap_write16(uint32_t addr32, uint16_t value) {}
inline static void swap_write32(uint32_t addr32, uint32_t value) {}
inline static void swap_file_read_block(uint8_t * dst, uint32_t file_offset, uint32_t size) {}
inline static void swap_file_flush_block(const uint8_t* src, uint32_t file_offset, uint32_t sz) {}

#endif
