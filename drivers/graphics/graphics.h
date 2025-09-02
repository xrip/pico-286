#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"
#include "stdio.h"
#include "stdint.h"

#ifdef TFT
#include "st7789.h"
#endif
#ifdef HDMI
#include "hdmi.h"
#endif
#ifdef VGA
#include "vga.h"
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
    VGA_320x200x256x4 = 0xff,

    COMPOSITE_160x200x16_force = 0x74,
    COMPOSITE_160x200x16 = 0x76,
    // planar VGA
};

// Буффер текстового режима
extern uint8_t *text_buffer;

void graphics_init();

void graphics_set_mode(enum graphics_mode_t mode);

void graphics_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height);

void graphics_set_offset(int x, int y);

void graphics_set_palette(uint8_t index, uint32_t color);

void graphics_set_textbuffer(uint8_t *buffer);

void graphics_set_bgcolor(uint32_t color888);

void graphics_set_flashmode(bool flash_line, bool flash_frame);

void draw_text(const char string[TEXTMODE_COLS + 1], uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor);
void draw_window(const char title[TEXTMODE_COLS + 1], uint32_t x, uint32_t y, uint32_t width, uint32_t height);

void clrScr(uint8_t color);

#ifdef __cplusplus
}
#endif
