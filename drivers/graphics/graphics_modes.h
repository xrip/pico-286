#pragma once

// Shared scanline state and decoders. PC renderers read word-backed VRAM
// and write 320 or 640 hardware pixels directly into output line buffers.
// The index renderer also handles scaling and shifted or wrapped rows.

#include <stdint.h>

#include "graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

// The fixed 16 CGA text colors as RGB888. Text does not go through the
// application palette, so an application keeps all 256 of its own colors, and
// each backend can turn these into a compile-time lookup table that lives in
// flash instead of RAM.
#define GRAPHICS_CGA_RGB(i) ( \
    (i) ==  0 ? 0x000000u : (i) ==  1 ? 0x0000aau : \
    (i) ==  2 ? 0x00aa00u : (i) ==  3 ? 0x00aaaau : \
    (i) ==  4 ? 0xaa0000u : (i) ==  5 ? 0xaa00aau : \
    (i) ==  6 ? 0xaa5500u : (i) ==  7 ? 0xaaaaaau : \
    (i) ==  8 ? 0x555555u : (i) ==  9 ? 0x5555ffu : \
    (i) == 10 ? 0x55ff55u : (i) == 11 ? 0x55ffffu : \
    (i) == 12 ? 0xff5555u : (i) == 13 ? 0xff55ffu : \
    (i) == 14 ? 0xffff55u : 0xffffffu)

#define GRAPHICS_CGA_RED(i) (GRAPHICS_CGA_RGB(i) >> 16 & 0xffu)
#define GRAPHICS_CGA_GREEN(i) (GRAPHICS_CGA_RGB(i) >> 8 & 0xffu)
#define GRAPHICS_CGA_BLUE(i) (GRAPHICS_CGA_RGB(i) & 0xffu)

// Repeats a macro for all 256 character attributes, low nibble first, so the
// result is indexed by the attribute byte itself.
#define GRAPHICS_TEXT_ATTRIBUTES(X) \
    GRAPHICS_TEXT_NIBBLE(X, 0)  GRAPHICS_TEXT_NIBBLE(X, 1) \
    GRAPHICS_TEXT_NIBBLE(X, 2)  GRAPHICS_TEXT_NIBBLE(X, 3) \
    GRAPHICS_TEXT_NIBBLE(X, 4)  GRAPHICS_TEXT_NIBBLE(X, 5) \
    GRAPHICS_TEXT_NIBBLE(X, 6)  GRAPHICS_TEXT_NIBBLE(X, 7) \
    GRAPHICS_TEXT_NIBBLE(X, 8)  GRAPHICS_TEXT_NIBBLE(X, 9) \
    GRAPHICS_TEXT_NIBBLE(X, 10) GRAPHICS_TEXT_NIBBLE(X, 11) \
    GRAPHICS_TEXT_NIBBLE(X, 12) GRAPHICS_TEXT_NIBBLE(X, 13) \
    GRAPHICS_TEXT_NIBBLE(X, 14) GRAPHICS_TEXT_NIBBLE(X, 15)

// Repeats a macro for the 16 CGA colors. A text renderer uses it to build its
// own color table at compile time, in whatever form its hardware wants.
#define GRAPHICS_TEXT_COLORS(X) \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7) \
    X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15)

#define GRAPHICS_TEXT_NIBBLE(X, background) \
    X(0, background)  X(1, background)  X(2, background)  X(3, background) \
    X(4, background)  X(5, background)  X(6, background)  X(7, background) \
    X(8, background)  X(9, background)  X(10, background) X(11, background) \
    X(12, background) X(13, background) X(14, background) X(15, background)

// Gives one output row of GRAPHICS_FRAME_WIDTH palette indices.
//
// `framebuffer` is the backend's own active 320 x 240 buffer. It is used only
// by the direct modes, so the present/pending handshake keeps working.
// `staging` is a GRAPHICS_FRAME_WIDTH-byte scratch line owned by the calling
// backend; each backend needs its own, because the backends run from separate
// interrupts.
//
// The returned pointer is good until the next call from the same backend.
// After a source-buffer rebind, keep the old allocation alive until all
// in-flight scanlines have finished reading it; rebinding does not copy pixels.
const uint8_t *graphics_source_line(const uint8_t *framebuffer,
                                    unsigned row,
                                    uint8_t *staging);

// PC VRAM has 65,536 words. A byte-wide CGA/Tandy/VGA address occupies one
// word; EGA planes and Mode X pixels occupy the four bytes of that word.
// The emulator takes this small snapshot once per scanline, so page, cursor
// and blink changes remain visible without copying VRAM.
typedef struct graphics_pc_state {
    uint32_t vram_offset;
    uint32_t tga_offset;
    uint8_t foreground;
    uint8_t blink_mask;
    uint8_t cursor_x;
    uint8_t cursor_y;
    uint8_t cursor_start;
    uint8_t cursor_end;
    bool cursor_phase;
    uint8_t ega_line_words; // CRTC 13h: the EGA line width in words
} graphics_pc_state_t;

void graphics_pc_snapshot(graphics_pc_state_t *state);

// EGA graphics: VRAM word address of display line `line`, before the 64K wrap.
static inline __attribute__((always_inline)) unsigned pc_ega_line(const graphics_pc_state_t *state, unsigned line) {
    return state->vram_offset + line * 2u * state->ega_line_words;
}

// Tandy graphics: offset of a display line inside its 8K bank. The CRTC start address wraps inside the bank.
static inline __attribute__((always_inline)) unsigned pc_bank_offset(const graphics_pc_state_t *state, unsigned line) {
    return ((state->vram_offset << 1) + line) & 0x1fffu;
}
typedef struct graphics_pc_view {
    enum graphics_mode_t mode;
    const uint32_t *vram;
    int shift_x;
    int shift_y;
} graphics_pc_view_t;
void graphics_source_pc_view(graphics_pc_view_t *view);
enum graphics_mode_t graphics_source_mode(void);
#if defined(GRAPHICS_PC_EMULATOR)
// PC modes always use word-backed VRAM. Unknown values keep the old VGA
// decoder's linear Mode 13h fallback, never the demo's byte framebuffer.
static inline bool graphics_source_is_pc_mode(void) { return true; }
#else
bool graphics_source_is_pc_mode(void);
#endif
// 320 for low-resolution graphics (including doubled 160-pixel modes),
// 640 for text and high-resolution graphics. Lets hardware double pixels.
unsigned graphics_source_native_width(void);

// Output geometry is 320 or 640 samples and 240 or 480 rows. The logical
// canvas is 640x480: 200-row modes double rows, 348/350/480-row modes retain
// their full height, and unused rows are black. No full framebuffer is made.
void graphics_source_render_line(unsigned row, unsigned width, unsigned height,
                                 uint8_t *indices);
// Composite CGA modes send the original 2-bpp signal on NTSC; RGB outputs
// use their 160-pixel artifact-color interpretation instead.
void graphics_source_render_ntsc_line(unsigned row, unsigned width, unsigned height,
                                      uint8_t *indices);

// Fused decoders write hardware pixels directly. The 8-bit form selects the
// low/high byte of a packed palette entry on even/odd samples (VGA dither or
// HDMI phase). The 16-bit form maps one index to one entry (RGB565/BGR555 or
// a VGA doubled-pixel pair at width 320).
void graphics_source_render_line8(unsigned row, unsigned width, unsigned height,
                                  uint8_t *output, const uint16_t *palette);
// Legacy VGA high-resolution graphics use the low byte of the selected
// frame/line dither palette for every sample, as the old VGA driver did.
void graphics_source_render_vga8(unsigned row, unsigned height, uint8_t *output,
                                 const uint16_t *palette);
void graphics_source_render_line16(unsigned row, unsigned width, unsigned height,
                                   uint16_t *output, const uint16_t *palette);
// One complete 32-bit palette entry per logical pixel. HSTX supplies two
// equal BGR555 halfwords per entry to double low-resolution pixels in DMA.
void graphics_source_render_line32(unsigned row, unsigned width, unsigned height,
                                   uint32_t *output, const uint32_t *palette);
// quad_pixels has 4096 four-byte entries: attribute * 16 + four glyph bits,
// lowest bit/pixel first. Build it when the VGA text palette is set. Both
// 40- and 80-column output share this table; no second doubled table is used.
void graphics_source_render_vga_text(unsigned row, unsigned height,
                                     uint16_t *output,
                                     const uint16_t *attribute_pairs,
                                     const uint32_t *quad_pixels);
// NTSC full palette tables have two samples per entry. The even table is
// phases 0/1, odd is phases 2/3. The 32-bit form emits a whole sample pair.
void graphics_source_render_ntsc16(unsigned row, unsigned width, unsigned height,
                                  uint16_t *output, const uint32_t *even,
                                  const uint32_t *odd);
void graphics_source_render_ntsc32(unsigned row, unsigned width, unsigned height,
                                  uint32_t *output, const uint32_t *even,
                                  const uint32_t *odd);
bool graphics_source_render_ntsc_fast(unsigned row, uint16_t *output,
                                      const uint32_t *even, const uint32_t *odd,
                                      const uint16_t *luma);

// True while a text mode is selected. A backend then leaves the composer above
// alone and builds its line from graphics_text_row() instead.
bool graphics_source_is_text(void);

// Columns in the selected text mode: 80, or 40 for TEXTMODE_53x30. A 40-column
// line sends every pixel twice, so both fill the same 640 samples.
unsigned graphics_text_columns(void);

// Start of the character/attribute pairs for the text row holding output line
// `line`, or NULL when there is no text buffer. `font_height` is 16 for a
// 480-line output and 8 for a 240-line one; `*glyph_line` gets the row inside
// the character cell.
const uint8_t *graphics_text_row(unsigned line,
                                 unsigned font_height,
                                 unsigned *glyph_line);

// State, set from the backend graphics_set_* functions.
#if defined(HDMI_HSTX)
// Call once on the scanout core before enabling either output IRQ. Setters
// remain usable before initialization while no scanout is running.
void graphics_source_init(void);
#endif
void graphics_source_set_mode(enum graphics_mode_t mode);
void graphics_source_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height);
void graphics_source_set_offset(int x, int y);

#ifdef __cplusplus
}
#endif
