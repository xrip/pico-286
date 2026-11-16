#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "graphics_modes.h"
#include "../vga_crtc.h"

static uint32_t vram[65536];
static uint16_t pairs[1024];
static uint16_t guarded[322];
static graphics_pc_state_t pc;
static enum graphics_mode_t source_mode = TEXTMODE_80x25_COLOR;
static uint16_t *const output = guarded + 1;

void graphics_pc_snapshot(graphics_pc_state_t *out) { *out = pc; }
void graphics_source_pc_view(graphics_pc_view_t *out) {
    *out = (graphics_pc_view_t){source_mode, vram, 0, 0};
}

static void reset(void) {
    memset(vram, 0, sizeof(vram));
    memset(&pc, 0, sizeof(pc));
    pc.blink_mask = 255;
    source_mode = TEXTMODE_80x25_COLOR;
    vga_crtc_reset(source_mode);
    assert(!vga_crtc_text_custom);
}

static void render(unsigned row) {
    guarded[0] = guarded[321] = 0xa55a;
    vga_crtc_render_text(row, 480, output, pairs);
    assert(guarded[0] == 0xa55a && guarded[321] == 0xa55a);
}

static void cell(unsigned byte_address, uint8_t character, uint8_t attr) {
    vram[0x8000 + (byte_address & 0x3fff)] = character;
    vram[0x8000 + ((byte_address + 1) & 0x3fff)] = attr;
}

int main(void) {
    for (unsigned attr = 0; attr < 256; ++attr) {
        const unsigned fg = 0xc0 | (attr & 15u), bg = 0xc0 | (attr >> 4);
        pairs[attr * 4] = bg | (bg << 8);
        pairs[attr * 4 + 1] = fg | (bg << 8);
        pairs[attr * 4 + 2] = bg | (fg << 8);
        pairs[attr * 4 + 3] = fg | (fg << 8);
    }
    assert(font_8x8[0xdb * 8] == 255 && font_8x16[0xdb * 16] == 255);

    reset();
    uint8_t x, y;
    assert(!vga_crtc_get_cursor(&x, &y));
    vga_crtc_write(9, 1);
    vga_crtc_write(6, 100);
    vga_crtc_write(7, 112);
    assert(vga_crtc_text_custom);
    cell(160, 0xdb, 2);
    source_mode = (enum graphics_mode_t)0x77;
    render(4); // 2 source scanlines per text row, doubled for VGA.
    assert(output[0] == 0xc2c2);
    render(400);
    for (unsigned i = 0; i < 320; ++i) assert(output[i] == 0xc0c0);

    reset();
    vga_crtc_write(1, 90);
    vga_crtc_write(2, 94);
    vga_crtc_write(6, 30);
    vga_crtc_write(7, 30);
    cell(180, 0xdb, 5);
    cell(180 + 79 * 2, 0xdb, 7);
    cell(180 + 80 * 2, 0xdb, 9);
    render(16);
    assert(output[0] == 0xc5c5 && output[319] == 0xc7c7);
    uint16_t before[320];
    memcpy(before, output, sizeof(before));
    vga_crtc_write(8, 3);
    render(16);
    assert(memcmp(before, output, sizeof(before)) == 0);

    reset();
    vga_crtc_write(12, 0x1f);
    vga_crtc_write(13, 0xff);
    cell(0x3ffe, 0xdb, 6);
    cell(0, 0xdb, 3);
    assert(vga_crtc_text_custom);
    render(0);
    assert(output[0] == 0xc6c6 && output[4] == 0xc3c3);
    vga_crtc_write(14, 0x20);
    vga_crtc_write(15, 0);
    assert(vga_crtc_get_cursor(&x, &y) && x == 1 && y == 0);

    vga_crtc_write(1, 0);
    assert(vga_crtc_get_cursor(&x, &y) && x == 255 && y == 255);
    render(0);
    for (unsigned i = 0; i < 320; ++i) assert(output[i] == 0xc0c0);
    vga_crtc_write(255, 255); // Invalid register cannot touch state.

    reset();
    vga_crtc_write(9, 2); // Non-power-of-two character height.
    cell(160, 0xdd, 4);
    render(10);
    const uint8_t glyph = font_8x8[0xdd * 8 + 2];
    const uint8_t *bytes = (const uint8_t *)output;
    for (unsigned bit = 0; bit < 8; ++bit)
        assert(bytes[bit] == ((glyph & (1u << bit)) ? 0xc4 : 0xc0));

    reset();
    vga_crtc_write(4, 0x3e);
    source_mode = (enum graphics_mode_t)0x79;
    assert(vga_crtc_text_custom);
    cell(6, ' ', 15);
    vga_crtc_write(14, 0);
    vga_crtc_write(15, 3);
    render(12);
    assert(output[12] == 0xcfcf);
    vga_crtc_write(10, 0x26); // Cursor mode 1 disables it.
    render(12);
    assert(output[12] == 0xc0c0);

    vga_crtc_reset(VGA_640x480x16);
    assert(!vga_crtc_text_custom && !vga_crtc_get_cursor(&x, &y));
    puts("VGA CRTC: pitch, height, paging, wrap, cursor, pseudo modes and bounds passed");
}
