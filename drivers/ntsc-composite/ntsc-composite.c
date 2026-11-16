/*----------------------------------------------------------------------------
Copyright (C) 2026, xrip, all right reserved.
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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("O3")
#endif

#include "ntsc-composite.h"

#include <stddef.h>

#include <graphics_modes.h>

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>

#if defined(GRAPHICS_PC_EMULATOR)
extern volatile uint8_t port3DA;
#endif

/* ===========================================================================
 * NTSC Video Format Constants
 * =========================================================================== */

// Frame dimensions
#define NTSC_FRAME_WIDTH    320
#define NTSC_FRAME_HEIGHT   240

// NTSC timing parameters
#define NTSC_SAMPLES_PER_LINE  908   // 227 * 4 samples per scanline
#define NTSC_TOTAL_LINES       262   // Total scanlines in NTSC frame
#define NTSC_VSYNC_LINES       10    // Vertical sync pulse lines
// Top blanking. 10 sync lines + 9 here = 19, which leaves 262 - 19 - 240 = 3
// blank lines after the picture. Broadcast NTSC puts 3 pre-equalizing lines in
// that slot, so the receiver sees a picture-free run-in to vertical sync.
#define NTSC_VBLANK_TOP        9     // Top blanking interval lines
#define NTSC_HSYNC_WIDTH       68    // Horizontal sync width in samples (~4.7μs)
#define NTSC_BACK_PORCH_SAMPLES 8
#define NTSC_COLOR_BURST_CYCLES 9
#define NTSC_ACTIVE_START      (NTSC_HSYNC_WIDTH + NTSC_BACK_PORCH_SAMPLES + \
                                NTSC_COLOR_BURST_CYCLES * 4 + 60)

// NTSC composite video signal levels (0-7 range for 3-bit PWM)
#define NTSC_LEVEL_SYNC          0    // Sync pulse level (lowest)
#define NTSC_LEVEL_BLANK         2    // Blanking/black level
#define NTSC_LEVEL_BLACK         2    // Black level (same as blanking)
#define NTSC_LEVEL_BURST_LOW     1    // Color burst low level
#define NTSC_LEVEL_BURST_HIGH    3    // Color burst high level

/* ===========================================================================
 * Hardware Pin Configuration
 * =========================================================================== */

// Scanout reads only the active buffer. A producer may prepare another
// framebuffer and request an atomic swap at the start of the next frame.
static const uint8_t *ntsc_active_framebuffer;
// The line now going out. Read from the other core, so it is volatile.
static volatile size_t ntsc_current_scanline = 2;
static const uint8_t *volatile ntsc_pending_framebuffer;

// Ping-pong buffers for DMA double-buffering
// While one buffer is being transmitted, the other is prepared
// Size aligned to the 4-byte boundary for DMA efficiency
static uint16_t ntsc_scanline_buffers[2][NTSC_SAMPLES_PER_LINE + 3 & ~3u] __attribute__ ((aligned (4)));

// Scratch line for the mode composer. VGA has its own, because the two
// backends build their lines from separate interrupts.
#if defined(NTSC_LOW_RAM) && NTSC_LOW_RAM
static uint8_t ntsc_staging[GRAPHICS_TEXT_SAMPLES] __attribute__ ((aligned (4)));
#else
static uint8_t ntsc_staging[NTSC_FRAME_WIDTH] __attribute__ ((aligned (4)));
#endif

// NTSC color palette lookup table
// Each color has 4 entries for the 4 phases of NTSC color subcarrier (0°, 90°, 180°, 270°)
// This allows proper color encoding at 3.579545 MHz
#if !PICO_RP2350 && defined(NTSC_USE_SCRATCH_Y) && NTSC_USE_SCRATCH_Y
#define NTSC_PALETTE_PLACEMENT(name) __scratch_y(name)
#else
#define NTSC_PALETTE_PLACEMENT(name)
#endif

#if defined(NTSC_LOW_RAM) && NTSC_LOW_RAM
typedef uint8_t ntsc_palette_entry_t;
// Compact palette: two 4-bit phase values packed per byte (512 bytes total)
// PWM compare values 0..11 fit in a nibble; phases 0°,90° in _even, 180°,270° in _odd
static uint8_t NTSC_PALETTE_PLACEMENT("ntsc_palette_even") ntsc_palette_even[256];
static uint8_t NTSC_PALETTE_PLACEMENT("ntsc_palette_odd") ntsc_palette_odd[256];
#else
typedef uint32_t ntsc_palette_entry_t;
// Packed palette: two 16-bit phase values per uint32_t (2048 bytes total)
// phases 0°,90° in _even, 180°,270° in _odd
static uint32_t NTSC_PALETTE_PLACEMENT("ntsc_palette_even") ntsc_palette_even[256] __attribute__ ((aligned (4)));
static uint32_t NTSC_PALETTE_PLACEMENT("ntsc_palette_odd") ntsc_palette_odd[256]  __attribute__ ((aligned (4)));
#endif

static uint16_t ntsc_luminance[256];

#undef NTSC_PALETTE_PLACEMENT

// Text uses one sample for each pixel, so a pixel is too short to carry a
// complete four-phase color cycle. Each color is sent as its luminance only,
// and the receiver makes the color back from the pixel pattern itself. This is
// the same artifact color the older driver produced, and it keeps all 640
// samples of the line instead of dropping to 320 double-width pixels.
//
// The 16 values are the zero-chroma form of the palette maths below. Legacy
// configurations keep this read-only table in flash.
#define NTSC_TEXT_LUMINANCE(color) ((150u * GRAPHICS_CGA_GREEN(color) + \
        29u * GRAPHICS_CGA_BLUE(color) + 77u * GRAPHICS_CGA_RED(color) + 128u) / 256u)
#define NTSC_TEXT_SAMPLE(color) \
    (uint16_t)((NTSC_TEXT_LUMINANCE(color) * 1792u + 2u * 65536u + 32768u) / 65536u),

#if defined(HDMI_HSTX)
// The shared descriptor renderer runs alongside HSTX: keep this 32-byte table
// in SRAM so its scanline path does not add XIP reads under combined load.
static uint16_t __not_in_flash("ntsc_text_sample") ntsc_text_sample[16] = {
#else
static const uint16_t ntsc_text_sample[16] = {
#endif
        GRAPHICS_TEXT_COLORS(NTSC_TEXT_SAMPLE)
};

// 240 active lines / 8 = the 30 text rows of TEXTMODE_ROWS.
#define NTSC_TEXT_FONT_HEIGHT 8

void ntsc_composite_request_framebuffer(const uint8_t *framebuffer) {
    // Publish all completed pixel writes before the IRQ sees the pointer.
    __mem_fence_release();
    ntsc_pending_framebuffer = framebuffer;
}

void ntsc_composite_wait_framebuffer(void) {
    // The IRQ clears this only after switching buffers at a frame boundary.
    while (ntsc_pending_framebuffer != NULL) {
        tight_loop_contents();
    }
    __mem_fence_acquire();
}

size_t ntsc_composite_current_line(void) {
    return ntsc_current_scanline;
}

void ntsc_composite_wait_vblank(void) {
    // Return the moment the line counter runs backwards, which is the start of
    // a frame. Called from anywhere in a frame this waits at most one frame,
    // and it hands the caller the longest possible lead over the beam.
    size_t previous = ntsc_current_scanline;
    while (true) {
        const size_t line = ntsc_current_scanline;
        if (line < previous) {
            return;
        }
        previous = line;
        tight_loop_contents();
    }
}

// The data channel sends a scanline. The control channel selects the next buffer.
static uint8_t ntsc_dma_chan_data;
static uintptr_t ntsc_dma_read_addresses[2] __attribute__ ((aligned (8)));

#if defined(NTSC_LOW_RAM) && NTSC_LOW_RAM
// Unpack a byte holding two 4-bit phase values into a 32-bit value
// suitable for a single 32-bit write to the scanline buffer
static inline uint32_t ntsc_unpack_pair(uint32_t pair) {
    return (pair * 0x1001u) & 0x000f000fu;
}
#endif

/* ===========================================================================
 * Function: ntsc_generate_scanline
 * Purpose: Generate NTSC composite video signal data for one scanline
 * =========================================================================== */
// One text scanline: 80 columns of 8 pixels fill all 640 active samples, one
// sample for each pixel, so the line keeps its full width.
static inline void ntsc_generate_text_line(uint16_t *output, const size_t active_line) {
#if defined(HDMI_HSTX)
    // Both outputs borrow the same text VRAM/font descriptor. Composite samples
    // every second physical HDMI row while retaining all 640 horizontal samples.
    // HSTX alone advances the shared blink counter; NTSC only reads it.
    struct graphics_text_line text;
    if (!graphics_text_line((unsigned)active_line, NTSC_FRAME_HEIGHT, &text)) {
        for (unsigned x = 0; x < GRAPHICS_TEXT_SAMPLES; ++x) {
            *output++ = ntsc_text_sample[0];
        }
        return;
    }

    const bool wide = text.columns == 40u;
    for (unsigned column = 0; column < text.columns; ++column) {
        const uint16_t cell = graphics_text_cell(&text, column);
        const uint8_t attribute = (uint8_t)(cell >> 8);
        uint8_t glyph = graphics_text_glyph(&text, column, (uint8_t)cell, attribute);
        const unsigned background = (attribute >> 4) & (text.blink_enabled ? 7u : 15u);
        const uint16_t colors[2] = {
            ntsc_text_sample[background], ntsc_text_sample[attribute & 15u]
        };
        for (unsigned bit = 0; bit < 8u; ++bit) {
            const uint16_t sample = colors[glyph & 1u];
            glyph >>= 1u;
            *output++ = sample;
            if (wide) {
                *output++ = sample;
            }
        }
    }
#else
    unsigned glyph_line;
    const uint8_t *cell = graphics_text_row(active_line, NTSC_TEXT_FONT_HEIGHT,
                                            &glyph_line);

    if (cell == NULL) {
        for (int i = 0; i < GRAPHICS_TEXT_SAMPLES; i++) {
            *output++ = ntsc_text_sample[0];
        }
        return;
    }

    const unsigned columns = graphics_text_columns();
    const bool wide = columns * 8u < GRAPHICS_TEXT_SAMPLES;

    for (unsigned column = columns; column--;) {
        const uint8_t character = *cell++;
        const uint8_t attribute = *cell++;
        uint8_t glyph = font_8x8[character * NTSC_TEXT_FONT_HEIGHT + glyph_line];
        const uint16_t color[2] = {
                ntsc_text_sample[attribute >> 4],
                ntsc_text_sample[attribute & 0x0fu]
        };

        for (unsigned bit = 8; bit--;) {
            const uint16_t sample = color[glyph & 1u];
            glyph >>= 1;
            *output++ = sample;
            if (wide) {
                *output++ = sample;
            }
        }
    }
#endif
}

static void __time_critical_func(ntsc_generate_scanline)(
        uint16_t *output_buffer, const size_t scanline_number) {
    uint16_t *buffer_ptr = output_buffer;
    const size_t active_line = scanline_number - (NTSC_VSYNC_LINES + NTSC_VBLANK_TOP);

    // Generate active video scanlines
    if (active_line < NTSC_FRAME_HEIGHT) {
        // Skip horizontal blanking interval
        buffer_ptr += NTSC_ACTIVE_START;

        const bool pc_mode = graphics_source_is_pc_mode();
        if (!pc_mode && graphics_source_is_text()) {
            ntsc_generate_text_line(buffer_ptr, active_line);
            return;
        }

        const ntsc_palette_entry_t *palette_even = ntsc_palette_even;
        const ntsc_palette_entry_t *palette_odd = ntsc_palette_odd;
        const uint8_t *pixel_ptr;
        if (pc_mode) {
#if !defined(NTSC_LOW_RAM) || !NTSC_LOW_RAM
            if (graphics_source_render_ntsc_fast(active_line, buffer_ptr,
                    ntsc_palette_even, ntsc_palette_odd, ntsc_luminance)) {
                return;
            }
#endif
            const enum graphics_mode_t mode = graphics_source_mode();
            const bool composite = mode == COMPOSITE_160x200x16 ||
                                   mode == COMPOSITE_160x200x16_force;
            // A 40-column glyph has two samples per pixel, enough to keep
            // both chroma phases. Only 80-column text needs luma-only samples.
            const bool text40 = mode == TEXTMODE_40x25_BW ||
                                mode == TEXTMODE_40x25_COLOR;
            const unsigned width = composite || text40 ? NTSC_FRAME_WIDTH :
                                                         graphics_source_native_width();
            const bool mono = (!text40 && graphics_source_is_text()) ||
                    mode == CGA_640x200x2 || mode == HERC_640x480x2 ||
                    mode == HERC_640x480x2_90 || mode == VGA_640x480x2;
#if !defined(NTSC_LOW_RAM) || !NTSC_LOW_RAM
            if (width == GRAPHICS_TEXT_SAMPLES) {
                if (mono) {
                    graphics_source_render_line16(active_line, width,
                            NTSC_FRAME_HEIGHT, buffer_ptr, ntsc_luminance);
                } else {
                    graphics_source_render_ntsc16(active_line, width,
                            NTSC_FRAME_HEIGHT, buffer_ptr,
                            ntsc_palette_even, ntsc_palette_odd);
                }
            } else {
                graphics_source_render_ntsc32(active_line, width,
                        NTSC_FRAME_HEIGHT, (uint32_t *)buffer_ptr,
                        ntsc_palette_even, ntsc_palette_odd);
            }
            return;
#else
            graphics_source_render_ntsc_line(active_line, width,
                                             NTSC_FRAME_HEIGHT, ntsc_staging);
            pixel_ptr = ntsc_staging;
            if (width == GRAPHICS_TEXT_SAMPLES) {
                if (mono) {
                    for (unsigned x = 0; x < GRAPHICS_TEXT_SAMPLES; ++x) {
                        buffer_ptr[x] = ntsc_luminance[pixel_ptr[x]];
                    }
                } else {
                    for (unsigned x = 0; x < GRAPHICS_TEXT_SAMPLES; x += 4u) {
                        buffer_ptr[x] = palette_even[pixel_ptr[x]] & 15u;
                        buffer_ptr[x + 1u] = palette_even[pixel_ptr[x + 1u]] >> 4;
                        buffer_ptr[x + 2u] = palette_odd[pixel_ptr[x + 2u]] & 15u;
                        buffer_ptr[x + 3u] = palette_odd[pixel_ptr[x + 3u]] >> 4;
                    }
                }
                return;
            }
#endif
        } else {
            pixel_ptr = graphics_source_line(ntsc_active_framebuffer,
                                             active_line, ntsc_staging);
        }
        uint32_t *output_ptr = (uint32_t *)buffer_ptr;
#if defined(NTSC_LOW_RAM) && NTSC_LOW_RAM && PICO_RP2040
        uint32_t pixel_groups = NTSC_FRAME_WIDTH / 2;
#else
        uint32_t pixel_groups = NTSC_FRAME_WIDTH / 4;
#endif

        // Process all pixels in the scanline
#if defined(NTSC_LOW_RAM) && NTSC_LOW_RAM
        // Compact palette: unpack four pixels per iteration.
#if PICO_RP2350
#pragma GCC unroll 2
#else
#pragma GCC unroll 4
#endif
        do {
#if PICO_RP2350
            uint32_t source_pixels;
            __builtin_memcpy(&source_pixels, pixel_ptr, sizeof(source_pixels));
            output_ptr[0] = ntsc_unpack_pair(palette_even[(uint8_t)source_pixels]);
            output_ptr[1] = ntsc_unpack_pair(palette_odd[(uint8_t)(source_pixels >> 8)]);
            output_ptr[2] = ntsc_unpack_pair(palette_even[(uint8_t)(source_pixels >> 16)]);
            output_ptr[3] = ntsc_unpack_pair(palette_odd[source_pixels >> 24]);
            pixel_ptr += 4;
            output_ptr += 4;
#else
            output_ptr[0] = ntsc_unpack_pair(palette_even[pixel_ptr[0]]);
            output_ptr[1] = ntsc_unpack_pair(palette_odd[pixel_ptr[1]]);
            pixel_ptr += 2;
            output_ptr += 2;
#endif
        } while (--pixel_groups);
#else
        // Packed palette: process four pixels per iteration.
#if PICO_RP2350
#pragma GCC unroll 2
#else
#pragma GCC unroll 4
#endif
        do {
#if PICO_RP2350
            uint32_t source_pixels;
            __builtin_memcpy(&source_pixels, pixel_ptr, sizeof(source_pixels));

            const uint32_t pixel0 = palette_even[(uint8_t)source_pixels];
            const uint32_t pixel1 = palette_odd[(uint8_t)(source_pixels >> 8)];
            output_ptr[0] = pixel0;
            output_ptr[1] = pixel1;

            const uint32_t pixel2 = palette_even[(uint8_t)(source_pixels >> 16)];
            const uint32_t pixel3 = palette_odd[source_pixels >> 24];
            output_ptr[2] = pixel2;
            output_ptr[3] = pixel3;
#else
            output_ptr[0] = palette_even[pixel_ptr[0]];
            output_ptr[1] = palette_odd[pixel_ptr[1]];
            output_ptr[2] = palette_even[pixel_ptr[2]];
            output_ptr[3] = palette_odd[pixel_ptr[3]];
#endif
            pixel_ptr += 4;
            output_ptr += 4;
        } while (--pixel_groups);
#endif
    }
    // Generate equalizing pulses for the first two scanlines
    else if (scanline_number < 2) {
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
        for (int j = 0; j < NTSC_BACK_PORCH_SAMPLES; j++)
            *buffer_ptr++ = NTSC_LEVEL_BLANK;

        // Color burst signal at 3.579545 MHz
        // Alternates between levels to create a reference signal for color decoding
        for (int j = 0; j < NTSC_COLOR_BURST_CYCLES; j++) {
            *buffer_ptr++ = NTSC_LEVEL_BLANK;      // Phase 0°
            *buffer_ptr++ = NTSC_LEVEL_BURST_LOW;  // Phase 90°
            *buffer_ptr++ = NTSC_LEVEL_BLANK;      // Phase 180°
            *buffer_ptr++ = NTSC_LEVEL_BURST_HIGH; // Phase 270°
        }

        // Fill remainder with blanking level
        while (buffer_ptr < output_buffer + NTSC_SAMPLES_PER_LINE)
            *buffer_ptr++ = NTSC_LEVEL_BLANK;
    }
    // Generate vertical blanking lines after active video
    else if (scanline_number == NTSC_VSYNC_LINES + NTSC_VBLANK_TOP + NTSC_FRAME_HEIGHT ||
             scanline_number == NTSC_VSYNC_LINES + NTSC_VBLANK_TOP + NTSC_FRAME_HEIGHT + 1) {
        // Skip horizontal blanking interval
        buffer_ptr += NTSC_ACTIVE_START;

        // Fill the active portion with blanking level
        // Width = NTSC_FRAME_WIDTH * 2 because each pixel generates 2 samples
        for (int i = 0; i < NTSC_FRAME_WIDTH * 2; i++)
            *buffer_ptr++ = NTSC_LEVEL_BLANK;
    }
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
    ntsc_luminance[palette_index] =
            (uint16_t)((luminance * 1792 + 2 * 65536 + 32768) / 65536);

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
    const int32_t phase0 = (luminance * 1792 + blue_chroma_0 + red_chroma_0 + 2 * 65536 + 32768) / 65536;
    // Phase 90°: Y + chroma(90°)
    const int32_t phase1 = (luminance * 1792 + blue_chroma_90 + red_chroma_90 + 2 * 65536 + 32768) / 65536;
    // Phase 180°: Y - chroma
    const int32_t phase2 = (luminance * 1792 - blue_chroma_0 - red_chroma_0 + 2 * 65536 + 32768) / 65536;
    // Phase 270°: Y - chroma(90°)
    const int32_t phase3 = (luminance * 1792 - blue_chroma_90 - red_chroma_90 + 2 * 65536 + 32768) / 65536;


#if defined(NTSC_LOW_RAM) && NTSC_LOW_RAM
    // Pack as nibble pairs. The current conversion produces values in the
    // 0..11 range; PWM compare values >= TOP + 1 naturally produce 100% duty.
    ntsc_palette_even[palette_index] =
        (uint8_t)(phase0 < 0 ? 0 : phase0) |
        ((uint8_t)(phase1 < 0 ? 0 : phase1) << 4);
    ntsc_palette_odd[palette_index] =
        (uint8_t)(phase2 < 0 ? 0 : phase2) |
        ((uint8_t)(phase3 < 0 ? 0 : phase3) << 4);
#else
    // Pack as 16-bit pairs in uint32_t tables
    ntsc_palette_even[palette_index] =
        (uint32_t)(phase0 < 0 ? 0 : phase0) | ((uint32_t)(phase1 < 0 ? 0 : phase1) << 16);
    ntsc_palette_odd[palette_index] =
        (uint32_t)(phase2 < 0 ? 0 : phase2) | ((uint32_t)(phase3 < 0 ? 0 : phase3) << 16);
#endif
}

void ntsc_composite_set_palette(const uint8_t index, uint32_t color888) {
#if defined(GRAPHICS_PC_EMULATOR)
    color888 = graphics_pc_palette_color(index, color888, true);
#endif
    const uint8_t red = (uint8_t)(color888 >> 16);
    const uint8_t green = (uint8_t)(color888 >> 8);
    const uint8_t blue = (uint8_t)color888;

    ntsc_set_color(index, blue, red, green);
}

void ntsc_composite_set_framebuffer(const uint8_t *framebuffer) {
    ntsc_active_framebuffer = framebuffer;
}

/* ===========================================================================
 * Function: ntsc_dma_irq_handler
 * Purpose: Handle DMA transfer completion and prepare next scanline
 * =========================================================================== */
static void __time_critical_func(ntsc_dma_irq_handler)() {
    dma_hw->ints0 = 1u << ntsc_dma_chan_data;

    const size_t scanline = ntsc_current_scanline;
#if defined(GRAPHICS_PC_EMULATOR) && !defined(VGA) && !defined(HDMI_HSTX) && !defined(HDMI)
    const size_t active = scanline - (NTSC_VSYNC_LINES + NTSC_VBLANK_TOP);
    port3DA = active >= NTSC_FRAME_HEIGHT ? 9u : 0u;
#endif
    uint16_t *output_buffer = ntsc_scanline_buffers[scanline & 1u];

    if (scanline == 0) {
        const uint8_t *pending_framebuffer = ntsc_pending_framebuffer;
        if (pending_framebuffer != NULL) {
            __mem_fence_acquire();
            ntsc_active_framebuffer = pending_framebuffer;
            __mem_fence_release();
            ntsc_pending_framebuffer = NULL;
        }
    }

    size_t next_scanline = scanline + 1u;
    if (next_scanline >= NTSC_TOTAL_LINES) {
        next_scanline = 0;
    }
    ntsc_current_scanline = next_scanline;

    ntsc_generate_scanline(output_buffer, scanline);
#if defined(GRAPHICS_PC_EMULATOR) && !defined(VGA) && !defined(HDMI_HSTX) && !defined(HDMI)
    port3DA |= 1u;
#endif
}


/* ===========================================================================
 * Function: ntsc_composite_init
 * Purpose: Initialize the complete NTSC video generation system
 * =========================================================================== */
void ntsc_composite_init(const uint8_t *framebuffer) {
    const uint32_t pwm_period_cycles = 11;

    ntsc_active_framebuffer = framebuffer;
    ntsc_pending_framebuffer = NULL;

    // Configure PWM output pin
    gpio_set_function(NTSC_PIN_OUTPUT, GPIO_FUNC_PWM);
    const uint pwm_slice = pwm_gpio_to_slice_num(NTSC_PIN_OUTPUT);

    // Configure PWM for video signal generation
    pwm_config pwm_cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&pwm_cfg, 2.0f); // 2x clock division

    pwm_init(pwm_slice, &pwm_cfg, true);
    pwm_set_wrap(pwm_slice, pwm_period_cycles - 1);

    // Get PWM compare register address for DMA writes. The two channels of a
    // slice share one 32-bit register: channel A is the low half, channel B the
    // high half. An even output pin is channel A, an odd one channel B.
    volatile void *pwm_compare_addr = &pwm_hw->slice[pwm_slice].cc;
    if (pwm_gpio_to_channel(NTSC_PIN_OUTPUT) == PWM_CHAN_B) {
        pwm_compare_addr = (volatile void *) ((uintptr_t) pwm_compare_addr + 2);
    }

    // Allocate one paced data channel and one one-word control channel.
    ntsc_dma_chan_data = dma_claim_unused_channel(true);
    const uint dma_chan_control = dma_claim_unused_channel(true);

    // The control channel reads these in a ring. The first completed transfer
    // changes the data source from buffer 0 to buffer 1.
    ntsc_dma_read_addresses[0] = (uintptr_t)ntsc_scanline_buffers[1];
    ntsc_dma_read_addresses[1] = (uintptr_t)ntsc_scanline_buffers[0];

    dma_channel_config data_config = dma_channel_get_default_config(ntsc_dma_chan_data);
    channel_config_set_transfer_data_size(&data_config, DMA_SIZE_16);
    channel_config_set_read_increment(&data_config, true);
    channel_config_set_write_increment(&data_config, false);
    channel_config_set_dreq(&data_config, DREQ_PWM_WRAP0 + pwm_slice);
    channel_config_set_chain_to(&data_config, dma_chan_control);

    dma_channel_configure(
        ntsc_dma_chan_data,
        &data_config,
        pwm_compare_addr, // Destination: PWM register
        ntsc_scanline_buffers[0], // Source: Buffer 0
        NTSC_SAMPLES_PER_LINE, // Transfer count
        false // Don't start yet
    );

    // RP2350 self-trigger cannot be used here: it would restart before the
    // next scanline address is loaded. This one-word transfer loads READ_ADDR,
    // then chains to the data channel. The 8-byte read ring alternates buffers.
    dma_channel_config control_config = dma_channel_get_default_config(dma_chan_control);
    channel_config_set_transfer_data_size(&control_config, DMA_SIZE_32);
    channel_config_set_read_increment(&control_config, true);
    channel_config_set_write_increment(&control_config, false);
    channel_config_set_ring(&control_config, false, 3);
    channel_config_set_dreq(&control_config, DREQ_FORCE);
    channel_config_set_chain_to(&control_config, ntsc_dma_chan_data);

    dma_channel_configure(
        dma_chan_control,
        &control_config,
        &dma_hw->ch[ntsc_dma_chan_data].read_addr,
        ntsc_dma_read_addresses,
        1,
        false // Don't start yet
    );

    // Pre-fill buffers with the first two scanlines
    ntsc_generate_scanline(ntsc_scanline_buffers[0], 0);
    ntsc_generate_scanline(ntsc_scanline_buffers[1], 1);

    // Only completed scanline transfers need an interrupt.
    dma_set_irq0_channel_mask_enabled(1u << ntsc_dma_chan_data, true);

    // Install and enable interrupt handler
    irq_set_exclusive_handler(DMA_IRQ_0, ntsc_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

uint32_t ntsc_composite_start_mask(void) {
    return 1u << ntsc_dma_chan_data;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
