// Host check: gcc -std=c17 -O2 -Wall -Wextra -Werror -Idrivers/hdmi tests/hdmi_codec_test.c -o hdmi_codec_test
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HDMI_PIN_invert_diffpairs 1
#define HDMI_PIN_RGB_notBGR 1
#include "hdmi_codec.h"

static union hdmi_workspace workspace;

static void test_palette_overlay(void) {
    uint32_t colors[HDMI_PALETTE_COLORS];
    for (unsigned i = 0; i < HDMI_PALETTE_COLORS; ++i) {
        colors[i] = ((i * 73u) & 255u) << 16 |
                    ((i * 151u) & 255u) << 8 | ((i * 199u) & 255u);
        hdmi_encode_graphics_rgb(workspace.graphics.palette[i], colors[i]);
        assert(hdmi_decode_graphics_rgb(workspace.graphics.palette[i]) == colors[i]);
    }
    hdmi_cache_graphics_palette(&workspace);
    memset(workspace.wide.palette, 0xa5, sizeof workspace.wide.palette);
    memset(workspace.wide.lines, 0x5a, sizeof workspace.wide.lines);
    for (unsigned i = 0; i < HDMI_PALETTE_COLORS; ++i) {
        const uint8_t *rgb = workspace.wide.graphics_rgb[i];
        assert(((uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2]) == colors[i]);
    }
    hdmi_restore_graphics_palette(&workspace);
    for (unsigned i = 0; i < HDMI_PALETTE_COLORS; ++i) {
        assert(hdmi_decode_graphics_rgb(workspace.graphics.palette[i]) == colors[i]);
    }
}

static void test_wide_pio_addresses(void) {
    // Model IN OSR,6 then IN X,23, both shifting right. These must give
    // an eight-byte entry address for every colour, phase and sync code.
    const uint32_t base = 0x20008000u;
    for (unsigned code = 0; code < 64; ++code) {
        uint32_t isr = code << 26;
        isr = (isr >> 23) | ((base >> 9) << 9);
        assert(isr == base + code * 8u);
    }
}

static void test_dma_row_lifetime(void) {
    for (unsigned doubled = 0; doubled < 2; ++doubled) {
        struct hdmi_line_schedule schedule = {UINT32_MAX, 0};
        unsigned slots[2] = {7, 7}; // the buffer each DMA ring slot points to; 7: none yet
        // Startup prepares two lines, then the first IRQ requests the second
        // one again while DMA reads the first. It must not touch that buffer.
        assert(hdmi_schedule_line(&schedule, 480, doubled, slots[1]));
        slots[0] = schedule.buffer;
        assert(hdmi_schedule_line(&schedule, 481, doubled, slots[0]));
        slots[1] = schedule.buffer;
        assert(slots[0] != slots[1]);
        assert(!hdmi_schedule_line(&schedule, 481, doubled, slots[0]));
        assert(schedule.buffer == slots[1]);

        // Each step the DMA starts the other slot; the IRQ fills the slot
        // after it. Every 100th IRQ is a line late, and the DMA has moved on.
        unsigned on_air = 0, line = 481;
        for (unsigned step = 0; step < 525 * 3; ++step) {
            on_air ^= 1u;
            if (step % 100 == 99) continue;
            line = (line + 1) % 525;
            const unsigned previous = schedule.buffer;
            const bool writes = hdmi_schedule_line(&schedule, line, doubled, slots[on_air]);
            if (doubled && line < 480 && (line & 1)) {
                assert(!writes);
                assert(schedule.buffer == previous);
            } else {
                // New rows never go into the buffer on air, also after a
                // late IRQ and at the odd-sized frame wrap.
                assert(writes);
                assert(schedule.buffer != slots[on_air]);
            }
            slots[on_air ^ 1u] = schedule.buffer;
        }
    }
}

int main(void) {
    test_palette_overlay();
    test_wide_pio_addresses();
    test_dma_row_lifetime();
    puts("HDMI palette overlay, wide PIO addresses, and DMA row lifetime tests passed");
}
