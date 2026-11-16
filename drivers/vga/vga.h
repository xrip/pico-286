#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIO_VGA (pio0)

#ifndef VGA_BASE_PIN
#define VGA_BASE_PIN 6
#endif

#ifndef VGA_ENABLE_DITHER
#define VGA_ENABLE_DITHER 1
#endif

// Set to 1 for 15 kHz 240p RGBs on the same pins, which is what a SCART set
// takes. Sync is then one composite wire on bit 7 (GPIO 13), and bit 6 is
// unused. A VGA monitor will not lock to it, and a SCART set will not lock to
// the normal 31.5 kHz timing, so this is chosen once at build time.
#ifndef VGA_RGBS
#define VGA_RGBS 0
#endif

#ifndef TEXTMODE_COLS
#define TEXTMODE_COLS 80
#endif

#ifndef TEXTMODE_ROWS
#define TEXTMODE_ROWS 30
#endif

#ifndef RGB888
#define RGB888(r, g, b) ((r << 16) | (g << 8) | b)
#endif

void vga_output_init(const uint8_t *framebuffer);
void vga_graphics_init(void);
uint32_t vga_start_mask(void);
void vga_enable(void);
void vga_set_framebuffer(const uint8_t *framebuffer);
void vga_request_framebuffer(const uint8_t *framebuffer);
void vga_wait_framebuffer(void);
void vga_set_palette(uint8_t index, uint32_t color888);
void vga_set_flashmode(bool flash_line, bool flash_frame);
size_t vga_current_line(void);
void vga_wait_vblank(void);

#ifdef __cplusplus
}
#endif
