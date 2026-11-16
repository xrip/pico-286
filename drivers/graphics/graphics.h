#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"
#include "stdio.h"
#include "stdint.h"

#define GRAPHICS_FRAME_WIDTH 320
#define GRAPHICS_FRAME_HEIGHT 240
#define GRAPHICS_COLOR_COUNT 256

// Native text and high-resolution graphics use all 640 output samples.
#define GRAPHICS_TEXT_SAMPLES 640

#ifdef TFT
#include "st7789.h"
#endif
#ifdef HDMI
#include "hdmi.h"
#endif
#ifdef HDMI_HSTX
#include "hdmi_hstx.h"
#endif
#ifdef VGA
#include "vga.h"
#endif
#ifdef NTSC_TV
#include "ntsc-composite.h"
#endif
#ifdef TV
#include "tv.h"
#endif
#ifdef SOFTTV
#include "tv-software.h"
#endif


#include "font4x6.h"
#include "font6x8.h"
#include "font8x8.h"
#include "font8x16.h"

#if defined(HDMI) || defined(HDMI_HSTX)
#include "graphics_text.h"
#endif

enum graphics_mode_t {
    TEXTMODE_40x25_BW,
    TEXTMODE_40x25_COLOR,
    TEXTMODE_80x25_BW,
    TEXTMODE_80x25_COLOR,
    CGA_320x200x4 = 4,
    CGA_320x200x4_BW = 5,

    CGA_640x200x2 = 6,
    HERC_640x480x2 = 7,
    HERC_640x480x2_90 = 0x1e,
    TGA_160x200x16 = 8,

    TGA_320x200x16 = 9,
    TGA_640x200x16 = 0xa,

    EGA_320x200x16x4 = 0x0d,
    EGA_640x200x16x4 = 0x0e,
    EGA_640x350x16x4 = 0x10,

    VGA_640x480x2 = 0x11,
    VGA_640x480x16 = 0x12,

    VGA_320x200x256 = 0x13,
    VGA_320x240x256 = 0x14,
    VGA_320x200x256x4 = 0xff,

    COMPOSITE_160x200x16_force = 0x74,
    COMPOSITE_160x200x16 = 0x76,
    // planar VGA

    // Modes kept for the Murmulator emulator family. The scanline composer in
    // drivers/graphics/graphics_modes.c gives these their picture layout.
    TEXTMODE_DEFAULT = 0x100,   // 80 x 30 text (HDMI: supplied font, normally 8x16)
    TEXTMODE_53x30 = 0x101,     // legacy name: 40 x 30 text at double width
    TEXTMODE_160x100 = 0x102,   // same layout as TEXTMODE_DEFAULT
    GRAPHICSMODE_DEFAULT = 0x103, // app buffer, 1x, centred on the 320-pixel line
    GG_160x144 = 0x104,         // Game Gear window, 1x, centred
    GG_160x144x4x3 = 0x105,     // Game Gear window, 2x wide, 2:3 line scale
    TEXTMODE_80x30 = TEXTMODE_DEFAULT,
};

// Буффер текстового режима
extern uint8_t *text_buffer;

void graphics_init(void);

// HSTX clock setup runs on core 0 before peripherals or core 1 start.
void graphics_prepare_clock(void);

#if defined(GRAPHICS_PC_EMULATOR)
uint32_t graphics_pc_palette_color(uint8_t index, uint32_t color, bool composite_output);
#endif

void graphics_set_mode(enum graphics_mode_t mode);

// Waits for the start of the next output frame. With a single framebuffer a
// producer calls this and then draws top to bottom, staying in front of the
// beam, so no half-drawn row reaches the screen and no second buffer is needed.
void graphics_wait_vblank(void);

void graphics_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height);

void graphics_set_offset(int x, int y);

void graphics_set_palette(uint8_t index, uint32_t color);

void graphics_set_textbuffer(uint8_t *buffer);

void graphics_set_bgcolor(uint32_t color888);

void graphics_set_flashmode(bool flash_line, bool flash_frame);

uint8_t *graphics_get_framebuffer(void);

void graphics_present_framebuffer(const uint8_t *framebuffer);

void draw_text(const char *string, uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor);
void draw_window(const char title[TEXTMODE_COLS + 1], uint32_t x, uint32_t y, uint32_t width, uint32_t height);

void clrScr(uint8_t color);

#ifdef __cplusplus
}
#endif
