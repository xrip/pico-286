#pragma once
#include "graphics_modes.h"

// Original 320-pixel PIO index loops. output is four-byte aligned and has
// room for 320 bytes. Source rows are 0..199. False asks the caller to use the
// common decoder (a different mode or a VRAM row crossing the 64 Ki-word wrap).
// The output buffer must not overlap VRAM.
bool graphics_pc_hdmi_render(enum graphics_mode_t mode, const uint32_t *vram,
                             unsigned row, uint8_t *output);
