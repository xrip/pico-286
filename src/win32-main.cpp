#include <windows.h>
#include <cwchar>
#include "MiniFB.h"
#include "emulator/emulator.h"
#include "emulator/includes/font8x16.h"
#include "emulator/includes/font8x8.h"
#include "emu8950.h"

static uint32_t ALIGN(4, SCREEN[640 * 480]);
uint8_t ALIGN(4, DEBUG_VRAM[80 * 10]) = {0};

int cursor_blink_state = 0;
uint8_t log_debug = 0;

HANDLE hComm;
DWORD bytesWritten;
DCB dcb;
extern OPL *emu8950_opl;

#define AUDIO_BUFFER_LENGTH ((SOUND_FREQUENCY / 10))
static int16_t audio_buffer[AUDIO_BUFFER_LENGTH * 2] = {};
static int sample_index = 0;

extern "C" void adlib_getsample(int16_t *sndptr, intptr_t numsamples);

static inline void draw_8_pixels_to_rgb(uint32_t vram_word, uint32_t* rgb_out) {
    // Extract all planes at once
    uint8_t plane0 = vram_word & 0xFF;
    uint8_t plane1 = (vram_word >> 8) & 0xFF;
    uint8_t plane2 = (vram_word >> 16) & 0xFF;
    uint8_t plane3 = (vram_word >> 24) & 0xFF;

    // Process 8 pixels in parallel using lookup table
    static uint8_t pixel_extract_lut[256][8];
    static int lut_initialized = 0;

    if (!lut_initialized) {
        // Build LUT once - extracts individual bits from byte
        for (int byte = 0; byte < 256; byte++) {
            for (int bit = 0; bit < 8; bit++) {
                pixel_extract_lut[byte][bit] = (byte >> (7-bit)) & 1;
            }
        }
        lut_initialized = 1;
    }

    // Use LUT to extract and combine all pixels
    for (int pix = 0; pix < 8; pix++) {
        uint8_t color = pixel_extract_lut[plane0][pix] |
                       (pixel_extract_lut[plane1][pix] << 1) |
                       (pixel_extract_lut[plane2][pix] << 2) |
                       (pixel_extract_lut[plane3][pix] << 3);

        // Write RGB directly
        *rgb_out++ = vga_palette[color];
    }
}
extern "C" union vga_sequencer_t {
    struct __attribute__((packed)) {
        uint8_t reset; // Reset Register (0x00)
        uint8_t clocking_mode; // Clocking Mode Register (0x01)
        uint8_t map_mask; // Map Mask Register (0x02)
        uint8_t char_map_select; // Character Map Select Register (0x03)
        uint8_t memory_mode; // Memory Mode Register (0x04)
    };

    uint8_t registers[5];
} vga_sequencer;

static INLINE void renderer() {
    // http://www.techhelpmanual.com/114-video_modes.html
    // http://www.techhelpmanual.com/89-video_memory_layouts.html
    // https://mendelson.org/wpdos/videomodes.txt
    static uint8_t v = 0;
    if (v != videomode) {
        printf("videomode %x %x\n", videomode, v);
        v = videomode;
        //vram_offset = 0;
    }


    //memcpy(localVRAM, VIDEORAM + 0x18000 + (vram_offset << 1), VIDEORAM_SIZE);
    uint8_t *vidramptr = (uint8_t *) (VIDEORAM + 0x8000 + ((vram_offset & 0xffff) << 1));
    uint8_t cols = 80;
    for (int y = 0; y < 480; y++) {
        if (y >= 399)
            port3DA = 8;
        else
            port3DA = 0;

        if (y & 1)
            port3DA |= 1;

        uint32_t *pixels = SCREEN + y * 640;
        if (y < 400)
            switch (videomode) {
                case 0x00:
                case 0x01: {
                    uint16_t y_div_16 = y / 16; // Precompute y / 16
                    uint8_t glyph_line = (y / 2) % 8; // Precompute y % 8 for font lookup
                    // Calculate screen position
                    uint32_t *text_buffer_line = &VIDEORAM[0x8000 + y_div_16 * 80];

                    for (int column = 0; column < 40; column++) {
                        uint8_t glyph_pixels = font_8x8[*text_buffer_line++ * 8 + glyph_line]; // Glyph row from font
                        uint8_t color = *text_buffer_line++; // Color attribute

                        // Cursor blinking check
                        uint8_t cursor_active = cursor_blink_state &&
                                                y_div_16 == CURSOR_Y && column == CURSOR_X &&
                                                glyph_line >= cursor_start && glyph_line <= cursor_end;

                        for (uint8_t bit = 0; bit < 8; bit++) {
                            uint8_t pixel_color;
                            if (cursor_active) {
                                pixel_color = color & 0x0F; // Cursor foreground color
                            } else if (cga_blinking && color >> 7 & 1) {
                                pixel_color = cursor_blink_state ? color >> 4 & 0x7 : color & 0x7; // Blinking background color
                            } else {
                                pixel_color = glyph_pixels >> bit & 1 ? color & 0x0f : color >> 4;
                                // Foreground or background color
                            }

                            // Write the pixel twice (horizontal scaling)
                            *pixels++ = *pixels++ = cga_palette[pixel_color];
                        }
                    }


                    break;
                }
                case 0x07:
                case 0x02:
                case 0x03: {
                    uint16_t y_div_16 = y / 16; // Precompute y / 16
                    uint8_t glyph_line = y % 16; // Precompute y % 8 for font lookup

                    // Calculate screen position
                    uint32_t *text_row = &VIDEORAM[0x8000 + y_div_16 * 160];
                    // printf("line start %x\n", 0x8000 + y_div_16 * 160);
                    for (uint8_t column = 0; column < 80; column++) {
                        // Access vidram and font data once per character
                        uint8_t charcode = *text_row++ & 0xff;
                        uint8_t color = *text_row++ & 0xff; // Color attribute
                        // printf("%c", charcode);
                        uint8_t glyph_row = font_8x16[charcode * 16 + glyph_line]; // Glyph row from font

                        // Cursor blinking check
                        uint8_t cursor_active =
                                cursor_blink_state && y_div_16 == CURSOR_Y && column == CURSOR_X &&
                                (cursor_start > cursor_end
                                     ? !(glyph_line >= cursor_end << 1 &&
                                         glyph_line <= cursor_start << 1)
                                     : glyph_line >= cursor_start << 1 && glyph_line <= cursor_end << 1);

                        // Unrolled bit loop: Write 8 pixels with scaling (2x horizontally)
                        for (int bit = 0; bit < 8; bit++) {
                            uint8_t pixel_color;
                            if (cursor_active) {
                                pixel_color = color & 0x0F; // Cursor foreground color
                            } else if (cga_blinking && color >> 7 & 1) {
                                if (cursor_blink_state) {
                                    pixel_color = color >> 4 & 0x7; // Blinking background color
                                } else {
                                    pixel_color = glyph_row >> bit & 1 ? color & 0x0f : (color >> 4 & 0x7);
                                }
                            } else {
                                // Foreground or background color
                                pixel_color = glyph_row >> bit & 1 ? color & 0x0f : color >> 4;
                            }

                            *pixels++ = cga_palette[pixel_color];
                        }
                    }
                    break;
                }
                case 0x04:
                case 0x05: {
                    uint32_t *cga_row = &VIDEORAM[0x8000 + ((y / 2 >> 1) * 80 + (y / 2 & 1) * 8192)]; // Precompute CGA row pointer
                    uint8_t *current_cga_palette = (uint8_t *) cga_gfxpal[cga_colorset][cga_intensity];

                    // Each byte containing 4 pixels
                    for (int x = 320 / 4; x--;) {
                        uint8_t cga_byte = *cga_row++ & 0xFF;

                        // Extract all four 2-bit pixels from the CGA byte
                        // and write each pixel twice for horizontal scaling
                        *pixels++ = *pixels++ = cga_palette[cga_byte >> 6 & 3
                                                                ? current_cga_palette[cga_byte >> 6 & 3]
                                                                : cga_foreground_color];
                        *pixels++ = *pixels++ = cga_palette[cga_byte >> 4 & 3
                                                                ? current_cga_palette[cga_byte >> 4 & 3]
                                                                : cga_foreground_color];
                        *pixels++ = *pixels++ = cga_palette[cga_byte >> 2 & 3
                                                                ? current_cga_palette[cga_byte >> 2 & 3]
                                                                : cga_foreground_color];
                        *pixels++ = *pixels++ = cga_palette[cga_byte >> 0 & 3
                                                                ? current_cga_palette[cga_byte >> 0 & 3]
                                                                : cga_foreground_color];
                    }
                    break;
                }
                case 0x06: {
                    uint8_t *cga_row = vidramptr + (y / 2 >> 1) * 80 + (y / 2 & 1) * 8192; // Precompute row start

                    // Each byte containing 8 pixels
                    for (int x = 640 / 8; x--;) {
                        uint8_t cga_byte = *cga_row++;

                        *pixels++ = cga_palette[(cga_byte >> 7 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(cga_byte >> 6 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(cga_byte >> 5 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(cga_byte >> 4 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(cga_byte >> 3 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(cga_byte >> 2 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(cga_byte >> 1 & 1) * cga_foreground_color];
                        *pixels++ = cga_palette[(cga_byte >> 0 & 1) * cga_foreground_color];
                    }

                    break;
                }
                case 0x1e:
                    cols = 90;
                    vram_offset = 5;
                    if (y >= 348) break;
                case 0x711: {
                    uint32_t *cga_row = &VIDEORAM[(y & 3) * 8192 + y / 4 * cols];
                    // Each byte containing 8 pixels
                    for (int x = 640 / 8; x--;) {
                        uint8_t cga_byte = *cga_row++ & 0xFF;

                        *pixels++ = cga_palette[(cga_byte >> 7 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 6 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 5 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 4 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 3 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 2 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 1 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 0 & 1) * 15];
                    }

                    break;
                }

                case 0x8:
                case 0x74: /* 160x200x16    */
                case 0x76: /* cga composite / tandy */ {
                    uint32_t *palette;
                    switch (videomode) {
                        case 0x08:
                            palette = tga_palette;
                            break;
                        case 0x74:
                            palette = cga_composite_palette[cga_intensity << 1];
                            break;
                        case 0x76:
                            palette = cga_composite_palette[0];
                            break;
                    }

                    uint8_t *cga_row = vidramptr + (y / 2 >> 1) * 80 + (y / 2 & 1) * 8192; // Precompute row start

                    // Each byte containing 8 pixels
                    for (int x = 640 / 8; x--;) {
                        uint8_t cga_byte = *cga_row++; // Fetch 8 pixels from TGA memory
                        uint8_t color1 = cga_byte >> 4 & 15;
                        uint8_t color2 = cga_byte & 15;

                        if (!color1 && videomode == 0x8) color1 = cga_foreground_color;
                        if (!color2 && videomode == 0x8) color2 = cga_foreground_color;

                        *pixels++ = *pixels++ = *pixels++ = *pixels++ = palette[color1];
                        *pixels++ = *pixels++ = *pixels++ = *pixels++ = palette[color2];
                    }

                    break;
                }
                case 0x09: /* tandy 320x200 16 color */ {
                    uint32_t *tga_row = &VIDEORAM[tga_offset + (y / 2 & 3) * 8192 + y / 8 * 160];
                    //                  uint8_t *tga_row = &VIDEORAM[tga_offset+(((y / 2) & 3) * 8192) + ((y / 8) * 160)];

                    // Each byte containing 4 pixels
                    for (int x = 320 / 2; x--;) {
                        uint8_t tga_byte = *tga_row++ & 0xFF;
                        *pixels++ = *pixels++ = tga_palette[tga_palette_map[tga_byte >> 4 & 15]];
                        *pixels++ = *pixels++ = tga_palette[tga_palette_map[tga_byte & 15]];
                    }
                    break;
                }
                case 0x0a: /* tandy 640x200 16 color */ {
                    uint8_t *tga_row = (uint8_t *) (VIDEORAM + y / 2 * 320);

                    // Each byte contains 2 pixels
                    for (int x = 640 / 2; x--;) {
                        uint8_t tga_byte = *tga_row++;
                        *pixels++ = tga_palette[tga_palette_map[tga_byte >> 4 & 15]];
                        *pixels++ = tga_palette[tga_palette_map[tga_byte & 15]];
                    }
                    break;
                }
                case 0x0D: /* EGA 320x200 16-color */ {
                    if (y >= 400) break;
                    uint32_t* vram_ptr = &VIDEORAM[(y / 2) * (320 / 8)];
                    for (int i = 0; i < (320 / 8); ++i) {
                        uint32_t eight_pixels = vram_ptr[i];
                        uint8_t plane0 =  eight_pixels        & 0xFF;
                        uint8_t plane1 = (eight_pixels >> 8)  & 0xFF;
                        uint8_t plane2 = (eight_pixels >> 16) & 0xFF;
                        uint8_t plane3 = (eight_pixels >> 24) & 0xFF;

                        for (int bit = 7; bit >= 0; --bit) {
                            uint8_t color_index = ((plane0 >> bit) & 1)
                                                | (((plane1 >> bit) & 1) << 1)
                                                | (((plane2 >> bit) & 1) << 2)
                                                | (((plane3 >> bit) & 1) << 3);
                            uint32_t color = vga_palette[color_index];
                            *pixels++ = color;
                            *pixels++ = color;
                        }
                    }
                    break;
                }
                case 0x0E: /* EGA 640x200 16-color */ {
                    if (y >= 400) break;
                    uint32_t* vram_ptr = &VIDEORAM[(y / 2) * (640 / 8)];
                    for (int i = 0; i < (640 / 8); ++i) {
                        uint32_t eight_pixels = vram_ptr[i];
                        uint8_t plane0 =  eight_pixels        & 0xFF;
                        uint8_t plane1 = (eight_pixels >> 8)  & 0xFF;
                        uint8_t plane2 = (eight_pixels >> 16) & 0xFF;
                        uint8_t plane3 = (eight_pixels >> 24) & 0xFF;

                        for (int bit = 7; bit >= 0; --bit) {
                            uint8_t color_index = ((plane0 >> bit) & 1)
                                                | (((plane1 >> bit) & 1) << 1)
                                                | (((plane2 >> bit) & 1) << 2)
                                                | (((plane3 >> bit) & 1) << 3);
                            *pixels++ = vga_palette[color_index];
                        }
                    }
                    break;
                }
                case 0x10: /* EGA 640x350 16-color */ {
                    uint32_t* vram_ptr = &VIDEORAM[y * (640 / 8)];
                    for (int i = 0; i < (640 / 8); ++i) {
                        uint32_t eight_pixels = vram_ptr[i];
                        uint8_t plane0 =  eight_pixels        & 0xFF;
                        uint8_t plane1 = (eight_pixels >> 8)  & 0xFF;
                        uint8_t plane2 = (eight_pixels >> 16) & 0xFF;
                        uint8_t plane3 = (eight_pixels >> 24) & 0xFF;

                        for (int bit = 7; bit >= 0; --bit) {
                            uint8_t color_index = ((plane0 >> bit) & 1)
                                                | (((plane1 >> bit) & 1) << 1)
                                                | (((plane2 >> bit) & 1) << 2)
                                                | (((plane3 >> bit) & 1) << 3);
                            *pixels++ = vga_palette[color_index];
                        }
                    }
                    break;
                }
                case 0x11: /* VGA 640x480 2-color */ {
                    uint8_t *cga_row = (uint8_t *) (VIDEORAM + y * 80);
                    // Each byte containing 8 pixels
                    for (int x = 640 / 8; x--;) {
                        uint8_t cga_byte = *cga_row++;

                        *pixels++ = cga_palette[(cga_byte >> 7 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 6 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 5 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 4 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 3 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 2 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 1 & 1) * 15];
                        *pixels++ = cga_palette[(cga_byte >> 0 & 1) * 15];
                    }

                    break;
                }
                case 0x12: /* VGA 640x480 16-color */ {
                    if (y >= 480) break;
                    uint32_t* vram_ptr = &VIDEORAM[y * (640 / 8)];
                    for (int i = 0; i < (640 / 8); ++i) {
                        uint32_t eight_pixels = vram_ptr[i];
                        uint8_t plane0 =  eight_pixels        & 0xFF;
                        uint8_t plane1 = (eight_pixels >> 8)  & 0xFF;
                        uint8_t plane2 = (eight_pixels >> 16) & 0xFF;
                        uint8_t plane3 = (eight_pixels >> 24) & 0xFF;

                        for (int bit = 7; bit >= 0; --bit) {
                            uint8_t color_index = ((plane0 >> bit) & 1)
                                                | (((plane1 >> bit) & 1) << 1)
                                                | (((plane2 >> bit) & 1) << 2)
                                                | (((plane3 >> bit) & 1) << 3);
                            *pixels++ = vga_palette[color_index];
                        }
                    }
                    break;
                }
                case 0x13: {
                    if (vga_planar_mode) {
                        uint32_t *vga_row = &VIDEORAM[vram_offset + (y >> 1) * (320 / 4)];
                        for (int x = 0; x < 320 / 4; x++) {
                            uint32_t four_pixels = *vga_row++;
                            *pixels++ = *pixels++ = vga_palette[four_pixels & 0xFF];
                            *pixels++ = *pixels++ = vga_palette[(four_pixels >> 8) & 0xFF];
                            *pixels++ = *pixels++ = vga_palette[(four_pixels >> 16) & 0xFF];
                            *pixels++ = *pixels++ = vga_palette[(four_pixels >> 24) & 0xFF];
                        }
                    } else {
                        uint32_t *vga_row = &VIDEORAM[vram_offset + (y >> 1) * (320)];
                        for (int x = 0; x < 320; x++) {
                            uint32_t four_pixels = *vga_row++;
                            *pixels++ = *pixels++ = vga_palette[four_pixels & 0xFF];
                        }
                    }

                    break;
                }
                case 0x78: /* 80x100x16 textmode */
                    cols = 40;
                case 0x77: /* 160x100x16 textmode */ {
                    uint16_t y_div_4 = y / 4; // Precompute y / 4
                    uint8_t odd_even = y / 2 & 1;
                    // Calculate screen position
                    uint8_t *cga_row = (uint8_t *) (VIDEORAM + 0x8000 + y_div_4 * 160);
                    for (uint8_t column = 0; column < cols; column++) {
                        // Access vidram and font data once per character
                        uint8_t *charcode = cga_row + column * 2; // Character code
                        uint8_t glyph_row = font_8x8[*charcode * 8 + odd_even]; // Glyph row from font
                        uint8_t color = *++charcode;

#pragma GCC unroll(8)
                        for (uint8_t bit = 0; bit < 8; bit++) {
                            *pixels++ = cga_palette[glyph_row >> bit & 1 ? color & 0x0f : color >> 4];
                        }
                    }
                    break;
                }
                case 0x79: /* 80x200x16 textmode */ {
                    int y_div_2 = y / 2; // Precompute y / 2
                    // Calculate screen position
                    uint8_t *cga_row = (uint8_t *) (VIDEORAM + 0x8000 + y_div_2 * 80 + (y_div_2 & 1 * 8192));
                    for (int column = 0; column < 40; column++) {
                        // Access vidram and font data once per character
                        uint8_t *charcode = cga_row + column * 2; // Character code
                        uint8_t glyph_row = font_8x8[*charcode * 8]; // Glyph row from font
                        uint8_t color = *++charcode;

#pragma GCC unroll(8)
                        for (int bit = 0; bit < 8; bit++) {
                            *pixels++ = *pixels++ = cga_palette[glyph_row >> bit & 1 ? color & 0x0f : color >> 4];
                        }
                    }
                    break;
                }
                case 0x87: {
                    /* 40x46 ??? */
                    int y_div_2 = y / 8; // Precompute y / 2
                    // Calculate screen position
                    uint8_t *cga_row = (uint8_t *) (VIDEORAM + 0x8000 + y_div_2 * 80 + (y_div_2 & 1 * 8192));
                    for (int column = 0; column < 40; column++) {
                        // Access vidram and font data once per character
                        uint8_t *charcode = cga_row + column * 2; // Character code
                        uint8_t glyph_row = font_8x8[*charcode * 8 + (y_div_2 % 8)]; // Glyph row from font
                        uint8_t color = *++charcode;

#pragma GCC unroll(8)
                        for (int bit = 0; bit < 8; bit++) {
                            *pixels++ = *pixels++ = cga_palette[glyph_row >> bit & 1 ? color & 0x0f : color >> 4];
                        }
                    }
                    break;
                }
                default:
                    printf("Unsupported videomode %x\n", videomode);
                    break;
            }
        else {
            uint8_t ydebug = y - 400;
            uint8_t y_div_8 = ydebug / 8;
            uint8_t glyph_line = ydebug % 8;

            const uint8_t colors[4] = {0x0f, 0xf0, 10, 12};
            //указатель откуда начать считывать символы
            uint8_t *text_buffer_line = &DEBUG_VRAM[y_div_8 * 80];
            for (uint8_t column = 80; column--;) {
                const uint8_t character = *text_buffer_line++;
                const uint8_t color = colors[character >> 6];
                uint8_t glyph_pixels = font_8x8[(32 + (character & 63)) * 8 + glyph_line];
                //считываем из быстрой палитры начало таблицы быстрого преобразования 2-битных комбинаций цветов пикселей
                // Unrolled bit loop: Write 8 pixels with scaling (2x horizontally)
                for (int bit = 0; bit < 8; bit++) {
                    *pixels++ = cga_palette[glyph_pixels >> bit & 1 ? color & 0x0f : color >> 4];
                }
            }
        }
    }
}

extern "C" uint64_t sb_samplerate;
HANDLE updateEvent;

DWORD WINAPI SoundThread(LPVOID lpParam) {
    WAVEHDR waveHeaders[4];

    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = SOUND_FREQUENCY;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HANDLE waveEvent = CreateEvent(NULL, 1, 0, NULL);

    HWAVEOUT hWaveOut;
    waveOutOpen(&hWaveOut, WAVE_MAPPER, &format, (DWORD_PTR) waveEvent, 0, CALLBACK_EVENT);

    for (size_t i = 0; i < 4; i++) {
        int16_t audio_buffers[4][AUDIO_BUFFER_LENGTH * 2];
        waveHeaders[i] = {
            .lpData = (char *) audio_buffers[i],
            .dwBufferLength = AUDIO_BUFFER_LENGTH * 2,
        };
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        waveHeaders[i].dwFlags |= WHDR_DONE;
    }
    WAVEHDR *currentHeader = waveHeaders;


    while (true) {
        if (WaitForSingleObject(waveEvent, INFINITE)) {
            //            fprintf(stderr, "Failed to wait for event.\n");
            return 1;
        }

        if (!ResetEvent(waveEvent)) {
            //            fprintf(stderr, "Failed to reset event.\n");
            return 1;
        }

        // Wait until audio finishes playing
        while (currentHeader->dwFlags & WHDR_DONE) {
            WaitForSingleObject(updateEvent, INFINITE);
            ResetEvent(updateEvent);
            //            PSG_calc_stereo(&psg, audiobuffer, AUDIO_BUFFER_LENGTH);
            memcpy(currentHeader->lpData, audio_buffer, AUDIO_BUFFER_LENGTH * 2);
            waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));
            //waveOutPrepareHeader(hWaveOut, currentHeader, sizeof(WAVEHDR));
            currentHeader++;
            if (currentHeader == waveHeaders + 4) { currentHeader = waveHeaders; }
        }
    }
    return 0;
}

extern uint16_t timeconst;

DWORD WINAPI TicksThread(LPVOID lpParam) {
    LARGE_INTEGER start, current, queryperf;


    QueryPerformanceFrequency(&queryperf);
    uint32_t hostfreq = (uint32_t) queryperf.QuadPart;

    QueryPerformanceCounter(&start); // Get the starting time

    uint32_t elapsed_system_timer = 0;
    uint32_t elapsed_blink_tics = 0;
    uint32_t elapsed_frame_tics = 0;
    uint32_t last_dss_tick = 0;
    uint32_t last_sb_tick = 0;
    uint32_t last_cms_tick = 0;
    uint32_t last_sound_tick = 0;

    int16_t last_dss_sample = 0;
    int16_t last_sb_sample = 0;
    int16_t last_cms_samples[2];


    updateEvent = CreateEvent(NULL, 1, 1, NULL);
    while (true) {
        QueryPerformanceCounter(&current); // Get the current time

        // Calculate elapsed time in ticks since the start
        uint32_t elapsedTime = (uint32_t) (current.QuadPart - start.QuadPart);

        if (elapsedTime - elapsed_system_timer >= hostfreq / timer_period) {
            doirq(0);
            elapsed_system_timer = elapsedTime; // Reset the tick counter for 1Hz
        }

        // Disney Sound Source frequency ~7KHz
        if (elapsedTime - last_dss_tick >= hostfreq / 7000) {
            last_dss_sample = dss_sample();
            last_dss_tick = elapsedTime;
        }

        // Sound Blaster
        if (elapsedTime - last_sb_tick >= hostfreq / sb_samplerate) {
            last_sb_sample = blaster_sample();
            last_sb_tick = elapsedTime;
        }

        if (elapsedTime - last_sound_tick >= hostfreq / SOUND_FREQUENCY) {
            get_sound_sample(last_dss_sample + last_sb_sample, &audio_buffer[sample_index]);
            sample_index += 2;

            if (sample_index >= AUDIO_BUFFER_LENGTH) {
                SetEvent(updateEvent);
                sample_index = 0;
            }

            last_sound_tick = elapsedTime;
        }

        if (elapsedTime - elapsed_blink_tics >= 333'333'3) {
            cursor_blink_state ^= 1;
            elapsed_blink_tics = elapsedTime;
        }

        if (elapsedTime - elapsed_frame_tics >= 16'666) {
            renderer();
            elapsed_frame_tics = elapsedTime;
        }
        //        _sleep(1);
    }
}


extern "C" void HandleMouse(int x, int y, uint8_t buttons) {
    static int prev_x = 0, prev_y = 0;
    sermouseevent(buttons, x - prev_x, y - prev_y);

    prev_y = y;
    prev_x = x;
}


extern "C" void HandleInput(WPARAM wParam, BOOL isKeyDown) {
    unsigned char scancode = 0;
    // Check if SCROLL LOCK is pressed
    if (wParam == VK_SCROLL && isKeyDown) {
        // Check if CTRL and ALT are pressed
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000)) {
            // Handle CTRL + ALT + SCROLL LOCK combination
            //            MessageBox(nullptr, "CTRL + ALT + SCROLL LOCK detected!", "COMPOSITE MODE ON", MB_OK);
            static uint8_t old_vm;
            if (videomode == 4 || videomode == 6) {
                old_vm = videomode;
                videomode += 0x70;
            } else if (videomode >= 0x74) {
                videomode = old_vm;
            }
            //            log_debug = !log_debug;
        }
    }
    switch (wParam) {
        // Row 1
        case VK_ESCAPE:
            scancode = 0x01;
            break;
        case '1':
            scancode = 0x02;
            break;
        case '2':
            scancode = 0x03;
            break;
        case '3':
            scancode = 0x04;
            break;
        case '4':
            scancode = 0x05;
            break;
        case '5':
            scancode = 0x06;
            break;
        case '6':
            scancode = 0x07;
            break;
        case '7':
            scancode = 0x08;
            break;
        case '8':
            scancode = 0x09;
            break;
        case '9':
            scancode = 0x0A;
            break;
        case '0':
            scancode = 0x0B;
            break;
        case VK_OEM_MINUS:
            scancode = 0x0C;
            break; // - key
        case VK_OEM_PLUS:
            scancode = 0x0D;
            break; // = key
        case VK_BACK:
            scancode = 0x0E;
            break; // Backspace

        // Row 2
        case VK_TAB:
            scancode = 0x0F;
            break;
        case 'Q':
            scancode = 0x10;
            break;
        case 'W':
            scancode = 0x11;
            break;
        case 'E':
            scancode = 0x12;
            break;
        case 'R':
            scancode = 0x13;
            break;
        case 'T':
            scancode = 0x14;
            break;
        case 'Y':
            scancode = 0x15;
            break;
        case 'U':
            scancode = 0x16;
            break;
        case 'I':
            scancode = 0x17;
            break;
        case 'O':
            scancode = 0x18;
            break;
        case 'P':
            scancode = 0x19;
            break;
        case VK_OEM_4:
            scancode = 0x1A;
            break; // [ key
        case VK_OEM_6:
            scancode = 0x1B;
            break; // ] key
        case VK_RETURN:
            scancode = 0x1C;
            break; // Enter

        // Row 3
        case VK_CONTROL:
            scancode = 0x1D;
            break; // Left Control
        case 'A':
            scancode = 0x1E;
            break;
        case 'S':
            scancode = 0x1F;
            break;
        case 'D':
            scancode = 0x20;
            break;
        case 'F':
            scancode = 0x21;
            break;
        case 'G':
            scancode = 0x22;
            break;
        case 'H':
            scancode = 0x23;
            break;
        case 'J':
            scancode = 0x24;
            break;
        case 'K':
            scancode = 0x25;
            break;
        case 'L':
            scancode = 0x26;
            break;
        case VK_OEM_1:
            scancode = 0x27;
            break; // ; key
        case VK_OEM_7:
            scancode = 0x28;
            break; // ' key
        case VK_OEM_3:
            scancode = 0x29;
            break; // ` key (backtick)

        // Row 4
        case VK_SHIFT:
            scancode = 0x2A;
            break; // Left Shift
        case VK_OEM_5:
            scancode = 0x2B;
            break; // \ key
        case 'Z':
            scancode = 0x2C;
            break;
        case 'X':
            scancode = 0x2D;
            break;
        case 'C':
            scancode = 0x2E;
            break;
        case 'V':
            scancode = 0x2F;
            break;
        case 'B':
            scancode = 0x30;
            break;
        case 'N':
            scancode = 0x31;
            break;
        case 'M':
            scancode = 0x32;
            break;
        case VK_OEM_COMMA:
            scancode = 0x33;
            break; // , key
        case VK_OEM_PERIOD:
            scancode = 0x34;
            break; // . key
        case VK_OEM_2:
            scancode = 0x35;
            break; // / key
        case VK_RSHIFT:
            scancode = 0x36;
            break; // Right Shift

        // Row 5
        case VK_MULTIPLY:
            scancode = 0x37;
            break; // Numpad *
        case VK_MENU:
            scancode = 0x38;
            break; // Left Alt
        case VK_SPACE:
            scancode = 0x39;
            break; // Spacebar
        case VK_CAPITAL:
            scancode = 0x3A;
            break; // Caps Lock

        // F1-F10
        case VK_F1:
            scancode = 0x3B;
            break;
        case VK_F2:
            scancode = 0x3C;
            break;
        case VK_F3:
            scancode = 0x3D;
            break;
        case VK_F4:
            scancode = 0x3E;
            break;
        case VK_F5:
            scancode = 0x3F;
            break;
        case VK_F6:
            scancode = 0x40;
            break;
        case VK_F7:
            scancode = 0x41;
            break;
        case VK_F8:
            scancode = 0x42;
            break;
        case VK_F9:
            scancode = 0x43;
            break;
        case VK_F10:
            scancode = 0x44;
            break;

        // Numpad
        case VK_NUMLOCK:
            scancode = 0x45;
            break;
        case VK_SCROLL:
            scancode = 0x46;
            break; // Scroll Lock
        case VK_NUMPAD7:
            scancode = 0x47;
            break; // Numpad 7
        case VK_NUMPAD8:
            scancode = 0x48;
            break; // Numpad 8
        case VK_NUMPAD9:
            scancode = 0x49;
            break; // Numpad 9
        case VK_SUBTRACT:
            scancode = 0x4A;
            break; // Numpad -
        case VK_NUMPAD4:
            scancode = 0x4B;
            break; // Numpad 4
        case VK_NUMPAD5:
            scancode = 0x4C;
            break; // Numpad 5
        case VK_NUMPAD6:
            scancode = 0x4D;
            break; // Numpad 6
        case VK_ADD:
            scancode = 0x4E;
            break; // Numpad +
        case VK_NUMPAD1:
            scancode = 0x4F;
            break; // Numpad 1
        case VK_NUMPAD2:
            scancode = 0x50;
            break; // Numpad 2
        case VK_NUMPAD3:
            scancode = 0x51;
            break; // Numpad 3
        case VK_NUMPAD0:
            scancode = 0x52;
            break; // Numpad 0
        case VK_DECIMAL:
            scancode = 0x53;
            break; // Numpad .

        // Additional keys (insert, delete, etc.)
        case VK_INSERT:
            scancode = 0x52;
            break; // Insert
        case VK_DELETE:
            scancode = 0x53;
            break; // Delete
        case VK_HOME:
            scancode = 0x47;
            break; // Home
        case VK_END:
            scancode = 0x4F;
            break; // End
        case VK_PRIOR:
            scancode = 0x49;
            break; // Page Up
        case VK_NEXT:
            scancode = 0x51;
            break; // Page Down
        case VK_UP:
            scancode = 0x48;
            break; // Up Arrow
        case VK_DOWN:
            scancode = 0x50;
            break; // Down Arrow
        case VK_LEFT:
            scancode = 0x4B;
            break; // Left Arrow
        case VK_RIGHT:
            scancode = 0x4D;
            break; // Right Arrow

        default:
            scancode = 0;
            break; // No translation
    }

    // For key release (KeyUp), add 0x80 to scancode
    if (!isKeyDown && scancode != 0) {
        scancode |= 0x80;
    }

    port60 = scancode;
    port64 |= 2;

    doirq(1);
    //printf("scancode %c", scancode);
}

#define QUEUE_SIZE 1000

typedef struct {
    uint16_t messages[QUEUE_SIZE];
    int front;
    int rear;
    int count;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cvNotEmpty;
    CONDITION_VARIABLE cvNotFull;
} MessageQueue;

MessageQueue queue;
HANDLE hThread;

DWORD WINAPI MessageHandler(LPVOID lpParam);

// Initialize the message queue
void InitQueue(MessageQueue *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
    InitializeCriticalSection(&q->cs);
    InitializeConditionVariable(&q->cvNotEmpty);
    InitializeConditionVariable(&q->cvNotFull);
}

// Destroy the message queue
void DestroyQueue(MessageQueue *q) {
    DeleteCriticalSection(&q->cs);
}

// Add a message to the queue
void Enqueue(MessageQueue *q, uint16_t message) {
    EnterCriticalSection(&q->cs);

    // Wait if the queue is full
    while (q->count == QUEUE_SIZE) {
        SleepConditionVariableCS(&q->cvNotFull, &q->cs, INFINITE);
    }

    q->messages[q->rear] = message;
    q->rear = (q->rear + 1) % QUEUE_SIZE;
    q->count++;

    // Signal that the queue is not empty
    WakeConditionVariable(&q->cvNotEmpty);

    LeaveCriticalSection(&q->cs);
}

// Remove a message from the queue
uint16_t Dequeue(MessageQueue *q) {
    EnterCriticalSection(&q->cs);

    // Wait if the queue is empty
    while (q->count == 0) {
        SleepConditionVariableCS(&q->cvNotEmpty, &q->cs, INFINITE);
    }

    uint16_t message = q->messages[q->front];
    q->front = (q->front + 1) % QUEUE_SIZE;
    q->count--;

    // Signal that the queue is not full
    WakeConditionVariable(&q->cvNotFull);

    LeaveCriticalSection(&q->cs);

    return message;
}

// Thread function to handle the message queue
DWORD WINAPI MessageHandler(LPVOID lpParam) {
    MessageQueue *q = (MessageQueue *) lpParam;

    while (1) {
        uint16_t message = Dequeue(q);

        // Process the message (just print it for now)
        //        printf("Message received: %u\n", message);
        if (hComm != NULL && !WriteFile(hComm, &message, 2, &bytesWritten, NULL)) {
            //            printf("!!!! Error in writing to serial port\n");
        }
        // Exit the thread if we receive a special "exit" message
        if (message == 0xFFFF) {
            //break;
        }
    }

    return 0;
}

#if 0
extern "C" void tandy_write(uint16_t reg, uint8_t value) {
    if (reg != 0xff) sn76489_out(value);
    Enqueue(&queue, (value & 0xff) << 8 | 0);
}

static INLINE void pcm_write(int mode, int16_t value) {
    if (mode) {
        Enqueue(&queue, (value >> 8) << 8 | 0xe << 4 | 0b0000);
        Enqueue(&queue, (value & 0xff) << 8 | 0xe << 4 | 0b0001);
    } else {
        Enqueue(&queue, (value & 0xff) << 8 | 0xe << 4 | 0b0011);
    }
}

extern "C" void adlib_init(uint32_t samplerate);

extern "C" void adlib_write(uintptr_t idx, uint8_t val);

extern "C" void adlib_write_d(uint16_t reg, uint8_t value) {
    static int latch = -1;
    if (latch == -1) {
        latch = value;
    } else {
        uint16_t data = (latch & 0xff) << 8 | 2 << 4 | 0b0000 | (latch >> 8) & 1;
        Enqueue(&queue, data);
        data = (value & 0xff) << 8 | 2 << 4 | 0b0010 | (latch >> 8) & 1;
        Enqueue(&queue, data);
        OPL_writeReg(emu8950_opl, latch, value);
        latch = -1;
    }
}

extern "C" void cms_write(uint16_t reg, uint8_t val) {
    cms_out(reg, val);
    switch (reg - 0x220) {
        case 0:
            Enqueue(&queue, (val & 0xff) << 8 | 3 << 4 | 0b0000);
            break;
        case 1:
            Enqueue(&queue, (val & 0xff) << 8 | 3 << 4 | 0b0010);
            break;
        case 2:
            Enqueue(&queue, (val & 0xff) << 8 | 3 << 4 | 0b0001);
            break;
        case 3:
            Enqueue(&queue, (val & 0xff) << 8 | 3 << 4 | 0b0011);
            break;
    }
}
#endif
extern "C" BOOL HanldeMenu(int menu_id, BOOL checked) {
    if (menu_id == 2) {
        static uint8_t old_vm;
        if (videomode == 4 || videomode == 6) {
            old_vm = videomode;
            videomode += 0x70;
            return TRUE;
        }
        if (videomode >= 0x74) {
            videomode = old_vm;
        }
        return FALSE;
    }
    return !checked;
}
#if 1

extern "C" void _putchar(char character) {
    putwchar(character);
    static uint8_t color = 0xf;
    static int x = 0, y = 0;

    if (y == 10) {
        y = 9;
        memmove(DEBUG_VRAM, DEBUG_VRAM + 80, 80 * 9);
        memset(DEBUG_VRAM + 80 * 9, 0, 80);
    }
    uint8_t *vidramptr = DEBUG_VRAM + y * 80 + x;

    if ((unsigned) character >= 32) {
        if (character >= 96) character -= 32; // uppercase
        *vidramptr = ((character - 32) & 63) | 0 << 6;
        if (x == 80) {
            x = 0;
            y++;
        } else
            x++;
    } else if (character == '\n') {
        x = 0;
        y++;
    } else if (character == '\r') {
        x = 0;
    } else if (character == 8 && x > 0) {
        x--;
        *vidramptr = 0;
    }
}
#endif

int main(int argc, char **argv) {
    int scale = 2;

    if (!mfb_open("PC", 640, 480, scale))
        return 1;

    // Initialize the message queue
    InitQueue(&queue);

    // Create the message handling thread
    hThread = CreateThread(NULL, 0, MessageHandler, &queue, 0, NULL);
    if (hThread == NULL) {
        printf("Error creating thread\n");
        return 1;
    }

    if (hComm == NULL) {
        // Open the serial port
        hComm = CreateFile("\\\\.\\COM7",
                           GENERIC_WRITE | GENERIC_READ,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);


        if (hComm == INVALID_HANDLE_VALUE) {
            printf("!!!! Error in opening serial port\n");
        }
        // Initialize the DCB structure
        SecureZeroMemory(&dcb, sizeof(DCB));
        dcb.DCBlength = sizeof(DCB);

        Sleep(10);
        // Get the current state
        BOOL fSuccess = GetCommState(hComm, &dcb);
        if (!fSuccess) {
            // Handle the error
            printf("!!!! Error in getting current serial port state\n");
            CloseHandle(hComm);
            //return 1;
        }

        // Set the new state
        dcb.BaudRate = CBR_115200; // set the baud rate
        dcb.ByteSize = 8; // data size, xmit, and rcv
        dcb.Parity = NOPARITY; // no parity bit
        dcb.StopBits = ONESTOPBIT; // one stop bit
        dcb.fDtrControl = DTR_CONTROL_ENABLE;


        fSuccess = SetCommState(hComm, &dcb);
        if (!fSuccess) {
            // Handle the error
            printf("!!!! Error in setting serial port state\n");
            CloseHandle(hComm);
            //return 1;
        }
        COMMTIMEOUTS timeouts;
        // Set the timeouts
        timeouts.ReadIntervalTimeout = 5;
        timeouts.ReadTotalTimeoutConstant = 5;
        timeouts.ReadTotalTimeoutMultiplier = 1;
        timeouts.WriteTotalTimeoutConstant = 5;
        timeouts.WriteTotalTimeoutMultiplier = 1;
        Sleep(10);
        fSuccess = SetCommTimeouts(hComm, &timeouts);
        if (!fSuccess) {
            // Handle the error
            printf("!!!! Error in setting timeouts\n");
            CloseHandle(hComm);
            //return 1;
        }
        Sleep(10);
    }

    //    adlib_init(SOUND_FREQUENCY);
    memset(SCREEN, 0, sizeof (SCREEN));
    emu8950_opl = OPL_new(3579552, SOUND_FREQUENCY);
    blaster_reset();
    sn76489_reset();
    reset86();

    CreateThread(NULL, 0, SoundThread, NULL, 0, NULL);
    CreateThread(NULL, 0, TicksThread, NULL, 0, NULL);

    while (true) {
        exec86(32768);
        if (mfb_update(SCREEN, 0) == -1)
            exit(1);
    }
    // Wait for the thread to finish
    //    WaitForSingleObject(hThread, INFINITE);

    // Clean up
    CloseHandle(hThread);
    DestroyQueue(&queue);
    //    CloseHandle(
}
