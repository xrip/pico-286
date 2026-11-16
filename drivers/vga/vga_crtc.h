#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reset on a BIOS mode change, not on the renderer's derived mode changes.
// R6 tweaks can turn the emulator mode into 0x77 while still using text VRAM.
void vga_crtc_reset(unsigned bios_mode);
void vga_crtc_write(uint8_t index, uint8_t value);
bool vga_crtc_get_cursor(uint8_t *x, uint8_t *y);

// Standard text keeps the measured fast renderer. Only changed layout uses
// the adapter; register writes cache this choice outside the pixel loop.
extern volatile bool vga_crtc_text_custom;
void vga_crtc_render_text(unsigned row, unsigned canvas_height,
                          uint16_t *output, const uint16_t *attribute_pairs);

#ifdef __cplusplus
}
#endif
