/* Graphics loops from pc 519a857001220a54ed0c9224be1d741a84b362a1,
 * drivers/hdmi/hdmi.c. No IRQ/DMA shell; active pixels only. */
#include "bench.h"
#include "pico.h"
#ifndef __fast_mul
#define __fast_mul(a, b) ((a) * (b))
#endif
#define VIDEORAM bench_vram
#define vram_offset bench_state.vram_offset
#define tga_offset bench_state.tga_offset
#define HDMI_CTRL_BASE_INDEX 252
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

void __attribute__((noinline)) __time_critical_func(bench_old_hdmi)(
        enum graphics_mode_t graphics_mode, unsigned row,
        const uint16_t *palette, uint8_t *output_buffer) {
    (void)palette;
    const unsigned y = row >> 1;
    switch (graphics_mode) {
            case CGA_320x200x4:
            case CGA_320x200x4_BW: {
                const register uint32_t *cga_row = &VIDEORAM[0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13)];
                //2bit buf
                for (int x = 320 / 4; x--;) {
                    const uint8_t cga_byte = *cga_row++;

                    uint8_t color = cga_byte >> 6;
                    *output_buffer++ = color;
                    color = (cga_byte >> 4) & 3;
                    *output_buffer++ = color;
                    color = (cga_byte >> 2) & 3;
                    *output_buffer++ = color;
                    color = (cga_byte >> 0) & 3;
                    *output_buffer++ = color;
                }
                break;
            }
            case COMPOSITE_160x200x16_force:
            case COMPOSITE_160x200x16:
            case TGA_160x200x16: {
                const register uint32_t *tga_row = &VIDEORAM[tga_offset + __fast_mul(y >> 1, 80) + ((y & 1) << 13)];
                uint32_t *output_buffer32 = (uint32_t *)output_buffer;
                for (int x = 320 / 4; x--;) {
                    uint8_t two_pixels = *tga_row++; // Fetch 2 pixels from TGA memory
                    uint8_t pixel1_color = two_pixels >> 4;
                    uint8_t pixel2_color = two_pixels & 15;
                    *output_buffer32++ = pixel1_color | (pixel1_color << 8) | (pixel2_color << 16) | (pixel2_color << 24);
                }
                break;
            }

            case TGA_320x200x16: {
                //4bit buf
                const register uint32_t *tga_row = &VIDEORAM[tga_offset + (y & 3) * 8192 + __fast_mul(y >> 2, 160)];
                for (int x = 320 / 2; x--;) {
                    const uint8_t two_pixels = *tga_row++; // Fetch 2 pixels from TGA memory
                    *output_buffer++ = two_pixels >> 4;
                    *output_buffer++ = two_pixels & 15;
                }
                break;
            }
            case EGA_320x200x16x4: {
                const register uint32_t *ega_row = &VIDEORAM[__fast_mul(y, 40)];

                // Process 40 dwords (320 pixels) in groups
                for (int x = 0; x < 40; x++) {
                    const uint32_t ega_planes = *ega_row++;

                    // Build 8 color nibbles packed into a 32-bit word
                    const uint32_t eight_pixels = ega_pack8_from_planes(ega_planes);

                    // Unroll writing 8 pixels, duplicating horizontally
                    *output_buffer++ = eight_pixels >> 28;
                    *output_buffer++ = eight_pixels >> 24 & 0xF;
                    *output_buffer++ = eight_pixels >> 20 & 0xF;
                    *output_buffer++ = eight_pixels >> 16 & 0xF;
                    *output_buffer++ = eight_pixels >> 12 & 0xF;
                    *output_buffer++ = eight_pixels >> 8 & 0xF;
                    *output_buffer++ = eight_pixels >> 4 & 0xF;
                    *output_buffer++ = eight_pixels & 0xF;
                }
                break;
            }
            case VGA_320x200x256x4: {
                const register uint32_t *vga_row = &VIDEORAM[__fast_mul(y, 80)];
                uint32_t *output_buffer32 = (uint32_t *)output_buffer;
                for (int x = 0; x < 80; x++) {
                    *output_buffer32++ = *vga_row++;
                }
                break;
            }
            case VGA_320x200x256:
            default: {
                const uint32_t *vga_row = &VIDEORAM[__fast_mul(y, 320)];
                for (int x = 320; x--;) {
                    const uint8_t color = *vga_row++ & 0xFF;
                    *output_buffer++ = color >= HDMI_CTRL_BASE_INDEX ? 0 : color;
                }
                break;
            }
        }

}
