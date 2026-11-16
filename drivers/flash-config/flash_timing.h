#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <hardware/platform_defs.h>
#include <hardware/regs/qmi.h>

static inline uint64_t flash_timing_ceil_div(uint64_t value, uint32_t divisor) {
    return (value + divisor - 1u) / divisor;
}

// Keep the boot flash rate and its minimum physical timing intervals.
// cap_hz may lower the flash rate; zero means no extra cap. This handles the
// stock flash profile (MAX_SELECT=0), not PSRAM or automatic timing tuning.
// Compute before entering direct mode; applying the result needs idle QMI.
static inline bool flash_timing_for_clock(uint32_t boot_timing, uint32_t boot_hz,
        uint32_t sys_hz, uint32_t cap_hz, uint32_t *out) {
    if (!out || !boot_hz || !sys_hz ||
        (boot_timing & ~QMI_M0_TIMING_BITS) ||
        (boot_timing & QMI_M0_TIMING_MAX_SELECT_BITS)) return false;

    uint32_t boot_div = (boot_timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB;
    if (!boot_div) boot_div = 256u;
    const uint32_t boot_half = (boot_div + 1u) / 2u;
    const uint32_t boot_rx = (boot_timing & QMI_M0_TIMING_RXDELAY_BITS) >> QMI_M0_TIMING_RXDELAY_LSB;
    const uint32_t boot_setup = (boot_timing & QMI_M0_TIMING_SELECT_SETUP_BITS) >> QMI_M0_TIMING_SELECT_SETUP_LSB;
    const uint32_t boot_hold = (boot_timing & QMI_M0_TIMING_SELECT_HOLD_BITS) >> QMI_M0_TIMING_SELECT_HOLD_LSB;
    const uint32_t boot_deselect = (boot_timing & QMI_M0_TIMING_MIN_DESELECT_BITS) >> QMI_M0_TIMING_MIN_DESELECT_LSB;

    uint64_t divider = flash_timing_ceil_div((uint64_t)sys_hz * boot_div, boot_hz);
    if (cap_hz) {
        const uint64_t capped = flash_timing_ceil_div(sys_hz, cap_hz);
        if (capped > divider) divider = capped;
    }
    const uint64_t setup_cycles = flash_timing_ceil_div((uint64_t)(boot_half + boot_setup) * sys_hz, boot_hz);
    const uint32_t setup_max = QMI_M0_TIMING_SELECT_SETUP_BITS >> QMI_M0_TIMING_SELECT_SETUP_LSB;
    // A slower SCK can supply setup time beyond the one-cycle setup field.
    if (setup_cycles > setup_max) {
        const uint64_t setup_divider = 2u * (setup_cycles - setup_max) - 1u;
        if (setup_divider > divider) divider = setup_divider;
    }
    if (divider > 256u) return false;
    const uint32_t half = ((uint32_t)divider + 1u) / 2u;

    const uint64_t rx = flash_timing_ceil_div((uint64_t)boot_rx * sys_hz, boot_hz);
    const uint64_t setup = setup_cycles > half ? setup_cycles - half : 0;
    const uint64_t hold = flash_timing_ceil_div((uint64_t)(boot_hold + 1u) * sys_hz, boot_hz) - 1u;
    const uint64_t deselect_cycles = flash_timing_ceil_div((uint64_t)(boot_half + boot_deselect) * sys_hz, boot_hz);
    const uint64_t deselect = deselect_cycles > half ? deselect_cycles - half : 0;
    if (rx > (QMI_M0_TIMING_RXDELAY_BITS >> QMI_M0_TIMING_RXDELAY_LSB) ||
        setup > (QMI_M0_TIMING_SELECT_SETUP_BITS >> QMI_M0_TIMING_SELECT_SETUP_LSB) ||
        hold > (QMI_M0_TIMING_SELECT_HOLD_BITS >> QMI_M0_TIMING_SELECT_HOLD_LSB) ||
        deselect > (QMI_M0_TIMING_MIN_DESELECT_BITS >> QMI_M0_TIMING_MIN_DESELECT_LSB)) return false;

    const uint32_t fields = QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS |
            QMI_M0_TIMING_SELECT_SETUP_BITS | QMI_M0_TIMING_SELECT_HOLD_BITS |
            QMI_M0_TIMING_MIN_DESELECT_BITS;
    *out = (boot_timing & ~fields) |
            ((uint32_t)divider & 255u) << QMI_M0_TIMING_CLKDIV_LSB |
            (uint32_t)rx << QMI_M0_TIMING_RXDELAY_LSB |
            (uint32_t)setup << QMI_M0_TIMING_SELECT_SETUP_LSB |
            (uint32_t)hold << QMI_M0_TIMING_SELECT_HOLD_LSB |
            (uint32_t)deselect << QMI_M0_TIMING_MIN_DESELECT_LSB;
    return true;
}
