#pragma once

// Pure packing helpers, also exercised by the host tests. One physical pixel
// occupies two words; the serializer consumes only the low 30 bits of each.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum { HDMI_WORKSPACE_BYTES = 4896, HDMI_PALETTE_COLORS = 252 };

struct hdmi_line_schedule {
    unsigned line;
    unsigned buffer;
};

// A repeated 320-pixel row reuses its DMA address, without copying 400 bytes.
// Return true only when the selected buffer needs fresh pixel data.
static inline bool hdmi_schedule_line(struct hdmi_line_schedule *state,
                                      unsigned line, bool doubled_rows) {
    const bool same = line == state->line;
    const bool repeat = doubled_rows && line < 480u && (line & 1u) &&
                        line == state->line + 1u;
    if (!same && !repeat) state->buffer ^= 1u;
    state->line = line;
    return !same && !repeat;
}

// The old allocation is shared by three mutually exclusive transport layouts.
// Keep this definition and the in-place conversion helpers visible to host
// tests: a mirrored test layout would miss real offset/overlap regressions.
union hdmi_workspace {
    struct {
        uint32_t palette[256][4];
        uint8_t lines[2][400];
    } graphics;
    struct {
        uint32_t palette[64][2];
        uint8_t lines[2][800];
        uint8_t reserved[1536];
        uint8_t graphics_rgb[256][3];
    } wide;
    struct {
        uint32_t palette[64][2];    // 32 colours/controls per even/odd phase
        uint32_t attributes[256];   // packed foreground/background command bits
        uint16_t glyphs[2][256];    // both font bit orders, including pixel phase
        uint16_t wide_glyphs[2][16]; // four font bits doubled to eight pixels
        uint8_t lines[2][400];      // 100 commands, eight physical pixels each
        uint32_t attribute_state;
        uint8_t reserved[220];     // keep the palette cache at its old offset
        uint8_t graphics_rgb[256][3];
    } text;
};
_Static_assert(sizeof(union hdmi_workspace) == HDMI_WORKSPACE_BYTES, "HDMI RAM budget changed");
_Static_assert(offsetof(union hdmi_workspace, text.graphics_rgb) == 3648,
               "Recheck in-place palette conversion order after changing the layout");
_Static_assert(offsetof(union hdmi_workspace, wide.graphics_rgb) ==
               offsetof(union hdmi_workspace, text.graphics_rgb),
               "Wide and text transport share the saved graphics palette");

static inline uint16_t hdmi_graphics_symbol(uint8_t value) {
    unsigned ones = 0;
    for (unsigned i = 0; i < 8; ++i) ones += (value >> i) & 1u;
    const unsigned xnor = ones > 4u || (ones == 4u && !(value & 1u));
    uint16_t q = value & 1u;
    unsigned previous = q;
    for (unsigned i = 1; i < 8; ++i) {
        previous ^= (value >> i) & 1u;
        previous ^= xnor;
        q |= previous << i;
    }
    return q | (xnor ? 0x200u : 0x100u);
}

static inline uint64_t hdmi_pack_symbols(uint16_t red, uint16_t green,
                                          uint16_t blue) {
    uint64_t packed = 0;
    for (unsigned bit = 0; bit < 10; ++bit) {
        const unsigned offset = bit < 5 ? bit * 6 : 32 + (bit - 5) * 6;
        unsigned r = (red >> bit) & 1u;
        unsigned g = (green >> bit) & 1u;
        unsigned b = (blue >> bit) & 1u;
#if HDMI_PIN_invert_diffpairs
        r ^= 1u; g ^= 1u; b ^= 1u;
#endif
        r |= (r ^ 1u) << 1;
        g |= (g ^ 1u) << 1;
        b |= (b ^ 1u) << 1;
#if HDMI_PIN_RGB_notBGR
        packed |= (uint64_t)(r << 4 | g << 2 | b) << offset;
#else
        packed |= (uint64_t)(b << 4 | g << 2 | r) << offset;
#endif
    }
    return packed;
}

static inline void hdmi_store_pixel(uint32_t *words, uint64_t packed) {
    words[0] = (uint32_t)packed;
    words[1] = (uint32_t)(packed >> 32);
}

static inline void hdmi_encode_graphics_rgb(uint32_t *words, uint32_t rgb) {
    const uint64_t first = hdmi_pack_symbols(
            hdmi_graphics_symbol((uint8_t)(rgb >> 16)),
            hdmi_graphics_symbol((uint8_t)(rgb >> 8)),
            hdmi_graphics_symbol((uint8_t)rgb));
    hdmi_store_pixel(words, first);
    // Original low-bit modulation: the paired symbols have zero net disparity.
    hdmi_store_pixel(words + 2, first ^ UINT64_C(0x0003ffffffffffff));
}

static inline uint32_t hdmi_decode_graphics_rgb(const uint32_t *words) {
    const uint64_t packed = (uint64_t)words[0] | (uint64_t)words[1] << 32;
    unsigned channels[3] = {0, 0, 0}; // Physical B/G/R or R/G/B pin order.
    for (unsigned bit = 0; bit < 10; ++bit) {
        const unsigned offset = bit < 5 ? bit * 6 : 32 + (bit - 5) * 6;
        const unsigned sample = (unsigned)(packed >> offset) & 0x3fu;
        for (unsigned channel = 0; channel < 3; ++channel) {
            unsigned data = (sample >> (channel * 2)) & 1u;
#if HDMI_PIN_invert_diffpairs
            data ^= 1u;
#endif
            channels[channel] |= data << bit;
        }
    }
    for (unsigned channel = 0; channel < 3; ++channel) {
        const unsigned q = channels[channel];
        // Inverse of hdmi_graphics_symbol(), not the receiver's TMDS decode:
        // this recovers even the original least significant input colour bit.
        channels[channel] = (q ^ (q << 1) ^ ((q & 0x100u) ? 0 : 0xfeu)) & 0xffu;
    }
#if HDMI_PIN_RGB_notBGR
    return channels[2] << 16 | channels[1] << 8 | channels[0];
#else
    return channels[0] << 16 | channels[1] << 8 | channels[2];
#endif
}

static inline void hdmi_cache_graphics_palette(union hdmi_workspace *storage) {
    // Destination (3648+3*i) is above source (16*i). Descending order
    // preserves every not-yet-decoded graphics entry despite overlap.
    for (unsigned i = HDMI_PALETTE_COLORS; i--;) {
        const uint32_t rgb = hdmi_decode_graphics_rgb(storage->graphics.palette[i]);
        storage->text.graphics_rgb[i][0] = (uint8_t)(rgb >> 16);
        storage->text.graphics_rgb[i][1] = (uint8_t)(rgb >> 8);
        storage->text.graphics_rgb[i][2] = (uint8_t)rgb;
    }
}

static inline void hdmi_restore_graphics_palette(union hdmi_workspace *storage) {
    // Destination (16*i) is below source (3648+3*i). Ascending order
    // restores the palette without destroying a still-needed cache byte.
    for (unsigned i = 0; i < HDMI_PALETTE_COLORS; ++i) {
        const uint8_t *cached = storage->text.graphics_rgb[i];
        const uint32_t rgb = (uint32_t)cached[0] << 16 |
                             (uint32_t)cached[1] << 8 | cached[2];
        hdmi_encode_graphics_rgb(storage->graphics.palette[i], rgb);
    }
}

// The twelve-instruction PIO expander consumes these command fields directly.
// Bits 24..26 must stay zero: the background path discards the low 24 bits.
static inline uint32_t hdmi_text_attribute_word(unsigned attribute,
                                               bool blink, bool hidden) {
    const unsigned background = (attribute >> 4) & (blink ? 7u : 15u);
    const unsigned foreground = blink && hidden && (attribute & 128u)
            ? background : attribute & 15u;
    return (uint32_t)background << 27 | foreground;
}

static inline uint16_t hdmi_text_glyph_pattern(unsigned glyph, bool msb_first) {
    unsigned pattern = 0x8888u; // every odd physical pixel uses TMDS phase one
    for (unsigned pixel = 0; pixel < 8; ++pixel) {
        const unsigned bit = msb_first ? 7u - pixel : pixel;
        pattern |= ((glyph >> bit) & 1u) << (pixel * 2u);
    }
    return (uint16_t)pattern;
}

static inline uint16_t hdmi_text_wide_pattern(unsigned nibble, bool msb_first) {
    unsigned pattern = 0x8888u;
    for (unsigned pixel = 0; pixel < 8; ++pixel) {
        const unsigned bit = msb_first ? 3u - pixel / 2u : pixel / 2u;
        pattern |= ((nibble >> bit) & 1u) << (pixel * 2u);
    }
    return (uint16_t)pattern;
}

static inline uint32_t hdmi_text_solid_command(unsigned color) {
    return (uint32_t)color << 27 | UINT32_C(0x00888800) | color;
}

static inline void hdmi_initialize_text_lookups(union hdmi_workspace *storage) {
    for (unsigned attribute = 0; attribute < 256; ++attribute)
        storage->text.attributes[attribute] =
                hdmi_text_attribute_word(attribute, false, false);
    for (unsigned order = 0; order < 2; ++order) {
        for (unsigned glyph = 0; glyph < 256; ++glyph)
            storage->text.glyphs[order][glyph] = hdmi_text_glyph_pattern(glyph, order != 0);
        for (unsigned nibble = 0; nibble < 16; ++nibble)
            storage->text.wide_glyphs[order][nibble] = hdmi_text_wide_pattern(nibble, order != 0);
    }
    storage->text.attribute_state = 0;
}

static inline uint64_t hdmi_text_pixel(uint32_t rgb, unsigned phase) {
    // PicoDVI's full-resolution RGB222 construction: every even symbol has
    // disparity -4 and every odd symbol +4, independently of its colour.
    // Component levels decode to 5/4, 80/81, 175/174, 250/251 (even/odd).
    // This is an approximation of the four CGA component levels, not a
    // stateful encoder for arbitrary VGA DAC values.
    static const uint16_t symbols[2][4] = {
        {0x103, 0x130, 0x230, 0x203},
        {0x1fc, 0x1cf, 0x2cf, 0x2fc}
    };
    return hdmi_pack_symbols(symbols[phase][((rgb >> 16) & 255u) / 85u],
                             symbols[phase][((rgb >> 8) & 255u) / 85u],
                             symbols[phase][(rgb & 255u) / 85u]);
}
