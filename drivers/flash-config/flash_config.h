#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Core 0 startup only: before overclocking, DMA, core 1, or QMI PSRAM setup.
// Use the RP2350 ROM's initial read mode (PICO_EMBED_XIP_SETUP=0).
// True: QE verified, quad XIP at divider 12.
// False: no safe quad setup; serial 03h XIP at divider 12. Do not overclock.
// A flash that stays busy enters ROM BOOTSEL rather than returning into XIP.
bool flash_config_init(void);

#ifdef __cplusplus
}
#endif
