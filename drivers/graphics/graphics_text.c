#include "graphics_modes.h"
#include "graphics_text.h"

#include <hardware/sync.h>
#include <pico.h>

// There is one text pointer, shared with the emulator. No character buffer or
// font allocation belongs to this module.
uint8_t *text_buffer;
static graphics_text_config_t text_config;
static spin_lock_t *text_lock;
static uint8_t text_columns = 80;
static uint8_t text_rows = 30;
static uint8_t text_frame;

// Setters may be used before graphics_init(), when no scanout is running yet.
static inline uint32_t text_enter(void) {
    return text_lock != NULL ? spin_lock_blocking(text_lock) : 0;
}

static inline void text_leave(uint32_t state) {
    if (text_lock != NULL) {
        spin_unlock(text_lock, state);
    }
}

void graphics_text_init(void) {
    if (text_lock == NULL) {
        text_lock = spin_lock_init(spin_lock_claim_unused(true));
    }
}

void graphics_set_textbuffer(uint8_t *buffer) {
    const uint32_t irq_state = text_enter();
    text_buffer = buffer;
    text_config.vram = buffer;
    // Legacy setter: bind exactly one page of the currently selected geometry.
    // Select the mode first, or use graphics_configure_text() for explicit sizes.
    text_config.vram_size = (size_t)text_columns * text_rows * 2u;
    text_config.start_offset = 0;
    text_config.row_stride = 0;
    text_config.wrap_vram = false;
    text_leave(irq_state);
}

void graphics_set_flashmode(bool flash_line, bool flash_frame) {
    // Legacy video-dither flags have never controlled VGA attribute blinking.
    // Keep this ABI a no-op; use graphics_set_text_blink() explicitly.
    (void)flash_line;
    (void)flash_frame;
}

void graphics_text_select_mode(unsigned mode) {
    unsigned columns;
    unsigned rows;
    switch (mode) {
        case TEXTMODE_40x25_BW:
        case TEXTMODE_40x25_COLOR:
            columns = 40; rows = 25; break;
        case TEXTMODE_80x25_BW:
        case TEXTMODE_80x25_COLOR:
            columns = 80; rows = 25; break;
        case TEXTMODE_53x30:
            columns = 40; rows = 30; break;
        case TEXTMODE_DEFAULT:
        case TEXTMODE_160x100:
            columns = 80; rows = 30; break;
        default:
            return;
    }
    const uint32_t irq_state = text_enter();
    text_columns = (uint8_t)columns;
    text_rows = (uint8_t)rows;
    text_leave(irq_state);
}

void __time_critical_func(graphics_text_frame_tick)(void) {
    // Only the scanout core writes this byte. Setters never wait for a frame.
    ++text_frame;
}

bool __time_critical_func(graphics_text_line)(unsigned output_line,
                                             unsigned output_height,
                                             struct graphics_text_line *line) {
    if (line == NULL || (output_height != 480u && output_height != 240u) ||
        output_line >= output_height) {
        return false;
    }
    const uint32_t irq_state = text_enter();
    const graphics_text_config_t config = text_config;
    const unsigned columns = text_columns;
    const unsigned rows = text_rows;
    const uint8_t frame = text_frame;
    text_leave(irq_state);

    if (config.vram == NULL || config.font == NULL || config.font_height == 0u) {
        return false;
    }
    // A CGA 8-row font gets doubled vertically. VGA 16-row fonts remain 1:1.
    // Other heights (e.g. EGA 14) are centred without resampling the font.
    const unsigned scale_y = config.font_height <= 8u ? 2u : 1u;
    const unsigned cell_height = config.font_height * scale_y;
    const unsigned picture_height = rows * cell_height;
    if (picture_height > 480u) {
        return false;
    }
    const unsigned y = output_height == 240u ? output_line * 2u : output_line;
    const unsigned top = (480u - picture_height) / 2u;
    if (y < top || y >= top + picture_height) {
        return false;
    }
    const unsigned text_y = y - top;
    const unsigned row = text_y / cell_height;
    const size_t stride = config.row_stride ? config.row_stride : columns * 2u;
    if (stride < columns * 2u || config.vram_size < columns * 2u) {
        return false;
    }
    // Avoid overflow even when an emulator exposes an invalid CRTC address.
    size_t offset;
    if (config.wrap_vram) {
        const size_t start = config.start_offset % config.vram_size;
        const size_t row_offset = ((size_t)row * stride) % config.vram_size;
        offset = start >= config.vram_size - row_offset
                ? start - (config.vram_size - row_offset) : start + row_offset;
    } else {
        const size_t row_offset = (size_t)row * stride;
        if (config.start_offset >= config.vram_size ||
            row_offset > config.vram_size - config.start_offset) {
            return false;
        }
        offset = config.start_offset + row_offset;
        if (columns * 2u > config.vram_size - offset) {
            return false;
        }
    }
    const unsigned glyph_line = text_y % cell_height / scale_y;
    const size_t remaining = config.vram_size - offset;
    size_t cursor = config.cursor_offset;
    if (config.wrap_vram) {
        cursor %= config.vram_size;
    }
    size_t cursor_delta = cursor >= offset ? cursor - offset : (size_t)-1;
    if (config.wrap_vram && cursor < offset) {
        cursor_delta = remaining + cursor;
    }
    *line = (struct graphics_text_line){
        .cells = config.vram + offset,
        .font = config.font,
        .wrap_cells = config.vram,
        .wrap_at = remaining,
        .columns = columns,
        .glyph_line = glyph_line,
        .font_stride = config.font_stride,
        .cursor_column = (unsigned)(cursor_delta / 2u),
        .msb_first = config.msb_first,
        .cursor_visible = config.cursor_enabled && (frame & 16u) == 0u &&
                cursor_delta < columns * 2u && (cursor_delta & 1u) == 0u &&
                glyph_line >= config.cursor_start && glyph_line <= config.cursor_end,
        .blink_hidden = (frame & 16u) != 0u,
        .blink_enabled = config.blink_enabled
    };
    return true;
}

