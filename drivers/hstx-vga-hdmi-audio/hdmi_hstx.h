#pragma once

#include <stdint.h>

#ifndef TEXTMODE_COLS
#define TEXTMODE_COLS 80
#endif
#ifndef TEXTMODE_ROWS
#define TEXTMODE_ROWS 30
#endif
#ifndef RGB888
#define RGB888(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Called by graphics_init(), on the core which will service scanout IRQs.
// SYS is 500 MHz; HSTX uses a separate PLL_USB at 126 MHz.
// USB and ADC clocks are stopped.
void hdmi_hstx_graphics_init(void);

#ifdef __cplusplus
}
#endif
