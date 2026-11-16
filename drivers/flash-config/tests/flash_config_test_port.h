#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint32_t flash_io_begin(void);
void flash_io_transfer(const uint8_t *tx, uint8_t *rx, size_t count);
uint32_t flash_io_time_us(void);
void flash_io_busy_timeout(void) __attribute__((noreturn));
void flash_io_end(uint32_t saved, bool quad);
