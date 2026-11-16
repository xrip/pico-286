#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct vga_timing {
    uint16_t active_lines;
    uint16_t total_lines;
    uint16_t vsync_first_line;
    uint16_t vsync_lines;
} vga_timing_t;

// All PC profiles keep 800 horizontal samples, a 96-sample HSync pulse,
// a 144-sample active offset, and a 25.175 MHz pixel clock. Only vertical
// CRTC timing changes. The old 400/449 profile was disabled, so 200-line
// graphics and text retain the old 480/525 output rather than enabling it.
static inline vga_timing_t vga_timing_for_mode(unsigned mode, bool rgbs) {
    if (rgbs) {
        return (vga_timing_t){240, 262, 243, 3};
    }
    if (mode == 0x10u) { // EGA_640x350x16x4
        return (vga_timing_t){350, 449, 387, 2};
    }
    // Includes 640x480x16 and EGA640x200, whose old mode-setting switch
    // omitted a case even though the old pixel renderer supported them.
    return (vga_timing_t){480, 525, 490, 2};
}
