#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// HDMI borrows these two allocations. It never copies or frees VRAM or fonts.
// A font has 256 glyphs, one byte per glyph scanline. font_stride is normally
// 8/14/16 for a packed font or 32 for a VGA font map. It is NOT a video stride.
// Sizes and offsets below are bytes; a text cell is {character, attribute}.
typedef struct graphics_text_config {
    uint8_t *vram;
    const uint8_t *font;
    size_t vram_size;
    size_t font_size;
    size_t start_offset;
    size_t cursor_offset;
    uint16_t row_stride;       // 0: columns * 2; otherwise the CRTC row pitch
    uint16_t font_stride;
    uint8_t font_height;
    uint8_t cursor_start;
    uint8_t cursor_end;
    bool msb_first;           // true for ordinary VGA fonts; bundled fonts: false
    bool blink_enabled;       // false: all 16 background colors are available
    bool cursor_enabled;
    bool wrap_vram;           // true: wrap display addresses at vram_size
} graphics_text_config_t;

// Copies only the small descriptor. VRAM and font remain owned by the caller.
// Setters synchronize the descriptor with the IRQ, not the backing storage.
// Keep both allocations alive while bound. After rebinding, retain the old
// allocations for a full frame (two graphics_wait_vblank() calls are safe), or
// stop scanout before freeing them: an in-flight IRQ may hold the old pointers.
// Direct VRAM/font writes are visible on subsequent scanlines; they are not an
// atomic screen update. Typical emulator-owned permanent allocations need no
// additional synchronization or copies.
bool graphics_configure_text(const graphics_text_config_t *config);

// Convenience for existing graphics_set_textbuffer() users. The supplied font
// must contain at least 255 * stride + height bytes. No fallback font is linked
// into the HDMI driver: the application/emulator explicitly supplies its font.
bool graphics_set_text_font(const uint8_t *font, uint8_t height,
                            uint16_t stride, bool msb_first);
void graphics_set_text_blink(bool enabled);
void graphics_set_text_cursor(size_t byte_offset, uint8_t first_scanline,
                              uint8_t last_scanline, bool enabled);

// Internal scanout interface. These are allocation-free and shared by the
// HDMI renderer and its host tests. Geometry comes from graphics_set_mode().
struct graphics_text_line {
    const uint8_t *cells;
    const uint8_t *font;
    const uint8_t *wrap_cells;
    size_t wrap_at;
    unsigned columns;
    unsigned glyph_line;
    unsigned font_stride;
    unsigned cursor_column;
    bool msb_first;
    bool cursor_visible;
    bool blink_hidden;
    bool blink_enabled;
};

void graphics_text_init(void);
void graphics_text_select_mode(unsigned mode);
void graphics_text_frame_tick(void);
bool graphics_text_line(unsigned output_line, unsigned output_height,
                        struct graphics_text_line *line);
unsigned graphics_text_columns_current(void);
void graphics_text_draw(const char *string, unsigned x, unsigned y,
                        uint8_t foreground, uint8_t background);

static inline uint16_t graphics_text_cell(const struct graphics_text_line *line,
                                          unsigned column) {
    const size_t offset = column * 2u;
    if (offset + 1u < line->wrap_at) {
        return (uint16_t)(line->cells[offset] |
                          (uint16_t)line->cells[offset + 1u] << 8);
    }
    const uint8_t character = offset < line->wrap_at
            ? line->cells[offset] : line->wrap_cells[offset - line->wrap_at];
    const uint8_t attribute = offset + 1u < line->wrap_at
            ? line->cells[offset + 1u]
            : line->wrap_cells[offset + 1u - line->wrap_at];
    return (uint16_t)(character | (uint16_t)attribute << 8);
}

static inline uint8_t graphics_text_glyph(const struct graphics_text_line *line,
                                          unsigned column, uint8_t character,
                                          uint8_t attribute) {
    uint8_t glyph = line->font[(size_t)character * line->font_stride +
                               line->glyph_line];
    if (line->msb_first) {
        glyph = (uint8_t)((glyph >> 4) | (glyph << 4));
        glyph = (uint8_t)(((glyph & 0xccu) >> 2) | ((glyph & 0x33u) << 2));
        glyph = (uint8_t)(((glyph & 0xaau) >> 1) | ((glyph & 0x55u) << 1));
    }
    if (line->blink_enabled && line->blink_hidden && (attribute & 0x80u)) {
        glyph = 0;
    }
    if (line->cursor_visible && column == line->cursor_column) {
        glyph = 0xffu;
    }
    return glyph;
}

#ifdef __cplusplus
}
#endif
