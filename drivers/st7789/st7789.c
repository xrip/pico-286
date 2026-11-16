#pragma GCC optimize("Ofast")
/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stddef.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/dma.h"

#include "graphics.h"
#include "graphics_modes.h"

#include "st7789.pio.h"

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 320
#endif

#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 240
#endif

// 126MHz SPI
#define SERIAL_CLK_DIV 2.5f
#define MADCTL_BGR_PIXEL_ORDER (1<<3)
#define MADCTL_ROW_COLUMN_EXCHANGE (1<<5)
#define MADCTL_COLUMN_ADDRESS_ORDER_SWAP (1<<6)


static uint sm_video_output = 0;
static PIO pio = pio0;
static uint16_t palette[256];

static const uint8_t *graphics_framebuffer;

// Scratch line for the mode composer, as the other backends keep.
static uint8_t tft_staging[SCREEN_WIDTH] __attribute__((aligned(4)));
static uint16_t tft_pc_lines[2][SCREEN_WIDTH] __attribute__((aligned(4)));
static uint tft_dma_channel;

// 240 panel lines / 8 = the 30 text rows, and 320 pixels / 8 = 40 columns.
// The panel has no frame rate of its own, so the timer stands in for one.
#define TFT_FRAME_PERIOD_US 16667

#define TFT_TEXT_FONT_HEIGHT 8
#define TFT_TEXT_COLUMNS (SCREEN_WIDTH / 8)

// The 16 CGA text colors as panel pixels. Read-only, so this stays in flash.
#define TFT_TEXT_RGB565(color) (uint16_t)rgb888(GRAPHICS_CGA_RED(color), \
        GRAPHICS_CGA_GREEN(color), GRAPHICS_CGA_BLUE(color)),

static const uint16_t tft_text_color[16] = {
        GRAPHICS_TEXT_COLORS(TFT_TEXT_RGB565)
};

static const uint8_t init_seq[] = {
    1, 20, 0x01, // Software reset
    1, 10, 0x11, // Exit sleep mode
    2, 2, 0x3a, 0x55, // Set colour mode to 16 bit
#ifdef ILI9341
    // ILI9341
    2, 0, 0x36, MADCTL_ROW_COLUMN_EXCHANGE | MADCTL_BGR_PIXEL_ORDER, // Set MADCTL
#else
    // ST7789
    2, 0, 0x36, MADCTL_COLUMN_ADDRESS_ORDER_SWAP | MADCTL_ROW_COLUMN_EXCHANGE, // Set MADCTL
#endif
    5, 0, 0x2a, 0x00, 0x00, SCREEN_WIDTH >> 8, SCREEN_WIDTH & 0xff, // CASET: column addresses
    5, 0, 0x2b, 0x00, 0x00, SCREEN_HEIGHT >> 8, SCREEN_HEIGHT & 0xff, // RASET: row addresses
    1, 2, 0x20, // Inversion OFF
    1, 2, 0x13, // Normal display on, then 10 ms delay
    1, 2, 0x29, // Main screen turn on, then wait 500 ms
    0 // Terminate list
};
// Format: cmd length (including cmd byte), post delay in units of 5 ms, then cmd payload
// Note the delays have been shortened a little

static inline void lcd_set_dc_cs(const bool dc, const bool cs) {
    sleep_us(5);
    gpio_put_masked((1u << TFT_DC_PIN) | (1u << TFT_CS_PIN), !!dc << TFT_DC_PIN | !!cs << TFT_CS_PIN);
    sleep_us(5);
}

static inline void lcd_write_cmd(const uint8_t *cmd, size_t count) {
    st7789_lcd_wait_idle(pio, sm_video_output);
    lcd_set_dc_cs(0, 0);
    st7789_lcd_put(pio, sm_video_output, *cmd++);
    if (count >= 2) {
        st7789_lcd_wait_idle(pio, sm_video_output);
        lcd_set_dc_cs(1, 0);
        for (size_t i = 0; i < count - 1; ++i)
            st7789_lcd_put(pio, sm_video_output, *cmd++);
    }
    st7789_lcd_wait_idle(pio, sm_video_output);
    lcd_set_dc_cs(1, 1);
}

static inline void lcd_set_window(const uint16_t x,
                                  const uint16_t y,
                                  const uint16_t width,
                                  const uint16_t height) {
    static uint8_t screen_width_cmd[] = {0x2a, 0x00, 0x00, SCREEN_WIDTH >> 8, SCREEN_WIDTH & 0xff};
    static uint8_t screen_height_command[] = {0x2b, 0x00, 0x00, SCREEN_HEIGHT >> 8, SCREEN_HEIGHT & 0xff};
    screen_width_cmd[2] = x;
    screen_width_cmd[4] = x + width - 1;

    screen_height_command[2] = y;
    screen_height_command[4] = y + height - 1;
    lcd_write_cmd(screen_width_cmd, 5);
    lcd_write_cmd(screen_height_command, 5);
}

static inline void lcd_init(const uint8_t *init_seq) {
    while (*init_seq) {
        lcd_write_cmd(init_seq + 2, *init_seq);
        sleep_ms(init_seq[1] * 5);
        init_seq += *init_seq + 2;
    }
}

static inline void start_pixels() {
    const uint8_t cmd = 0x2c; // RAMWR
    st7789_lcd_wait_idle(pio, sm_video_output);
    st7789_set_pixel_mode(pio, sm_video_output, false);
    lcd_write_cmd(&cmd, 1);
    st7789_set_pixel_mode(pio, sm_video_output, true);
    lcd_set_dc_cs(1, 0);
}

static inline void stop_pixels(void) {
    st7789_lcd_wait_idle(pio, sm_video_output);
    lcd_set_dc_cs(1, 1);
    st7789_set_pixel_mode(pio, sm_video_output, false);
}

void tft_init(const uint8_t *framebuffer) {
    graphics_framebuffer = framebuffer;

    gpio_init(TFT_CS_PIN);
    gpio_init(TFT_DC_PIN);
    gpio_init(TFT_RST_PIN);
    gpio_init(TFT_LED_PIN);

    const uint offset = pio_add_program(pio, &st7789_lcd_program);
    sm_video_output = pio_claim_unused_sm(pio, true);
    st7789_lcd_program_init(pio, sm_video_output, offset, TFT_DATA_PIN, TFT_CLK_PIN, SERIAL_CLK_DIV);

    tft_dma_channel = dma_claim_unused_channel(true);
    dma_channel_config dma_config = dma_channel_get_default_config(tft_dma_channel);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm_video_output, true));
    dma_channel_configure(tft_dma_channel, &dma_config, &pio->txf[sm_video_output],
                          NULL, SCREEN_WIDTH, false);


    gpio_set_dir(TFT_CS_PIN, GPIO_OUT);
    gpio_set_dir(TFT_DC_PIN, GPIO_OUT);
    gpio_set_dir(TFT_RST_PIN, GPIO_OUT);
    gpio_set_dir(TFT_LED_PIN, GPIO_OUT);

    gpio_put(TFT_CS_PIN, 1);
    gpio_put(TFT_RST_PIN, 1);
    lcd_init(init_seq);
    gpio_put(TFT_LED_PIN, 1);

    for (int i = 0; i < 256; ++i) {
        tft_set_palette((uint8_t)i, 0x0000);
    }

    lcd_set_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    start_pixels();

    uint32_t pixel_count = SCREEN_WIDTH * SCREEN_HEIGHT;
    while (pixel_count--) {
        st7789_lcd_put_pixel(pio, sm_video_output, 0x0);
    }
    stop_pixels();
}

void tft_set_framebuffer(const uint8_t *framebuffer) {
    graphics_framebuffer = framebuffer;
}

void tft_set_palette(const uint8_t index, uint32_t color888) {
#if defined(GRAPHICS_PC_EMULATOR)
    color888 = graphics_pc_palette_color(index, color888, false);
#endif
    palette[index] = rgb888(color888 >> 16, color888 >> 8 & 0xff, color888 & 0xff);
}

// One text line of the panel. The glyph is read from the lowest bit up, the way
// the font is stored and the way the other backends read it.
static void __time_critical_func(tft_push_text_line)(const unsigned row) {
    unsigned glyph_line;
    const uint8_t *cell = graphics_text_row(row, TFT_TEXT_FONT_HEIGHT,
                                            &glyph_line);
    unsigned drawn = 0;

    if (cell != NULL) {
        // The panel holds 40 cells. A wider text buffer, as an NTSC build
        // keeps, shows its left-hand 40 columns here.
        unsigned columns = graphics_text_columns();
        if (columns > TFT_TEXT_COLUMNS) {
            columns = TFT_TEXT_COLUMNS;
        }

        for (unsigned column = columns; column--;) {
            const uint8_t character = *cell++;
            const uint8_t attribute = *cell++;
            uint8_t glyph = font_8x8[character * TFT_TEXT_FONT_HEIGHT +
                                     glyph_line];
            const uint16_t color[2] = {
                    tft_text_color[attribute >> 4],
                    tft_text_color[attribute & 0x0fu]
            };

            for (unsigned bit = 8; bit--;) {
                st7789_lcd_put_pixel(pio, sm_video_output, color[glyph & 1u]);
                glyph >>= 1;
            }
        }
        drawn = columns * 8u;
    }

    for (unsigned pixel = drawn; pixel < SCREEN_WIDTH; ++pixel) {
        st7789_lcd_put_pixel(pio, sm_video_output, tft_text_color[0]);
    }
}

void __time_critical_func(refresh_lcd)(void) {
    const bool pc_mode = graphics_source_is_pc_mode();
    const bool text = !pc_mode && graphics_source_is_text();

    if (!pc_mode && !text && graphics_framebuffer == NULL) {
        return;
    }

    lcd_set_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    start_pixels();

    if (pc_mode) {
        // Decode the next row while DMA sends the previous one. Each source
        // remains alive until that transfer ends; no full framebuffer is used.
        for (unsigned row = 0; row < SCREEN_HEIGHT; ++row) {
            uint16_t *pixels = tft_pc_lines[row & 1u];
            graphics_source_render_line16(row, SCREEN_WIDTH, SCREEN_HEIGHT,
                                          pixels, palette);
            dma_channel_wait_for_finish_blocking(tft_dma_channel);
            dma_channel_set_read_addr(tft_dma_channel, pixels, false);
            dma_channel_set_trans_count(tft_dma_channel, SCREEN_WIDTH, true);
        }
        dma_channel_wait_for_finish_blocking(tft_dma_channel);
        st7789_lcd_wait_idle(pio, sm_video_output);
        stop_pixels();
        return;
    }

    for (unsigned row = 0; row < SCREEN_HEIGHT; ++row) {
        if (text) {
            tft_push_text_line(row);
            continue;
        }

        const uint8_t *line = graphics_source_line(graphics_framebuffer, row,
                                                  tft_staging);
        for (unsigned pixel = 0; pixel < SCREEN_WIDTH; ++pixel) {
            st7789_lcd_put_pixel(pio, sm_video_output, palette[line[pixel]]);
        }
    }

    st7789_lcd_wait_idle(pio, sm_video_output);
    stop_pixels();
}

// Below here the panel is the only output, so it owns the graphics API. An
// NTSC build supplies its own and drives the panel through the calls above.
#if !defined(NTSC_TV)

uint8_t *text_buffer = NULL;
#if defined(GRAPHICS_NO_BUILTIN_FRAMEBUFFER)
#define TFT_INITIAL_FRAMEBUFFER NULL
#else
static uint8_t tft_primary_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT]
        __attribute__((aligned(4)));
#define TFT_INITIAL_FRAMEBUFFER tft_primary_framebuffer
#endif

void tft_graphics_init(void) {
    tft_init(TFT_INITIAL_FRAMEBUFFER);
}

void graphics_set_mode(const enum graphics_mode_t mode) {
    graphics_source_set_mode(mode);
}

void graphics_wait_vblank(void) {
    // The panel holds its own frame memory and takes a whole frame at a time,
    // so there is no beam to stay in front of. An application still needs a
    // frame clock, though, so this paces on the timer at the same rate the
    // other outputs run, rather than returning at once and letting the caller
    // free-run.
    static uint64_t next_frame;
    const uint64_t now = time_us_64();

    if (next_frame <= now) {
        next_frame = now;   // fell behind: take the current time as the mark
    }
    next_frame += TFT_FRAME_PERIOD_US;

    while (time_us_64() < next_frame) {
        tight_loop_contents();
    }
}

void graphics_set_buffer(uint8_t *buffer,
                         const uint16_t width,
                         const uint16_t height) {
    graphics_source_set_buffer(buffer, width, height);

    if (width == SCREEN_WIDTH && height == SCREEN_HEIGHT) {
        graphics_framebuffer = buffer;
    }
}

void graphics_set_textbuffer(uint8_t *buffer) {
    text_buffer = buffer;
}

void graphics_set_offset(const int x, const int y) {
    graphics_source_set_offset(x, y);
}

void graphics_set_bgcolor(const uint32_t color888) {
    (void)color888;
}

void graphics_set_flashmode(const bool flash_line, const bool flash_frame) {
    (void)flash_line;
    (void)flash_frame;
}

void graphics_set_palette(const uint8_t index, const uint32_t color) {
    tft_set_palette(index, color);
}

uint8_t *graphics_get_framebuffer(void) {
    return (uint8_t *)graphics_framebuffer;
}

void graphics_present_framebuffer(const uint8_t *framebuffer) {
    __mem_fence_release();
    graphics_framebuffer = framebuffer;
    refresh_lcd();
}

#endif
