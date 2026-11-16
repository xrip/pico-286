#pragma GCC optimize("O3")
#include <stdalign.h>
#include <stddef.h>
#include <string.h>

#include <hardware/dma.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>

#include "graphics.h"
#include "graphics_modes.h"
#include "graphics_pc_raster.h"
#include "graphics_pc_hdmi.h"
#include "hdmi_codec.h"
#include "hdmi_text.pio.h"

#ifndef HDMI_PROFILE_GPIO
#define HDMI_PROFILE_GPIO (-1)
#endif
#if HDMI_PROFILE_GPIO >= 0
_Static_assert(HDMI_PROFILE_GPIO < NUM_BANK0_GPIOS &&
               (HDMI_PROFILE_GPIO < HDMI_PIN_CLOCK || HDMI_PROFILE_GPIO >= HDMI_PIN_DATA + 6),
               "HDMI profiling GPIO must be a valid, unused pin");
#endif

enum {
    HDMI_FRAME_WIDTH = 320,
    HDMI_FRAME_HEIGHT = 240,
    HDMI_ACTIVE_SCANLINES = 480,
    HDMI_TOTAL_SCANLINES = 525,
    HDMI_VSYNC_FIRST_LINE = 490,
    HDMI_VSYNC_LINES = 2,
    HDMI_TEXT_HSYNC_BYTES = 48, // twelve commands, eight physical pixels each
    HDMI_GRAPHICS_HSYNC_BYTES = 48,
    HDMI_TEXT_ACTIVE_OFFSET = 72,
    HDMI_GRAPHICS_ACTIVE_OFFSET = 72,
    HDMI_CTRL_BASE_INDEX = 252,
    HDMI_TEXT_CTRL_BASE_INDEX = 16
};

// Exactly the old driver's 4,896-byte allocation. The palette, lines and
// cached graphics colours are overlaid only while all HDMI DMA is stopped.
// Text commands and the 640-pixel scanlines share this workspace. PC scanout
// adds only a small code-mapping table, not a text VRAM or framebuffer copy.
static alignas(4096) union hdmi_workspace workspace;
_Static_assert(sizeof(workspace) == HDMI_WORKSPACE_BYTES, "HDMI RAM budget changed");
_Static_assert(sizeof(workspace.text) <= HDMI_WORKSPACE_BYTES, "Text exceeds existing workspace");
_Static_assert(offsetof(union hdmi_workspace, wide.palette) == offsetof(union hdmi_workspace, text.palette),
               "Wide and text modes share one palette layout");
_Static_assert(offsetof(union hdmi_workspace, graphics.lines) % 4u == 0u &&
               offsetof(union hdmi_workspace, text.lines) % 4u == 0u &&
               sizeof(workspace.graphics.lines[0]) % 4u == 0u &&
               sizeof(workspace.text.lines[0]) % 4u == 0u,
               "HDMI line helpers require aligned, whole-word buffers");
_Static_assert(HDMI_TEXT_HSYNC_BYTES % 4u == 0u &&
               HDMI_GRAPHICS_HSYNC_BYTES % 4u == 0u &&
               HDMI_TEXT_ACTIVE_OFFSET % 4u == 0u &&
               HDMI_GRAPHICS_ACTIVE_OFFSET % 4u == 0u &&
               HDMI_FRAME_WIDTH % 4u == 0u,
               "HDMI line fill regions must span aligned, whole words");

#ifndef GRAPHICS_NO_BUILTIN_FRAMEBUFFER
static uint8_t hdmi_primary_framebuffer[HDMI_FRAME_WIDTH * HDMI_FRAME_HEIGHT]
        __attribute__((aligned(4)));
#endif
static const uint8_t *graphics_framebuffer;
static const uint32_t *pc_vram;
static int source_shift_x;
static int source_shift_y;
static const uint8_t *volatile pending_framebuffer;
static volatile uint16_t hdmi_current_scanline;
static bool initialized;
static bool palette_initialized;
static bool text_mode;
static bool wide_mode;
static spin_lock_t *hdmi_lock;
#if defined(GRAPHICS_PC_EMULATOR)
extern volatile uint8_t port3DA;
#endif
// Four codes are reserved for sync in the 320-pixel PIO transport. Keep
// their requested RGB values separate so they never overwrite colours 48..51.
// Approximate them with the first 16 colours. This bounds the work while
// holding the IRQ lock; it avoids decoding all 252 hardware palette entries.
static uint32_t base_rgb[16];
static uint32_t tail_rgb[4];
static uint32_t tail_distance[4];
static uint8_t tail_code[4];
static uint16_t pc_codes[256];

static uint pio_program_offset_video;
static uint pio_program_offset_converter;
static int sm_video_output;
static int sm_address_converter;
static int dma_channel_control;
static int dma_channel_data;
static int dma_channel_palette_control;
static int dma_channel_palette_data;

#if defined(PC_USB_CONSOLE)
static volatile uint32_t hdmi_irq_entries, hdmi_irq_exits, hdmi_frames;

void hdmi_debug_snapshot(hdmi_debug_state_t *state) {
    state->irq_entries = hdmi_irq_entries;
    state->irq_exits = hdmi_irq_exits;
    state->frames = hdmi_frames;
    state->line = hdmi_current_scanline;
    state->channel[0] = dma_channel_control;
    state->channel[1] = dma_channel_data;
    state->channel[2] = dma_channel_palette_control;
    state->channel[3] = dma_channel_palette_data;
}
#endif

// The control channel advances through this ring itself. The IRQ fills the
// next buffer, never the buffer whose pointer has just been consumed by DMA.
static alignas(8) volatile uint32_t dma_buffer_addresses[2];
static struct hdmi_line_schedule line_schedule;

static inline uint32_t hdmi_enter(void) {
    return hdmi_lock != NULL ? spin_lock_blocking(hdmi_lock) : 0;
}

static inline void hdmi_leave(uint32_t state) {
    if (hdmi_lock != NULL) spin_unlock(hdmi_lock, state);
}

static const uint16_t pio_instructions_address_converter[] = {
    0x80a0, // pull block
    0x40e8, // in osr, 8
    0x4034, // in x, 20 (graphics palette: sixteen bytes per doubled pixel)
    0x8020, // push block
};

// One physical pixel per entry. Bits 0..3 select a colour, bit 5 selects
// TMDS phase, and values 16..19 carry sync independently of the palette.
static const uint16_t pio_instructions_wide_converter[] = {
    0x80a0, // pull block
    0x40e6, // in osr, 6
    0x4037, // in x, 23 (eight-byte entry, 512-byte aligned palette)
    0x8020, // push block
};

static const uint16_t pio_instructions_hdmi_output[] = {
    0x7006, 0x7006, 0x7006, 0x7006, 0x7006,
    0x6806, 0x6806, 0x6806, 0x6806, 0x6806,
};
static const struct pio_program pio_program_hdmi_output = {
    .instructions = pio_instructions_hdmi_output,
    .length = 10,
    .origin = -1,
};

static inline uint8_t hdmi_color_code(uint8_t index) {
    return index < HDMI_CTRL_BASE_INDEX ? index :
            tail_code[index - HDMI_CTRL_BASE_INDEX];
}

static uint32_t hdmi_color_distance(uint32_t a, uint32_t b) {
    const int r = (int)((a >> 16) & 255u) - (int)((b >> 16) & 255u);
    const int g = (int)((a >> 8) & 255u) - (int)((b >> 8) & 255u);
    const int blue = (int)(a & 255u) - (int)(b & 255u);
    return (uint32_t)(r * r + g * g + blue * blue);
}

// In SRAM: graphics_set_palette() calls it under hdmi_lock.
static void __time_critical_func(hdmi_find_tail_color)(unsigned tail) {
    uint32_t best = UINT32_MAX;
    for (unsigned i = 0; i < 16u; ++i) {
        const uint32_t distance = hdmi_color_distance(base_rgb[i], tail_rgb[tail]);
        if (distance < best) {
            best = distance;
            tail_code[tail] = (uint8_t)i;
            if (best == 0u) break;
        }
    }
    tail_distance[tail] = best;
}

static void hdmi_initialize_palette(void) {
    if (palette_initialized) return;
    for (unsigned i = 0; i < HDMI_CTRL_BASE_INDEX; ++i)
        hdmi_encode_graphics_rgb(workspace.graphics.palette[i], 0);
    palette_initialized = true;
}

static void hdmi_write_controls(void) {
    static const uint16_t symbols[4] = {0x354, 0x0ab, 0x154, 0x2ab};
    for (unsigned i = 0; i < 4; ++i) {
        // Negative H/V polarity: blank uses CTL=3; H pulse uses CTL=2.
        const uint64_t packed = hdmi_pack_symbols(symbols[0], symbols[0], symbols[3 - i]);
        if (text_mode || wide_mode) {
            for (unsigned phase = 0; phase < 2; ++phase)
                hdmi_store_pixel(workspace.text.palette[HDMI_TEXT_CTRL_BASE_INDEX + i + phase * 32], packed);
        } else {
            uint32_t *entry = workspace.graphics.palette[HDMI_CTRL_BASE_INDEX + i];
            hdmi_store_pixel(entry, packed);
            hdmi_store_pixel(entry + 2, packed);
        }
    }
}

// The glyph expander draws text; an emulator build sends PC text there too.
static bool hdmi_wants_text(void) {
#if defined(GRAPHICS_PC_EMULATOR)
    return graphics_source_is_text();
#else
    return !graphics_source_is_pc_mode() && graphics_source_is_text();
#endif
}

static bool hdmi_wants_wide(void) {
    return graphics_source_is_pc_mode() && !hdmi_wants_text() &&
           graphics_source_native_width() == 640u;
}

static void hdmi_change_workspace_mode(bool want_text, bool want_wide) {
    hdmi_initialize_palette();
    const bool had_small_palette = text_mode || wide_mode;
    const bool want_small_palette = want_text || want_wide;
    if (want_small_palette && !had_small_palette) {
        hdmi_cache_graphics_palette(&workspace);
    } else if (!want_small_palette && had_small_palette) {
        hdmi_restore_graphics_palette(&workspace);
    }
    if (want_small_palette) {
        for (unsigned color = 0; color < 16; ++color) {
            const uint8_t *cached = workspace.text.graphics_rgb[color];
#if defined(GRAPHICS_PC_EMULATOR)
            const bool cached_color = true; // the emulator also sets the text colours
#else
            const bool cached_color = want_wide;
#endif
            const uint32_t rgb = cached_color ?
                    ((uint32_t)cached[0] << 16 | (uint32_t)cached[1] << 8 | cached[2]) :
                    GRAPHICS_CGA_RGB(color);
            for (unsigned phase = 0; phase < 2; ++phase)
                hdmi_store_pixel(workspace.text.palette[color + phase * 32],
                                  hdmi_text_pixel(rgb, phase));
        }
        if (want_text) hdmi_initialize_text_lookups(&workspace);
    }
    text_mode = want_text;
    wide_mode = want_wide;
    for (unsigned i = 0; i < 256u; ++i) {
        const uint8_t code = want_wide ? (i & 15u) : hdmi_color_code((uint8_t)i);
        pc_codes[i] = code | (uint16_t)(code | (want_wide ? 32u : 0u)) << 8;
    }
    hdmi_write_controls();
}

// Keep all scanline memory operations in the SRAM-resident caller. In
// particular, do not let GCC turn these loops back into XIP libc calls.
// Every caller supplies a four-byte aligned range and a multiple of four
// bytes. may_alias permits word accesses to the uint8_t line arrays.
typedef uint32_t hdmi_line_word_t __attribute__((may_alias));

static __force_inline void hdmi_fill_words(uint8_t *destination,
                                         uint32_t word, unsigned bytes) {
    volatile hdmi_line_word_t *output = (volatile hdmi_line_word_t *)destination;
#if PICO_RP2350 && defined(__ARM_ARCH_8M_MAIN__)
    // M33 STRD accepts a word-aligned address and the same source twice.
    // Constant-size callers fold these prefixes: the eight-byte back porch
    // becomes one STRD. Larger fills write four words per loop without extra
    // registers or a scanline cache; an odd word count still works.
    if (bytes & 4u) *output++ = word;
    unsigned pairs = bytes >> 3u;
    if (pairs & 1u) {
        __asm volatile (
            "strd %[word], %[word], [%[output]], #8\n"
            : [output] "+&r" (output)
            : [word] "r" (word)
            : "memory"
        );
    }
    pairs >>= 1u;
    if (pairs == 0u) return;
    __asm volatile (
        ".syntax unified\n"
        "1:\n"
        "strd %[word], %[word], [%[output]], #8\n"
        "strd %[word], %[word], [%[output]], #8\n"
        "subs %[pairs], %[pairs], #1\n"
        "bne 1b\n"
        : [output] "+&r" (output), [pairs] "+&r" (pairs)
        : [word] "r" (word)
        : "cc", "memory"
    );
#else
#pragma GCC unroll 1
    for (unsigned i = 0; i < bytes / 4u; ++i) output[i] = word;
#endif
}

static __force_inline void hdmi_fill_line(uint8_t *destination, uint8_t value,
                                         unsigned bytes) {
    hdmi_fill_words(destination, (uint32_t)value * UINT32_C(0x01010101), bytes);
}

static __attribute__((noinline, noclone))
void __time_critical_func(hdmi_update_text_attributes)(unsigned state) {
    // Only attributes with bit 7 set change. Folding blink into the colour
    // table removes a conditional from every character of every scanline.
#pragma GCC unroll 1
    for (unsigned attribute = 128; attribute < 256; ++attribute)
        workspace.text.attributes[attribute] =
                hdmi_text_attribute_word(attribute, state & 1u, state & 2u);
    workspace.text.attribute_state = state;
}

// A text cell is a character byte and an attribute byte. The PC emulator
// keeps each VRAM byte in a 32-bit word: its cells are eight bytes, with the
// attribute at +4. The asm strings repeat these offsets.
#if defined(GRAPHICS_PC_EMULATOR)
#define HDMI_CELL_BYTES 8u
#define HDMI_ATTRIBUTE_OFFSET 4u
#define HDMI_ASM_CELL "8"            // one cell
#define HDMI_ASM_ATTRIBUTE "4"       // attribute of this cell
#define HDMI_ASM_PAIR "16"           // two cells
#define HDMI_ASM_ATTRIBUTE0 "-12"    // after a pair step: first attribute
#define HDMI_ASM_GLYPH1 "-8"         // second character
#define HDMI_ASM_ATTRIBUTE1 "-4"     // second attribute; also after a one-cell step
#else
#define HDMI_CELL_BYTES 2u
#define HDMI_ATTRIBUTE_OFFSET 1u
#define HDMI_ASM_CELL "2"
#define HDMI_ASM_ATTRIBUTE "1"
#define HDMI_ASM_PAIR "4"
#define HDMI_ASM_ATTRIBUTE0 "-3"
#define HDMI_ASM_GLYPH1 "-2"
#define HDMI_ASM_ATTRIBUTE1 "-1"
#endif

// Keep a fixed ABI at the hot kernels. The M0+ blocks explicitly preserve
// r8/r9 too: these registers belong to the interrupted application. The M33
// blocks use allocator-owned operands, so GCC generates their saved registers.
static __attribute__((noipa))
void __time_critical_func(hdmi_text_span_80)(uint8_t *output,
        const uint8_t *cells, unsigned count, const struct graphics_text_line *text) {
    const uint8_t *font = text->font + text->glyph_line;
    const unsigned stride = text->font_stride;
    const uint16_t *patterns = workspace.text.glyphs[text->msb_first];
    const uint32_t *colors = workspace.text.attributes;
    volatile hdmi_line_word_t *words = (volatile hdmi_line_word_t *)output;
#if PICO_RP2040 && defined(__arm__)
    // Cortex-M0+ has only eight low registers. Keep the loop bound in r8 so
    // GCC cannot spill the stride and LUT pointers on every character. This
    // Thumb-1 loop needs 21 ideal core clocks/cell, including the taken branch.
    if (count == 0) return;
    const uint8_t *end = cells + count * HDMI_CELL_BYTES;
    register const uint8_t *limit __asm("r12") = end;
    uint32_t glyph, attribute;
    __asm volatile (
        ".syntax unified\n"
        "mov %[glyph], r8\n"
        "push {%[glyph]}\n"
        "mov r8, %[limit]\n"
        "1:\n"
        "ldrb %[glyph], [%[cells], #0]\n"
        "muls %[glyph], %[stride]\n"
        "ldrb %[glyph], [%[font], %[glyph]]\n"
        "lsls %[glyph], %[glyph], #1\n"
        "ldrh %[glyph], [%[patterns], %[glyph]]\n"
        "lsls %[glyph], %[glyph], #8\n"
        "ldrb %[attribute], [%[cells], #" HDMI_ASM_ATTRIBUTE "]\n"
        "lsls %[attribute], %[attribute], #2\n"
        "ldr %[attribute], [%[colors], %[attribute]]\n"
        "orrs %[glyph], %[attribute]\n"
        "stmia %[words]!, {%[glyph]}\n"
        "adds %[cells], %[cells], #" HDMI_ASM_CELL "\n"
        "cmp %[cells], r8\n"
        "bne 1b\n"
        "pop {%[glyph]}\n"
        "mov r8, %[glyph]\n"
        ".syntax divided\n"
        : [words] "+&l" (words), [cells] "+&l" (cells),
          [glyph] "=&l" (glyph), [attribute] "=&l" (attribute)
        : [font] "l" (font), [stride] "l" (stride),
          [patterns] "l" (patterns), [colors] "l" (colors), [limit] "r" (limit)
        : "r8", "cc", "memory"
    );
#elif PICO_RP2350 && defined(__ARM_ARCH_8M_MAIN__)
    // Byte loads retain support for an unaligned caller-owned VRAM/font.
    // Handle an odd span first; STRD needs only four-byte output alignment.
    if (count & 1u) {
        const unsigned glyph = font[(unsigned)cells[0] * stride];
        *words++ = colors[cells[HDMI_ATTRIBUTE_OFFSET]] | (uint32_t)patterns[glyph] << 8;
        cells += HDMI_CELL_BYTES;
    }
    count >>= 1u; // pairs, including a possible final pair
    if (count == 0u) return;
    uint32_t glyph0, glyph1, attribute0, attribute1;
    // Interleave two independent cells to separate dependent SRAM loads.
#define HDMI_M33_TEXT_PAIR \
        "ldrb %[glyph0], [%[cells]], #" HDMI_ASM_PAIR "\n" \
        "ldrb %[attribute0], [%[cells], #" HDMI_ASM_ATTRIBUTE0 "]\n" \
        "ldrb %[glyph1], [%[cells], #" HDMI_ASM_GLYPH1 "]\n" \
        "ldrb %[attribute1], [%[cells], #" HDMI_ASM_ATTRIBUTE1 "]\n" \
        "mul %[glyph0], %[glyph0], %[stride]\n" \
        "mul %[glyph1], %[glyph1], %[stride]\n" \
        "ldr %[attribute0], [%[colors], %[attribute0], lsl #2]\n" \
        "ldr %[attribute1], [%[colors], %[attribute1], lsl #2]\n" \
        "ldrb %[glyph0], [%[font], %[glyph0]]\n" \
        "ldrb %[glyph1], [%[font], %[glyph1]]\n" \
        "ldrh %[glyph0], [%[patterns], %[glyph0], lsl #1]\n" \
        "ldrh %[glyph1], [%[patterns], %[glyph1], lsl #1]\n" \
        "orr %[glyph0], %[attribute0], %[glyph0], lsl #8\n" \
        "orr %[glyph1], %[attribute1], %[glyph1], lsl #8\n" \
        "strd %[glyph0], %[glyph1], [%[words]], #8\n"
    __asm volatile (
        ".syntax unified\n"
        // Run two pairs per loop. An odd pair count enters at the second
        // body, so no duplicated tail or read beyond the span is needed.
        "lsrs %[glyph0], %[count], #1\n"
        "adc %[count], %[glyph0], #0\n" // non-S: retain odd-pair carry
        "bcs 2f\n"
        ".p2align 2\n"
        "1:\n"
        HDMI_M33_TEXT_PAIR
        "2:\n"
        HDMI_M33_TEXT_PAIR
        "subs %[count], %[count], #1\n"
        "bne 1b\n"
        : [words] "+&r" (words), [cells] "+&r" (cells), [count] "+&r" (count),
          [glyph0] "=&r" (glyph0), [glyph1] "=&r" (glyph1),
          [attribute0] "=&r" (attribute0), [attribute1] "=&r" (attribute1)
        : [font] "r" (font), [stride] "r" (stride),
          [patterns] "r" (patterns), [colors] "r" (colors)
        : "cc", "memory"
    );
#undef HDMI_M33_TEXT_PAIR
#else
    const uint8_t *end = cells + count * HDMI_CELL_BYTES;
#pragma GCC unroll 1
    while (cells != end) {
        const unsigned glyph = font[(unsigned)cells[0] * stride];
        *words++ = colors[cells[HDMI_ATTRIBUTE_OFFSET]] | (uint32_t)patterns[glyph] << 8;
        cells += HDMI_CELL_BYTES;
    }
#endif
}

static __attribute__((noipa))
void __time_critical_func(hdmi_text_span_40)(uint8_t *output,
        const uint8_t *cells, unsigned count, const struct graphics_text_line *text) {
    const uint8_t *font = text->font + text->glyph_line;
    const unsigned stride = text->font_stride;
    const uint16_t *patterns = workspace.text.wide_glyphs[text->msb_first];
    const uint32_t *colors = workspace.text.attributes;
    volatile hdmi_line_word_t *words = (volatile hdmi_line_word_t *)output;
#if PICO_RP2040 && defined(__arm__)
    if (count == 0) return;
    const uint8_t *end = cells + count * HDMI_CELL_BYTES;
    register const uint8_t *limit __asm("r12") = end;
    uint32_t glyph, attribute;
    // As in the 80-column loop, no stack loads occur inside the hot loop.
    // Two fixed nibble orders avoid variable shifts and per-cell branches.
#define HDMI_WIDE_LOW_NIBBLE \
        "lsls %[glyph], %[glyph], #28\n" \
        "lsrs %[glyph], %[glyph], #27\n"
#define HDMI_WIDE_HIGH_NIBBLE \
        "lsrs %[glyph], %[glyph], #4\n" \
        "lsls %[glyph], %[glyph], #1\n"
#define HDMI_WIDE_LOOP(first, second) \
    __asm volatile ( \
        ".syntax unified\n" \
        "mov %[glyph], r8\n" \
        "push {%[glyph]}\n" \
        "mov %[attribute], r9\n" \
        "push {%[attribute]}\n" \
        "mov r8, %[limit]\n" \
        "1:\n" \
        "ldrb %[glyph], [%[cells], #0]\n" \
        "muls %[glyph], %[stride]\n" \
        "ldrb %[glyph], [%[font], %[glyph]]\n" \
        "mov r9, %[glyph]\n" \
        "ldrb %[attribute], [%[cells], #" HDMI_ASM_ATTRIBUTE "]\n" \
        "lsls %[attribute], %[attribute], #2\n" \
        "ldr %[attribute], [%[colors], %[attribute]]\n" \
        first \
        "ldrh %[glyph], [%[patterns], %[glyph]]\n" \
        "lsls %[glyph], %[glyph], #8\n" \
        "orrs %[glyph], %[attribute]\n" \
        "str %[glyph], [%[words], #0]\n" \
        "mov %[glyph], r9\n" \
        second \
        "ldrh %[glyph], [%[patterns], %[glyph]]\n" \
        "lsls %[glyph], %[glyph], #8\n" \
        "orrs %[glyph], %[attribute]\n" \
        "str %[glyph], [%[words], #4]\n" \
        "adds %[words], %[words], #8\n" \
        "adds %[cells], %[cells], #" HDMI_ASM_CELL "\n" \
        "cmp %[cells], r8\n" \
        "bne 1b\n" \
        "pop {%[attribute]}\n" \
        "mov r9, %[attribute]\n" \
        "pop {%[glyph]}\n" \
        "mov r8, %[glyph]\n" \
        ".syntax divided\n" \
        : [words] "+&l" (words), [cells] "+&l" (cells), \
          [glyph] "=&l" (glyph), [attribute] "=&l" (attribute) \
        : [font] "l" (font), [stride] "l" (stride), \
          [patterns] "l" (patterns), [colors] "l" (colors), [limit] "r" (limit) \
        : "r8", "r9", "cc", "memory" \
    )
    if (text->msb_first) {
        HDMI_WIDE_LOOP(HDMI_WIDE_HIGH_NIBBLE, HDMI_WIDE_LOW_NIBBLE);
    } else {
        HDMI_WIDE_LOOP(HDMI_WIDE_LOW_NIBBLE, HDMI_WIDE_HIGH_NIBBLE);
    }
#undef HDMI_WIDE_LOOP
#undef HDMI_WIDE_HIGH_NIBBLE
#undef HDMI_WIDE_LOW_NIBBLE
#elif PICO_RP2350 && defined(__ARM_ARCH_8M_MAIN__)
    if (count == 0u) return;
    uint32_t glyph, attribute, low;
    // Fixed nibble extraction removes both per-cell variable shifts and
    // their redundant masks. Only the store order depends on font bit order.
#define HDMI_M33_WIDE_LOOP(first, second) \
    __asm volatile ( \
        ".syntax unified\n" \
        "1:\n" \
        "ldrb %[glyph], [%[cells]], #" HDMI_ASM_CELL "\n" \
        "ldrb %[attribute], [%[cells], #" HDMI_ASM_ATTRIBUTE1 "]\n" \
        "mul %[glyph], %[glyph], %[stride]\n" \
        "ldr %[attribute], [%[colors], %[attribute], lsl #2]\n" \
        "ldrb %[glyph], [%[font], %[glyph]]\n" \
        "and %[low], %[glyph], #15\n" \
        "lsr %[glyph], %[glyph], #4\n" \
        "ldrh %[low], [%[patterns], %[low], lsl #1]\n" \
        "ldrh %[glyph], [%[patterns], %[glyph], lsl #1]\n" \
        "orr %[low], %[attribute], %[low], lsl #8\n" \
        "orr %[glyph], %[attribute], %[glyph], lsl #8\n" \
        "strd " first ", " second ", [%[words]], #8\n" \
        "subs %[count], %[count], #1\n" \
        "bne 1b\n" \
        : [words] "+&r" (words), [cells] "+&r" (cells), [count] "+&r" (count), \
          [glyph] "=&r" (glyph), [attribute] "=&r" (attribute), [low] "=&r" (low) \
        : [font] "r" (font), [stride] "r" (stride), \
          [patterns] "r" (patterns), [colors] "r" (colors) \
        : "cc", "memory" \
    )
    if (text->msb_first) {
        HDMI_M33_WIDE_LOOP("%[glyph]", "%[low]");
    } else {
        HDMI_M33_WIDE_LOOP("%[low]", "%[glyph]");
    }
#undef HDMI_M33_WIDE_LOOP
#else
    const uint8_t *end = cells + count * HDMI_CELL_BYTES;
    const unsigned first_shift = text->msb_first ? 4u : 0u;
    const unsigned second_shift = first_shift ^ 4u;
#pragma GCC unroll 1
    while (cells != end) {
        const unsigned glyph = font[(unsigned)cells[0] * stride];
        const uint32_t color = colors[cells[HDMI_ATTRIBUTE_OFFSET]];
        words[0] = color | (uint32_t)patterns[(glyph >> first_shift) & 15u] << 8;
        words[1] = color | (uint32_t)patterns[(glyph >> second_shift) & 15u] << 8;
        words += 2;
        cells += HDMI_CELL_BYTES;
    }
#endif
}

static __force_inline void hdmi_render_text(unsigned line, uint8_t *output) {
    struct graphics_text_line text;
#if defined(GRAPHICS_PC_EMULATOR)
    if (!graphics_source_pc_text_line(line, &text)) {
#else
    if (!graphics_text_line(line, HDMI_ACTIVE_SCANLINES, &text)) {
#endif
        hdmi_fill_words(output, hdmi_text_solid_command(0), 320);
        return;
    }
    const unsigned state = text.blink_enabled ? (text.blink_hidden ? 3u : 1u) : 0u;
    if (state != workspace.text.attribute_state) hdmi_update_text_attributes(state);
    // A wrapped VRAM row consists of at most two spans; the API validates
    // even sizes and offsets. Byte loads also support an unaligned VRAM base.
    const unsigned first = text.wrap_at / HDMI_CELL_BYTES < text.columns
            ? (unsigned)(text.wrap_at / HDMI_CELL_BYTES) : text.columns;
    const unsigned bytes_per_cell = text.columns == 40 ? 8u : 4u;
    if (text.columns == 40) {
        hdmi_text_span_40(output, text.cells, first, &text);
        if (first != text.columns)
            hdmi_text_span_40(output + first * bytes_per_cell, text.wrap_cells,
                              text.columns - first, &text);
    } else {
        hdmi_text_span_80(output, text.cells, first, &text);
        if (first != text.columns)
            hdmi_text_span_80(output + first * bytes_per_cell, text.wrap_cells,
                              text.columns - first, &text);
    }
    // The cursor overrides the glyph, including a blinking glyph. Patch just
    // its one/two commands instead of testing the cursor in every cell.
    if (text.cursor_visible) {
        const unsigned attribute = text.cursor_column < first
                ? text.cells[text.cursor_column * HDMI_CELL_BYTES + HDMI_ATTRIBUTE_OFFSET]
                : text.wrap_cells[(text.cursor_column - first) * HDMI_CELL_BYTES + HDMI_ATTRIBUTE_OFFSET];
        const uint32_t command = hdmi_text_attribute_word(attribute, text.blink_enabled, false) |
                                 UINT32_C(0x00dddd00); // eight foreground pixels
        volatile hdmi_line_word_t *cursor =
                (volatile hdmi_line_word_t *)(output + text.cursor_column * bytes_per_cell);
        cursor[0] = command;
        if (text.columns == 40) cursor[1] = command;
    }
}

// A colour written while the DMA reads its entry can go out half old, half
// new: often an illegal TMDS code, sometimes a control token in the middle of
// a line, and receivers lose sync. So graphics_set_palette() queues the new
// entry words, and the scanline IRQ stores them first thing at the next line,
// while the DMA still sends its sync and porch (control entries only).
static uint32_t pending_words[HDMI_CTRL_BASE_INDEX][4];
static uint32_t pending_colors[(HDMI_CTRL_BASE_INDEX + 31u) / 32u]; // one bit per queued colour

// Stores up to `limit` queued colours. The caller holds hdmi_lock.
static void __time_critical_func(hdmi_store_pending_colors)(unsigned limit) {
    for (unsigned word = 0; word < count_of(pending_colors); ++word) {
        for (unsigned bit = 0; pending_colors[word] != 0u && bit < 32u; ++bit) {
            if (!(pending_colors[word] & (1u << bit))) continue;
            if (limit-- == 0u) return;
            pending_colors[word] &= ~(1u << bit);
            const unsigned index = word * 32u + bit;
            const uint32_t *words = pending_words[index];
            if (text_mode || wide_mode) { // the two pixel phases of a 16-colour entry
                workspace.wide.palette[index][0] = words[0];
                workspace.wide.palette[index][1] = words[1];
                workspace.wide.palette[index + 32u][0] = words[2];
                workspace.wide.palette[index + 32u][1] = words[3];
            } else {
                for (unsigned i = 0; i < 4u; ++i) workspace.graphics.palette[index][i] = words[i];
            }
        }
    }
}

static __force_inline uint8_t *hdmi_line_buffer(unsigned buffer) {
    return text_mode ? workspace.text.lines[buffer] :
            (wide_mode ? workspace.wide.lines[buffer] : workspace.graphics.lines[buffer]);
}

static void __time_critical_func(hdmi_prepare_line)(unsigned line, unsigned slot) {
    // The other ring slot is the one on air.
    const unsigned sending = dma_buffer_addresses[slot ^ 1u] == (uint32_t)(uintptr_t)hdmi_line_buffer(1) ? 1u : 0u;
    const bool needs_pixels = hdmi_schedule_line(&line_schedule, line,
                                                 !text_mode && !wide_mode, sending);
    uint8_t *dest = hdmi_line_buffer(line_schedule.buffer);
    dma_buffer_addresses[slot] = (uint32_t)(uintptr_t)dest;
    // Both ring slots may point at the same immutable row. The next new
    // row goes into the other buffer while DMA sends the repeated row.
    if (!needs_pixels) return;
    const bool vsync = line >= HDMI_VSYNC_FIRST_LINE &&
                       line < HDMI_VSYNC_FIRST_LINE + HDMI_VSYNC_LINES;
    if (wide_mode) {
        const uint8_t blank = HDMI_TEXT_CTRL_BASE_INDEX + (vsync ? 2u : 0u);
        hdmi_fill_line(dest, blank + 1u, 96u);
        hdmi_fill_line(dest + 96u, blank, 48u);
        hdmi_fill_line(dest + 784u, blank, 16u);
        if (line < HDMI_ACTIVE_SCANLINES) {
            graphics_source_render_line8(line, 640u, HDMI_ACTIVE_SCANLINES,
                                         dest + 144u, pc_codes);
        } else {
            hdmi_fill_line(dest + 144u, blank, 640u);
        }
        return;
    }
    const unsigned hsync = text_mode ? HDMI_TEXT_HSYNC_BYTES : HDMI_GRAPHICS_HSYNC_BYTES;
    const unsigned total = text_mode ? sizeof(workspace.text.lines[0]) :
                                       sizeof(workspace.graphics.lines[0]);
    const uint8_t blank = (text_mode ? HDMI_TEXT_CTRL_BASE_INDEX : HDMI_CTRL_BASE_INDEX) +
                          (vsync ? 2 : 0);
    if (text_mode) hdmi_fill_words(dest, hdmi_text_solid_command(blank + 1u), hsync);
    else hdmi_fill_line(dest, blank + 1u, hsync);
    if (line >= HDMI_ACTIVE_SCANLINES) {
        if (text_mode) hdmi_fill_words(dest + hsync, hdmi_text_solid_command(blank), total - hsync);
        else hdmi_fill_line(dest + hsync, blank, total - hsync);
        return;
    }
    // The renderer writes every active pixel. Fill just the porches instead
    // of writing the entire line twice (80 instead of 400 command bytes).
    const unsigned active_offset = text_mode ? HDMI_TEXT_ACTIVE_OFFSET :
                                              HDMI_GRAPHICS_ACTIVE_OFFSET;
    const unsigned active_bytes = HDMI_FRAME_WIDTH;
    if (text_mode) {
        const uint32_t command = hdmi_text_solid_command(blank);
        hdmi_fill_words(dest + hsync, command, active_offset - hsync);
        hdmi_fill_words(dest + active_offset + active_bytes, command,
                           total - active_offset - active_bytes);
    } else {
        hdmi_fill_line(dest + hsync, blank, active_offset - hsync);
        hdmi_fill_line(dest + active_offset + active_bytes, blank,
                       total - active_offset - active_bytes);
    }
    if (text_mode) {
        hdmi_render_text(line, dest + HDMI_TEXT_ACTIVE_OFFSET);
    } else {
        uint8_t *output = dest + HDMI_GRAPHICS_ACTIVE_OFFSET;
        if (graphics_source_is_pc_mode()) {
            if (source_shift_x == 0 && source_shift_y == 0 &&
                    graphics_pc_hdmi_render(graphics_source_mode(),
                                            pc_vram,
                                            line >> 1u, output)) return;
            graphics_source_render_line8(line >> 1u, HDMI_FRAME_WIDTH,
                                         HDMI_FRAME_HEIGHT, output, pc_codes);
            return;
        }
        if (graphics_framebuffer == NULL) {
            hdmi_fill_line(output, 0, HDMI_FRAME_WIDTH);
            return;
        }
        const uint8_t *source = graphics_framebuffer + (line >> 1u) * HDMI_FRAME_WIDTH;
        for (unsigned x = 0; x < HDMI_FRAME_WIDTH; ++x)
            output[x] = hdmi_color_code(source[x]);
    }
}

static void __time_critical_func(hdmi_scanline_interrupt_handler)(void) {
#if defined(PC_USB_CONSOLE)
    ++hdmi_irq_entries;
#endif
#if HDMI_PROFILE_GPIO >= 0
    gpio_put(HDMI_PROFILE_GPIO, true);
#endif
    // NVIC pending state is per-core. A reconfiguration requested by the
    // other core must not turn a stale pending IRQ into a fictitious line.
    const uint32_t irq_state = hdmi_enter();
    if (!(dma_hw->ints0 & (1u << dma_channel_control))) {
        hdmi_leave(irq_state);
#if defined(PC_USB_CONSOLE)
        ++hdmi_irq_exits;
#endif
#if HDMI_PROFILE_GPIO >= 0
        gpio_put(HDMI_PROFILE_GPIO, false);
#endif
        return;
    }
    dma_hw->ints0 = 1u << dma_channel_control;
    // The DMA now sends this line's sync and porch: store queued colours first.
    hdmi_store_pending_colors(16u);
    unsigned line = hdmi_current_scanline + 1u;
    if (line == HDMI_TOTAL_SCANLINES) line = 0;
    hdmi_current_scanline = (uint16_t)line;
#if defined(GRAPHICS_PC_EMULATOR)
    const enum graphics_mode_t mode = graphics_source_mode();
    port3DA = graphics_pc_raster_status(mode, line, false);
#endif
    unsigned next_line = line + 1u;
    if (next_line == HDMI_TOTAL_SCANLINES) next_line = 0;
    if (next_line == 0) {
#if defined(PC_USB_CONSOLE)
        ++hdmi_frames;
#endif
        graphics_text_frame_tick();
        if (pending_framebuffer != NULL) {
            __mem_fence_acquire();
            graphics_framebuffer = pending_framebuffer;
            __mem_fence_release();
            pending_framebuffer = NULL;
        }
    }
    // Control DMA has already advanced its ring pointer to the free buffer.
    const unsigned next_buffer = dma_hw->ch[dma_channel_control].read_addr ==
            (uintptr_t)&dma_buffer_addresses[0] ? 0u : 1u;
    hdmi_prepare_line(next_line, next_buffer);
#if defined(GRAPHICS_PC_EMULATOR)
    port3DA = graphics_pc_raster_status(mode, line, true);
#endif
    hdmi_leave(irq_state);
#if defined(PC_USB_CONSOLE)
    ++hdmi_irq_exits;
#endif
#if HDMI_PROFILE_GPIO >= 0
    gpio_put(HDMI_PROFILE_GPIO, false);
#endif
}

static void hdmi_stop_output(void) {
    // Caller holds hdmi_lock, which also drains an IRQ already executing on
    // the scanout core. Never enable/disable another core's NVIC here.
    dma_channel_set_irq0_enabled(dma_channel_control, false);
    // Disable chaining first so an in-flight completion cannot restart a
    // channel after it was aborted. IRQs are disabled throughout the overlay.
    const unsigned channels[4] = {dma_channel_data, dma_channel_control,
                                 dma_channel_palette_data, dma_channel_palette_control};
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned channel = channels[i];
        hw_write_masked(&dma_hw->ch[channel].ctrl_trig,
                         channel << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
                         DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    }
    dma_hw->abort = (1u << dma_channel_control) | (1u << dma_channel_data) |
                    (1u << dma_channel_palette_control) | (1u << dma_channel_palette_data);
    while (dma_hw->abort) tight_loop_contents();
    pio_sm_set_enabled(PIO_VIDEO, sm_video_output, false);
    pio_sm_set_enabled(PIO_VIDEO_ADDR, sm_address_converter, false);
    dma_channel_acknowledge_irq0(dma_channel_control);
}

static void hdmi_start_output(void) {
    // The same twelve reserved PIO words hold the glyph expander in text
    // mode or the original four-word address converter in graphics mode.
    const unsigned program_length = text_mode ? hdmi_text_program.length : 4u;
    const uint16_t *instructions = text_mode ? hdmi_text_program.instructions :
            (wide_mode ? pio_instructions_wide_converter : pio_instructions_address_converter);
    for (unsigned i = 0; i < program_length; ++i)
        PIO_VIDEO_ADDR->instr_mem[pio_program_offset_converter + i] = instructions[i];
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, pio_program_offset_converter,
                       pio_program_offset_converter + program_length - 1u);
    sm_config_set_in_shift(&config, true, text_mode, text_mode ? 24 : 32);
    sm_config_set_out_shift(&config, true, false, text_mode ? 24 : 32);
    pio_sm_init(PIO_VIDEO_ADDR, sm_address_converter,
                pio_program_offset_converter + (text_mode ? hdmi_text_offset_entry : 0u), &config);
    pio_sm_put_blocking(PIO_VIDEO_ADDR, sm_address_converter,
                         (uint32_t)(uintptr_t)&workspace >> ((text_mode || wide_mode) ? 9 : 12));
    pio_sm_exec(PIO_VIDEO_ADDR, sm_address_converter, pio_encode_pull(false, true));
    pio_sm_exec(PIO_VIDEO_ADDR, sm_address_converter,
                pio_encode_mov(text_mode ? pio_y : pio_x, pio_osr));
    pio_sm_exec(PIO_VIDEO_ADDR, sm_address_converter, pio_encode_mov(pio_isr, pio_null));

    config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, pio_program_offset_video, pio_program_offset_video + 9);
    sm_config_set_sideset_pins(&config, HDMI_PIN_CLOCK);
    sm_config_set_sideset(&config, 2, false, false);
    sm_config_set_out_pins(&config, HDMI_PIN_DATA, 6);
    sm_config_set_out_shift(&config, true, true, 30);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / 252000000.0f);
    pio_sm_init(PIO_VIDEO, sm_video_output, pio_program_offset_video, &config);

    // Begin with a complete vertical blank interval. Both buffers are valid
    // before the first DMA trigger; the first IRQ describes line 480.
    hdmi_current_scanline = HDMI_ACTIVE_SCANLINES - 1;
    line_schedule.line = UINT32_MAX;
    line_schedule.buffer = 0;
    for (unsigned i = 0; i < 2; ++i) {
        hdmi_prepare_line(HDMI_ACTIVE_SCANLINES + i, i);
    }

    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel_data);
    channel_config_set_transfer_data_size(&dma_config, text_mode ? DMA_SIZE_32 : DMA_SIZE_8);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_chain_to(&dma_config, dma_channel_control);
    channel_config_set_dreq(&dma_config, pio_get_dreq(PIO_VIDEO_ADDR, sm_address_converter, true));
    channel_config_set_high_priority(&dma_config, true);
    dma_channel_configure(dma_channel_data, &dma_config,
                          &PIO_VIDEO_ADDR->txf[sm_address_converter],
                          (const void *)(uintptr_t)dma_buffer_addresses[0],
                          text_mode ? 100 : (wide_mode ? 800 : 400), false);

    dma_config = dma_channel_get_default_config(dma_channel_control);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_ring(&dma_config, false, 3);
    channel_config_set_chain_to(&dma_config, dma_channel_data);
    channel_config_set_high_priority(&dma_config, true);
    dma_channel_configure(dma_channel_control, &dma_config,
                          &dma_hw->ch[dma_channel_data].read_addr,
                          (const void *)dma_buffer_addresses, 1, false);

    dma_config = dma_channel_get_default_config(dma_channel_palette_data);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_chain_to(&dma_config, dma_channel_palette_control);
    channel_config_set_dreq(&dma_config, pio_get_dreq(PIO_VIDEO, sm_video_output, true));
    channel_config_set_high_priority(&dma_config, true);
    dma_channel_configure(dma_channel_palette_data, &dma_config,
                          &PIO_VIDEO->txf[sm_video_output], &workspace,
                          (text_mode || wide_mode) ? 2 : 4, false);

    dma_config = dma_channel_get_default_config(dma_channel_palette_control);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_chain_to(&dma_config, dma_channel_palette_data);
    channel_config_set_dreq(&dma_config, pio_get_dreq(PIO_VIDEO_ADDR, sm_address_converter, false));
    channel_config_set_high_priority(&dma_config, true);
    dma_channel_configure(dma_channel_palette_control, &dma_config,
                          &dma_hw->ch[dma_channel_palette_data].read_addr,
                          &PIO_VIDEO_ADDR->rxf[sm_address_converter], 1, false);

    dma_channel_acknowledge_irq0(dma_channel_control);
    dma_channel_set_irq0_enabled(dma_channel_control, true);
    pio_sm_set_enabled(PIO_VIDEO_ADDR, sm_address_converter, true);
    dma_start_channel_mask((1u << dma_channel_palette_control) | (1u << dma_channel_control));
    // Pre-fill the serializer FIFO before enabling its continuously clocked SM.
    while (!pio_sm_is_tx_fifo_full(PIO_VIDEO, sm_video_output)) tight_loop_contents();
    pio_sm_set_enabled(PIO_VIDEO, sm_video_output, true);
}

void graphics_set_mode(enum graphics_mode_t mode) {
    if (initialized) {
        // Geometry changes also happen outside the active image. A class
        // change then restarts the serial pipeline (a sink may briefly relock).
        while (hdmi_current_scanline < HDMI_ACTIVE_SCANLINES) tight_loop_contents();
    }
    const uint32_t irq_state = hdmi_enter();
    graphics_source_set_mode(mode);
    const bool want_text = hdmi_wants_text();
    const bool want_wide = hdmi_wants_wide();
    if (initialized && (want_text != text_mode || want_wide != wide_mode)) {
        hdmi_stop_output();
        hdmi_store_pending_colors(HDMI_CTRL_BASE_INDEX);
        hdmi_change_workspace_mode(want_text, want_wide);
        hdmi_start_output();
    } else if (!initialized) {
        hdmi_store_pending_colors(HDMI_CTRL_BASE_INDEX);
        hdmi_change_workspace_mode(want_text, want_wide);
    }
    hdmi_leave(irq_state);
}

void graphics_wait_vblank(void) {
    if (!initialized) return;
    uint16_t previous = hdmi_current_scanline;
    while (true) {
        const uint16_t line = hdmi_current_scanline;
        if (line < previous) return;
        previous = line;
        tight_loop_contents();
    }
}

// In SRAM: it holds hdmi_lock, which the scanline IRQ needs at the start of each line.
void __time_critical_func(graphics_set_palette)(uint8_t index, uint32_t color888) {
#if defined(GRAPHICS_PC_EMULATOR)
    color888 = graphics_pc_palette_color(index, color888, false);
#endif
    uint32_t graphics_words[4], small_words[4];
    hdmi_encode_graphics_rgb(graphics_words, color888);
    hdmi_store_pixel(small_words, hdmi_text_pixel(color888, 0));
    hdmi_store_pixel(small_words + 2, hdmi_text_pixel(color888, 1));
    const uint32_t irq_state = hdmi_enter();
    hdmi_initialize_palette();
    if (index >= HDMI_CTRL_BASE_INDEX) {
        const unsigned tail = index - HDMI_CTRL_BASE_INDEX;
        tail_rgb[tail] = color888;
        hdmi_find_tail_color(tail);
        if (!wide_mode) pc_codes[index] = (uint16_t)tail_code[tail] * 0x0101u;
        hdmi_leave(irq_state);
        return;
    }
    bool queue = true;
    if (text_mode || wide_mode) {
        uint8_t *cached = workspace.text.graphics_rgb[index];
        cached[0] = (uint8_t)(color888 >> 16);
        cached[1] = (uint8_t)(color888 >> 8);
        cached[2] = (uint8_t)color888;
#if defined(GRAPHICS_PC_EMULATOR)
        queue = index < 16u; // PC text follows the palette too; both layouts share it
#else
        queue = wide_mode && index < 16u;
#endif
    }
    if (queue) {
        const uint32_t *words = text_mode || wide_mode ? small_words : graphics_words;
        for (unsigned i = 0; i < 4u; ++i) pending_words[index][i] = words[i];
        pending_colors[index / 32u] |= 1u << (index % 32u);
    }
    if (index < 16u) {
        base_rgb[index] = color888;
        for (unsigned tail = 0; tail < 4; ++tail) {
            if (tail_code[tail] == index) {
                hdmi_find_tail_color(tail);
            } else {
                const uint32_t distance = hdmi_color_distance(color888, tail_rgb[tail]);
                if (distance < tail_distance[tail]) {
                    tail_distance[tail] = distance;
                    tail_code[tail] = index;
                }
            }
            if (!wide_mode)
                pc_codes[HDMI_CTRL_BASE_INDEX + tail] = (uint16_t)tail_code[tail] * 0x0101u;
        }
    }
    hdmi_leave(irq_state);
}

void graphics_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height) {
    const uint32_t irq_state = hdmi_enter();
    graphics_source_set_buffer(buffer, width, height);
    pc_vram = (const uint32_t *)buffer;
    if (width == HDMI_FRAME_WIDTH && height == HDMI_FRAME_HEIGHT)
        graphics_framebuffer = buffer;
    hdmi_leave(irq_state);
}

void hdmi_graphics_init(void) {
    if (initialized) return;
    hdmi_lock = spin_lock_init(spin_lock_claim_unused(true));
#ifndef GRAPHICS_NO_BUILTIN_FRAMEBUFFER
    if (graphics_framebuffer == NULL) graphics_framebuffer = hdmi_primary_framebuffer;
#endif
    pending_framebuffer = NULL;
    hdmi_store_pending_colors(HDMI_CTRL_BASE_INDEX);
    hdmi_change_workspace_mode(hdmi_wants_text(), hdmi_wants_wide());
    sm_video_output = pio_claim_unused_sm(PIO_VIDEO, true);
    sm_address_converter = pio_claim_unused_sm(PIO_VIDEO_ADDR, true);
    dma_channel_control = dma_claim_unused_channel(true);
    dma_channel_data = dma_claim_unused_channel(true);
    dma_channel_palette_control = dma_claim_unused_channel(true);
    dma_channel_palette_data = dma_claim_unused_channel(true);
    // OUT PC,1 dispatches to absolute addresses 0/1, so the text program
    // explicitly reserves instructions 0..11. Serializer uses ten more.
    pio_program_offset_converter = pio_add_program(PIO_VIDEO_ADDR, &hdmi_text_program);
    pio_program_offset_video = pio_add_program(PIO_VIDEO, &pio_program_hdmi_output);

    for (unsigned i = 0; i < 2; ++i) {
        pio_gpio_init(PIO_VIDEO, HDMI_PIN_CLOCK + i);
        gpio_set_drive_strength(HDMI_PIN_CLOCK + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(HDMI_PIN_CLOCK + i, GPIO_SLEW_RATE_FAST);
    }
    for (unsigned i = 0; i < 6; ++i) {
        pio_gpio_init(PIO_VIDEO, HDMI_PIN_DATA + i);
        gpio_set_drive_strength(HDMI_PIN_DATA + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(HDMI_PIN_DATA + i, GPIO_SLEW_RATE_FAST);
    }
#if HDMI_PROFILE_GPIO >= 0
    gpio_init(HDMI_PROFILE_GPIO);
    gpio_set_dir(HDMI_PROFILE_GPIO, GPIO_OUT);
    gpio_put(HDMI_PROFILE_GPIO, false);
#endif
    pio_sm_set_pins_with_mask(PIO_VIDEO, sm_video_output,
                             3u << HDMI_PIN_CLOCK, 3u << HDMI_PIN_CLOCK);
    pio_sm_set_consecutive_pindirs(PIO_VIDEO, sm_video_output, HDMI_PIN_CLOCK, 2, true);
    pio_sm_set_consecutive_pindirs(PIO_VIDEO, sm_video_output, HDMI_PIN_DATA, 6, true);
    irq_set_exclusive_handler(VIDEO_DMA_IRQ, hdmi_scanline_interrupt_handler);
    irq_set_priority(VIDEO_DMA_IRQ, 0);
    irq_clear(VIDEO_DMA_IRQ);
    irq_set_enabled(VIDEO_DMA_IRQ, true);
    initialized = true;
    hdmi_start_output();
}

uint8_t *graphics_get_framebuffer(void) {
#ifndef GRAPHICS_NO_BUILTIN_FRAMEBUFFER
    if (graphics_framebuffer == NULL) return hdmi_primary_framebuffer;
#endif
    return (uint8_t *)graphics_framebuffer;
}

void graphics_present_framebuffer(const uint8_t *framebuffer) {
    if (!initialized) {
        graphics_framebuffer = framebuffer;
        return;
    }
    __mem_fence_release();
    pending_framebuffer = framebuffer;
    while (pending_framebuffer != NULL) tight_loop_contents();
    __mem_fence_acquire();
}

void graphics_set_bgcolor(uint32_t color888) {
    (void)color888;
}

void graphics_set_offset(int x, int y) {
    const uint32_t irq_state = hdmi_enter();
    source_shift_x = x;
    source_shift_y = y;
    graphics_source_set_offset(x, y);
    hdmi_leave(irq_state);
}
