/* Text loops from the same old VGA revision as old_vga.c. */
#include "bench.h"
#include "pico.h"
#ifndef __fast_mul
#define __fast_mul(a, b) ((a) * (b))
#endif
#define VIDEORAM bench_vram
#define vram_offset bench_state.vram_offset
#define cga_blinking bench_state.blink_mask
#define cursor_blink_state bench_state.cursor_phase
#define CURSOR_X bench_state.cursor_x
#define CURSOR_Y bench_state.cursor_y
#define cursor_start bench_state.cursor_start
#define cursor_end bench_state.cursor_end
#define txt_palette_fast bench_text_pairs
void __attribute__((noinline)) __time_critical_func(bench_old_text)(
        enum graphics_mode_t graphics_mode, unsigned screen_line,
        const uint16_t *palette, uint8_t *output) {
    (void)palette;
    uint16_t *output_buffer_16bit = (uint16_t *)output;
    switch (graphics_mode) {
        case TEXTMODE_40x25_COLOR:
        case TEXTMODE_40x25_BW: {
            // "слой" символа
            uint8_t y_div_16 = screen_line / 16;
            register uint8_t glyph_line = screen_line & 15;

            //указатель откуда начать считывать символы
            register uint32_t *text_buffer_line = &VIDEORAM[0x8000 + (vram_offset << 1) + __fast_mul(y_div_16, 80)];

            for (uint8_t column = 0; column < 40; column++) {
                register uint8_t glyph_pixels = font_8x16[(*text_buffer_line++ & 0xFF) * 16 + glyph_line];
                const uint8_t color = *text_buffer_line++;
                const uint16_t *palette_color = &txt_palette_fast[4 * (color & cga_blinking)];

                const uint8_t cursor_active =
                        cursor_blink_state && y_div_16 == CURSOR_Y && column == CURSOR_X &&
                        (cursor_start > cursor_end
                             ? !(glyph_line >= cursor_end << 1 && glyph_line <= cursor_start << 1)
                             : glyph_line >= cursor_start << 1 && glyph_line <= cursor_end << 1);

                if (cga_blinking == 0x7F && (color & 0x80) && cursor_blink_state) {
                    glyph_pixels = 0;
                }
                
                if (cursor_active) {
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                } else {
                    register uint32_t hi_pix = palette_color[glyph_pixels & 3];
                    register uint32_t lo_pix = hi_pix & 0xFF; hi_pix >>= 8;
                    *output_buffer_16bit++ = (lo_pix << 8) | lo_pix;
                    *output_buffer_16bit++ = (hi_pix << 8) | hi_pix;
                    glyph_pixels >>= 2;
                    hi_pix = palette_color[glyph_pixels & 3];
                    lo_pix = hi_pix & 0xFF; hi_pix >>= 8;
                    *output_buffer_16bit++ = (lo_pix << 8) | lo_pix;
                    *output_buffer_16bit++ = (hi_pix << 8) | hi_pix;
                    glyph_pixels >>= 2;
                    hi_pix = palette_color[glyph_pixels & 3];
                    lo_pix = hi_pix & 0xFF; hi_pix >>= 8;
                    *output_buffer_16bit++ = (lo_pix << 8) | lo_pix;
                    *output_buffer_16bit++ = (hi_pix << 8) | hi_pix;
                    glyph_pixels >>= 2;
                    hi_pix = palette_color[glyph_pixels & 3];
                    lo_pix = hi_pix & 0xFF; hi_pix >>= 8;
                    *output_buffer_16bit++ = (lo_pix << 8) | lo_pix;
                    *output_buffer_16bit++ = (hi_pix << 8) | hi_pix;
                }
            }
            return;
        }
        case TEXTMODE_80x25_COLOR:
        case TEXTMODE_80x25_BW: {
            // "слой" символа
            uint8_t y_div_16 = screen_line / 16;
            const uint8_t glyph_line = screen_line & 15;

            //указатель откуда начать считывать символы
            const uint32_t *text_buffer_line = &VIDEORAM[0x8000 + (vram_offset << 1) + __fast_mul(y_div_16, 160)];

            for (uint8_t column = 0; column < 80; column++) {
                uint8_t glyph_pixels = font_8x16[(*text_buffer_line++ & 0xFF) * 16 + glyph_line];
                const uint8_t color = *text_buffer_line++;
                const uint16_t *palette_color = &txt_palette_fast[4 * (color & cga_blinking)];

                const uint8_t cursor_active =
                        cursor_blink_state && y_div_16 == CURSOR_Y && column == CURSOR_X &&
                        (cursor_start > cursor_end
                             ? !(glyph_line >= cursor_end << 1 && glyph_line <= cursor_start << 1)
                             : glyph_line >= cursor_start << 1 && glyph_line <= cursor_end << 1);

                if (cga_blinking == 0x7F && (color & 0x80) && cursor_blink_state) {
                    glyph_pixels = 0;
                }

                if (cursor_active) {
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                } else {
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels];
                }
            }
            return;
        }
    }

}
