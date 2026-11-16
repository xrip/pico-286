/* Graphics loops copied from pc commit 519a857001220a54ed0c9224be1d741a84b362a1,
 * drivers/vga-nextgen/vga.c. Only the IRQ/DMA shell is replaced by arguments.
 * Keep this baseline independent from the new renderer.
 */
#include "bench.h"
#include "pico.h"
#ifndef __fast_mul
#define __fast_mul(a, b) ((a) * (b))
#endif
#define VIDEORAM bench_vram
#define vram_offset bench_state.vram_offset
#define tga_offset bench_state.tga_offset
#define cga_foreground_color bench_state.foreground
static inline uint32_t spread8(uint32_t plane) {
    plane = (plane | (plane << 12)) & 0x000F000Fu;
    plane = (plane | (plane <<  6)) & 0x03030303u;
    plane = (plane | (plane <<  3)) & 0x11111111u;
    return plane;
}

// Merge 4 plane bytes [P3|P2|P1|P0] into 8 nibbles (pixel color indices).
static inline uint32_t ega_pack8_from_planes(const uint32_t ega_planes) {
    const uint32_t pixel1 = spread8(ega_planes        & 0xFFu);
    const uint32_t pixel2 = spread8((ega_planes >> 8) & 0xFFu);
    const uint32_t pixel3 = spread8((ega_planes >>16) & 0xFFu);
    const uint32_t pixel4 = spread8(ega_planes >>24);

    return pixel1 | pixel2 << 1 | pixel3 << 2 | pixel4 << 3;
}

void __attribute__((noinline)) __time_critical_func(bench_old_vga)(
        enum graphics_mode_t graphics_mode, unsigned screen_line,
        const uint16_t *current_palette, uint8_t *output) {
    const uint8_t videomode = (uint8_t)graphics_mode;
    const unsigned y = screen_line >> 1;
    uint16_t *output_buffer_16bit = (uint16_t *)output;
    uint8_t *output_buffer_8bit;
    switch (graphics_mode) {
        case CGA_320x200x4:
        case CGA_320x200x4_BW: {
            const register uint32_t *cga_row = &VIDEORAM[0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13)];
            //2bit buf
            for (int x = 320 / 4; x--;) {
                const uint8_t cga_byte = *cga_row++ & 0xFF;

                uint8_t color = cga_byte >> 6;
                *output_buffer_16bit++ = current_palette[color];
                color = (cga_byte >> 4) & 3;
                *output_buffer_16bit++ = current_palette[color];
                color = (cga_byte >> 2) & 3;
                *output_buffer_16bit++ = current_palette[color];
                color = (cga_byte >> 0) & 3;
                *output_buffer_16bit++ = current_palette[color];
            }
            break;
        }
        case CGA_640x200x2: {
            const register uint32_t *cga_row = &VIDEORAM[0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13)];
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;
            //1bit buf
            for (int x = 640 / 8; x--;) {
                uint8_t cga_byte = *cga_row++ & 0xFF;

                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 7)), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 6) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 5) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 4) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 3) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 2) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 1) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte) & 1), cga_foreground_color)];
            }
            break;
        }
        case HERC_640x480x2_90:
            if (screen_line >= 348) break;
        case HERC_640x480x2:
            //4bit buf
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;
            register uint32_t *hercules_row;
            if (graphics_mode == HERC_640x480x2_90) {
                hercules_row = &VIDEORAM[5 + (screen_line & 3) * 8192 + __fast_mul((screen_line >> 2), 90)];
            } else {
                hercules_row = &VIDEORAM[(screen_line & 3) * 8192 + __fast_mul((screen_line >> 2), 90)];
            }
            // Each byte containing 8 pixels
            for (int x = 640 / 8; x--;) {
                const uint8_t cga_byte = *hercules_row++;

                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 7) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 6) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 5) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 4) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 3) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 2) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 1) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 0) & 1), 15)];
            }
            break;
        case COMPOSITE_160x200x16_force:
        case COMPOSITE_160x200x16:
        case TGA_160x200x16: {
            const register uint32_t *tga_row = &VIDEORAM[tga_offset + __fast_mul(y >> 1, 80) + ((y & 1) << 13)];
            for (int x = 320 / 4; x--;) {
                uint8_t two_pixels = *tga_row++; // Fetch 2 pixels from TGA memory
                uint8_t pixel1_color = two_pixels >> 4;
                uint8_t pixel2_color = two_pixels & 15;

                // TODO: fixme by updating palette instead of branching!!
                if (!pixel1_color && videomode == 0x8) pixel1_color = cga_foreground_color;
                if (!pixel2_color && videomode == 0x8) pixel2_color = cga_foreground_color;

                *output_buffer_16bit++ = current_palette[pixel1_color];
                *output_buffer_16bit++ = current_palette[pixel1_color];
                *output_buffer_16bit++ = current_palette[pixel2_color];
                *output_buffer_16bit++ = current_palette[pixel2_color];
            }
            break;
        }

        case TGA_320x200x16: {
            //4bit buf
            const register uint32_t *tga_row = &VIDEORAM[tga_offset + (y & 3) * 8192 + __fast_mul(y >> 2, 160)];
            for (int x = 320 / 2; x--;) {
                const uint8_t two_pixels = *tga_row++; // Fetch 2 pixels from TGA memory
                *output_buffer_16bit++ = current_palette[two_pixels >> 4];
                *output_buffer_16bit++ = current_palette[two_pixels & 15];
            }
            break;
        }
        case TGA_640x200x16: {
            //4bit buf
            const register uint32_t *tga_row = &VIDEORAM[__fast_mul(y, 320)];
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;

            for (int x = 640 / 2; x--;) {
                const uint8_t two_pixels = *tga_row++; // Fetch 2 pixels from TGA memory
                *output_buffer_8bit++ = current_palette[two_pixels >> 4];
                *output_buffer_8bit++ = current_palette[two_pixels & 15];
            }
            break;
        }
        case VGA_640x480x2: {
            const register uint32_t *vga_row = &VIDEORAM[__fast_mul(screen_line, 80)];
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;
            for (int x = 640 / 8; x--;) {
                const uint8_t cga_byte = *vga_row++;

                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 7) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 6) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 5) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 4) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 3) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 2) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 1) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 0) & 1), 15)];
            }
            break;
        }
        case EGA_320x200x16x4: {
            const register uint32_t *ega_row = &VIDEORAM[__fast_mul(y, 40)];

            // Process 40 dwords (320 pixels) in groups
            for (int x = 0; x < 40; x++) {
                uint32_t ega_planes = *ega_row++;

                // Build 8 color nibbles packed into a 32-bit word
                uint32_t eight_pixels = ega_pack8_from_planes(ega_planes);

                // Unroll writing 8 pixels, duplicating horizontally
                *output_buffer_16bit++ = current_palette[eight_pixels >> 28];
                *output_buffer_16bit++ = current_palette[eight_pixels >> 24 & 0xF];
                *output_buffer_16bit++ = current_palette[eight_pixels >> 20 & 0xF];
                *output_buffer_16bit++ = current_palette[eight_pixels >> 16 & 0xF];
                *output_buffer_16bit++ = current_palette[eight_pixels >> 12 & 0xF];
                *output_buffer_16bit++ = current_palette[eight_pixels >> 8 & 0xF];
                *output_buffer_16bit++ = current_palette[eight_pixels >> 4 & 0xF];
                *output_buffer_16bit++ = current_palette[eight_pixels & 0xF];
            }
            break;
        }
        case EGA_640x200x16x4: {
            const register uint32_t *ega_row = &VIDEORAM[__fast_mul(y, 80)];
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;
            for (int i = 0; i < 80; ++i) {
                uint32_t ega_planes = *ega_row++;

                // Build 8 color nibbles packed into a 32-bit word
                uint32_t eight_pixels = ega_pack8_from_planes(ega_planes);

                // Unroll writing 8 pixels, duplicating horizontally
                *output_buffer_8bit++ = current_palette[eight_pixels >> 28];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 24 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 20 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 16 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 12 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 8 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 4 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels & 0xF];
            }
            break;
        }
        case VGA_640x480x16: /* VGA 640x480 16-color */
        case EGA_640x350x16x4: /* EGA 640x350 16-color */ {
            const register uint32_t *ega_row = &VIDEORAM[__fast_mul(screen_line, 80)];
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;

            for (int i = 0; i < 80; ++i) {
                uint32_t ega_planes = *ega_row++;

                // Build 8 color nibbles packed into a 32-bit word
                uint32_t eight_pixels = ega_pack8_from_planes(ega_planes);

                // Unroll writing 8 pixels, duplicating horizontally
                *output_buffer_8bit++ = current_palette[eight_pixels >> 28];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 24 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 20 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 16 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 12 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 8 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels >> 4 & 0xF];
                *output_buffer_8bit++ = current_palette[eight_pixels & 0xF];
            }
            break;
        }
        case VGA_320x200x256x4: {
            const register uint32_t *vga_row = &VIDEORAM[__fast_mul(y, 80)];

            for (int x = 0; x < 80; x++) {
                const uint32_t four_pixels = *vga_row++;
                *output_buffer_16bit++ = current_palette[four_pixels & 0xFF];
                *output_buffer_16bit++ = current_palette[(four_pixels >> 8) & 0xFF];
                *output_buffer_16bit++ = current_palette[(four_pixels >> 16) & 0xFF];
                *output_buffer_16bit++ = current_palette[(four_pixels >> 24)];
            }
            break;
        }
        case VGA_320x200x256:
        default:
            const uint32_t *vga_row = &VIDEORAM[__fast_mul(y, 320)];
            for (int x = 320; x--;) {
                *output_buffer_16bit++ = current_palette[*vga_row++ & 0xFF];
            }
            break;
    }
}
