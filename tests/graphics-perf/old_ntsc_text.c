/* Text loops from old NTSC graphics.h, same baseline revision as old_ntsc.c. */
#include "bench.h"
#include "pico.h"
#ifndef __fast_mul
#define __fast_mul(a, b) ((a) * (b))
#endif
#define VIDEORAM bench_vram
#define cga_blinking bench_state.blink_mask
#define cursor_blink_state bench_state.cursor_phase
#define CURSOR_X bench_state.cursor_x
#define CURSOR_Y bench_state.cursor_y
#define ntsc_palette bench_ntsc_palette
static const uint8_t cga_brightness[16] = {
    2, // 0 black
    3, // 1 blue
    5, // 2 green
    6, // 3 cyan
    4, // 4 red
    4, // 5 magenta
    6, // 6 brown
    7, // 7 light gray
    4, // 8 dark gray
    5, // 9 light blue
    6, // 10 light green
    7, // 11 light cyan
    5, // 12 light red
    6, // 13 light magenta
    8, // 14 yellow
    9  // 15 white
};


void __attribute__((noinline)) __time_critical_func(bench_old_ntsc_text)(
        enum graphics_mode_t graphics_mode, unsigned row,
        const uint16_t *palette, uint8_t *output) {
    (void)palette;
    const unsigned y = row >> 1;
    uint16_t *buffer_ptr = (uint16_t *)output;
    switch (graphics_mode) {
            case TEXTMODE_40x25_BW:
            case TEXTMODE_40x25_COLOR: {
                const uint8_t glyph_line = y & 7;

                uint32_t *input_buffer_8bit = &VIDEORAM[0x8000 + __fast_mul(y >> 3, 80)];

                uint8_t phase = 0;
                for (int col = 0; col < 40; ++col) {
                    const uint8_t ch = *input_buffer_8bit++;
                    const uint8_t attr = *input_buffer_8bit++;
                    uint8_t glyph_row = font_8x8[ch * 8 + glyph_line];
                    const uint8_t fg = attr & 0xf;
                    const uint8_t bg = attr >> 4;

#pragma GCC unroll(8)
                    for (int bit = 0; bit < 8; ++bit) {
                        uint8_t pixel_color = glyph_row & 1 ? fg : bg;
                        *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + pixel_color * 4 + phase);
                        buffer_ptr += 2;
                        glyph_row >>= 1;
                        phase ^= 2;
                    }
                }
            }
            break;
            case TEXTMODE_80x25_COLOR:
            case TEXTMODE_80x25_BW: {
                const uint8_t glyph_line = y & 7;

                uint32_t *text_row = &VIDEORAM[0x8000 + __fast_mul(y >> 3, 160)];
                uint8_t y_div_8 = y / 8;

                const bool cursor_on_line = cursor_blink_state && (y_div_8 == CURSOR_Y) && (glyph_line >= 4);
                const int cursor_col = cursor_on_line ? CURSOR_X : -1;

                for (int column = 0; column < TEXTMODE_COLS; ++column) {
                    const uint8_t ch = *text_row++;
                    const uint8_t color = *text_row++;

                    if (cursor_col == column) {
                            *(uint64_t *) buffer_ptr = 0x5050505050505050;
                            buffer_ptr += 8;
                    } else {
                        uint8_t glyph_pixels;

                        const uint8_t fg = cga_brightness[color & 0xf];
                        const uint8_t bg = cga_brightness[color >> 4];

                        if (cga_blinking == 0x7F && (color & 0x80) && cursor_blink_state) {
                            glyph_pixels = 0;
                        } else {
                            glyph_pixels = font_8x8[ch * 8 + glyph_line];
                        }

                        *buffer_ptr++ = (glyph_pixels & 0x01) ? fg : bg;
                        *buffer_ptr++ = (glyph_pixels & 0x02) ? fg : bg;
                        *buffer_ptr++ = (glyph_pixels & 0x04) ? fg : bg;
                        *buffer_ptr++ = (glyph_pixels & 0x08) ? fg : bg;
                        *buffer_ptr++ = (glyph_pixels & 0x10) ? fg : bg;
                        *buffer_ptr++ = (glyph_pixels & 0x20) ? fg : bg;
                        *buffer_ptr++ = (glyph_pixels & 0x40) ? fg : bg;
                        *buffer_ptr++ = (glyph_pixels & 0x80) ? fg : bg;
                    }
                }
            }
            break;
    }
}
