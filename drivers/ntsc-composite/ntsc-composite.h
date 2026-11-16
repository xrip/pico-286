#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// GPIO28 is PWM slice 6 channel A. GPIO26 and GPIO27 share slice 5, and sound
// hardware usually sits there, so 28 is the pin most often left free.
#ifndef NTSC_PIN_OUTPUT
#define NTSC_PIN_OUTPUT 28
#endif

#ifndef GRAPHICS_FRAME_WIDTH
#define GRAPHICS_FRAME_WIDTH 320
#endif

#ifndef GRAPHICS_FRAME_HEIGHT
#define GRAPHICS_FRAME_HEIGHT 240
#endif

#ifndef GRAPHICS_COLOR_COUNT
#define GRAPHICS_COLOR_COUNT 256
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

void ntsc_composite_init(const uint8_t *framebuffer);
void ntsc_composite_graphics_init(void);
uint32_t ntsc_composite_start_mask(void);
void ntsc_composite_set_framebuffer(const uint8_t *framebuffer);
void ntsc_composite_request_framebuffer(const uint8_t *framebuffer);
void ntsc_composite_wait_framebuffer(void);
void ntsc_composite_set_palette(uint8_t index, uint32_t color888);
size_t ntsc_composite_current_line(void);
void ntsc_composite_wait_vblank(void);

#ifdef __cplusplus
}
#endif
