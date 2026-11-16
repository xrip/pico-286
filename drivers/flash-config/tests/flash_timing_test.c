// Host build needs the Pico SDK's src/rp2350/hardware_regs/include directory.
#include <assert.h>
#include <stdio.h>
#include "flash_timing.h"

static unsigned field(uint32_t timing, uint32_t mask, unsigned shift) {
    return (timing & mask) >> shift;
}

static unsigned divider(uint32_t timing) {
    const unsigned value = field(timing, QMI_M0_TIMING_CLKDIV_BITS, QMI_M0_TIMING_CLKDIV_LSB);
    return value ? value : 256;
}

static void check_intervals(uint32_t before, uint32_t after,
                            uint32_t boot_hz, uint32_t sys_hz, uint32_t cap_hz) {
    const unsigned old_div = divider(before), new_div = divider(after);
    const unsigned old_half = (old_div + 1u) / 2u, new_half = (new_div + 1u) / 2u;
    assert((uint64_t)new_div * boot_hz >= (uint64_t)old_div * sys_hz);
    if (cap_hz) assert((uint64_t)cap_hz * new_div >= sys_hz);

    const uint32_t masks[] = {QMI_M0_TIMING_RXDELAY_BITS, QMI_M0_TIMING_SELECT_SETUP_BITS,
                             QMI_M0_TIMING_SELECT_HOLD_BITS, QMI_M0_TIMING_MIN_DESELECT_BITS};
    const unsigned shifts[] = {QMI_M0_TIMING_RXDELAY_LSB, QMI_M0_TIMING_SELECT_SETUP_LSB,
                               QMI_M0_TIMING_SELECT_HOLD_LSB, QMI_M0_TIMING_MIN_DESELECT_LSB};
    const unsigned old_base[] = {0, old_half, 1, old_half};
    const unsigned new_base[] = {0, new_half, 1, new_half};
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned old_cycles = old_base[i] + field(before, masks[i], shifts[i]);
        const unsigned new_cycles = new_base[i] + field(after, masks[i], shifts[i]);
        // Cross-products compare actual time intervals without rounded ns.
        assert((uint64_t)new_cycles * boot_hz >= (uint64_t)old_cycles * sys_hz);
    }
    assert(!(after & ~QMI_M0_TIMING_BITS));
    const uint32_t retained = QMI_M0_TIMING_COOLDOWN_BITS | QMI_M0_TIMING_PAGEBREAK_BITS;
    assert((before & retained) == (after & retained));
}

static void rejected(uint32_t boot, uint32_t boot_hz, uint32_t sys_hz, uint32_t cap) {
    uint32_t result = 0x12345678;
    assert(!flash_timing_for_clock(boot, boot_hz, sys_hz, cap, &result));
    assert(result == 0x12345678);
}

int main(void) {
    const uint32_t boot = 0x60007203u, boot_hz = 150000000u;
    const uint32_t clocks[] = {150000000u, 252000000u, 315000000u, 378000000u,
                               500000000u, 501000000u, 504000000u};
    const uint32_t caps[] = {0, 100000000u, 50000000u, 42000000u, 25000000u};
    uint32_t result;
    for (unsigned c = 0; c < sizeof clocks / sizeof clocks[0]; ++c) {
        for (unsigned k = 0; k < sizeof caps / sizeof caps[0]; ++k) {
            assert(flash_timing_for_clock(boot, boot_hz, clocks[c], caps[k], &result));
            check_intervals(boot, result, boot_hz, clocks[c], caps[k]);
            // Every smaller divider must break the rate cap or setup interval.
            for (unsigned d = 1; d < divider(result); ++d) {
                const unsigned half = (d + 1u) / 2u;
                assert((uint64_t)d * boot_hz < (uint64_t)divider(boot) * clocks[c] ||
                       (caps[k] && (uint64_t)caps[k] * d < clocks[c]) ||
                       (uint64_t)(half + 1u) * boot_hz < 2ull * clocks[c]);
            }
        }
    }
    assert(flash_timing_for_clock(boot, boot_hz, boot_hz, 0, &result) && result == boot);
    assert(flash_timing_for_clock(boot, boot_hz, 504000000u, 100000000u, &result));
    assert(result == 0x6381970bu);
    assert(flash_timing_for_clock(boot, boot_hz, 500000000u, 100000000u, &result));
    assert(divider(result) == 11);
    const uint32_t longer_setup = boot | QMI_M0_TIMING_SELECT_SETUP_BITS;
    assert(flash_timing_for_clock(longer_setup, boot_hz, 504000000u, 0, &result));
    check_intervals(longer_setup, result, boot_hz, 504000000u, 0);
    assert(divider(result) == 19);
    assert(flash_timing_for_clock(boot, boot_hz, boot_hz, 585938u, &result));
    assert(divider(result) == 256 && (result & QMI_M0_TIMING_CLKDIV_BITS) == 0);
    check_intervals(boot, result, boot_hz, boot_hz, 585938u);

    rejected(boot, 0, 504000000u, 0);
    rejected(boot, boot_hz, 0, 0);
    rejected(boot, boot_hz, 504000000u, 1000000u); // Divider exceeds 256.
    rejected(boot | (1u << QMI_M0_TIMING_MAX_SELECT_LSB), boot_hz, 504000000u, 0);
    rejected(boot | 0x800u, boot_hz, 504000000u, 0); // Reserved bit, including RX spill.
    rejected((boot & ~QMI_M0_TIMING_RXDELAY_BITS) | (3u << QMI_M0_TIMING_RXDELAY_LSB), boot_hz, 504000000u, 0);
    // Rate permits divider 256, but setup needs 257 and cannot fit.
    rejected((longer_setup & ~QMI_M0_TIMING_CLKDIV_BITS) | 255u,
             boot_hz, 150100000u, 0);
    rejected(boot | (1u << QMI_M0_TIMING_SELECT_HOLD_LSB), boot_hz, 504000000u, 0);
    rejected(boot | QMI_M0_TIMING_MIN_DESELECT_BITS, boot_hz, 504000000u, 0);
    rejected(boot, 1, UINT32_MAX, 0); // Wide intermediate arithmetic must not wrap.
    assert(!flash_timing_for_clock(boot, boot_hz, 504000000u, 0, NULL));
    puts("Flash timing: physical intervals, minimal dividers, 378/500/501/504 MHz and field bounds passed");
}
