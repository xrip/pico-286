#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "inttypes.h"
#include "stdbool.h"

#include "hardware/pio.h"

#define PIO_VIDEO pio0
#define PIO_VIDEO_ADDR pio0
#define VIDEO_DMA_IRQ (DMA_IRQ_0)

#ifndef HDMI_BASE_PIN
#define HDMI_BASE_PIN (6)
#endif

#define HDMI_PIN_invert_diffpairs (1)
#define HDMI_PIN_RGB_notBGR (1)
#define beginHDMI_PIN_data (HDMI_BASE_PIN+2)
#define beginHDMI_PIN_clk (HDMI_BASE_PIN)


#define SCREEN_WIDTH (320)
#define SCREEN_HEIGHT (240)

#define TEXTMODE_COLS (SCREEN_WIDTH/4)
#define TEXTMODE_ROWS (SCREEN_HEIGHT/6)

#define RGB888(r, g, b) ((r<<16) | (g << 8 ) | b )

// TODO: Сделать настраиваемо
static const uint8_t textmode_palette[16] = {
    00, 01, 02, 03, 04, 05, 06, 07, 8, 9, 10, 11, 12, 13, 14, 15
};


static void graphics_set_flashmode(bool flash_line, bool flash_frame) {
    // dummy
}


#ifdef __cplusplus
}
#endif
