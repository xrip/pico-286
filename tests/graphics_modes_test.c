#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "graphics_modes.h"

static uint32_t vram[65536];
static graphics_pc_state_t state;
static uint8_t guarded[644];
#define pixels (guarded + 2)

void graphics_pc_snapshot(graphics_pc_state_t *out) { *out = state; }
// EGA lines at the standard width (CRTC 13h after the BIOS mode set), from the start address
uint32_t graphics_pc_ega_line(unsigned line) {
    return state.vram_offset + line * (graphics_source_mode() == EGA_320x200x16x4 ? 40u : 80u);
}
// Mode X: 320x200 after the BIOS mode set; a test can make the CRTC give another size or a split screen
static unsigned x_words, x_rows, x_split_row;
uint32_t graphics_pc_vga_x_line(unsigned row, unsigned *words) {
    *words = row < x_rows ? x_words : 0;
    return row < x_split_row ? state.vram_offset + row * x_words : (row - x_split_row) * x_words;
}

static void reset(enum graphics_mode_t mode) {
    memset(vram, 0, sizeof(vram));
    memset(&state, 0, sizeof(state));
    state.tga_offset = 0x8000;
    state.foreground = 15;
    state.blink_mask = 255;
    x_words = 80, x_rows = 200, x_split_row = 1024;
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
            // RGB has the two artifact-color nibbles.
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

// Flashback: 256x224 in the middle; Lost Vikings: 320x240 with a split screen
static void test_mode_x(void) {
    reset(VGA_320x200x256x4);
    x_words = 64, x_rows = 224;
    vram[0] = 0x04030201;
    vram[223 * 64 + 63] = 0x09000000;
    line(0, 320, 240);
    for (unsigned x = 0; x < 32; ++x) assert(pixels[x] == 0 && pixels[319 - x] == 0);
    assert(pixels[32] == 1 && pixels[35] == 4);
    uint16_t palette[256], mapped[320];
    for (unsigned i = 0; i < 256; ++i) palette[i] = (uint16_t)(i * 3 + 1);
    graphics_source_render_line16(0, 320, 240, mapped, palette); // the fast path
    for (unsigned x = 0; x < 320; ++x) assert(mapped[x] == palette[pixels[x]]);
    line(0, 640, 480);
    assert(pixels[63] == 0 && pixels[64] == 1 && pixels[65] == 1);
    line(447, 640, 480); assert(pixels[575] == 9 && pixels[576] == 0);
    line(448, 640, 480); expect_zero(640);
    x_words = 80, x_rows = 240, x_split_row = 176;
    state.vram_offset = 0x1000;
    vram[0x1000] = 7;
    line(0, 320, 240); assert(pixels[0] == 7);
    line(175, 320, 240); assert(pixels[0] == 0);
    line(176, 320, 240); assert(pixels[0] == 1);
    line(479, 640, 480); assert(pixels[0] == 0);
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

static void test_rebind_offsets_and_modes(void) {
    reset(VGA_320x200x256);
    vram[0] = 99;
    graphics_source_set_mode(CGA_320x200x4);
    graphics_source_set_mode(VGA_320x200x256);
    assert(vram[0] == 99); // selecting a mode must never clear emulator VRAM
    const unsigned pseudo_modes[] = {0x77, 0x79, 0x87};
    for (unsigned i = 0; i < sizeof pseudo_modes / sizeof *pseudo_modes; ++i) {
        graphics_source_set_mode((enum graphics_mode_t)pseudo_modes[i]);
        assert(graphics_source_mode() == VGA_320x200x256);
        line(0, 320, 240);
        assert(pixels[0] == 99);
    }
    graphics_source_set_offset(1, 1);
    line(0, 320, 240); expect_zero(320);
    line(1, 320, 240); assert(pixels[0] == 0 && pixels[1] == 99);
    graphics_source_set_offset(-1, 0);
    vram[1] = 55;
    line(0, 320, 240); assert(pixels[0] == 55 && pixels[319] == 0);
    graphics_source_set_buffer(NULL, 320, 200);
    line(0, 640, 480); expect_zero(640);
    graphics_source_set_mode(VGA_320x240x256);
    assert(graphics_source_is_pc_mode()); // no demo byte-buffer fallback
    line(0, 640, 480); expect_zero(640);
    graphics_source_set_mode(TEXTMODE_DEFAULT);
    assert(graphics_source_is_pc_mode());
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
    uint8_t indices[640];
    uint16_t mapped16[640];
    for (unsigned i = 0; i < 256; ++i) {
        palette[i] = (uint16_t)((i << 8) | (255 - i));
        palette32[i] = palette[i] | (uint32_t)palette[i] << 16;
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
                    graphics_source_render_line16(rows[r], width, height, mapped16, palette);
                    memset(guarded32, 0xa5, sizeof(guarded32));
                    graphics_source_render_line32(rows[r], width, height, guarded32 + 2, palette32);
                    assert(guarded32[0] == 0xa5a5a5a5u && guarded32[1] == 0xa5a5a5a5u);
                    for (unsigned x = width + 2; x < 644; ++x)
                        assert(guarded32[x] == 0xa5a5a5a5u);
                    for (unsigned x = 0; x < width; ++x) {
                        assert(mapped16[x] == palette[indices[x]]);
                        assert(guarded32[x + 2] == palette32[indices[x]]);
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
    graphics_source_set_mode((enum graphics_mode_t)0x77);
    assert(graphics_source_mode() == VGA_320x200x256);
    graphics_source_render_line32(0, 320, 240, output, palette);
    assert(output[0] == 0xd00da55au);
}

int main(void) {
    test_cga();
    test_hercules_and_mono();
    test_tandy_and_composite();
    test_planar_and_indexed();
    test_start_address();
    test_mode_x();
    test_text();
    test_rebind_offsets_and_modes();
    test_fused_mapping();
    test_line32_wrapping();
    puts("PC graphics modes: all 21 modes, packing, banks, rows, mode X, text, cursor, blink, bounds passed");
    return 0;
}
