#include "graphics.h"
#include "graphics_modes.h"
#include <string.h>

#if defined(HDMI) || defined(HDMI_HSTX) || defined(NTSC_TV)
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <hardware/vreg.h>
#include <pico/time.h>
#endif

#if defined(HDMI_HSTX)
#if !PICO_RP2350
#error "HDMI_HSTX requires RP2350"
#endif
#include <hardware/pll.h>
#include <hardware/resets.h>
#include <hardware/structs/clocks.h>
#include <pico/stdlib.h>
#endif

#if defined(HDMI) && PICO_RP2040
#include <hardware/regs/vreg_and_chip_reset.h>
#include <hardware/structs/vreg_and_chip_reset.h>
#endif

enum {
    GRAPHICS_NTSC_SYSTEM_CLOCK_KHZ = 315000,
    GRAPHICS_HDMI_SYSTEM_CLOCK_KHZ = 378000,
    GRAPHICS_HSTX_CLOCK_HZ = 126000000
};

// An application which already owns the system clock, or which must set it
// before starting a second core, defines GRAPHICS_NO_CLOCK_SETUP. It then has
// to supply the clock the selected form needs by itself. HSTX validates the
// actual PLLs and clock mux/divider registers even when setup is disabled.
#if defined(HDMI_HSTX)
#if defined(CPU_FREQ_MHZ)
#define GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ (CPU_FREQ_MHZ * 1000u)
#else
#define GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ 315000u
#endif
_Static_assert(GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ == 315000u ||
               GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ == 500000u,
               "HSTX supports the 315 MHz and 500 MHz clock profiles");
#if defined(NTSC_TV)
_Static_assert(GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ == 315000u, "NTSC requires 315 MHz");
#endif

static bool graphics_pll_is_frequency(PLL pll, uint32_t frequency_hz) {
    const uint32_t cs = pll->cs;
    const uint32_t refdiv = cs & PLL_CS_REFDIV_BITS;
    const uint32_t fbdiv = pll->fbdiv_int & PLL_FBDIV_INT_BITS;
    const uint32_t postdiv1 =
        (pll->prim & PLL_PRIM_POSTDIV1_BITS) >> PLL_PRIM_POSTDIV1_LSB;
    const uint32_t postdiv2 =
        (pll->prim & PLL_PRIM_POSTDIV2_BITS) >> PLL_PRIM_POSTDIV2_LSB;

    if (!(cs & PLL_CS_LOCK_BITS) || (cs & PLL_CS_BYPASS_BITS) ||
        (pll->pwr & (PLL_PWR_PD_BITS | PLL_PWR_VCOPD_BITS |
                     PLL_PWR_POSTDIVPD_BITS)) ||
        !refdiv || !fbdiv || !postdiv1 || !postdiv2) {
        return false;
    }

    // Compare the exact ratio without truncating an intermediate frequency.
    return (uint64_t) XOSC_HZ * fbdiv ==
           (uint64_t) frequency_hz * refdiv * postdiv1 * postdiv2;
}

static void graphics_validate_hstx_clocks(void) {
    const uint32_t system_hz = GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ * 1000u;
    const uint32_t sys_ctrl = clocks_hw->clk[clk_sys].ctrl;
    const uint32_t sys_source =
        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX |
        (CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS <<
         CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB);

    if ((sys_ctrl & (CLOCKS_CLK_SYS_CTRL_SRC_BITS |
                     CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS)) != sys_source ||
        clocks_hw->clk[clk_sys].selected !=
            (1u << CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX) ||
        clocks_hw->clk[clk_sys].div !=
            (1u << CLOCKS_CLK_SYS_DIV_INT_LSB) ||
        !graphics_pll_is_frequency(pll_sys, system_hz) ||
        clock_get_hz(clk_sys) != system_hz) {
        panic("HDMI_HSTX needs direct PLL_SYS at the configured CPU frequency");
    }

    const uint32_t hstx_ctrl = clocks_hw->clk[clk_hstx].ctrl;
    const uint32_t hstx_source = CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB <<
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_LSB;
    if ((hstx_ctrl & CLOCKS_CLK_HSTX_CTRL_AUXSRC_BITS) != hstx_source ||
        (hstx_ctrl & (CLOCKS_CLK_HSTX_CTRL_ENABLE_BITS |
                      CLOCKS_CLK_HSTX_CTRL_ENABLED_BITS)) !=
            (CLOCKS_CLK_HSTX_CTRL_ENABLE_BITS |
             CLOCKS_CLK_HSTX_CTRL_ENABLED_BITS) ||
        (hstx_ctrl & CLOCKS_CLK_HSTX_CTRL_KILL_BITS) ||
        clocks_hw->clk[clk_hstx].div != (1u << CLOCKS_CLK_HSTX_DIV_INT_LSB) ||
        !graphics_pll_is_frequency(pll_usb, GRAPHICS_HSTX_CLOCK_HZ) ||
        clock_get_hz(clk_hstx) != GRAPHICS_HSTX_CLOCK_HZ ||
        (resets_hw->reset & RESETS_RESET_HSTX_BITS)) {
        panic("HDMI_HSTX needs undivided PLL_USB at 126 MHz");
    }

    if ((clocks_hw->clk[clk_usb].ctrl &
         (CLOCKS_CLK_USB_CTRL_ENABLE_BITS | CLOCKS_CLK_USB_CTRL_ENABLED_BITS)) ||
        (clocks_hw->clk[clk_adc].ctrl &
         (CLOCKS_CLK_ADC_CTRL_ENABLE_BITS | CLOCKS_CLK_ADC_CTRL_ENABLED_BITS))) {
        panic("HDMI_HSTX reserves PLL_USB: USB and ADC clocks must be stopped");
    }
}

#if !defined(GRAPHICS_NO_CLOCK_SETUP)
static void graphics_stop_peripheral_clock(clock_handle_t clock) {
    if (clock != clk_hstx && clock != clk_peri &&
        clock != clk_usb && clock != clk_adc) {
        panic("Invalid clock in HSTX startup");
    }

    clock_stop(clock);
    // These RP2350 clock slices all expose ENABLED at the same bit position.
    while (clocks_hw->clk[clock].ctrl & CLOCKS_CLK_USB_CTRL_ENABLED_BITS) {
        tight_loop_contents();
    }
}
#endif

void graphics_prepare_clock(void) {
    if (XOSC_HZ != 12000000u) {
        panic("HDMI_HSTX clock profile requires a 12 MHz crystal");
    }

#if !defined(GRAPHICS_NO_CLOCK_SETUP)
    // set_sys_clock_khz() temporarily uses PLL_USB and assumes it is 48 MHz.
    // Run this startup once, before any video DMA or the second core starts.
    if (!graphics_pll_is_frequency(pll_usb, 48000000u)) {
        panic("HDMI_HSTX startup expects PLL_USB at 48 MHz; use GRAPHICS_NO_CLOCK_SETUP for external setup");
    }

    // The SDK initially sources HSTX from clk_sys. Stop it before raising SYS.
    reset_block_mask(RESETS_RESET_HSTX_BITS);
    graphics_stop_peripheral_clock(clk_hstx);

    if (GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ == 500000u) {
        vreg_disable_voltage_limit();
        vreg_set_voltage(VREG_VOLTAGE_1_60);
    } else {
        vreg_set_voltage(VREG_VOLTAGE_1_30);
    }
    sleep_ms(10);
    set_sys_clock_khz(GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ, true);

    // The SDK normally leaves PERI on PLL_USB after changing SYS. Move it to
    // an integer division of SYS. Initialise application UART/SPI after this
    // call (105 MHz at 315, 125 MHz at 500).
    graphics_stop_peripheral_clock(clk_peri);
    clock_configure_int_divider(
        clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
        GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ * 1000u,
        GRAPHICS_HSTX_SYSTEM_CLOCK_KHZ == 500000u ? 4u : 3u);

    reset_block_mask(RESETS_RESET_USBCTRL_BITS | RESETS_RESET_ADC_BITS);
    graphics_stop_peripheral_clock(clk_usb);
    graphics_stop_peripheral_clock(clk_adc);

    // Keep scanout independent of SYS: 12 MHz * 105 / 5 / 2 = 126 MHz.
    pll_init(pll_usb, 1u, 1260000000u, 5u, 2u);
    clock_configure_undivided(
        clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        GRAPHICS_HSTX_CLOCK_HZ);
    while (!(clocks_hw->clk[clk_hstx].ctrl &
             CLOCKS_CLK_HSTX_CTRL_ENABLED_BITS)) {
        tight_loop_contents();
    }
    unreset_block_mask_wait_blocking(RESETS_RESET_HSTX_BITS);
#endif

    graphics_validate_hstx_clocks();
}
#elif defined(GRAPHICS_NO_CLOCK_SETUP)
void graphics_prepare_clock(void) {
}
#elif defined(HDMI)
void graphics_prepare_clock(void) {
#if PICO_RP2040
    hw_set_bits(&vreg_and_chip_reset_hw->vreg,
                VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    sleep_ms(33);
#else
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
#endif
    set_sys_clock_khz(GRAPHICS_HDMI_SYSTEM_CLOCK_KHZ, true);
}
#elif defined(NTSC_TV)
void graphics_prepare_clock(void) {
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_sys_clock_khz(GRAPHICS_NTSC_SYSTEM_CLOCK_KHZ, true);
}
#else
void graphics_prepare_clock(void) {
}
#endif

void graphics_init(void) {
#if defined(HDMI_HSTX)
    static bool initialized;
    if (initialized) {
        panic("HDMI_HSTX graphics_init must run once on the scanout core");
    }
    initialized = true;
#endif
    // The application prepares HSTX clocks on core 0; IRQs belong to the
    // scanout core. Never retune PLLs after SD, PSRAM or core 1 have started.
#if defined(HDMI_HSTX)
    graphics_validate_hstx_clocks();
#else
    graphics_prepare_clock();
#endif

#if defined(HDMI_HSTX)
    graphics_text_init();
    hdmi_hstx_graphics_init();
#elif defined(HDMI)
    graphics_text_init();
    hdmi_graphics_init();
#elif defined(NTSC_TV)
    ntsc_composite_graphics_init();
#elif defined(TFT)
    tft_graphics_init();
#elif defined(VGA)
    vga_graphics_init();
#else
#error "No graphics backend selected"
#endif
}

#if defined(GRAPHICS_PC_EMULATOR)
enum { PC_TEXT_ROWS = 25 };

static void write_pc_text_cell(uint32_t *cell, uint8_t character, uint8_t attribute) {
    cell[0] = (cell[0] & 0xffffff00u) | character;
    cell[1] = (cell[1] & 0xffffff00u) | attribute;
}
#endif

void draw_text(const char *string, uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor) {
#if defined(GRAPHICS_PC_EMULATOR)
    const unsigned columns = graphics_text_columns();
    if (!string || !text_buffer || x >= columns || y >= PC_TEXT_ROWS) return;
    uint32_t *cells = (uint32_t *)text_buffer + (y * columns + x) * 2u;
    const uint8_t attribute = (uint8_t)((bgcolor & 15u) << 4 | (color & 15u));
    while (*string && x++ < columns) {
        write_pc_text_cell(cells, (uint8_t)*string++, attribute);
        cells += 2;
    }
#elif defined(HDMI) || defined(HDMI_HSTX)
    graphics_text_draw(string, x, y, color, bgcolor);
#else
    const unsigned columns = graphics_text_columns();
    if (!string || !text_buffer || x >= columns || y >= TEXTMODE_ROWS) return;
    uint8_t *cells = text_buffer + (y * columns + x) * 2u;
    const uint8_t attribute = (uint8_t)((bgcolor & 15u) << 4 | (color & 15u));
    while (*string && x++ < columns) {
        *cells++ = (uint8_t)*string++;
        *cells++ = attribute;
    }
#endif
}

#if defined(GRAPHICS_PC_EMULATOR) || \
        (!defined(HDMI) && !defined(HDMI_HSTX) && \
         (defined(VGA) || defined(NTSC_TV) || defined(TFT)))
void clrScr(uint8_t color) {
    if (!text_buffer) return;
    const unsigned columns = graphics_text_columns();
    const uint8_t attribute = (uint8_t)((color & 15u) << 4);
#if defined(GRAPHICS_PC_EMULATOR)
    uint32_t *cells = (uint32_t *)text_buffer;
    for (unsigned count = columns * PC_TEXT_ROWS; count; --count) {
        write_pc_text_cell(cells, ' ', attribute);
        cells += 2;
    }
#else
    uint8_t *cells = text_buffer;
    for (unsigned count = columns * TEXTMODE_ROWS; count; --count) {
        *cells++ = ' ';
        *cells++ = attribute;
    }
#endif
}
#endif

void draw_window(const char title[2*TEXTMODE_COLS + 1], uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    char line[width + 1];
    memset(line, 0, sizeof line);
    width--;
    height--;
    // Рисуем рамки

    memset(line, 0xCD, width); // ═══


    line[0] = 0xC9; // ╔
    line[width] = 0xBB; // ╗
    draw_text(line, x, y, 11, 1);

    line[0] = 0xC8; // ╚
    line[width] = 0xBC; //  ╝
    draw_text(line, x, height + y, 11, 1);

    memset(line, ' ', width);
    line[0] = line[width] = 0xBA;

    for (int i = 1; i < height; i++) {
        draw_text(line, x, y + i, 11, 1);
    }

    snprintf(line, width - 1, " %s ", title);
    draw_text(line, x + (width - strlen(line)) / 2, y, 14, 3);
}
