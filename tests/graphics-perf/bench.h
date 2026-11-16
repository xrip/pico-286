#pragma once
#include "graphics_modes.h"

extern uint32_t bench_vram[65536];
extern graphics_pc_state_t bench_state;
extern uint16_t bench_text_pairs[1024], bench_ntsc_palette[1024];
void bench_old_vga(enum graphics_mode_t mode, unsigned row,
                   const uint16_t *palette, uint8_t *output);
void bench_old_text(enum graphics_mode_t mode, unsigned row,
                    const uint16_t *palette, uint8_t *output);
void bench_old_hdmi(enum graphics_mode_t mode, unsigned row,
                    const uint16_t *palette, uint8_t *output);
void bench_old_ntsc(enum graphics_mode_t mode, unsigned row,
                    const uint16_t *palette, uint8_t *output);
void bench_old_ntsc_text(enum graphics_mode_t mode, unsigned row,
                         const uint16_t *palette, uint8_t *output);
