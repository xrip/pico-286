#pragma GCC optimize("O3")
#include "graphics_modes.h"

#include <string.h>

#include <pico.h>
#include <hardware/sync.h>

enum {
    GRAPHICS_GG_WIDTH = 160,
    GRAPHICS_GG_SOURCE_X = 48
};

static volatile enum graphics_mode_t graphics_mode = VGA_320x240x256;
static uint8_t *graphics_source_buffer = NULL;
static uint16_t graphics_source_width = GRAPHICS_FRAME_WIDTH;
static uint16_t graphics_source_height = GRAPHICS_FRAME_HEIGHT;
static int graphics_shift_x = 0;
static int graphics_shift_y = 0;

// A rebind must publish the pointer and its geometry together: both HSTX and
// NTSC read this state from IRQs, potentially while the other core updates it.
typedef struct {
    enum graphics_mode_t mode;
    uint8_t *buffer;
    uint16_t width;
    uint16_t height;
    int shift_x;
    int shift_y;
} graphics_source_state_t;

static spin_lock_t *source_lock;

void graphics_source_init(void) {
    if (source_lock == NULL) {
        source_lock = spin_lock_init(spin_lock_claim_unused(true));
    }
}

static inline uint32_t source_enter(void) {
    return source_lock != NULL ? spin_lock_blocking(source_lock) : 0;
}

static inline void source_leave(uint32_t state) {
    if (source_lock != NULL) spin_unlock(source_lock, state);
}

static graphics_source_state_t __time_critical_func(source_snapshot)(void) {
    const uint32_t irq_state = source_enter();
    const graphics_source_state_t state = {
        graphics_mode, graphics_source_buffer, graphics_source_width,
        graphics_source_height, graphics_shift_x, graphics_shift_y
    };
    source_leave(irq_state);
    return state;
}

// Border and off-picture rows use palette index 0, as the older driver did.
// Kept out of flash so an interrupt never waits on XIP.
static uint8_t graphics_blank_line[GRAPHICS_FRAME_WIDTH] __attribute__((aligned(4)));

// Every backend asks this for each of its lines, so like the line builders it
// belongs in RAM and not in flash.
static bool __time_critical_func(graphics_mode_is_text)(const enum graphics_mode_t mode) {
    if (mode >= TEXTMODE_40x25_BW && mode <= TEXTMODE_80x25_COLOR) {
        return true;
    }
    return mode == TEXTMODE_DEFAULT ||
           mode == TEXTMODE_53x30 ||
           mode == TEXTMODE_160x100;
}

static bool graphics_pc_mode(enum graphics_mode_t mode);

void graphics_source_set_mode(enum graphics_mode_t mode) {
    // The old VGA decoder used linear Mode 13h for its default case.
    // Keep that behavior for the emulator's legacy pseudo-mode values.
    if (!graphics_pc_mode(mode)) mode = VGA_320x200x256;
    const uint32_t irq_state = source_enter();
    graphics_mode = mode;
    source_leave(irq_state);

    // A mode change must never erase borrowed emulator VRAM.
    graphics_text_select_mode((unsigned)mode);
}

enum graphics_mode_t __time_critical_func(graphics_source_mode)(void) {
    // A classification needs just this naturally aligned word, not a copy of
    // the source pointer and dimensions. The actual renderer takes the lock
    // when it snapshots that complete descriptor.
    return graphics_mode;
}

void __time_critical_func(graphics_source_pc_view)(graphics_pc_view_t *view) {
    const graphics_source_state_t state = source_snapshot();
    *view = (graphics_pc_view_t){state.mode, (const uint32_t *)state.buffer,
                               state.shift_x, state.shift_y};
}

static bool __time_critical_func(graphics_pc_mode)(enum graphics_mode_t mode) {
    switch (mode) {
        case TEXTMODE_40x25_BW: case TEXTMODE_40x25_COLOR:
        case TEXTMODE_80x25_BW: case TEXTMODE_80x25_COLOR:
        case CGA_320x200x4: case CGA_320x200x4_BW: case CGA_640x200x2:
        case HERC_640x480x2: case HERC_640x480x2_90:
        case TGA_160x200x16: case TGA_320x200x16: case TGA_640x200x16:
        case EGA_320x200x16x4: case EGA_640x200x16x4: case EGA_640x350x16x4:
        case VGA_640x480x2: case VGA_640x480x16:
        case VGA_320x200x256: case VGA_320x200x256x4:
        case COMPOSITE_160x200x16: case COMPOSITE_160x200x16_force:
            return true;
        default:
            return false;
    }
}


unsigned __time_critical_func(graphics_source_native_width)(void) {
    const enum graphics_mode_t mode = graphics_source_mode();
    switch (mode) {
        case CGA_320x200x4: case CGA_320x200x4_BW:
        case TGA_160x200x16: case TGA_320x200x16:
        case EGA_320x200x16x4: case VGA_320x200x256:
        case VGA_320x200x256x4:
        case COMPOSITE_160x200x16: case COMPOSITE_160x200x16_force:
            return 320;
        default:
            return graphics_pc_mode(mode) ? 640 : 320;
    }
}

void graphics_source_set_buffer(uint8_t *buffer,
                                const uint16_t width,
                                const uint16_t height) {
    const uint32_t irq_state = source_enter();
    graphics_source_buffer = buffer;
    graphics_source_width = width;
    graphics_source_height = height;
    source_leave(irq_state);
}

void graphics_source_set_offset(const int x, const int y) {
    const uint32_t irq_state = source_enter();
    graphics_shift_x = x;
    graphics_shift_y = y;
    source_leave(irq_state);
}

bool __time_critical_func(graphics_source_is_text)(void) {
    return graphics_mode_is_text(graphics_source_mode());
}

unsigned __time_critical_func(graphics_text_columns)(void) {
    const enum graphics_mode_t mode = graphics_source_mode();
    return mode == TEXTMODE_40x25_BW || mode == TEXTMODE_40x25_COLOR ? 40u : 80u;
}

// One row of the application buffer, centred on the 320-pixel line.
// `source_x` and `pixels` pick the visible window inside the source row.
static void __time_critical_func(graphics_compose_indexed)(
        const uint8_t *buffer, const uint16_t width, const int shift_x,
        const int source_row, uint8_t *staging,
        const unsigned source_x, const unsigned pixels, const bool doubled) {
    const unsigned output_pixels = doubled ? pixels * 2u : pixels;
    unsigned left = (GRAPHICS_FRAME_WIDTH - output_pixels) / 2u;

    if (shift_x > 0) {
        left += (unsigned)shift_x;
        if (left > GRAPHICS_FRAME_WIDTH - output_pixels) {
            left = GRAPHICS_FRAME_WIDTH - output_pixels;
        }
    }

    const uint8_t *source = buffer + (size_t)source_row * width + source_x;
    uint8_t *output = staging;

    for (unsigned i = left; i--;) {
        *output++ = 0;
    }
    for (unsigned i = pixels; i--;) {
        const uint8_t index = *source++ & 0x1fu;
        *output++ = index;
        if (doubled) {
            *output++ = index;
        }
    }
    for (unsigned i = GRAPHICS_FRAME_WIDTH - left - output_pixels; i--;) {
        *output++ = 0;
    }
}

const uint8_t *__time_critical_func(graphics_source_line)(
        const uint8_t *framebuffer, const unsigned row, uint8_t *staging) {
    if (graphics_source_is_pc_mode()) {
        graphics_source_render_line(row, GRAPHICS_FRAME_WIDTH, GRAPHICS_FRAME_HEIGHT, staging);
        return staging;
    }
    // Release the lock before reading pixels or filling a row. A caller keeps
    // borrowed buffers alive through any in-flight scanline after a rebind.
    const graphics_source_state_t state = source_snapshot();
    const enum graphics_mode_t graphics_mode = state.mode;
    const uint8_t *graphics_source_buffer = state.buffer;
    const uint16_t graphics_source_width = state.width;
    const uint16_t graphics_source_height = state.height;
    const int graphics_shift_x = state.shift_x;
    const int graphics_shift_y = state.shift_y;
    switch (graphics_mode) {
        case GRAPHICSMODE_DEFAULT:
        case GG_160x144:
        case GG_160x144x4x3: {
            if (graphics_source_buffer == NULL) {
                return graphics_blank_line;
            }

            // The 4:3 Game Gear mode puts three physical lines on one source
            // row. `row` is already half of the physical line, so the source
            // step is two thirds of a row.
            const int scaled = graphics_mode == GG_160x144x4x3
                    ? (int)(row * 2u / 3u)
                    : (int)row;
            const int source_row = scaled - graphics_shift_y;
            if (source_row < 0 || source_row >= (int)graphics_source_height) {
                return graphics_blank_line;
            }

            if (graphics_mode == GRAPHICSMODE_DEFAULT) {
                const unsigned pixels =
                        graphics_source_width < GRAPHICS_FRAME_WIDTH
                        ? graphics_source_width : GRAPHICS_FRAME_WIDTH;
                graphics_compose_indexed(graphics_source_buffer, graphics_source_width,
                                         graphics_shift_x, source_row,
                                         staging, 0, pixels, false);
            } else {
                graphics_compose_indexed(graphics_source_buffer, graphics_source_width,
                                         graphics_shift_x, source_row, staging,
                                         GRAPHICS_GG_SOURCE_X,
                                         GRAPHICS_GG_WIDTH,
                                         graphics_mode == GG_160x144x4x3);
            }
            return staging;
        }

        default:
            if (framebuffer == NULL) {
                return graphics_blank_line;
            }
            return framebuffer + (size_t)row * GRAPHICS_FRAME_WIDTH;
    }
}

// Four plane bytes become eight packed color indices. One shared 1 KiB
// SRAM table replaces the repeated bit-spread masks in every scanline.
static uint32_t pc_spread_lut[256]
        __attribute__((section(".data.pc_spread_lut"), aligned(4))) = {
    0x00000000u, 0x00000001u, 0x00000010u, 0x00000011u, 0x00000100u, 0x00000101u, 0x00000110u, 0x00000111u,
    0x00001000u, 0x00001001u, 0x00001010u, 0x00001011u, 0x00001100u, 0x00001101u, 0x00001110u, 0x00001111u,
    0x00010000u, 0x00010001u, 0x00010010u, 0x00010011u, 0x00010100u, 0x00010101u, 0x00010110u, 0x00010111u,
    0x00011000u, 0x00011001u, 0x00011010u, 0x00011011u, 0x00011100u, 0x00011101u, 0x00011110u, 0x00011111u,
    0x00100000u, 0x00100001u, 0x00100010u, 0x00100011u, 0x00100100u, 0x00100101u, 0x00100110u, 0x00100111u,
    0x00101000u, 0x00101001u, 0x00101010u, 0x00101011u, 0x00101100u, 0x00101101u, 0x00101110u, 0x00101111u,
    0x00110000u, 0x00110001u, 0x00110010u, 0x00110011u, 0x00110100u, 0x00110101u, 0x00110110u, 0x00110111u,
    0x00111000u, 0x00111001u, 0x00111010u, 0x00111011u, 0x00111100u, 0x00111101u, 0x00111110u, 0x00111111u,
    0x01000000u, 0x01000001u, 0x01000010u, 0x01000011u, 0x01000100u, 0x01000101u, 0x01000110u, 0x01000111u,
    0x01001000u, 0x01001001u, 0x01001010u, 0x01001011u, 0x01001100u, 0x01001101u, 0x01001110u, 0x01001111u,
    0x01010000u, 0x01010001u, 0x01010010u, 0x01010011u, 0x01010100u, 0x01010101u, 0x01010110u, 0x01010111u,
    0x01011000u, 0x01011001u, 0x01011010u, 0x01011011u, 0x01011100u, 0x01011101u, 0x01011110u, 0x01011111u,
    0x01100000u, 0x01100001u, 0x01100010u, 0x01100011u, 0x01100100u, 0x01100101u, 0x01100110u, 0x01100111u,
    0x01101000u, 0x01101001u, 0x01101010u, 0x01101011u, 0x01101100u, 0x01101101u, 0x01101110u, 0x01101111u,
    0x01110000u, 0x01110001u, 0x01110010u, 0x01110011u, 0x01110100u, 0x01110101u, 0x01110110u, 0x01110111u,
    0x01111000u, 0x01111001u, 0x01111010u, 0x01111011u, 0x01111100u, 0x01111101u, 0x01111110u, 0x01111111u,
    0x10000000u, 0x10000001u, 0x10000010u, 0x10000011u, 0x10000100u, 0x10000101u, 0x10000110u, 0x10000111u,
    0x10001000u, 0x10001001u, 0x10001010u, 0x10001011u, 0x10001100u, 0x10001101u, 0x10001110u, 0x10001111u,
    0x10010000u, 0x10010001u, 0x10010010u, 0x10010011u, 0x10010100u, 0x10010101u, 0x10010110u, 0x10010111u,
    0x10011000u, 0x10011001u, 0x10011010u, 0x10011011u, 0x10011100u, 0x10011101u, 0x10011110u, 0x10011111u,
    0x10100000u, 0x10100001u, 0x10100010u, 0x10100011u, 0x10100100u, 0x10100101u, 0x10100110u, 0x10100111u,
    0x10101000u, 0x10101001u, 0x10101010u, 0x10101011u, 0x10101100u, 0x10101101u, 0x10101110u, 0x10101111u,
    0x10110000u, 0x10110001u, 0x10110010u, 0x10110011u, 0x10110100u, 0x10110101u, 0x10110110u, 0x10110111u,
    0x10111000u, 0x10111001u, 0x10111010u, 0x10111011u, 0x10111100u, 0x10111101u, 0x10111110u, 0x10111111u,
    0x11000000u, 0x11000001u, 0x11000010u, 0x11000011u, 0x11000100u, 0x11000101u, 0x11000110u, 0x11000111u,
    0x11001000u, 0x11001001u, 0x11001010u, 0x11001011u, 0x11001100u, 0x11001101u, 0x11001110u, 0x11001111u,
    0x11010000u, 0x11010001u, 0x11010010u, 0x11010011u, 0x11010100u, 0x11010101u, 0x11010110u, 0x11010111u,
    0x11011000u, 0x11011001u, 0x11011010u, 0x11011011u, 0x11011100u, 0x11011101u, 0x11011110u, 0x11011111u,
    0x11100000u, 0x11100001u, 0x11100010u, 0x11100011u, 0x11100100u, 0x11100101u, 0x11100110u, 0x11100111u,
    0x11101000u, 0x11101001u, 0x11101010u, 0x11101011u, 0x11101100u, 0x11101101u, 0x11101110u, 0x11101111u,
    0x11110000u, 0x11110001u, 0x11110010u, 0x11110011u, 0x11110100u, 0x11110101u, 0x11110110u, 0x11110111u,
    0x11111000u, 0x11111001u, 0x11111010u, 0x11111011u, 0x11111100u, 0x11111101u, 0x11111110u, 0x11111111u,
};

static inline __attribute__((always_inline)) uint32_t pc_pack_planes(uint32_t planes) {
    return pc_spread_lut[planes & 255u] | (pc_spread_lut[(planes >> 8) & 255u] << 1) |
           (pc_spread_lut[(planes >> 16) & 255u] << 2) | (pc_spread_lut[planes >> 24] << 3);
}

static inline __attribute__((always_inline)) uint32_t pc_word(const uint32_t *vram, unsigned address) {
    return vram[address & 0xffffu];
}

// CGA programs use 0..7 cursor scanlines; VGA programs (and the emulator's
// default 12/13 cursor) use 0..15. Keep CGA's doubled 480-line shape while
// accepting native VGA cursor scanlines and their half-height NTSC/TFT form.
static inline __attribute__((always_inline)) bool pc_cursor_on_line(
        const graphics_pc_state_t *state, unsigned text_row,
        unsigned glyph_line, unsigned height) {
    const unsigned first = state->cursor_start & 31u;
    const unsigned last = state->cursor_end & 31u;
    const bool font16 = (first | last) >= 8;
    const unsigned cursor_line = font16 ? (height == 480 ? glyph_line : glyph_line * 2u) :
                                         (height == 480 ? glyph_line >> 1 : glyph_line);
    return state->cursor_phase && !(state->cursor_start & 0x20u) &&
            text_row == state->cursor_y &&
            (first > last ? !(cursor_line >= last && cursor_line <= first) :
                           cursor_line >= first && cursor_line <= last);
}

#define PC_FAST_TYPE uint16_t
#define PC_FAST_PALETTE uint16_t
#define PC_FAST_PAIR uint32_t
#define PC_FAST_FN(name) pc_fast16_##name
#define PC_FAST_MAP(color) palette[color]
#include "graphics_pc_fast.inc"
#undef PC_FAST_MAP
#undef PC_FAST_FN
#undef PC_FAST_PAIR
#undef PC_FAST_TYPE
#undef PC_FAST_PALETTE

#define PC_FAST_TYPE uint32_t
#define PC_FAST_PALETTE uint32_t
#define PC_FAST_PAIR uint64_t
#define PC_FAST_FN(name) pc_fast32_##name
#define PC_FAST_MAP(color) palette[color]
#include "graphics_pc_fast.inc"
#undef PC_FAST_MAP
#undef PC_FAST_FN
#undef PC_FAST_PAIR
#undef PC_FAST_TYPE
#undef PC_FAST_PALETTE

#define PC_PIXEL uint8_t
#define PC_PALETTE uint8_t
#define PC_FN(name) pc_index_##name
#define PC_MAP(color) (color)
#include "graphics_pc_render.inc"
#undef PC_MAP
#undef PC_FN
#undef PC_PALETTE
#undef PC_PIXEL

void __time_critical_func(graphics_source_render_line)(
        unsigned row, unsigned width, unsigned height, uint8_t *indices) {
    pc_index_render(row, width, height, indices, NULL);
}

#define PC_PIXEL uint16_t
#define PC_PALETTE uint16_t
#define PC_FN(name) pc_mapped16_##name
#define PC_MAP(color) palette[color]
#include "graphics_pc_render.inc"
#undef PC_MAP
#undef PC_FN
#undef PC_PALETTE
#undef PC_PIXEL

void __time_critical_func(graphics_source_render_line16)(
        unsigned row, unsigned width, unsigned height, uint16_t *output,
        const uint16_t *palette) {
    if (!pc_fast16_render(row, width, height, output, palette))
        pc_mapped16_render(row, width, height, output, palette);
}

#define PC_PIXEL uint32_t
#define PC_PALETTE uint32_t
#define PC_FN(name) pc_mapped32_##name
#define PC_MAP(color) palette[color]
#include "graphics_pc_render.inc"
#undef PC_MAP
#undef PC_FN
#undef PC_PALETTE
#undef PC_PIXEL

void __time_critical_func(graphics_source_render_line32)(
        unsigned row, unsigned width, unsigned height, uint32_t *output,
        const uint32_t *palette) {
    if (!pc_fast32_render(row, width, height, output, palette))
        pc_mapped32_render(row, width, height, output, palette);
}





