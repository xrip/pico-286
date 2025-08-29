/*----------------------------------------------------------------------------
Copyright (C) 2025, xrip, all right reserved.
Copyright (C) 2024, KenKen, all right reserved.

This program supplied herewith by KenKen is free software; you can
redistribute it and/or modify it under the terms of the same license written
here and only for non-commercial purpose.

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of FITNESS FOR A PARTICULAR
PURPOSE. The copyright owner and contributors are NOT LIABLE for any damages
caused by using this program.

----------------------------------------------------------------------------*/

// This signal generation program (using PWM and DMA) is the idea of @lovyan03.
// https://github.com/lovyan03/

#ifndef RP2040_PWM_NTSC_H
#define RP2040_PWM_NTSC_H

#pragma GCC optimize ("O3")

#include <hardware/dma.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>

#include "emulator/emulator.h"
#include "emulator/includes/font8x8.h"

extern int cursor_blink_state;

#define TEXTMODE_COLS 80

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
    VGA_640x480x2 = 0x11,
    VGA_320x200x256 = 0x13,
    VGA_320x200x256x4 = 0xff,

    COMPOSITE_160x200x16_force = 0x74,
    COMPOSITE_160x200x16 = 0x76,
    // planar VGA
};

extern enum graphics_mode_t graphics_mode;

/* ===========================================================================
 * NTSC Video Format Constants
 * =========================================================================== */

// Frame dimensions
#define NTSC_FRAME_WIDTH    320
#define NTSC_FRAME_HEIGHT   200

// NTSC timing parameters
#define NTSC_SAMPLES_PER_LINE  908   // 227 * 4 samples per scanline
#define NTSC_TOTAL_LINES       262   // Total scanlines in NTSC frame
#define NTSC_VSYNC_LINES       10    // Vertical sync pulse lines
#define NTSC_VBLANK_TOP        26    // Top blanking interval lines
#define NTSC_HSYNC_WIDTH       68    // Horizontal sync width in samples (~4.7μs)
#define NTSC_ACTIVE_START      (NTSC_HSYNC_WIDTH + 8 + 9 * 4 + 60)  // Start of active video

// NTSC composite video signal levels (0-7 range for 3-bit PWM)
#define NTSC_LEVEL_SYNC          0    // Sync pulse level (lowest)
#define NTSC_LEVEL_BLANK         2    // Blanking/black level
#define NTSC_LEVEL_BLACK         2    // Black level (same as blanking)
#define NTSC_LEVEL_BURST_LOW     1    // Color burst low level
#define NTSC_LEVEL_BURST_HIGH    3    // Color burst high level

/* ===========================================================================
 * Hardware Pin Configuration
 * =========================================================================== */

// Pin for NTSC composite video signal output via PWM
#ifndef NTSC_PIN_OUTPUT
#define NTSC_PIN_OUTPUT 22
#endif

// Graphics framebuffer - stores raw pixel data for the display
// Aligned to the 4-byte boundary for efficient DMA transfers
// static uint8_t ntsc_framebuffer[NTSC_FRAME_WIDTH * NTSC_FRAME_HEIGHT] __attribute__ ((aligned (4)));
// extern uint8_

#if !NDEBUG
// Flag indicating active video region processing
//  1: Currently generating visible scanlines
//  0: In a vertical blanking interval
static volatile uint8_t ntsc_is_rendering_active;

// Frame counter - increments after each complete frame
// Application code should reset this to track frame timing
static volatile uint16_t ntsc_frame_counter = 0;
#endif
// Ping-pong buffers for DMA double-buffering
// While one buffer is being transmitted, the other is prepared
// Size aligned to the 4-byte boundary for DMA efficiency
static uint16_t ntsc_scanline_buffers[2][NTSC_SAMPLES_PER_LINE + 3 & ~3u] __attribute__ ((aligned (4)));

// NTSC color palette lookup table
// Each color has 4 entries for the 4 phases of NTSC color subcarrier (0°, 90°, 180°, 270°)
// This allows proper color encoding at 3.579545 MHz
extern uint16_t ntsc_palette[4 * 256] __attribute__ ((aligned (4)));

// DMA channel handles for ping-pong operation
static uint ntsc_dma_chan_primary, ntsc_dma_chan_secondary;
extern uint8_t VIDEORAM[(64 << 10) + 4];
extern volatile uint8_t port3DA;

static const uint8_t cga_brightness[16] = {
    2, // 0 black
    3, // 1 blue
    4, // 2 green
    5, // 3 cyan
    3, // 4 red
    4, // 5 magenta
    4, // 6 brown
    5, // 7 light gray
    3, // 8 dark gray
    4, // 9 light blue
    5, // 10 light green
    6, // 11 light cyan
    5, // 12 light red
    6, // 13 light magenta
    6, // 14 yellow
    7 // 15 white
};

/* ===========================================================================
 * Function: ntsc_generate_scanline
 * Purpose: Generate NTSC composite video signal data for one scanline
 * =========================================================================== */
static inline void ntsc_generate_scanline(uint16_t *output_buffer, const size_t scanline_number) {
    uint16_t *buffer_ptr = output_buffer;

    // Generate equalizing pulses for the first two scanlines
    if (scanline_number < 2) {
        // Fill most of the line with sync level (black)
        for (int j = 0; j < NTSC_SAMPLES_PER_LINE - NTSC_HSYNC_WIDTH; j++)
            *buffer_ptr++ = NTSC_LEVEL_SYNC;

        // Add horizontal sync pulse at the end
        while (buffer_ptr < output_buffer + NTSC_SAMPLES_PER_LINE)
            *buffer_ptr++ = NTSC_LEVEL_BLANK;
    }
    // Generate vertical sync pulses
    else if (scanline_number == NTSC_VSYNC_LINES || scanline_number == NTSC_VSYNC_LINES + 1) {
        // Horizontal sync pulse
        for (int j = 0; j < NTSC_HSYNC_WIDTH; j++)
            *buffer_ptr++ = NTSC_LEVEL_SYNC;

        // Back porch before color burst
        for (int j = 0; j < 8; j++)
            *buffer_ptr++ = NTSC_LEVEL_BLANK;

        // Color burst signal - 9 cycles at 3.579545 MHz
        // Alternates between levels to create a reference signal for color decoding
        for (int j = 0; j < 9; j++) {
            *buffer_ptr++ = 2; // Phase 0°
            *buffer_ptr++ = 1; // Phase 90°
            *buffer_ptr++ = 2; // Phase 180°
            *buffer_ptr++ = 3; // Phase 270°
        }

        // Fill remainder with blanking level
        while (buffer_ptr < output_buffer + NTSC_SAMPLES_PER_LINE)
            *buffer_ptr++ = NTSC_LEVEL_BLANK;
    }
    // Generate active video scanlines
    else if (scanline_number >= NTSC_VSYNC_LINES + NTSC_VBLANK_TOP &&
             scanline_number < NTSC_VSYNC_LINES + NTSC_VBLANK_TOP + NTSC_FRAME_HEIGHT) {
        // Skip horizontal blanking interval
        buffer_ptr += NTSC_ACTIVE_START;

#if !NDEBUG
        // Reset framebuffer pointer at start of first visible scanline
        if (scanline_number == NTSC_VSYNC_LINES + NTSC_VBLANK_TOP) {
            ntsc_is_rendering_active = 1;
        }
#endif

        const uint16_t y = scanline_number - (NTSC_VSYNC_LINES + NTSC_VBLANK_TOP);
        switch (graphics_mode) {
            case TEXTMODE_40x25_BW:
            case TEXTMODE_40x25_COLOR: {
                const uint8_t glyph_line = y & 7;

                uint8_t *input_buffer_8bit = VIDEORAM + 0x8000 + __fast_mul(y >> 3, 80);

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

                uint8_t *input_buffer_8bit = VIDEORAM + 0x8000 + __fast_mul(y >> 3, 160);
                uint8_t y_div_8 = y / 8;

                for (int column = 0; column < TEXTMODE_COLS; ++column) {
                    const uint8_t ch = *input_buffer_8bit++;
                    const uint8_t color = *input_buffer_8bit++;
                    uint8_t glyph_pixels = font_8x8[ch * 8 + glyph_line];
                    const uint8_t fg = cga_brightness[color & 0xf];
                    const uint8_t bg = cga_brightness[color >> 4];

                    const uint8_t cursor_active = cursor_blink_state &&
                                                  y_div_8 == CURSOR_Y && column == CURSOR_X &&
                                                  glyph_line >= 4;

                    if (cursor_active) {
                            *(uint64_t *) buffer_ptr = 0x5050505050505050;
                            buffer_ptr += 8;
                    } else {
                        if (cga_blinking == 0x7F && (color & 0x80) && cursor_blink_state) {
                            glyph_pixels = 0;
                        }
                        #pragma GCC unroll(8)
                        for (int bit = 0; bit < 8; ++bit) {
                            uint8_t pixel_color = glyph_pixels & 1 ? fg : bg;
                            glyph_pixels >>= 1;
                            *buffer_ptr++ = pixel_color;
                        }
                    }
                }
            }
            break;
            case COMPOSITE_160x200x16_force:
            case COMPOSITE_160x200x16:
            case CGA_320x200x4:
            case CGA_320x200x4_BW: {
                uint8_t *input_buffer_8bit = VIDEORAM + 0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13);

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
                uint8_t *input_buffer_8bit = VIDEORAM + 0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13);

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
                const uint8_t *input_buffer_8bit = tga_offset + VIDEORAM + __fast_mul(y >> 1, 80) + ((y & 1) << 13);

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
                uint8_t *input_buffer_8bit = tga_offset + VIDEORAM + ((y & 3) << 13) + __fast_mul(y >> 2, 160);
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
            case VGA_320x200x256:
            default: {
                uint8_t *line_buffer = VIDEORAM + __fast_mul(y, 320);
                for (int x = 0; x < 320; x++) {
                    const uint8_t color = *line_buffer++;

                    // Write 4 NTSC phase values for this pixel
                    // Using 32-bit writes for efficiency (2 phase values at once)
                    // Phase offset alternates: 0,2,0,2... for proper color encoding
                    const uint32_t phase_offset = (x & 1) << 1;
                    *(uint32_t *) buffer_ptr = *(uint32_t *) (ntsc_palette + color * 4 + phase_offset);
                    buffer_ptr += 2;
                }
            }
        }
    }
    // Generate vertical blanking lines after active video
    else if (scanline_number == NTSC_VSYNC_LINES + NTSC_VBLANK_TOP + NTSC_FRAME_HEIGHT ||
             scanline_number == NTSC_VSYNC_LINES + NTSC_VBLANK_TOP + NTSC_FRAME_HEIGHT + 1) {
#if !NDEBUG
        // Mark end of active video on first blanking line
        if (scanline_number == NTSC_VSYNC_LINES + NTSC_VBLANK_TOP + NTSC_FRAME_HEIGHT) {
            ntsc_is_rendering_active = 0;
            ntsc_frame_counter++;
        }
#endif
        // Skip horizontal blanking interval
        buffer_ptr += NTSC_ACTIVE_START;

        // Fill the active portion with blanking level
        // Width = NTSC_FRAME_WIDTH * 2 because each pixel generates 2 samples
        for (int i = 0; i < NTSC_FRAME_WIDTH * 2; i++)
            *buffer_ptr++ = NTSC_LEVEL_BLANK;
    }
    port3DA = (scanline_number >= NTSC_FRAME_HEIGHT ? 8 : 0) | scanline_number & 1;
}

/* ===========================================================================
 * Function: ntsc_set_color
 * Purpose: Configure a color palette entry for NTSC encoding
 * =========================================================================== */
static void ntsc_set_color(const uint8_t palette_index, const uint8_t blue, const uint8_t red, const uint8_t green) {
    // Calculate NTSC luminance using standard weights
    // Y = 0.587*G + 0.114*B + 0.299*R
    // Using integer math: (150*G + 29*B + 77*R) / 256
    const int32_t luminance = (150 * green + 29 * blue + 77 * red + 128) / 256;

    // Pre-calculated chrominance modulation factors
    // These compensate for phase shifts in the NTSC encoding
    // Original formula: signal = Y + 0.4921*(B-Y)*sin(θ) + 0.8773*(R-Y)*cos(θ)

    // Phase 0° and 180° components
    const int32_t blue_chroma_0 = (blue - luminance) * 441; // (B-Y) * 0.4921 * scale
    const int32_t red_chroma_0 = (red - luminance) * 1361; // (R-Y) * 0.8773 * scale

    // Phase 90° and 270° components
    const int32_t blue_chroma_90 = (blue - luminance) * 764; // (B-Y) * 0.4921 * scale
    const int32_t red_chroma_90 = (red - luminance) * -786; // (R-Y) * 0.8773 * scale

    // Generate composite signal values for each subcarrier phase

    // Phase 0°: Y + chroma
    int32_t composite_signal = (luminance * 1792 + blue_chroma_0 + red_chroma_0 + 2 * 65536 + 32768) / 65536;
    ntsc_palette[palette_index * 4] = composite_signal < 0 ? 0 : composite_signal;

    // Phase 90°: Y + chroma(90°)
    composite_signal = (luminance * 1792 + blue_chroma_90 + red_chroma_90 + 2 * 65536 + 32768) / 65536;
    ntsc_palette[palette_index * 4 + 1] = composite_signal < 0 ? 0 : composite_signal;

    // Phase 180°: Y - chroma
    composite_signal = (luminance * 1792 - blue_chroma_0 - red_chroma_0 + 2 * 65536 + 32768) / 65536;
    ntsc_palette[palette_index * 4 + 2] = composite_signal < 0 ? 0 : composite_signal;

    // Phase 270°: Y - chroma(90°)
    composite_signal = (luminance * 1792 - blue_chroma_90 - red_chroma_90 + 2 * 65536 + 32768) / 65536;
    ntsc_palette[palette_index * 4 + 3] = composite_signal < 0 ? 0 : composite_signal;
}

/* ===========================================================================
 * Function: ntsc_dma_irq_handler
 * Purpose: Handle DMA transfer completion and prepare next scanline
 * =========================================================================== */
static void __time_critical_func(ntsc_dma_irq_handler)() {
    static size_t current_scanline = 0;
    // Read and clear DMA interrupt flags
    const volatile uint32_t interrupt_flags = dma_hw->ints0;
    dma_hw->ints0 = interrupt_flags;

    const uint8_t scanline_buffer_index = interrupt_flags & (1u << ntsc_dma_chan_secondary) ? 1 : 0;
    // Determine which channel completed and prepare its buffer
    ntsc_generate_scanline(ntsc_scanline_buffers[scanline_buffer_index], current_scanline);
    if (scanline_buffer_index) {
        dma_channel_set_read_addr(ntsc_dma_chan_secondary, ntsc_scanline_buffers[scanline_buffer_index], false);
    } else {
        dma_channel_set_read_addr(ntsc_dma_chan_primary, ntsc_scanline_buffers[scanline_buffer_index], false);
    }

    // Advance to the next scanline with wraparound
    if (++current_scanline >= NTSC_TOTAL_LINES) {
        current_scanline = 0;
    }
}


#define graphics_set_buffer(a,b,c)
#define graphics_set_textbuffer(a)
#define graphics_set_bgcolor(a)
#define graphics_set_mode(mode) graphics_mode = mode
#define graphics_set_offset(a,b)
#define graphics_set_flashmode(a,b)

void graphics_init();

void graphics_set_palette(uint8_t index, uint32_t rgb);
#endif // RP2040_PWM_NTSC_H
