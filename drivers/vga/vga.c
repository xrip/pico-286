#include "vga.h"
#include "vga_timing.h"
#include "vga_crtc.h"

#include <stddef.h>
#include <string.h>

#include <graphics_modes.h>
#include <graphics_pc_raster.h>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/sync.h>
#include <pico/platform.h>

#if defined(GRAPHICS_PC_EMULATOR)
extern volatile uint8_t port3DA;
#endif

// Two timings share this file, because they also share one 8-bit resistor bus
// and the one PIO instruction below. VGA is 31.5 kHz with separate H and V sync
// on bits 6 and 7. VGA_RGBS is the 15.734 kHz 240p that a SCART set wants, with
// one composite sync wire on bit 7. Both keep 800 samples in a line, so the
// buffers, the DMA and the line-building code do not change.
#if VGA_RGBS
enum {
    VGA_DUAL_LINE_SAMPLES = 800,
    // A sample is 79.4 ns, so 59 of them make the 4.7 us sync pulse.
    VGA_DUAL_HSYNC_SAMPLES = 59,
    // 128 samples is 10.2 us from the start of sync, so the back porch is
    // 5.5 us, safely over the 4.7 us a set expects, and the left edge is never
    // clipped. The picture then ends 32 samples (2.5 us) before the next sync,
    // which is a slightly wide right border. Move this to shift the picture;
    // it must stay a multiple of 4 for the 32-bit stores further down.
    VGA_DUAL_ACTIVE_OFFSET = 128,
    VGA_DUAL_CANVAS_LINES = 240,
    VGA_DUAL_SOURCE_WIDTH = 320,
    VGA_DUAL_LINE_WORDS = VGA_DUAL_LINE_SAMPLES / sizeof(uint32_t),
    VGA_DUAL_BLANK_BUFFER = 0,
    VGA_DUAL_VSYNC_BUFFER = 1,
    VGA_DUAL_ACTIVE_BUFFER_0 = 2,
    VGA_DUAL_ACTIVE_BUFFER_1 = 3,
    // Bit 7 is the one sync wire, so every pulse pulls only bit 7 low and
    // bit 6 is held high and unused. Bits 5:0 are still R2 G2 B2, which is why
    // the palette and the text table below need no change.
    VGA_DUAL_BLANK_LEVEL = 0xc0,
    VGA_DUAL_HSYNC_LEVEL = 0x40,
    // 800 samples at 15.734 kHz lines, which is half the VGA pixel clock.
    VGA_DUAL_PIXEL_CLOCK_HZ = 12587500,
    // 240 active lines / 8 = the 30 text rows of TEXTMODE_ROWS.
    VGA_DUAL_TEXT_FONT_HEIGHT = 8,
    // Half a line. The broad vertical pulse is cut by a serration here, so the
    // set keeps its line lock while the vertical pulse is low.
    VGA_DUAL_SERRATION_STEP = VGA_DUAL_LINE_SAMPLES / 2
};
#else
enum {
    VGA_DUAL_LINE_SAMPLES = 800,
    VGA_DUAL_HSYNC_SAMPLES = 96,
    VGA_DUAL_ACTIVE_OFFSET = 144,
    VGA_DUAL_CANVAS_LINES = 480,
    VGA_DUAL_SOURCE_WIDTH = 320,
    VGA_DUAL_LINE_WORDS = VGA_DUAL_LINE_SAMPLES / sizeof(uint32_t),
    VGA_DUAL_BLANK_BUFFER = 0,
    VGA_DUAL_VSYNC_BUFFER = 1,
    VGA_DUAL_ACTIVE_BUFFER_0 = 2,
    VGA_DUAL_ACTIVE_BUFFER_1 = 3,
    VGA_DUAL_BLANK_LEVEL = 0xc0,
    VGA_DUAL_HSYNC_LEVEL = 0x80,
    VGA_DUAL_VSYNC_LEVEL = 0x40,
    VGA_DUAL_COMBINED_SYNC_LEVEL = 0x00,
    VGA_DUAL_PIXEL_CLOCK_HZ = 25175000,
    // 480 active lines / 16 = the 30 text rows of TEXTMODE_ROWS.
    VGA_DUAL_TEXT_FONT_HEIGHT = 16
};
#endif

static const uint16_t vga_dual_program_instructions[] = {
        0x6008 // out pins, 8
};

static const struct pio_program vga_dual_program = {
        .instructions = vga_dual_program_instructions,
        .length = 1,
        .origin = -1
};

// Text colors as VGA bus values. The RGB parts of the CGA set are only 0x00,
// 0x55, 0xaa, or 0xff, and for those four the lower and upper dither levels of
// vga_set_palette() agree, so one value serves and text never shimmers.
#define VGA_DUAL_TEXT_LEVEL(part) ((part) / 85u)
#define VGA_DUAL_TEXT_DAC(color) ((uint16_t)(VGA_DUAL_BLANK_LEVEL | \
        VGA_DUAL_TEXT_LEVEL(GRAPHICS_CGA_RED(color)) << 4 | \
        VGA_DUAL_TEXT_LEVEL(GRAPHICS_CGA_GREEN(color)) << 2 | \
        VGA_DUAL_TEXT_LEVEL(GRAPHICS_CGA_BLUE(color))))

// Two pixels for one 16-bit store. The low byte is the left pixel, and the
// index is the next two glyph bits, lowest bit first.
#define VGA_DUAL_TEXT_QUAD(foreground, background) \
    (uint16_t)(VGA_DUAL_TEXT_DAC(background) | VGA_DUAL_TEXT_DAC(background) << 8), \
    (uint16_t)(VGA_DUAL_TEXT_DAC(foreground) | VGA_DUAL_TEXT_DAC(background) << 8), \
    (uint16_t)(VGA_DUAL_TEXT_DAC(background) | VGA_DUAL_TEXT_DAC(foreground) << 8), \
    (uint16_t)(VGA_DUAL_TEXT_DAC(foreground) | VGA_DUAL_TEXT_DAC(foreground) << 8),

// The generated table is the initial data only; vga_output_init() copies it into the
// 2 KB RAM table below, which is where the older driver kept it too.
//
// This one table cannot stay in flash. A text line reads it four times for
// every character cell, so 320 flash reads land inside one 31.7 us VGA line,
// and each shares the single QSPI bus with whatever the other core is running.
// A late line buffer then shows up as a shaking picture. The font is read once
// per cell, an eighth as often, and stays in flash as before; VGA and NTSC read
// their fonts from there without trouble.
static const uint16_t vga_dual_text_pairs_source[256 * 4] = {
        GRAPHICS_TEXT_ATTRIBUTES(VGA_DUAL_TEXT_QUAD)
};

static uint16_t vga_dual_text_pairs[256 * 4] __attribute__((aligned(8)));
// Four VGA samples per lookup. An 80-column glyph now needs two loads and
// two word stores; 40-column text selects repeated-bit patterns in this table.
static uint32_t vga_dual_text_quads[256 * 16] __attribute__((aligned(8)));

#if VGA_ENABLE_DITHER
static uint16_t vga_dual_palette[2][256] __attribute__((aligned(4)));
static uint8_t vga_dual_frame_phase;
static bool vga_flash_line = true;
static bool vga_flash_frame = true;
#else
static uint16_t vga_dual_palette[256] __attribute__((aligned(4)));
#endif
static uint32_t vga_dual_scanline_buffers[4][VGA_DUAL_LINE_WORDS]
        __attribute__((aligned(16)));
// Scratch line for the mode composer. NTSC has its own, because the two
// backends build their lines from separate interrupts.
static uint8_t vga_dual_staging[VGA_DUAL_SOURCE_WIDTH] __attribute__((aligned(4)));

static PIO vga_dual_pio = PIO_VGA;
static uint vga_dual_sm;
static uint vga_dual_dma_data;
static uint vga_dual_dma_control;
static volatile uintptr_t vga_dual_next_line_address;
// The line now going out. Read from the other core, so it is volatile.
static volatile size_t vga_dual_current_line;
// Only the scanout core changes this, at the start of a frame. Horizontal
// timing is shared by both PC profiles, so DMA and PIO need no restart.
static vga_timing_t vga_active_timing;
static const uint8_t *vga_dual_active_framebuffer;
static const uint8_t *volatile vga_dual_pending_framebuffer;

static inline uint32_t *vga_dual_line_buffer(const size_t index) {
    return vga_dual_scanline_buffers[index];
}

static void vga_dual_make_templates(void) {
    uint8_t *blank = (uint8_t *)vga_dual_line_buffer(VGA_DUAL_BLANK_BUFFER);
    uint8_t *vsync = (uint8_t *)vga_dual_line_buffer(VGA_DUAL_VSYNC_BUFFER);

    memset(blank, VGA_DUAL_BLANK_LEVEL, VGA_DUAL_LINE_SAMPLES);
    memset(blank, VGA_DUAL_HSYNC_LEVEL, VGA_DUAL_HSYNC_SAMPLES);

#if VGA_RGBS
    // The broad pulse of a composite sync: the one sync wire is low for the
    // whole line, cut by a short serration at the end of each half line.
    memset(vsync, VGA_DUAL_HSYNC_LEVEL, VGA_DUAL_LINE_SAMPLES);
    for (size_t half = 0; half < VGA_DUAL_LINE_SAMPLES;
         half += VGA_DUAL_SERRATION_STEP) {
        memset(vsync + half + VGA_DUAL_SERRATION_STEP - VGA_DUAL_HSYNC_SAMPLES,
               VGA_DUAL_BLANK_LEVEL, VGA_DUAL_HSYNC_SAMPLES);
    }
#else
    memset(vsync, VGA_DUAL_VSYNC_LEVEL, VGA_DUAL_LINE_SAMPLES);
    memset(vsync, VGA_DUAL_COMBINED_SYNC_LEVEL, VGA_DUAL_HSYNC_SAMPLES);
#endif

    memcpy(vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_0),
           blank, VGA_DUAL_LINE_SAMPLES);
    memcpy(vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_1),
           blank, VGA_DUAL_LINE_SAMPLES);
}

#if PICO_RP2350
static void __time_critical_func(vga_dual_generate_active_line)(
        const size_t source_line, uint32_t *line_buffer) {
#else
static __force_inline void vga_dual_generate_active_line(
        const size_t source_line, uint32_t *line_buffer) {
#endif
    const uint8_t *source = graphics_source_line(vga_dual_active_framebuffer,
                                                 source_line,
                                                 vga_dual_staging);
    uint16_t *output = (uint16_t *)((uint8_t *)line_buffer +
                                  VGA_DUAL_ACTIVE_OFFSET);
    uint32_t groups = VGA_DUAL_SOURCE_WIDTH / 4u;
#if VGA_ENABLE_DITHER
    const uint16_t *palette =
            vga_dual_palette[((source_line & vga_flash_line) ^
                              (vga_dual_frame_phase & vga_flash_frame)) & 1u];
#else
    const uint16_t *palette = vga_dual_palette;
#endif

#pragma GCC unroll 4
    do {
#if PICO_RP2350
        uint32_t pixels;
        __builtin_memcpy(&pixels, source, sizeof(pixels));
        const uint32_t pixel0 = palette[(uint8_t)pixels];
        const uint32_t pixel1 = palette[(uint8_t)(pixels >> 8)];
        const uint32_t pixel2 = palette[(uint8_t)(pixels >> 16)];
        const uint32_t pixel3 = palette[(uint8_t)(pixels >> 24)];
        ((uint32_t *)output)[0] = pixel0 | pixel1 << 16;
        ((uint32_t *)output)[1] = pixel2 | pixel3 << 16;
#else
        output[0] = palette[source[0]];
        output[1] = palette[source[1]];
        output[2] = palette[source[2]];
        output[3] = palette[source[3]];
#endif
        source += 4;
        output += 4;
    } while (--groups);
}

// One text scanline: 80 columns of 8 pixels fill the whole 640-sample active
// area, so the picture keeps its full 640 x 480 size. Two pixels are written
// per 16-bit store; the four (left, right) color pairs of a cell are built once
// per column. The low byte of a 16-bit store is the left pixel.
static __force_inline void vga_dual_generate_text_line(
        const size_t line, uint32_t *line_buffer) {
    unsigned glyph_line;
    const uint8_t *cell = graphics_text_row(line, VGA_DUAL_TEXT_FONT_HEIGHT,
                                            &glyph_line);
    uint16_t *output = (uint16_t *)((uint8_t *)line_buffer +
                                    VGA_DUAL_ACTIVE_OFFSET);

    if (cell == NULL) {
        memset(output, (uint8_t)vga_dual_text_pairs[0], GRAPHICS_TEXT_SAMPLES);
        return;
    }

    const unsigned columns = graphics_text_columns();
    const bool wide = columns * 8u < GRAPHICS_TEXT_SAMPLES;

    for (unsigned column = columns; column--;) {
        const uint8_t character = *cell++;
        const uint8_t attribute = *cell++;
#if VGA_RGBS
        uint8_t glyph = font_8x8[character * VGA_DUAL_TEXT_FONT_HEIGHT +
                                 glyph_line];
#else
        uint8_t glyph = font_8x16[character * VGA_DUAL_TEXT_FONT_HEIGHT +
                                  glyph_line];
#endif
        const uint16_t *pair = &vga_dual_text_pairs[attribute * 4u];

        for (unsigned step = 4; step--;) {
            const uint16_t both = pair[glyph & 3u];
            glyph >>= 2;
            if (wide) {
                // A 40-column cell is twice as wide, so each pixel is sent
                // twice: the pair becomes two same-color pairs.
                *output++ = (uint16_t)((both & 0xffu) * 0x0101u);
                *output++ = (uint16_t)((both >> 8) * 0x0101u);
            } else {
                *output++ = both;
            }
        }
    }
}

static bool __time_critical_func(vga_pc_double_lines)(void) {
#if VGA_RGBS
    return false;
#else
    switch (graphics_source_mode()) {
        case TEXTMODE_40x25_BW:
        case TEXTMODE_40x25_COLOR:
        case TEXTMODE_80x25_BW:
        case TEXTMODE_80x25_COLOR:
        case HERC_640x480x2:
        case HERC_640x480x2_90:
        case EGA_640x350x16x4:
        case VGA_640x480x2:
        case VGA_640x480x16:
            return false;
        default:
            return true;
    }
#endif
}

static void __time_critical_func(vga_dual_generate_pc_line)(
        const size_t line, const size_t source_line, uint32_t *line_buffer) {
    // The composer uses a 480-row canvas even when the CRTC sends only
    // 350 active rows. Passing 350 would scale/crop the source incorrectly.
    const unsigned width = graphics_source_native_width();
    uint8_t *output = (uint8_t *)line_buffer + VGA_DUAL_ACTIVE_OFFSET;
    if (vga_crtc_text_custom) {
        vga_crtc_render_text(line, VGA_DUAL_CANVAS_LINES,
                (uint16_t *)output, vga_dual_text_pairs);
        return;
    }
    if (graphics_source_is_text()) {
        graphics_source_render_vga_text(line, VGA_DUAL_CANVAS_LINES,
                (uint16_t *)output, vga_dual_text_pairs, vga_dual_text_quads);
        return;
    }
#if VGA_ENABLE_DITHER
    const uint16_t *palette = vga_dual_palette[
            ((source_line & vga_flash_line) ^ (vga_dual_frame_phase & vga_flash_frame)) & 1u];
#else
    const uint16_t *palette = vga_dual_palette;
#endif
    if (width == VGA_DUAL_SOURCE_WIDTH) {
        graphics_source_render_line16(line, width, VGA_DUAL_CANVAS_LINES,
                                      (uint16_t *)output, palette);
    } else {
        graphics_source_render_vga8(line, VGA_DUAL_CANVAS_LINES,
                                     output, palette);
    }
}

static void __time_critical_func(vga_dual_prepare_line)(const size_t line) {
    if (line < vga_active_timing.active_lines) {
        if (graphics_source_is_pc_mode()) {
            const bool double_lines = !vga_crtc_text_custom && vga_pc_double_lines();
            if (double_lines && (line & 1u)) {
                return;
            }
            if (line == 0u && vga_dual_pending_framebuffer != NULL) {
                __mem_fence_acquire();
                vga_dual_active_framebuffer = vga_dual_pending_framebuffer;
                __mem_fence_release();
                vga_dual_pending_framebuffer = NULL;
            }
            const size_t source_line = double_lines ? line >> 1u : line;
            uint32_t *line_buffer = vga_dual_line_buffer(
                    VGA_DUAL_ACTIVE_BUFFER_0 + (source_line & 1u));
            vga_dual_generate_pc_line(line, source_line, line_buffer);
            vga_dual_next_line_address = (uintptr_t)line_buffer;
            return;
        }
        // Text has one source line for every physical line, so unlike the
        // graphics modes it cannot skip the odd ones.
        if (graphics_source_is_text()) {
            uint32_t *line_buffer = (line & 1u) != 0u
                                    ? vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_1)
                                    : vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_0);
            vga_dual_generate_text_line(line, line_buffer);
            vga_dual_next_line_address = (uintptr_t)line_buffer;
            if (line == 0u && vga_dual_pending_framebuffer != NULL) {
                __mem_fence_acquire();
                vga_dual_active_framebuffer = vga_dual_pending_framebuffer;
                __mem_fence_release();
                vga_dual_pending_framebuffer = NULL;
            }
            return;
        }

#if VGA_RGBS
        // 240 output lines take the 240 source lines one for one. Nothing is
        // doubled, so there is no odd line to skip and the picture comes out at
        // its own size.
        const size_t source_line = line;
#else
        if ((line & 1u) != 0u) {
            return;
        }

        const size_t source_line = line >> 1u;
#endif
        const uint8_t *pending = NULL;
        if (line == 0u) {
            pending = vga_dual_pending_framebuffer;
            if (pending != NULL) {
                __mem_fence_acquire();
                vga_dual_active_framebuffer = pending;
            }
        }
        uint32_t *line_buffer = (source_line & 1u) != 0u
                                ? vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_1)
                                : vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_0);
        vga_dual_generate_active_line(source_line, line_buffer);
        if (pending != NULL) {
            __mem_fence_release();
            vga_dual_pending_framebuffer = NULL;
        }
        vga_dual_next_line_address = (uintptr_t)line_buffer;
        return;
    }

    if (line >= vga_active_timing.vsync_first_line &&
        line < vga_active_timing.vsync_first_line + vga_active_timing.vsync_lines) {
        vga_dual_next_line_address =
                (uintptr_t)vga_dual_line_buffer(VGA_DUAL_VSYNC_BUFFER);
        return;
    }
    vga_dual_next_line_address =
            (uintptr_t)vga_dual_line_buffer(VGA_DUAL_BLANK_BUFFER);
}

size_t vga_current_line(void) {
    return vga_dual_current_line;
}

void vga_wait_vblank(void) {
    // Return the moment the line counter runs backwards, which is the start of
    // a frame. Called from anywhere in a frame this waits at most one frame,
    // and it hands the caller the longest possible lead over the beam.
    size_t previous = vga_dual_current_line;
    while (true) {
        const size_t line = vga_dual_current_line;
        if (line < previous) {
            return;
        }
        previous = line;
        tight_loop_contents();
    }
}

static void __time_critical_func(vga_dual_dma_handler)(void) {
    dma_hw->ints1 = 1u << vga_dual_dma_control;

    size_t current_line = vga_dual_current_line;
    if (++current_line == vga_active_timing.total_lines) {
        current_line = 0;
        vga_active_timing = vga_timing_for_mode(graphics_source_mode(), VGA_RGBS);
    }
    vga_dual_current_line = current_line;
#if defined(GRAPHICS_PC_EMULATOR)
#if VGA_RGBS
    port3DA = current_line >= vga_active_timing.active_lines ? 9u : 0u;
#else
    const enum graphics_mode_t mode = graphics_source_mode();
    port3DA = graphics_pc_raster_status(mode, current_line, false);
#endif
#endif
    size_t following_line = current_line + 1u;
    if (following_line == vga_active_timing.total_lines) {
        following_line = 0;
    }
#if VGA_ENABLE_DITHER
    if (following_line == 0u) {
        vga_dual_frame_phase ^= 1u;
    }
#endif
    vga_dual_prepare_line(following_line);
#if defined(GRAPHICS_PC_EMULATOR)
#if VGA_RGBS
    port3DA |= 1u;
#else
    port3DA = graphics_pc_raster_status(mode, current_line, true);
#endif
#endif
}

void vga_set_flashmode(const bool flash_line, const bool flash_frame) {
#if VGA_ENABLE_DITHER
    vga_flash_line = flash_line;
    vga_flash_frame = flash_frame;
#else
    (void)flash_line;
    (void)flash_frame;
#endif
}

void vga_set_palette(const uint8_t index, uint32_t color888) {
#if defined(GRAPHICS_PC_EMULATOR)
    color888 = graphics_pc_palette_color(index, color888, false);
#endif
    static const uint8_t lower_level[8] = {
            0, 0, 1, 2, 2, 2, 3, 3
    };
    static const uint8_t upper_level[8] = {
            0, 1, 1, 1, 2, 3, 3, 3
    };

    const uint8_t red = (uint8_t)((color888 >> 16) & 0xffu) / 42u;
    const uint8_t green = (uint8_t)((color888 >> 8) & 0xffu) / 42u;
    const uint8_t blue = (uint8_t)(color888 & 0xffu) / 42u;
    const uint8_t first = (uint8_t)(VGA_DUAL_BLANK_LEVEL |
            lower_level[red] << 4 |
            lower_level[green] << 2 |
            lower_level[blue]);
    const uint8_t second = (uint8_t)(VGA_DUAL_BLANK_LEVEL |
            upper_level[red] << 4 |
            upper_level[green] << 2 |
            upper_level[blue]);

#if VGA_ENABLE_DITHER
    vga_dual_palette[0][index] =
            (uint16_t)(second | (uint16_t)first << 8);
    vga_dual_palette[1][index] =
            (uint16_t)(first | (uint16_t)second << 8);
#else
    vga_dual_palette[index] = (uint16_t)(second | (uint16_t)first << 8);
#endif
}

void vga_output_init(const uint8_t *framebuffer) {
    vga_active_timing = vga_timing_for_mode(graphics_source_mode(), VGA_RGBS);
    // Take the busiest text lookup out of flash, so a text line does not wait
    // on the QSPI bus while the other core is also reading flash.
    memcpy(vga_dual_text_pairs, vga_dual_text_pairs_source,
           sizeof vga_dual_text_pairs);
    for (unsigned attribute = 0; attribute < 256u; ++attribute) {
        const uint32_t foreground = vga_dual_text_pairs[attribute * 4u + 3u] & 255u;
        const uint32_t background = vga_dual_text_pairs[attribute * 4u] & 255u;
        for (unsigned pattern = 0; pattern < 16u; ++pattern) {
            uint32_t pixels = 0;
            for (unsigned bit = 0; bit < 4u; ++bit)
                pixels |= ((pattern & (1u << bit)) ? foreground : background) << (bit * 8u);
            vga_dual_text_quads[attribute * 16u + pattern] = pixels;
        }
    }

    vga_dual_active_framebuffer = framebuffer;
    vga_dual_pending_framebuffer = NULL;
#if VGA_ENABLE_DITHER
    vga_dual_frame_phase = 0;
#endif
    vga_dual_make_templates();
    vga_dual_generate_active_line(
            0, vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_0));
    vga_dual_next_line_address = (uintptr_t)vga_dual_line_buffer(
            VGA_DUAL_ACTIVE_BUFFER_0);

    const uint program_offset = pio_add_program(vga_dual_pio,
                                                &vga_dual_program);
    vga_dual_sm = pio_claim_unused_sm(vga_dual_pio, true);

    for (uint pin = 0; pin < 8; pin++) {
        const uint gpio = VGA_BASE_PIN + pin;
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_OUT);
        pio_gpio_init(vga_dual_pio, gpio);
    }
    pio_sm_set_consecutive_pindirs(vga_dual_pio, vga_dual_sm,
                                   VGA_BASE_PIN, 8, true);

    pio_sm_config sm_config = pio_get_default_sm_config();
    sm_config_set_wrap(&sm_config, program_offset, program_offset);
    sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&sm_config, true, true, 32);
    sm_config_set_out_pins(&sm_config, VGA_BASE_PIN, 8);
    sm_config_set_clkdiv(&sm_config,
            (float)clock_get_hz(clk_sys) / VGA_DUAL_PIXEL_CLOCK_HZ);
    pio_sm_init(vga_dual_pio, vga_dual_sm, program_offset, &sm_config);

    vga_dual_dma_data = dma_claim_unused_channel(true);
    vga_dual_dma_control = dma_claim_unused_channel(true);

    dma_channel_config data_config =
            dma_channel_get_default_config(vga_dual_dma_data);
    channel_config_set_transfer_data_size(&data_config, DMA_SIZE_32);
    channel_config_set_read_increment(&data_config, true);
    channel_config_set_write_increment(&data_config, false);
    channel_config_set_dreq(&data_config,
                            pio_get_dreq(vga_dual_pio, vga_dual_sm, true));
    channel_config_set_chain_to(&data_config, vga_dual_dma_control);
    dma_channel_configure(
            vga_dual_dma_data,
            &data_config,
            &vga_dual_pio->txf[vga_dual_sm],
            vga_dual_line_buffer(VGA_DUAL_ACTIVE_BUFFER_0),
            VGA_DUAL_LINE_WORDS,
            false);

    dma_channel_config control_config =
            dma_channel_get_default_config(vga_dual_dma_control);
    channel_config_set_transfer_data_size(&control_config, DMA_SIZE_32);
    channel_config_set_read_increment(&control_config, false);
    channel_config_set_write_increment(&control_config, false);
    channel_config_set_chain_to(&control_config, vga_dual_dma_data);
    dma_channel_configure(
            vga_dual_dma_control,
            &control_config,
            &dma_hw->ch[vga_dual_dma_data].read_addr,
            (const void *)&vga_dual_next_line_address,
            1,
            false);

    irq_set_exclusive_handler(DMA_IRQ_1, vga_dual_dma_handler);
    dma_channel_set_irq1_enabled(vga_dual_dma_control, true);
    irq_set_enabled(DMA_IRQ_1, true);
}

uint32_t vga_start_mask(void) {
    return 1u << vga_dual_dma_data;
}

void vga_enable(void) {
    pio_sm_set_enabled(vga_dual_pio, vga_dual_sm, true);
}

void vga_set_framebuffer(const uint8_t *framebuffer) {
    vga_dual_active_framebuffer = framebuffer;
}

void vga_request_framebuffer(const uint8_t *framebuffer) {
    __mem_fence_release();
    vga_dual_pending_framebuffer = framebuffer;
}

void vga_wait_framebuffer(void) {
    while (vga_dual_pending_framebuffer != NULL) {
        tight_loop_contents();
    }
    __mem_fence_acquire();
}
