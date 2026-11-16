#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"
#include "graphics_pc_hdmi.h"

uint32_t bench_vram[65536];
graphics_pc_state_t bench_state;
static uint32_t old_output[82], new_output[82];

void graphics_pc_snapshot(graphics_pc_state_t *state) {
    *state = bench_state;
}
uint32_t graphics_pc_ega_line(unsigned line) { return line * 40u; } // EGA 0Dh at the standard width, start 0

int main(void) {
    const enum graphics_mode_t modes[] = {
        CGA_320x200x4, CGA_320x200x4_BW, TGA_160x200x16,
        TGA_320x200x16, EGA_320x200x16x4, VGA_320x200x256,
        VGA_320x200x256x4, COMPOSITE_160x200x16, COMPOSITE_160x200x16_force
    };
    unsigned comparisons = 0;
    for (unsigned seed = 0; seed < 3; ++seed) {
        uint32_t rng = seed + 1;
        for (unsigned i = 0; i < 65536; ++i) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            bench_vram[i] = rng;
        }
        bench_state.tga_offset = seed * 0x4000;
        for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; ++m) {
            // The old loop ignores the CRTC start address in Tandy modes.
            const bool tandy = modes[m] == TGA_160x200x16 || modes[m] == TGA_320x200x16 ||
                               modes[m] == COMPOSITE_160x200x16 || modes[m] == COMPOSITE_160x200x16_force;
            bench_state.vram_offset = tandy ? 0 : seed * 0x117;
            for (unsigned row = 0; row < 200; ++row) {
                memset(old_output, 0xa5, sizeof old_output);
                memset(new_output, 0xa5, sizeof new_output);
                bench_old_hdmi(modes[m], row * 2, NULL, (uint8_t *)(old_output + 1));
                assert(graphics_pc_hdmi_render(modes[m], bench_vram, row,
                                                (uint8_t *)(new_output + 1)));
                // The first and last words are guards; compare them as well.
                assert(memcmp(old_output, new_output, sizeof old_output) == 0);
                ++comparisons;
            }
        }
    }
    assert(!graphics_pc_hdmi_render(VGA_640x480x2, bench_vram, 0,
                                    (uint8_t *)(new_output + 1)));
    // A wrapped CGA row falls back to the common decoder, without a partial
    // fast-path write or an out-of-range read from the borrowed allocation.
    bench_state.vram_offset = 0x3fff;
    memset(new_output, 0xa5, sizeof new_output);
    assert(!graphics_pc_hdmi_render(CGA_320x200x4, bench_vram, 0,
                                    (uint8_t *)(new_output + 1)));
    for (unsigned i = 0; i < 82; ++i) assert(new_output[i] == 0xa5a5a5a5u);

    assert(graphics_pc_hdmi_render(VGA_320x200x256, bench_vram, 200,
                                   (uint8_t *)(new_output + 1)));
    for (unsigned i = 1; i < 81; ++i) assert(new_output[i] == 0);
    assert(new_output[0] == 0xa5a5a5a5u && new_output[81] == 0xa5a5a5a5u);
    printf("PIO fast renderer: %u full-row comparisons to old driver passed\n", comparisons);
}
