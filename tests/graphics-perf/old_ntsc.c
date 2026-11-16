/* Graphics loops from pc 519a857001220a54ed0c9224be1d741a84b362a1,
 * drivers/ntsc-tv/graphics.h. Only active pixels; output_buffer equals the
 * active pointer, avoiding the old EGA sync-overwrite defect in the shell. */
#include "bench.h"
#include "pico.h"
#ifndef __fast_mul
#define __fast_mul(a, b) ((a) * (b))
#endif
#define VIDEORAM bench_vram
#define vram_offset bench_state.vram_offset
#define tga_offset bench_state.tga_offset
#define cga_foreground_color bench_state.foreground
#define ntsc_palette bench_ntsc_palette
#define NTSC_LEVEL_BLANK 2
static inline uint32_t spread4_u32(uint32_t b) {
    b &= 0xFFu;
    b = (b | (b << 12)) & 0x000F000Fu;
    b = (b | (b << 6)) & 0x03030303u;
    return (b | (b << 3)) & 0x11111111u;
}
void __attribute__((noinline)) __time_critical_func(bench_old_ntsc)(
        enum graphics_mode_t graphics_mode, unsigned row,
        const uint16_t *palette, uint8_t *output) {
    (void)palette;
    const unsigned y = row >> 1;
    uint16_t *output_buffer = (uint16_t *)output;
    uint16_t *buffer_ptr = output_buffer;
    switch (graphics_mode) {
            case COMPOSITE_160x200x16_force:
            case COMPOSITE_160x200x16:
            case CGA_320x200x4:
            case CGA_320x200x4_BW: {
                uint32_t *input_buffer_8bit = &VIDEORAM[0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13)];

                for (int x = 0; x < 320 / 4; x++) {
                    uint8_t four_pixels = *input_buffer_8bit++;

                    uint8_t color = four_pixels >> 6;
                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color * 4 + 0); // phase 0
                    buffer_ptr += 2;

                    color = four_pixels >> 4 & 3;
                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color * 4 + 2); // phase 2
                    buffer_ptr += 2;

                    color = four_pixels >> 2 & 3;
                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color * 4 + 0); // phase 0
                    buffer_ptr += 2;

                    color = four_pixels & 3;
                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color * 4 + 2); // phase 2
                    buffer_ptr += 2;
                }
                break;
            }
            case CGA_640x200x2: {
                uint32_t *input_buffer_8bit =  &VIDEORAM[0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13)];

                static const uint8_t brightness[2] = {NTSC_LEVEL_BLANK, 6};
                for (int x = 80; x--;) {
                    uint8_t cga_byte = *input_buffer_8bit++;

                    *buffer_ptr++ = brightness[cga_byte >> 7 & 1];
                    *buffer_ptr++ = brightness[cga_byte >> 6 & 1];
                    *buffer_ptr++ = brightness[cga_byte >> 5 & 1];
                    *buffer_ptr++ = brightness[cga_byte >> 4 & 1];
                    *buffer_ptr++ = brightness[cga_byte >> 3 & 1];
                    *buffer_ptr++ = brightness[cga_byte >> 2 & 1];
                    *buffer_ptr++ = brightness[cga_byte >> 1 & 1];
                    *buffer_ptr++ = brightness[cga_byte >> 0 & 1];
                }
                break;
            }

            case TGA_160x200x16: {
                const uint32_t *input_buffer_8bit =  &VIDEORAM[tga_offset +  __fast_mul(y >> 1, 80) + ((y & 1) << 13)];

                for (int x = 0; x < 320 / 4; x++) {
                    const uint8_t two_pixels = *input_buffer_8bit++; // Fetch 2 pixels from TGA memory
                    uint8_t color1 = two_pixels >> 4;
                    uint8_t color2 = two_pixels & 15;

                    if (!color1) color1 = cga_foreground_color;
                    if (!color2) color2 = cga_foreground_color;

                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color1 * 4 + 0); // phase 0
                    buffer_ptr += 2;

                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color1 * 4 + 2); // phase 2
                    buffer_ptr += 2;


                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color2 * 4 + 0); // phase 0
                    buffer_ptr += 2;

                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color2 * 4 + 2); // phase 2
                    buffer_ptr += 2;
                }
                break;
            }
            case TGA_320x200x16: {
                uint32_t *input_buffer_8bit =  &VIDEORAM[tga_offset + ((y & 3) << 13) + __fast_mul(y >> 2, 160)];
                for (int x = 0; x < 320 / 2; x++) {
                    uint8_t two_pixels = *input_buffer_8bit++;

                    uint8_t color = two_pixels >> 4;
                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color * 4 + 0); // phase 0
                    buffer_ptr += 2;

                    color = two_pixels & 15;
                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color * 4 + 2); // phase 2
                    buffer_ptr += 2;
                }
                break;
            }
            case EGA_320x200x16x4: {
                const register uint32_t *ega_row = &VIDEORAM[__fast_mul(y, 40)];
                uint32_t *buffer32 = (uint32_t *)output_buffer;
                // Process 40 dwords (320 pixels) in groups
                for (int x = 0; x < 40; x++) {
                    uint32_t ega_planes = *ega_row++;

                    // Build 8 color nibbles packed into a 32-bit word
                    uint32_t eight_pixels = spread4_u32(ega_planes & 0xFFu)
                                            | spread4_u32(ega_planes >> 8 & 0xFFu) << 1
                                            | spread4_u32(ega_planes >> 16 & 0xFFu) << 2
                                            | spread4_u32(ega_planes >> 24) << 3;

                    // Unroll writing 8 pixels, duplicating horizontally
                    *buffer32++ = *(uint32_t *) (ntsc_palette + (eight_pixels >> 28)  * 4 + 0); // phase 0
                    *buffer32++ = *(uint32_t *) (ntsc_palette + ((eight_pixels >> 24) & 0xF)  * 4 + 2); // phase 2
                    *buffer32++ = *(uint32_t *) (ntsc_palette + ((eight_pixels >> 20) & 0xF)  * 4 + 0); // phase 0
                    *buffer32++ = *(uint32_t *) (ntsc_palette + ((eight_pixels >> 16) & 0xF)   * 4 + 2); // phase 2
                    *buffer32++ = *(uint32_t *) (ntsc_palette + ((eight_pixels >> 12) & 0xF) * 4 + 0); // phase 0
                    *buffer32++ = *(uint32_t *) (ntsc_palette + ((eight_pixels >> 8) & 0xF)  * 4 + 2); // phase 2
                    *buffer32++ = *(uint32_t *) (ntsc_palette + ((eight_pixels >> 4) & 0xF) * 4 + 0); // phase 0
                    *buffer32++ = *(uint32_t *) (ntsc_palette + (eight_pixels & 0xF)  * 4 + 2); // phase 2
                }
                break;
            }
            case VGA_320x200x256x4: {
                const register uint32_t *vga_row = &VIDEORAM[__fast_mul(y, 80)];
                uint32_t *buffer32 = (uint32_t *)buffer_ptr;
                for (int x = 0; x < 80; x++) {
                    const uint32_t four_pixels = *vga_row++;
                    *buffer32++ = *(uint32_t *) (ntsc_palette + (four_pixels & 0xFF) * 4 + 0); // phase 0
                    *buffer32++ = *(uint32_t *) (ntsc_palette + (four_pixels >> 8 & 0xFF) * 4 + 2); // phase 2
                    *buffer32++ = *(uint32_t *) (ntsc_palette + (four_pixels >> 16 & 0xFF) * 4 + 0); // phase 0
                    *buffer32++ = *(uint32_t *) (ntsc_palette + (four_pixels >> 24) * 4 + 2); // phase 2
                }
                break;
            }

            case VGA_320x200x256:
            default: {
                uint32_t *line_buffer = &VIDEORAM[__fast_mul(y, 320)];
                uint32_t *buffer32 = (uint32_t *)buffer_ptr;
                for (int x = 0; x < 320; x++) {
                    const uint8_t color = *line_buffer++;

                    // Write 4 NTSC phase values for this pixel
                    // Using 32-bit writes for efficiency (2 phase values at once)
                    // Phase offset alternates: 0,2,0,2... for proper color encoding
                    const uint32_t phase_offset = (x & 1) << 1;
                    *buffer32++ = *(uint32_t *) (ntsc_palette + color * 4 + phase_offset);
                }
            }
        }
}
