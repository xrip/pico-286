// Build with graphics.c and graphics_modes.c, -DVGA=1, and graphics-stubs.
// Add -DGRAPHICS_PC_EMULATOR=1 for word-backed PC VRAM; omit for packed text.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "graphics_modes.h"

uint8_t *text_buffer;
void vga_graphics_init(void) {}
void graphics_pc_snapshot(graphics_pc_state_t *state) { memset(state, 0, sizeof(*state)); }
uint32_t graphics_pc_ega_line(unsigned line) { return line * 40u; }

#if defined(GRAPHICS_PC_EMULATOR)
static uint32_t storage[80 * 30 * 2 + 4];
static uint32_t expected[sizeof(storage) / sizeof(*storage)];

static void expect_cell(unsigned index, uint8_t character, uint8_t attribute) {
    expected[2 + index * 2] = (expected[2 + index * 2] & 0xffffff00u) | character;
    expected[3 + index * 2] = (expected[3 + index * 2] & 0xffffff00u) | attribute;
}

static void test_pc_mode(enum graphics_mode_t mode, unsigned columns) {
    for (unsigned i = 0; i < sizeof(storage) / sizeof(*storage); ++i)
        storage[i] = 0xa5b60000u | i;
    memcpy(expected, storage, sizeof(storage));
    text_buffer = (uint8_t *)(storage + 2);
    graphics_source_set_mode(mode);
    assert(graphics_text_columns() == columns);

    draw_text("AB", 0, 1, 0x1e, 0x2d);
    expect_cell(columns, 'A', 0xde);
    expect_cell(columns + 1, 'B', 0xde);
    assert(memcmp(storage, expected, sizeof(storage)) == 0);

    draw_text("XYZ", columns - 1, 24, 3, 4);
    expect_cell(columns * 25 - 1, 'X', 0x43);
    assert(memcmp(storage, expected, sizeof(storage)) == 0);

    draw_text("no", columns, 0, 1, 2);
    draw_text("no", 0, 25, 1, 2);
    draw_text("no", UINT32_MAX, UINT32_MAX, 1, 2);
    draw_text(NULL, 0, 0, 1, 2);
    draw_text("", 0, 0, 1, 2);
    assert(memcmp(storage, expected, sizeof(storage)) == 0);

    clrScr(0xab);
    for (unsigned cell = 0; cell < columns * 25; ++cell)
        expect_cell(cell, ' ', 0xb0);
    // Also checks both leading guards, all words after the 25-row screen,
    // and every upper plane byte, including those in written cells.
    assert(memcmp(storage, expected, sizeof(storage)) == 0);

    text_buffer = NULL;
    draw_text("no", 0, 0, 1, 2);
    clrScr(1);
    assert(memcmp(storage, expected, sizeof(storage)) == 0);
}
#else
static uint8_t storage[80 * 30 * 2 + 8];
static uint8_t expected[sizeof(storage)];

static void test_packed_mode(enum graphics_mode_t mode, unsigned columns) {
    memset(storage, 0xa5, sizeof(storage));
    memcpy(expected, storage, sizeof(storage));
    text_buffer = storage + 4;
    graphics_source_set_mode(mode);
    assert(graphics_text_columns() == columns);

    draw_text("AB", 0, 1, 0x1e, 0x2d);
    expected[4 + columns * 2] = 'A';
    expected[5 + columns * 2] = 0xde;
    expected[6 + columns * 2] = 'B';
    expected[7 + columns * 2] = 0xde;
    assert(memcmp(storage, expected, sizeof(storage)) == 0);

    draw_text("XYZ", columns - 1, TEXTMODE_ROWS - 1, 3, 4);
    expected[4 + columns * TEXTMODE_ROWS * 2 - 2] = 'X';
    expected[4 + columns * TEXTMODE_ROWS * 2 - 1] = 0x43;
    assert(memcmp(storage, expected, sizeof(storage)) == 0);

    draw_text("no", columns, 0, 1, 2);
    draw_text("no", 0, TEXTMODE_ROWS, 1, 2);
    draw_text(NULL, 0, 0, 1, 2);
    assert(memcmp(storage, expected, sizeof(storage)) == 0);

    clrScr(0xab);
    for (unsigned cell = 0; cell < columns * TEXTMODE_ROWS; ++cell) {
        expected[4 + cell * 2] = ' ';
        expected[5 + cell * 2] = 0xb0;
    }
    assert(memcmp(storage, expected, sizeof(storage)) == 0);
    text_buffer = NULL;
    clrScr(0);
    draw_text("no", 0, 0, 1, 2);
    assert(memcmp(storage, expected, sizeof(storage)) == 0);
}
#endif

int main(void) {
#if defined(GRAPHICS_PC_EMULATOR)
    test_pc_mode(TEXTMODE_40x25_BW, 40);
    test_pc_mode(TEXTMODE_40x25_COLOR, 40);
    test_pc_mode(TEXTMODE_80x25_BW, 80);
    test_pc_mode(TEXTMODE_80x25_COLOR, 80);
    puts("PC text writers: 40/80 columns, 25 rows, clipping, guards and plane bytes passed");
#else
    test_packed_mode(TEXTMODE_DEFAULT, 80);
    test_packed_mode(TEXTMODE_53x30, 40);
    puts("Packed text writers: 40/80 columns, clipping and guards passed");
#endif
    return 0;
}
