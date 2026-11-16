#pragma GCC optimize("O3", "no-tree-loop-distribute-patterns")
#include "graphics_pc_hdmi.h"
#include <pico.h>

typedef uint32_t hdmi_pixels32_t __attribute__((may_alias));
typedef uint16_t hdmi_pixels16_t __attribute__((may_alias));

static inline uint32_t spread8(uint32_t plane) {
    plane = (plane | (plane << 12)) & 0x000f000fu;
    plane = (plane | (plane << 6)) & 0x03030303u;
    return (plane | (plane << 3)) & 0x11111111u;
}

static inline uint32_t pack_planes(uint32_t planes) {
    return spread8(planes & 255u) | spread8((planes >> 8) & 255u) << 1 |
           spread8((planes >> 16) & 255u) << 2 | spread8(planes >> 24) << 3;
}

static inline uint32_t four_nibbles(uint32_t pixels) {
    pixels = (pixels | (pixels << 8)) & 0x00ff00ffu;
    pixels = (pixels | (pixels << 4)) & 0x0f0f0f0fu;
    return __builtin_bswap32(pixels);
}

// 252..255 are this driver's sync codes: a picture shows them as colour 0.
static inline uint32_t picture_color(uint32_t color) {
    return color >= 252u ? 0u : color;
}

bool __time_critical_func(graphics_pc_hdmi_render)(
        enum graphics_mode_t mode, const uint32_t *restrict vram,
        unsigned row, uint8_t *restrict output) {
    hdmi_pixels32_t *words = (hdmi_pixels32_t *)output;
    if (vram == NULL || row >= 200u) {
#pragma GCC unroll 4
        for (unsigned x = 0; x < 80; ++x) words[x] = 0;
        return true;
    }
    switch (mode) {
        case CGA_320x200x4:
        case CGA_320x200x4_BW: {
            graphics_pc_state_t state;
            graphics_pc_snapshot(&state);
            const unsigned base = (0x8000u + (state.vram_offset << 1) +
                    (row >> 1) * 80u + ((row & 1u) << 13)) & 0xffffu;
            if (base > 65536u - 80u) return false;
            const uint32_t *source = vram + base;
#pragma GCC unroll 4
            for (unsigned x = 0; x < 80; ++x) {
                uint32_t pixels = (uint8_t)*source++;
                pixels = (pixels | (pixels << 12)) & 0x000f000fu;
                pixels = (pixels | (pixels << 6)) & 0x03030303u;
                *words++ = __builtin_bswap32(pixels);
            }
            return true;
        }
        case COMPOSITE_160x200x16_force:
        case COMPOSITE_160x200x16:
        case TGA_160x200x16: {
            graphics_pc_state_t state;
            graphics_pc_snapshot(&state);
            const unsigned offset = pc_bank_offset(&state, (row >> 1) * 80u);
            if (offset > 0x2000u - 80u) return false; // the line wraps inside its bank
            const unsigned base = (state.tga_offset + offset + ((row & 1u) << 13)) & 0xffffu;
            if (base > 65536u - 80u) return false;
            const uint32_t *source = vram + base;
#pragma GCC unroll 4
            for (unsigned x = 0; x < 80; ++x) {
                const uint8_t bits = *source++;
                const uint32_t left = bits >> 4;
                const uint32_t right = bits & 15u;
                *words++ = left | left << 8 | right << 16 | right << 24;
            }
            return true;
        }
        case TGA_320x200x16: {
            graphics_pc_state_t state;
            graphics_pc_snapshot(&state);
            const unsigned offset = pc_bank_offset(&state, (row >> 2) * 160u);
            if (offset > 0x2000u - 160u) return false;
            const unsigned base = (state.tga_offset + offset + (row & 3u) * 8192u) & 0xffffu;
            if (base > 65536u - 160u) return false;
            const uint32_t *source = vram + base;
            hdmi_pixels16_t *pairs = (hdmi_pixels16_t *)output;
#pragma GCC unroll 4
            for (unsigned x = 0; x < 160; ++x) {
                const uint8_t bits = *source++;
                *pairs++ = (bits >> 4) | (uint16_t)(bits & 15u) << 8;
            }
            return true;
        }
        case EGA_320x200x16x4: {
            const unsigned base = graphics_pc_ega_line(row) & 0xffffu;
            if (base > 65536u - 40u) return false;
            const uint32_t *source = vram + base;
#pragma GCC unroll 2
            for (unsigned x = 0; x < 40; ++x) {
                const uint32_t packed = pack_planes(*source++);
                *words++ = four_nibbles(packed >> 16);
                *words++ = four_nibbles(packed & 0xffffu);
            }
            return true;
        }
        case VGA_320x200x256x4: {
            const uint32_t *source = vram + row * 80u;
            // Four indices per word.
#pragma GCC unroll 4
            for (unsigned x = 0; x < 80; ++x) {
                const uint32_t pixels = source[x];
                words[x] = picture_color(pixels & 255u) | picture_color((pixels >> 8) & 255u) << 8 |
                           picture_color((pixels >> 16) & 255u) << 16 | picture_color(pixels >> 24) << 24;
            }
            return true;
        }
        case VGA_320x200x256: {
            const uint32_t *source = vram + row * 320u;
#pragma GCC unroll 4
            for (unsigned x = 0; x < 320; ++x) {
                const uint8_t color = *source++;
                *output++ = (uint8_t)picture_color(color);
            }
            return true;
        }
        default:
            return false;
    }
}
