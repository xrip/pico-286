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
// HDMI-only: SYS is 500 MHz. With NTSC: SYS is 315 MHz.
// HSTX uses a separate PLL_USB at 126 MHz in both profiles.
// USB and ADC clocks are stopped.
void hdmi_hstx_graphics_init(void);

#ifdef __cplusplus
}
#endif
