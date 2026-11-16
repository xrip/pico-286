#include "vga_crtc.h"

#include "graphics_modes.h"
#include "../../src/emulator/video/mc6845.h"
#include <pico.h>
#if PICO_ON_DEVICE
#include <hardware/sync.h>
#endif

// Use the project's existing MC6845 model. RP8086 supplied the layout idea,
// but its R8 interlace shortcut is not valid CGA address generation. The
// model deliberately keeps doubled rows and leaves physical VGA timing alone.
static mc6845_state_s crtc;
static unsigned base_mode = TEXTMODE_80x25_COLOR;
static bool cursor_written;
static bool text_mode_override;
static volatile uint32_t state_sequence;
static volatile uint32_t cursor_position;
static volatile bool cursor_valid;
volatile bool vga_crtc_text_custom;

static inline uint32_t enter_write(void) {
#if PICO_ON_DEVICE
    const uint32_t saved = save_and_disable_interrupts();
#else
    const uint32_t saved = 0;
#endif
    ++state_sequence;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return saved;
}

static inline void leave_write(uint32_t saved) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ++state_sequence;
#if PICO_ON_DEVICE
    restore_interrupts(saved);
#else
    (void)saved;
#endif
}

static void update_cached_layout(void) {
    const unsigned normal_columns = base_mode <= TEXTMODE_40x25_COLOR ? 40u : 80u;
    vga_crtc_text_custom = base_mode <= TEXTMODE_80x25_COLOR &&
            (text_mode_override || crtc.horizontal_displayed != normal_columns ||
             crtc.vertical_displayed != 25u || crtc.character_height != 8u ||
             crtc.horizontal_position != 0 || crtc.vertical_position != 0 ||
             (uint32_t)crtc.start_address * 2u +
                     crtc.horizontal_displayed * crtc.vertical_displayed * 2u > 0x4000u);
    // One aligned store publishes both coordinates to the standard renderer.
    cursor_position = crtc.cursor_x | (uint32_t)crtc.cursor_y << 16;
    cursor_valid = base_mode <= TEXTMODE_80x25_COLOR && cursor_written;
}

void vga_crtc_reset(unsigned bios_mode) {
    const uint32_t saved = enter_write();
    base_mode = bios_mode;
    mc6845_set_cga_mode(&crtc, (uint8_t)bios_mode);
    cursor_written = false;
    text_mode_override = false;
    update_cached_layout();
    leave_write(saved);
}

void vga_crtc_write(uint8_t index, uint8_t value) {
    if (index >= MC6845_STORAGE_COUNT) return;
    const uint32_t saved = enter_write();
    mc6845_write(&crtc, index, value);
    // ports.c uses these writes to select text-layout pseudo modes. Retain
    // the BIOS text mode until the next reset even if the shared mode changes.
    if ((index == 4u && value == 0x3eu) ||
        (index == 6u && (value == 0x64u || value == 0x2eu))) text_mode_override = true;
    if (index == 14u || index == 15u) cursor_written = true;
    update_cached_layout();
    leave_write(saved);
}

bool __time_critical_func(vga_crtc_get_cursor)(uint8_t *x, uint8_t *y) {
    if (!cursor_valid) return false;
    const uint32_t position = cursor_position;
    const unsigned column = position & 0xffffu;
    const unsigned row = position >> 16;
    // A cursor outside the visible text page must not wrap back into view.
    *x = column < 255u ? (uint8_t)column : 255u;
    *y = row < 255u ? (uint8_t)row : 255u;
    return true;
}

static void __time_critical_func(crtc_snapshot)(mc6845_state_s *state, unsigned *mode) {
    uint32_t before;
    do {
        before = state_sequence;
        if (before & 1u) continue;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        *state = crtc;
        *mode = base_mode;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
    } while ((before & 1u) || before != state_sequence);
}

void __time_critical_func(vga_crtc_render_text)(
        unsigned row, unsigned canvas_height, uint16_t *output,
        const uint16_t *attribute_pairs) {
    // This path is only for changed CRTC geometry. It always writes all 640
    // samples so short rows and blanked rows cannot leave old pixels behind.
    for (unsigned x = 0; x < 320; ++x) output[x] = attribute_pairs[0];
    if (canvas_height != 480u && canvas_height != 240u) return;
    mc6845_state_s state;
    unsigned mode;
    crtc_snapshot(&state, &mode);
    if (mode > TEXTMODE_80x25_COLOR || state.horizontal_displayed == 0) return;

    graphics_pc_view_t view;
    graphics_source_pc_view(&view);
    if (view.vram == NULL) return;
    const int output_y = (int)(canvas_height == 480u ? row : row * 2u) - view.shift_y * 2;
    if (output_y < 0 || output_y >= 480) return;
    const mc6845_scanline_s line = mc6845_scanline(&state, (uint16_t)output_y);
    if (!line.displayed) return;

    graphics_pc_state_t pc;
    graphics_pc_snapshot(&pc);
    const bool wide = mode <= TEXTMODE_40x25_COLOR;
    const unsigned cell_pixels = wide ? 16u : 8u;
    const int left = state.horizontal_position * (int)cell_pixels + view.shift_x * 2;
    const unsigned row_address = mc6845_text_byte_offset(&state, line.row,
                                                         state.horizontal_displayed);
    const bool cursor_line = cursor_valid && line.row == state.cursor_y &&
            mc6845_cursor_raster_visible(&state, line.raster, pc.cursor_phase);
    uint8_t *const bytes = (uint8_t *)output;

    for (unsigned column = 0; column < state.horizontal_displayed; ++column) {
        const int cell_x = left + (int)(column * cell_pixels);
        if (cell_x >= 640) break;
        if (cell_x + (int)cell_pixels <= 0) continue;
        const unsigned address = (row_address + column * 2u) & 0x3fffu;
        const uint8_t character = (uint8_t)view.vram[0x8000u + address];
        const uint8_t attribute = (uint8_t)view.vram[0x8000u + ((address + 1u) & 0x3fffu)];
        uint8_t glyph = 0;
        if (state.character_height == 8u && canvas_height == 480u) {
            glyph = font_8x16[character * 16u + line.raster * 2u + ((unsigned)output_y & 1u)];
        } else if (state.character_height <= 8u) {
            if (line.raster < 8u) glyph = font_8x8[character * 8u + line.raster];
        } else if (line.raster < 16u) {
            glyph = font_8x16[character * 16u + line.raster];
        }
        if (pc.blink_mask == 0x7fu && (attribute & 0x80u) && pc.cursor_phase) glyph = 0;
        if (cursor_line && column == state.cursor_x) glyph = 0xffu;
        const uint16_t *pairs = attribute_pairs + (attribute & pc.blink_mask) * 4u;

        if (cell_x >= 0 && cell_x + (int)cell_pixels <= 640) {
            uint16_t *dest = (uint16_t *)(bytes + cell_x);
            if (!wide) {
                dest[0] = pairs[glyph & 3u];
                dest[1] = pairs[(glyph >> 2) & 3u];
                dest[2] = pairs[(glyph >> 4) & 3u];
                dest[3] = pairs[glyph >> 6];
            } else {
                for (unsigned group = 0; group < 4; ++group) {
                    const uint16_t pair = pairs[glyph & 3u];
                    *dest++ = (uint16_t)((pair & 255u) * 0x0101u);
                    *dest++ = (uint16_t)((pair >> 8) * 0x0101u);
                    glyph >>= 2;
                }
            }
        } else {
            // Only a clipped edge cell takes the byte path.
            for (unsigned bit = 0; bit < 8; ++bit) {
                const uint8_t pixel = (uint8_t)pairs[(glyph >> bit) & 1u];
                const int x = cell_x + (int)(bit * (wide ? 2u : 1u));
                if (x >= 0 && x < 640) bytes[x] = pixel;
                if (wide && x + 1 >= 0 && x + 1 < 640) bytes[x + 1] = pixel;
            }
        }
    }
}
