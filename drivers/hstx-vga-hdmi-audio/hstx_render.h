#pragma once

#include <stdint.h>
#include <graphics_text.h>

typedef uint32_t __attribute__((may_alias)) hstx_pixel_pair_t;

// The HSTX encoder consumes BGR555: red in bits 0..4, blue in bits 10..14.
// Keep these helpers in the RAM-resident caller, including with LTO enabled.
static inline __attribute__((always_inline)) uint16_t hstx_rgb888_to_bgr555(uint32_t rgb) {
    return (uint16_t)(((rgb >> 19) & 0x001fu) |
                      ((rgb >> 6) & 0x03e0u) |
                      ((rgb << 7) & 0x7c00u));
}

// VGA: a channel level 0..12 is four 2-bit time steps, the extra steps spread out
// (from wbcbz7's VGA/DVI/HDMI HSTX driver, MIT). Each byte has blue, green, red in bits 0..5.
static inline __attribute__((always_inline)) uint32_t hstx_vga_steps(uint32_t value) {
    const uint32_t level = (value * 13u + 6u) >> 8;
    return level / 4u * 0x01010101u +
           (level % 4u == 1u ? 0x1u : level % 4u == 2u ? 0x10001u : level % 4u == 3u ? 0x10101u : 0u);
}

static inline __attribute__((always_inline)) uint32_t hstx_rgb888_to_vga(uint32_t rgb) {
    return hstx_vga_steps(rgb & 0xffu) | hstx_vga_steps(rgb >> 8 & 0xffu) << 2 |
           hstx_vga_steps(rgb >> 16 & 0xffu) << 4;
}

// A successful graphics_text_line() supplies either 40 or 80 cells. Output is
// always 640 pixels, aligned to four bytes, with no intermediate text bitmap.
static inline __attribute__((always_inline)) void hstx_render_text(
        uint16_t *output, const struct graphics_text_line *line,
        const uint16_t *palette) {
    hstx_pixel_pair_t *words = (hstx_pixel_pair_t *)output;
    for (unsigned column = 0; column < line->columns; ++column) {
        const uint16_t cell = graphics_text_cell(line, column);
        const uint8_t attribute = (uint8_t)(cell >> 8);
        const uint8_t glyph = graphics_text_glyph(line, column,
                                                  (uint8_t)cell, attribute);
        const unsigned bg = (attribute >> 4) & (line->blink_enabled ? 7u : 15u);
        const uint32_t background = palette[bg];
        const uint32_t foreground = palette[attribute & 15u];
        if (line->columns == 40u) {
            for (unsigned bit = 0; bit < 8u; ++bit) {
                const uint32_t pixel = (glyph & (1u << bit)) ? foreground : background;
                *words++ = pixel | (pixel << 16);
            }
        } else {
            const uint32_t pairs[4] = {
                background | (background << 16), foreground | (background << 16),
                background | (foreground << 16), foreground | (foreground << 16)
            };
            *words++ = pairs[glyph & 3u];
            *words++ = pairs[(glyph >> 2) & 3u];
            *words++ = pairs[(glyph >> 4) & 3u];
            *words++ = pairs[glyph >> 6];
        }
    }
}
