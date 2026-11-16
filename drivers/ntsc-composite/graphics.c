#include <graphics.h>

#if defined(NTSC_TV) && !defined(HDMI_HSTX)

#include <stddef.h>

#include <hardware/dma.h>

#include <graphics_modes.h>

#include "ntsc-composite.h"

#if defined(VGA)
#include <vga.h>
#endif

#if defined(TFT)
#include <st7789.h>
#endif

#if defined(GRAPHICS_NO_BUILTIN_FRAMEBUFFER)
#define GRAPHICS_INITIAL_FRAMEBUFFER NULL
#else
static uint8_t graphics_framebuffer[GRAPHICS_FRAME_WIDTH * GRAPHICS_FRAME_HEIGHT]
        __attribute__((aligned(4)));
#define GRAPHICS_INITIAL_FRAMEBUFFER graphics_framebuffer
#endif

uint8_t *text_buffer = NULL;

void ntsc_composite_graphics_init(void) {
    uint32_t start_mask = 0;

#if defined(VGA)
    vga_output_init(GRAPHICS_INITIAL_FRAMEBUFFER);
    start_mask |= vga_start_mask();
#endif

#if defined(TFT)
    // The panel is pushed a frame at a time, so it needs no DMA start mask.
    tft_init(GRAPHICS_INITIAL_FRAMEBUFFER);
#endif

    ntsc_composite_init(GRAPHICS_INITIAL_FRAMEBUFFER);
    start_mask |= ntsc_composite_start_mask();

    dma_start_channel_mask(start_mask);

#if defined(VGA)
    vga_enable();
#endif
}

void graphics_set_mode(const enum graphics_mode_t mode) {
    graphics_source_set_mode(mode);
}

void graphics_wait_vblank(void) {
    // A tear on composite is hard to see: the signal is soft and band-limited,
    // so the join has no sharp edge. VGA into a flat panel shows the same tear
    // as a clean line, so when both run the caller is paced by VGA.
#if defined(VGA)
    vga_wait_vblank();
#else
    ntsc_composite_wait_vblank();
#endif
}

void graphics_set_buffer(uint8_t *buffer,
                         const uint16_t width,
                         const uint16_t height) {
    graphics_source_set_buffer(buffer, width, height);

    // A full-size buffer is also the scanout buffer, so the direct modes keep
    // their zero-copy path.
    if (width != GRAPHICS_FRAME_WIDTH || height != GRAPHICS_FRAME_HEIGHT) {
        return;
    }

#if defined(VGA)
    vga_set_framebuffer(buffer);
#endif
#if defined(TFT)
    tft_set_framebuffer(buffer);
#endif
    ntsc_composite_set_framebuffer(buffer);
}

void graphics_set_offset(const int x, const int y) {
    graphics_source_set_offset(x, y);
}

void graphics_set_palette(const uint8_t index, const uint32_t color) {
#if defined(VGA)
    vga_set_palette(index, color);
#endif
#if defined(TFT)
    tft_set_palette(index, color);
#endif
    ntsc_composite_set_palette(index, color);
}

void graphics_set_textbuffer(uint8_t *buffer) {
    text_buffer = buffer;
}

void graphics_set_bgcolor(const uint32_t color888) {
    (void)color888;
}

void graphics_set_flashmode(const bool flash_line, const bool flash_frame) {
#if defined(VGA)
    vga_set_flashmode(flash_line, flash_frame);
#else
    (void)flash_line;
    (void)flash_frame;
#endif
}

uint8_t *graphics_get_framebuffer(void) {
    return GRAPHICS_INITIAL_FRAMEBUFFER;
}

void graphics_present_framebuffer(const uint8_t *framebuffer) {
#if defined(VGA)
    vga_request_framebuffer(framebuffer);
#endif
    ntsc_composite_request_framebuffer(framebuffer);

    ntsc_composite_wait_framebuffer();
#if defined(VGA)
    vga_wait_framebuffer();
#endif
#if defined(TFT)
    // Pushing the panel blocks, so it goes last, after the scanout drivers
    // have taken the new buffer.
    tft_set_framebuffer(framebuffer);
    refresh_lcd();
#endif
}

#endif
