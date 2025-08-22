#pragma GCC optimize("Ofast")
/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>
#include <emulator/emulator.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

#include "graphics.h"

#include <pico/multicore.h>

#include "st7789.pio.h"
#include "hardware/dma.h"

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


#define CHECK_BIT(var, pos) (((var)>>(pos)) & 1)

static uint sm_video_output = 0;
static PIO pio = pio0;
static uint st7789_chan;

uint16_t palette[256];

uint8_t *text_buffer = NULL;
static uint8_t *graphics_framebuffer = NULL;

static uint framebuffer_width = 0;
static uint framebuffer_height = 0;
static int framebuffer_offset_x = 0;
static int framebuffer_offset_y = 0;

enum graphics_mode_t graphics_mode = TEXTMODE_80x25_COLOR;

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

void stop_pixels() {
    st7789_lcd_wait_idle(pio, sm_video_output);
    lcd_set_dc_cs(1, 1);
    st7789_set_pixel_mode(pio, sm_video_output, false);
}

void create_dma_channel() {
    st7789_chan = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(st7789_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_video_output, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    dma_channel_configure(
        st7789_chan, // Channel to be configured
        &c, // The configuration we just created
        &pio->txf[sm_video_output], // The write address
        NULL, // The initial read address - set later
        0, // Number of transfers - set later
        false // Don't start yet
    );
}

void graphics_init() {
    gpio_init(TFT_CS_PIN);
    gpio_init(TFT_DC_PIN);
    gpio_init(TFT_RST_PIN);
    gpio_init(TFT_LED_PIN);

    const uint offset = pio_add_program(pio, &st7789_lcd_program);
    sm_video_output = pio_claim_unused_sm(pio, true);
    st7789_lcd_program_init(pio, sm_video_output, offset, TFT_DATA_PIN, TFT_CLK_PIN, SERIAL_CLK_DIV);


    gpio_set_dir(TFT_CS_PIN, GPIO_OUT);
    gpio_set_dir(TFT_DC_PIN, GPIO_OUT);
    gpio_set_dir(TFT_RST_PIN, GPIO_OUT);
    gpio_set_dir(TFT_LED_PIN, GPIO_OUT);

    gpio_put(TFT_CS_PIN, 1);
    gpio_put(TFT_RST_PIN, 1);
    lcd_init(init_seq);
    gpio_put(TFT_LED_PIN, 1);

    for (int i = 0; i < sizeof palette; i++) {
        graphics_set_palette(i, 0x0000);
    }

    create_dma_channel();

    uint32_t pixel_count = SCREEN_WIDTH * SCREEN_HEIGHT;
    while (pixel_count--) {
        st7789_lcd_put_pixel(pio, sm_video_output, 0x0);
    }


    lcd_set_window(framebuffer_offset_x, framebuffer_offset_y, framebuffer_width,
                   framebuffer_height);
}

void inline graphics_set_mode(const enum graphics_mode_t mode) {
    graphics_mode = mode;
}

void graphics_set_buffer(uint8_t *buffer, const uint16_t width, const uint16_t height) {
    graphics_framebuffer = buffer;
    framebuffer_width = width;
    framebuffer_height = height;
}

void graphics_set_textbuffer(uint8_t *buffer) {
    text_buffer = buffer;
}

void graphics_set_offset(const int x, const int y) {
    framebuffer_offset_x = x;
    framebuffer_offset_y = y;
}

static INLINE void st7789_dma_pixels(const uint16_t *pixels, const uint num_pixels) {
    // Ensure any previous transfer is finished.
    dma_channel_wait_for_finish_blocking(st7789_chan);

    dma_channel_hw_addr(st7789_chan)->read_addr = (uintptr_t) pixels;
    dma_channel_hw_addr(st7789_chan)->transfer_count = num_pixels;
    // const uint ctrl = dma_channel_hw_addr(st7789_chan)->ctrl_trig;
    dma_channel_hw_addr(st7789_chan)->ctrl_trig |= DMA_CH0_CTRL_TRIG_INCR_READ_BITS;
}

INLINE void __time_critical_func() refresh_lcd() {
    const uint8_t *input_buffer_8bit = graphics_framebuffer;

    // start_pixels();
    switch (graphics_mode) {
        case TEXTMODE_80x25_COLOR:
            for (int y = 0; y < SCREEN_HEIGHT; y++) {
                // TODO add auto adjustable padding?
                for (int x = 0; x < TEXTMODE_COLS; x++) {
                    const uint16_t offset = (y / 8) * (80 * 2) + x * 2;
                    const uint8_t c = text_buffer[offset];
                    const uint8_t colorIndex = text_buffer[offset + 1];
                    const uint8_t glyph_row = font_8x8[c * 8 + y % 8];

                    for (uint8_t bit = 0; bit < 8; bit++) {
                        st7789_lcd_put_pixel(pio, sm_video_output, textmode_palette[(c && CHECK_BIT(glyph_row, bit))
                                                                           ? colorIndex & 0x0F
                                                                           : colorIndex >> 4 & 0x0F]);

                        // st7789_lcd_put_pixel(pio,sm, CHECK_BIT(glyph_row, bit) ? 0xFFFF00 + bit * 4 : 0);
                    }
                }
            }
            break;
        case CGA_320x200x4:
        case CGA_320x200x4_BW: {
            for (int y = 0; y < 200; y++) {
                input_buffer_8bit = graphics_framebuffer + 0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
                //2bit buf
                for (int x = 320 / 4; x--;) {
                    const uint8_t cga_byte = *input_buffer_8bit++;

                    uint8_t color = cga_byte >> 6 & 3;
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color ? color : cga_foreground_color]);
                    color = cga_byte >> 4 & 3;
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color ? color : cga_foreground_color]);
                    color = cga_byte >> 2 & 3;
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color ? color : cga_foreground_color]);
                    color = cga_byte >> 0 & 3;
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color ? color : cga_foreground_color]);
                }
            }
            break;
        }
        case COMPOSITE_160x200x16_force:
        case COMPOSITE_160x200x16:
        case TGA_160x200x16:
            for (int y = 0; y < 200; y++) {
                input_buffer_8bit = tga_offset + graphics_framebuffer + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
                for (int x = 320 / 4; x--;) {
                    const uint8_t cga_byte = *input_buffer_8bit++; // Fetch 8 pixels from TGA memory
                    uint8_t color1 = cga_byte >> 4;
                    uint8_t color2 = cga_byte & 15;

                    if (videomode == 0x8) {
                        if (!color1) color1 = cga_foreground_color;
                        if (!color2) color2 = cga_foreground_color;
                    }

                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color1]);
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color1]);
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color2]);
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[color2]);
                }
            }
            break;
        case TGA_320x200x16: {
            //4bit buf
            //+ (y & 3) * 8192 + __fast_mul(y >> 2, 160);
            for (int y = 0; y < framebuffer_height; y++) {
                input_buffer_8bit = tga_offset + graphics_framebuffer + (y & 3) * 8192 + __fast_mul(y >> 2, 160);
                for (int x = 320 / 2; x--;) {
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[*input_buffer_8bit >> 4 & 15]);
                    st7789_lcd_put_pixel(pio, sm_video_output, palette[*input_buffer_8bit & 15]);
                    input_buffer_8bit++;
                }
            }
            break;
        }
        default:
        case VGA_320x200x256: {
            uint32_t i = 320 * 200;
            while (i--)
                st7789_lcd_put_pixel(pio, sm_video_output, palette[*input_buffer_8bit++]);
            break;
        }
    }
    // stop_pixels();
    // st7789_lcd_wait_idle(pio, sm);
}

void graphics_set_palette(const uint8_t index, const uint32_t color) {
    palette[index] = rgb888(color >> 16, color >> 8 & 0xff, color & 0xff);
}
