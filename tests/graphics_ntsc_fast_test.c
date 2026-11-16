#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "graphics_modes.h"

static uint32_t vram[65536];
static graphics_pc_state_t state;
uint8_t *text_buffer;

void graphics_pc_snapshot(graphics_pc_state_t *out) { *out = state; }
uint32_t graphics_pc_ega_line(unsigned line) { return state.vram_offset + line * 40u; } // EGA 0Dh only

int main(void) {
    const enum graphics_mode_t modes[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 13, 19, 255, 116, 118};
    const unsigned pages[] = {0, 0x123, 0x3fff, 0x7ff0, 0xffff};
    uint32_t even[256], odd[256], guarded[322], expected[320];
    uint16_t luma[256];
    uint8_t indices[640];
    unsigned fast_rows = 0, fallback_rows = 0;
    uint32_t seed = 1;
    for (unsigned i = 0; i < 65536; ++i) {
        seed = seed * 1664525u + 1013904223u;
        vram[i] = seed;
    }
    for (unsigned i = 0; i < 256; ++i) {
        even[i] = (i * 19u + 3u) | ((i * 31u + 7u) << 16);
        odd[i] = (i * 13u + 5u) | ((i * 29u + 11u) << 16);
        luma[i] = i * 123u + 17u;
    }
    graphics_source_set_buffer((uint8_t *)vram, 320, 200);
    graphics_source_set_offset(0, 0);
    for (unsigned m = 0; m < sizeof modes / sizeof *modes; ++m) {
        const enum graphics_mode_t mode = modes[m];
        const bool mono = mode == 2 || mode == 3 || mode == 6;
        const unsigned width = mono ? 640u : 320u;
        graphics_source_set_mode(mode);
        for (unsigned p = 0; p < sizeof pages / sizeof *pages; ++p) {
            state.vram_offset = pages[p];
            state.tga_offset = (0x8000u + pages[p]) & 65535u;
            state.foreground = 11;
            state.blink_mask = p & 1u ? 0x7fu : 0xffu;
            state.cursor_phase = p != 0;
            state.cursor_x = p * 7u;
            state.cursor_y = p * 5u;
            state.cursor_start = p == 4 ? 32 : p == 3 ? 7 : 4;
            state.cursor_end = p == 3 ? 2 : 7;
            for (unsigned row = 0; row < 200; ++row) {
                memset(guarded, 0xa5, sizeof guarded);
                uint16_t *output = (uint16_t *)(guarded + 1);
                const bool fast = graphics_source_render_ntsc_fast(row, output, even, odd, luma);
                assert(guarded[0] == 0xa5a5a5a5u && guarded[321] == 0xa5a5a5a5u);
                if (!fast) {
                    ++fallback_rows;
                    for (unsigned x = 1; x < 321; ++x) assert(guarded[x] == 0xa5a5a5a5u);
                    continue;
                }
                ++fast_rows;
                graphics_source_render_ntsc_line(row, width, 240, indices);
                if (mono) {
                    uint16_t *samples = (uint16_t *)expected;
                    for (unsigned x = 0; x < 640; ++x) samples[x] = luma[indices[x]];
                } else {
                    for (unsigned x = 0; x < 320; ++x)
                        expected[x] = x & 1u ? odd[indices[x]] : even[indices[x]];
                }
                assert(memcmp(output, expected, sizeof expected) == 0);
            }
        }
    }
    assert(fast_rows != 0 && fallback_rows != 0);
    graphics_source_set_mode(CGA_320x200x4);
    assert(!graphics_source_render_ntsc_fast(200, (uint16_t *)expected, even, odd, luma));
    graphics_source_set_offset(1, 0);
    assert(!graphics_source_render_ntsc_fast(0, (uint16_t *)expected, even, odd, luma));
    graphics_source_set_offset(0, -1);
    assert(!graphics_source_render_ntsc_fast(0, (uint16_t *)expected, even, odd, luma));
    graphics_source_set_offset(0, 0);
    graphics_source_set_mode(VGA_640x480x16);
    assert(!graphics_source_render_ntsc_fast(0, (uint16_t *)expected, even, odd, luma));
    graphics_source_set_buffer(NULL, 320, 200);
    assert(!graphics_source_render_ntsc_fast(0, (uint16_t *)expected, even, odd, luma));
    printf("NTSC fast paths: %u rows, %u wrap fallbacks; cursor/blink/page/bounds passed\n",
            fast_rows, fallback_rows);
    return 0;
}
