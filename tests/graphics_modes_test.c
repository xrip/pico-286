#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "graphics_modes.h"

static uint32_t vram[65536];
static graphics_pc_state_t state;
uint8_t *text_buffer;
static uint8_t guarded[644];
#define pixels (guarded + 2)

void graphics_pc_snapshot(graphics_pc_state_t *out) { *out = state; }
// EGA lines at the standard width (CRTC 13h after the BIOS mode set), from the start address
uint32_t graphics_pc_ega_line(unsigned line) {
    return state.vram_offset + line * (graphics_source_mode() == EGA_320x200x16x4 ? 40u : 80u);
}

static void reset(enum graphics_mode_t mode) {
    memset(vram, 0, sizeof(vram));
    memset(&state, 0, sizeof(state));
    state.tga_offset = 0x8000;
    state.foreground = 15;
    state.blink_mask = 255;
    graphics_source_set_buffer((uint8_t *)vram, 320, 200);
    graphics_source_set_offset(0, 0);
    graphics_source_set_mode(mode);
}

static void line(unsigned y, unsigned width, unsigned height) {
    memset(guarded, 0xa5, sizeof(guarded));
    graphics_source_render_line(y, width, height, pixels);
    assert(guarded[0] == 0xa5 && guarded[1] == 0xa5);
    for (unsigned x = width + 2; x < sizeof(guarded); ++x) assert(guarded[x] == 0xa5);
}

static void expect_zero(unsigned width) {
    for (unsigned x = 0; x < width; ++x) assert(pixels[x] == 0);
}

static void test_cga(void) {
    const enum graphics_mode_t modes[] = {CGA_320x200x4, CGA_320x200x4_BW};
    for (unsigned m = 0; m < 2; ++m) {
        reset(modes[m]);
        assert(graphics_source_native_width() == 320);
        vram[0x8000] = 0xdeadbe1b; // low byte only: 0,1,2,3
        line(0, 320, 240);
        for (unsigned x = 0; x < 4; ++x) assert(pixels[x] == x);
        line(1, 640, 480);
        for (unsigned x = 0; x < 8; ++x) assert(pixels[x] == x / 2);
        vram[0xa000] = 0xe4; // alternate CGA bank: 3,2,1,0
        line(2, 640, 480);
        for (unsigned x = 0; x < 8; ++x) assert(pixels[x] == 3 - x / 2);
        vram[0x8050] = 0x55;
        line(4, 320, 480);
        for (unsigned x = 0; x < 4; ++x) assert(pixels[x] == 1);
        state.vram_offset = 1;
        vram[0x8002] = 0xaa;
        line(0, 320, 240);
        for (unsigned x = 0; x < 4; ++x) assert(pixels[x] == 2);
        line(400, 640, 480); expect_zero(640);
    }
    reset(CGA_640x200x2);
    state.foreground = 6;
    vram[0x8000] = 0x81;
    line(0, 640, 480);
    assert(pixels[0] == 6 && pixels[7] == 6);
    for (unsigned x = 1; x < 7; ++x) assert(pixels[x] == 0);
    line(0, 320, 240);
    assert(pixels[0] == 6 && pixels[1] == 0 && pixels[3] == 0);
}

static void test_hercules_and_mono(void) {
    const enum graphics_mode_t modes[] = {HERC_640x480x2, HERC_640x480x2_90, VGA_640x480x2};
    for (unsigned m = 0; m < 3; ++m) {
        reset(modes[m]);
        const unsigned first = m == 1 ? 5 : 0;
        vram[first] = 0x80;
        line(0, 640, 480);
        assert(pixels[0] == 15 && pixels[1] == 0);
        const unsigned bank = m == 2 ? 80 : 8192 + first;
        vram[bank] = 0x40;
        line(1, 640, 480);
        assert(pixels[0] == 0 && pixels[1] == 15);
        const unsigned last_y = m == 1 ? 347 : 479;
        const unsigned last_word = m == 2 ? last_y * 80 :
                (last_y & 3) * 8192 + (last_y / 4) * 90 + first;
        vram[last_word + 79] = 1;
        line(last_y, 640, 480);
        assert(pixels[639] == 15);
        line(last_y + 1, 640, 480); expect_zero(640);
    }
}

static void test_tandy_and_composite(void) {
    const enum graphics_mode_t modes[] = {TGA_160x200x16, COMPOSITE_160x200x16, COMPOSITE_160x200x16_force};
    for (unsigned m = 0; m < 3; ++m) {
        reset(modes[m]);
        state.foreground = 7;
        vram[0x8000] = 0x09;
        line(0, 640, 480);
        for (unsigned x = 0; x < 4; ++x) assert(pixels[x] == (m == 0 ? 7 : 0));
        for (unsigned x = 4; x < 8; ++x) assert(pixels[x] == 9);
        vram[0xa000] = 0x34;
        line(2, 320, 480);
        assert(pixels[0] == 3 && pixels[1] == 3 && pixels[2] == 4 && pixels[3] == 4);
        if (m != 0) {
            vram[0x8000] = 0x1b;
            graphics_source_render_ntsc_line(0, 320, 240, pixels);
            for (unsigned x = 0; x < 4; ++x) assert(pixels[x] == x);
            // RGB still has the two artifact-color nibbles.
            line(0, 320, 240);
            assert(pixels[0] == 1 && pixels[1] == 1 && pixels[2] == 11);
        }
    }
    reset(TGA_320x200x16);
    state.tga_offset = 0;
    for (unsigned bank = 0; bank < 4; ++bank) {
        vram[bank * 8192] = ((bank + 1) << 4) | (bank + 5);
        line(bank, 320, 240);
        assert(pixels[0] == bank + 1 && pixels[1] == bank + 5);
    }
    vram[160] = 0xde;
    line(4, 320, 240); assert(pixels[0] == 13 && pixels[1] == 14);
    reset(TGA_640x200x16);
    vram[320] = 0x29;
    line(2, 640, 480); assert(pixels[0] == 2 && pixels[1] == 9);
    line(1, 320, 240); assert(pixels[0] == 2);
}

// The CRTC start address moves the picture: EGA lines step by the line width,
// Tandy lines wrap inside their 8K bank.
static void test_start_address(void) {
    reset(EGA_320x200x16x4);
    state.vram_offset = 5;
    vram[5] = vram[5 + 40] = 0xffffffffu;
    line(0, 320, 240); assert(pixels[0] == 15 && pixels[7] == 15 && pixels[8] == 0);
    line(1, 320, 240); assert(pixels[0] == 15 && pixels[8] == 0);
    reset(TGA_320x200x16);
    state.tga_offset = 0;
    state.vram_offset = 80; // 160 bytes: one group of 4 lines
    vram[160] = 0xab;
    line(0, 320, 240); assert(pixels[0] == 10 && pixels[1] == 11);
    state.vram_offset = 0x1ffe / 2;
    vram[0x1ffe] = 0x12; vram[0x1fff] = 0x34; vram[0] = 0x56;
    line(0, 320, 240);
    for (unsigned x = 0; x < 6; ++x) assert(pixels[x] == x + 1);
}

// The PIO HDMI glyph expander draws PC text from graphics_source_pc_text_line().
// Decode its cells like the expander (attribute word, blink, cursor, 64K wrap)
// and compare with the pixel renderer.
static void test_pc_text_line_source(void) {
    const enum graphics_mode_t modes[] = {TEXTMODE_40x25_COLOR, TEXTMODE_80x25_COLOR};
    const uint32_t offsets[] = {0, 1, 0x3ff0};
    uint8_t expected[640];
    for (unsigned m = 0; m < 2; ++m)
    for (unsigned o = 0; o < 3; ++o)
    for (unsigned blink = 0; blink < 4; ++blink) {
        reset(modes[m]);
        uint32_t seed = o * 7u + blink + 1u;
        for (unsigned i = 0; i < 65536; ++i) vram[i] = (seed = seed * 1664525u + 1013904223u) >> 8;
        state.vram_offset = offsets[o];
        state.blink_mask = blink & 1u ? 0x7fu : 0xffu;
        state.cursor_phase = (blink & 2u) != 0;
        state.cursor_x = 7; state.cursor_y = 3; state.cursor_start = 13; state.cursor_end = 14;
        for (unsigned row = 0; row < 480; ++row) {
            struct graphics_text_line text;
            const bool drawn = graphics_source_pc_text_line(row, &text);
            line(row, 640, 480);
            if (!drawn) { assert(row >= 400); expect_zero(640); continue; }
            const unsigned first = text.wrap_at / 8u < text.columns ? (unsigned)(text.wrap_at / 8u) : text.columns;
            const unsigned width = 640u / text.columns;
            for (unsigned c = 0; c < text.columns; ++c) {
                const uint8_t *cell = c < first ? text.cells + c * 8u : text.wrap_cells + (c - first) * 8u;
                const unsigned attr = cell[4];
                unsigned glyph = text.font[cell[0] * text.font_stride + text.glyph_line];
                const unsigned background = (attr >> 4) & (text.blink_enabled ? 7u : 15u);
                const bool cursor = text.cursor_visible && c == text.cursor_column;
                // The cursor shows the real foreground, also on a hidden blinking cell.
                const unsigned foreground = !cursor && text.blink_enabled && text.blink_hidden &&
                        (attr & 128u) ? background : attr & 15u;
                if (cursor) glyph = 0xffu;
                for (unsigned x = 0; x < width; ++x)
                    expected[c * width + x] = (glyph >> (x * 8u / width)) & 1u ? foreground : background;
            }
            assert(memcmp(pixels, expected, 640) == 0);
        }
    }
}

static void test_planar_and_indexed(void) {
    const enum graphics_mode_t modes[] = {EGA_320x200x16x4, EGA_640x200x16x4,
                                         EGA_640x350x16x4, VGA_640x480x16};
    for (unsigned m = 0; m < 4; ++m) {
        reset(modes[m]);
        vram[0] = 0xfff0ccaa;
        line(0, 640, 480);
        for (unsigned x = 0; x < (m == 0 ? 16u : 8u); ++x)
            assert(pixels[x] == 15 - (m == 0 ? x / 2 : x));
        line(0, 320, 240);
        for (unsigned x = 0; x < (m == 0 ? 8u : 4u); ++x)
            assert(pixels[x] == 15 - (m == 0 ? x : x * 2));
        const unsigned native_height = m < 2 ? 200 : m == 2 ? 350 : 480;
        const unsigned words = m == 0 ? 40 : 80;
        vram[(native_height - 1) * words + words - 1] = 0x01010101;
        line(m < 2 ? 399 : native_height - 1, 640, 480);
        assert(pixels[639] == 15);
        line(m < 2 ? 400 : native_height, 640, 480); expect_zero(640);
    }
    reset(VGA_320x200x256x4);
    vram[0] = 0xff804020;
    line(0, 320, 240);
    assert(pixels[0] == 32 && pixels[1] == 64 && pixels[2] == 128 && pixels[3] == 255);
    line(0, 640, 480);
    assert(pixels[0] == 32 && pixels[1] == 32 && pixels[6] == 255 && pixels[7] == 255);
    reset(VGA_320x200x256);
    vram[0] = 0x123456fe;
    vram[1] = 255;
    vram[199 * 320 + 319] = 252;
    line(0, 320, 240); assert(pixels[0] == 254 && pixels[1] == 255);
    line(399, 640, 480); assert(pixels[639] == 252);
    line(400, 640, 480); expect_zero(640);
}

static void test_text(void) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        reset((enum graphics_mode_t)mode);
        assert(graphics_source_is_text());
        assert(graphics_source_native_width() == 640);
        const unsigned columns = mode < 2 ? 40 : 80;
        vram[0x8000] = 'A';
        vram[0x8001] = 0x13;
        assert(font_8x16['A' * 16 + 2] == 8);
        line(2, 640, 480);
        for (unsigned x = 0; x < (mode < 2 ? 16u : 8u); ++x)
            assert(pixels[x] == ((mode < 2 ? x / 2 : x) == 3 ? 3 : 1));
        vram[0x8000] = ' '; // space gives only background
        vram[0x8001] = 0xe3;
        line(0, 640, 480);
        for (unsigned x = 0; x < (mode < 2 ? 16u : 8u); ++x) assert(pixels[x] == 14);
        state.cursor_phase = true;
        state.cursor_start = 12; // emulator's native VGA default
        state.cursor_end = 13;
        line(12, 640, 480); assert(pixels[0] == 3);
        line(13, 640, 480); assert(pixels[0] == 3);
        line(14, 640, 480); assert(pixels[0] == 14);
        line(6, 640, 240); assert(pixels[0] == 3);
        line(7, 640, 240); assert(pixels[0] == 14);
        state.blink_mask = 0x7f;
        state.cursor_phase = true;
        state.cursor_start = 6;
        state.cursor_end = 7;
        line(0, 640, 480);
        assert(pixels[0] == 6); // blink attribute has 3-bit background
        line(12, 640, 480);
        assert(pixels[0] == 3); // cursor overrides hidden blinking character
        state.cursor_start |= 0x20;
        line(12, 640, 480);
        assert(pixels[0] == 6); // cursor disable bit
        state.cursor_start = 6;
        state.cursor_y = 24;
        state.cursor_x = columns - 1;
        vram[0x8000 + (25 * columns - 1) * 2 + 1] = 0x0b;
        line(399, 640, 480); assert(pixels[639] == 11);
        line(400, 640, 480); expect_zero(640);
        line(199, 320, 240); assert(pixels[319] == 11);
        state.vram_offset = 0xffff;
        state.cursor_phase = false;
        state.blink_mask = 255;
        vram[0x7ffe] = ' ';
        vram[0x7fff] = 0x50;
        line(0, 640, 480); assert(pixels[0] == 5);
    }
}

static void test_text_pairs(void) {
    uint16_t pairs[1024], output[320];
    uint32_t quad_pixels[4096];
    uint8_t reference[640];
    for (unsigned attr = 0; attr < 256; ++attr) {
        const uint8_t fg = 0xc0u | (attr & 15u);
        const uint8_t bg = 0xc0u | (attr >> 4);
        for (unsigned bits = 0; bits < 4; ++bits) {
            pairs[attr * 4 + bits] = (bits & 1 ? fg : bg) |
                    (uint16_t)(bits & 2 ? fg : bg) << 8;

        }
        for (unsigned pattern = 0; pattern < 16; ++pattern) {
            uint32_t quad = 0;
            for (unsigned bit = 0; bit < 4; ++bit)
                quad |= (uint32_t)(pattern & (1u << bit) ? fg : bg) << (bit * 8);
            quad_pixels[attr * 16 + pattern] = quad;
        }
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        reset((enum graphics_mode_t)mode);
        for (unsigned i = 0; i < 65536; ++i) vram[i] = i * 19u + 7u;
        for (unsigned setting = 0; setting < 4; ++setting) {
            state.blink_mask = setting & 1 ? 0x7f : 0xff;
            state.cursor_phase = (setting & 2) != 0;
            state.cursor_x = 12;
            state.cursor_y = 3;
            state.cursor_start = 5;
            state.cursor_end = 7;
            state.vram_offset = setting * 160;
            for (int shift = -1; shift <= 1; ++shift) {
                graphics_source_set_offset(shift, 0);
                for (unsigned height = 240; height <= 480; height += 240) {
                    for (unsigned y = 0; y < height; ++y) {
                        graphics_source_render_line(y, 640, height, reference);
                        graphics_source_render_vga_text(y, height, output, pairs, quad_pixels);
                        for (unsigned x = 0; x < 640; ++x)
                            assert(((uint8_t *)output)[x] == (0xc0u | reference[x]));
                    }
                }
            }
        }
    }
}

static void test_rebind_offsets_and_modes(void) {
    reset(VGA_320x200x256);
    vram[0] = 99;
    graphics_source_set_mode(CGA_320x200x4);
    graphics_source_set_mode(VGA_320x200x256);
    assert(vram[0] == 99); // selecting a mode must never clear emulator VRAM
#if defined(GRAPHICS_PC_EMULATOR)
    const unsigned pseudo_modes[] = {0x77, 0x79, 0x87};
    for (unsigned i = 0; i < sizeof pseudo_modes / sizeof *pseudo_modes; ++i) {
        graphics_source_set_mode((enum graphics_mode_t)pseudo_modes[i]);
        assert(graphics_source_mode() == VGA_320x200x256);
        line(0, 320, 240);
        assert(pixels[0] == 99);
    }
#endif
    graphics_source_set_offset(1, 1);
    line(0, 320, 240); expect_zero(320);
    line(1, 320, 240); assert(pixels[0] == 0 && pixels[1] == 99);
    graphics_source_set_offset(-1, 0);
    vram[1] = 55;
    line(0, 320, 240); assert(pixels[0] == 55 && pixels[319] == 0);
    graphics_source_set_buffer(NULL, 320, 200);
    line(0, 640, 480); expect_zero(640);
    graphics_source_set_mode(VGA_320x240x256);
#if defined(GRAPHICS_PC_EMULATOR)
    assert(graphics_source_is_pc_mode()); // no demo byte-buffer fallback
    line(0, 640, 480); expect_zero(640);
    graphics_source_set_mode(TEXTMODE_DEFAULT);
    assert(graphics_source_is_pc_mode());
#else
    assert(!graphics_source_is_pc_mode()); // upstream direct byte framebuffer
    graphics_source_set_mode(TEXTMODE_DEFAULT);
    assert(!graphics_source_is_pc_mode() && graphics_source_is_text());
#endif
}

static void test_fused_mapping(void) {
    const enum graphics_mode_t modes[] = {
        TEXTMODE_40x25_BW, TEXTMODE_40x25_COLOR, TEXTMODE_80x25_BW, TEXTMODE_80x25_COLOR,
        CGA_320x200x4, CGA_320x200x4_BW, CGA_640x200x2,
        HERC_640x480x2, HERC_640x480x2_90,
        TGA_160x200x16, TGA_320x200x16, TGA_640x200x16,
        EGA_320x200x16x4, EGA_640x200x16x4, EGA_640x350x16x4,
        VGA_640x480x2, VGA_640x480x16, VGA_320x200x256, VGA_320x200x256x4,
        COMPOSITE_160x200x16, COMPOSITE_160x200x16_force
    };
    const unsigned rows[] = {0, 1, 2, 15, 199, 200, 347, 349, 399, 479};
    uint16_t palette[256];
    uint32_t palette32[256], guarded32[644];
    uint32_t even[256], odd[256];
    uint8_t indices[640], mapped8[640];
    uint16_t mapped16[640];
    uint32_t mapped32[640];
    for (unsigned i = 0; i < 256; ++i) {
        palette[i] = (uint16_t)((i << 8) | (255 - i));
        palette32[i] = palette[i] | (uint32_t)palette[i] << 16;
        even[i] = (i * 19 + 3) | ((i * 31 + 7) << 16);
        odd[i] = (i * 13 + 5) | ((i * 29 + 11) << 16);
    }
    for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); ++m) {
        reset(modes[m]);
        uint32_t seed = 1;
        for (unsigned i = 0; i < 65536; ++i) {
            seed = seed * 1664525u + 1013904223u;
            vram[i] = seed;
        }
        for (unsigned geometry = 0; geometry < 4; ++geometry) {
            const unsigned width = geometry & 1 ? 640 : 320;
            const unsigned height = geometry & 2 ? 480 : 240;
            for (int shift = -1; shift <= 1; ++shift) {
                graphics_source_set_offset(shift, 0);
                for (unsigned r = 0; r < sizeof(rows) / sizeof(*rows); ++r) {
                    graphics_source_render_line(rows[r], width, height, indices);
                    graphics_source_render_line8(rows[r], width, height, mapped8, palette);
                    graphics_source_render_line16(rows[r], width, height, mapped16, palette);
                    memset(guarded32, 0xa5, sizeof(guarded32));
                    graphics_source_render_line32(rows[r], width, height, guarded32 + 2, palette32);
                    assert(guarded32[0] == 0xa5a5a5a5u && guarded32[1] == 0xa5a5a5a5u);
                    for (unsigned x = width + 2; x < 644; ++x)
                        assert(guarded32[x] == 0xa5a5a5a5u);
                    for (unsigned x = 0; x < width; ++x) {
                        assert(mapped8[x] == (uint8_t)(palette[indices[x]] >> ((x & 1) * 8)));
                        assert(mapped16[x] == palette[indices[x]]);
                        assert(guarded32[x + 2] == palette32[indices[x]]);
                    }
                    if (width == 640) {
                        graphics_source_render_vga8(rows[r], height, mapped8, palette);
                        for (unsigned x = 0; x < width; ++x)
                            assert(mapped8[x] == (uint8_t)palette[indices[x]]);
                    }
                    graphics_source_render_ntsc_line(rows[r], width, height, indices);
                    graphics_source_render_ntsc16(rows[r], width, height, mapped16, even, odd);
                    graphics_source_render_ntsc32(rows[r], width, height, mapped32, even, odd);
                    for (unsigned x = 0; x < width; ++x) {
                        const uint32_t entry = x & 2 ? odd[indices[x]] : even[indices[x]];
                        assert(mapped16[x] == (uint16_t)(entry >> ((x & 1) * 16)));
                        assert(mapped32[x] == (x & 1 ? odd[indices[x]] : even[indices[x]]));
                    }
                }
            }
        }
    }
}

static void test_line32_wrapping(void) {
    const enum graphics_mode_t modes[] = {CGA_320x200x4, CGA_640x200x2,
        TGA_160x200x16, TGA_320x200x16, TEXTMODE_40x25_COLOR, TEXTMODE_80x25_COLOR};
    uint32_t palette[256], output[640];
    uint8_t indices[640];
    for (unsigned i = 0; i < 256; ++i)
        palette[i] = (0x3100u | i) * 0x00010001u;
    for (unsigned mode = 0; mode < sizeof(modes) / sizeof(*modes); ++mode) {
        reset(modes[mode]);
        for (unsigned i = 0; i < 65536; ++i) vram[i] = i * 37u + 19u;
        state.vram_offset = 0x3fff;
        state.tga_offset = 0xfffe;
        graphics_source_render_line(0, 640, 480, indices);
        graphics_source_render_line32(0, 640, 480, output, palette);
        for (unsigned x = 0; x < 640; ++x) assert(output[x] == palette[indices[x]]);
        graphics_source_set_offset(1, -1);
        graphics_source_render_line(1, 320, 240, indices);
        graphics_source_render_line32(1, 320, 240, output, palette);
        for (unsigned x = 0; x < 320; ++x) assert(output[x] == palette[indices[x]]);
    }
    reset(VGA_320x200x256);
    vram[0] = 12;
    palette[12] = 0xd00da55au; // All 32 palette bits, not a rebuilt halfword.
    graphics_source_render_line32(0, 320, 240, output, palette);
    assert(output[0] == 0xd00da55au);
#if defined(GRAPHICS_PC_EMULATOR)
    graphics_source_set_mode((enum graphics_mode_t)0x77);
    assert(graphics_source_mode() == VGA_320x200x256);
    graphics_source_render_line32(0, 320, 240, output, palette);
    assert(output[0] == 0xd00da55au);
#endif
}

int main(void) {
    test_cga();
    test_hercules_and_mono();
    test_tandy_and_composite();
    test_planar_and_indexed();
    test_start_address();
    test_pc_text_line_source();
    test_text();
    test_rebind_offsets_and_modes();
    test_fused_mapping();
    test_line32_wrapping();
    test_text_pairs();
    puts("PC graphics modes: all 21 modes, packing, banks, rows, text, cursor, blink, bounds passed");
    return 0;
}
