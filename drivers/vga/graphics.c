#include <graphics.h>

#if defined(VGA) && !defined(NTSC_TV)

#include <stddef.h>

#include <hardware/dma.h>

#include <graphics_modes.h>

#include "vga.h"

#if defined(GRAPHICS_NO_BUILTIN_FRAMEBUFFER)
#define GRAPHICS_INITIAL_FRAMEBUFFER NULL
#else
static uint8_t graphics_framebuffer[GRAPHICS_FRAME_WIDTH * GRAPHICS_FRAME_HEIGHT]
        __attribute__((aligned(4)));
#define GRAPHICS_INITIAL_FRAMEBUFFER graphics_framebuffer
#endif

uint8_t *text_buffer = NULL;

void vga_graphics_init(void) {
    vga_output_init(GRAPHICS_INITIAL_FRAMEBUFFER);
    dma_start_channel_mask(vga_start_mask());
    vga_enable();
}

void graphics_set_mode(const enum graphics_mode_t mode) {
    graphics_source_set_mode(mode);
}

void graphics_wait_vblank(void) {
    vga_wait_vblank();
}

void graphics_set_buffer(uint8_t *buffer,
                         const uint16_t width,
                         const uint16_t height) {
    graphics_source_set_buffer(buffer, width, height);

    // A full-size buffer is also the scanout buffer, so the direct modes keep
    // their zero-copy path.
    if (width == GRAPHICS_FRAME_WIDTH && height == GRAPHICS_FRAME_HEIGHT) {
        vga_set_framebuffer(buffer);
    }
}

void graphics_set_offset(const int x, const int y) {
    graphics_source_set_offset(x, y);
}

void graphics_set_palette(const uint8_t index, const uint32_t color) {
    vga_set_palette(index, color);
}

void graphics_set_textbuffer(uint8_t *buffer) {
    text_buffer = buffer;
}

void graphics_set_bgcolor(const uint32_t color888) {
    (void)color888;
}

void graphics_set_flashmode(const bool flash_line, const bool flash_frame) {
    vga_set_flashmode(flash_line, flash_frame);
}

uint8_t *graphics_get_framebuffer(void) {
    return GRAPHICS_INITIAL_FRAMEBUFFER;
}

void graphics_present_framebuffer(const uint8_t *framebuffer) {
    vga_request_framebuffer(framebuffer);
    vga_wait_framebuffer();
}

#endif
