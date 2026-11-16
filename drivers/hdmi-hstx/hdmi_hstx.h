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
// HDMI-only: SYS is 504 MHz and HSTX uses PLL_SYS / 4 = 126 MHz;
// PLL_USB remains at 48 MHz for USB and ADC.
// With NTSC: SYS is 315 MHz and HSTX uses PLL_USB at 126 MHz;
// USB and ADC clocks are stopped in this profile.
void hdmi_hstx_graphics_init(void);

#ifdef __cplusplus
}
#endif
