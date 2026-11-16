#pragma once
#include "graphics.h"

// Keep the old VGA port 3DA contract. Bit 3 spans vertical blanking, not
// just the output connector's short sync pulse. Doubled graphics rows leave
// bit 0 clear on the repeated line, as the old VGA IRQ did.
static inline __attribute__((always_inline)) uint8_t graphics_pc_raster_status(enum graphics_mode_t mode,
                                               unsigned line, bool rendered) {
    const unsigned active_lines = mode == EGA_640x350x16x4 ? 350u : 480u;
    if (line >= active_lines) return 9;
    if (!rendered) return 0;
    const bool every_line = line >= 400u || mode <= TEXTMODE_80x25_COLOR ||
            mode == HERC_640x480x2 || mode == HERC_640x480x2_90 ||
            mode == VGA_640x480x2 || mode == VGA_640x480x16 ||
            mode == EGA_640x350x16x4;
    return every_line || !(line & 1u) ? 1u : 0u;
}
