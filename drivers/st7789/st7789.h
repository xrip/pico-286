#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TFT_RST_PIN
#define TFT_RST_PIN 8
#endif

#ifndef TFT_CS_PIN
#define TFT_CS_PIN 6
#endif

#ifndef TFT_LED_PIN
#define TFT_LED_PIN 9
#endif

#ifndef TFT_CLK_PIN
#define TFT_CLK_PIN 13
#endif

#ifndef TFT_DATA_PIN
#define TFT_DATA_PIN 12
#endif

#ifndef TFT_DC_PIN
#define TFT_DC_PIN 10
#endif

// The panel is 320 x 240 and the text font is 8 x 8, so it holds 40 columns of
// 30 rows. A build which also drives NTSC keeps that driver's wider buffer,
// because the header it pre-includes sets these first; the panel then shows the
// left 40 columns of each row.
#ifndef TEXTMODE_COLS
#define TEXTMODE_COLS 40
#endif

#ifndef TEXTMODE_ROWS
#define TEXTMODE_ROWS 30
#endif

#define rgb888(r, g, b) ((((r) >> 3) << 11) | \
                         (((g) >> 2) << 5) | ((b) >> 3))

void refresh_lcd(void);
void tft_graphics_init(void);

// Used when another driver owns the graphics API and drives the panel as a
// second output.
void tft_init(const uint8_t *framebuffer);
void tft_set_framebuffer(const uint8_t *framebuffer);
void tft_set_palette(uint8_t index, uint32_t color888);

#ifdef __cplusplus
}
#endif
