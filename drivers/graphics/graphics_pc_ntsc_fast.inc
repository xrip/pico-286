// The old PC NTSC pixel loops, with the new driver's split phase tables.
// Each packed source group writes fixed phase positions directly. The
// generic decoder handles uncommon shifted/wrapped rows and new modes.
typedef struct __attribute__((may_alias)) {
    uint32_t even, odd;
} pc_ntsc_color_pair_t;

static inline __attribute__((always_inline)) void pc_ntsc_text40(
        const uint32_t *source, uint32_t *pairs, const uint32_t *even,
        const uint32_t *odd, const uint8_t *font, unsigned blink_mask,
        bool hide_blink, int cursor) {
    for (unsigned column = 0; column < 40u; ++column) {
        const uint8_t ch = (uint8_t)source[0];
        const uint8_t attr = (uint8_t)source[1];
        source += 2;
        unsigned glyph = font[(unsigned)ch * 8u];
        if (hide_blink && (attr & 128u)) glyph = 0;
        if ((int)column == cursor) glyph = 255u;
        const unsigned fg = attr & 15u;
        const unsigned bg = (attr & blink_mask) >> 4;
        const uint32_t fg0 = even[fg], fg1 = odd[fg];
        const uint32_t bg0 = even[bg], bg1 = odd[bg];
        pairs[0] = glyph & 1u ? fg0 : bg0;
        pairs[1] = glyph & 2u ? fg1 : bg1;
        pairs[2] = glyph & 4u ? fg0 : bg0;
        pairs[3] = glyph & 8u ? fg1 : bg1;
        pairs[4] = glyph & 16u ? fg0 : bg0;
        pairs[5] = glyph & 32u ? fg1 : bg1;
        pairs[6] = glyph & 64u ? fg0 : bg0;
        pairs[7] = glyph & 128u ? fg1 : bg1;
        pairs += 8;
    }
}

bool __time_critical_func(graphics_source_render_ntsc_fast)(
        unsigned row, uint16_t *output, const uint32_t *even,
        const uint32_t *odd, const uint16_t *luma) {
    graphics_pc_view_t view;
    graphics_source_pc_view(&view);
    if (view.vram == NULL || view.shift_x != 0 || view.shift_y != 0 || row >= 200u)
        return false;

    graphics_pc_state_t state;
    graphics_pc_snapshot(&state);
    const uint32_t *source;
    uint32_t *pairs = (uint32_t *)output;
    unsigned base;
    switch (view.mode) {
        case TEXTMODE_40x25_BW:
        case TEXTMODE_40x25_COLOR:
        case TEXTMODE_80x25_BW:
        case TEXTMODE_80x25_COLOR: {
            const unsigned columns = view.mode <= TEXTMODE_40x25_COLOR ? 40u : 80u;
            const unsigned text_row = row >> 3;
            const unsigned glyph_line = row & 7u;
            base = (0x8000u + (state.vram_offset << 1) + text_row * columns * 2u) & 65535u;
            if (base + columns * 2u > 65536u) return false;
            source = view.vram + base;
            const bool hide_blink = state.blink_mask == 0x7fu && state.cursor_phase;
            const int cursor = pc_cursor_on_line(&state, text_row, glyph_line, 240u)
                    ? state.cursor_x : -1;
            const uint8_t *font = font_8x8 + glyph_line;
            if (columns == 40u) {
                if (!hide_blink && cursor < 0 && state.blink_mask == 255u) {
                    pc_ntsc_text40(source, pairs, even, odd, font, 255u, false, -1);
                } else {
                    pc_ntsc_text40(source, pairs, even, odd, font, state.blink_mask, hide_blink, cursor);
                }
            } else {
                for (unsigned column = 0; column < 80u; ++column) {
                    const uint8_t ch = (uint8_t)source[0];
                    const uint8_t attr = (uint8_t)source[1];
                    source += 2;
                    unsigned glyph = font[(unsigned)ch * 8u];
                    if (hide_blink && (attr & 128u)) glyph = 0;
                    if ((int)column == cursor) glyph = 255u;
                    const uint32_t fg = luma[attr & 15u];
                    const uint32_t bg = luma[(attr & state.blink_mask) >> 4];
                    const uint32_t colors[4] = {
                        bg | bg << 16, fg | bg << 16,
                        bg | fg << 16, fg | fg << 16
                    };
                    pairs[0] = colors[glyph & 3u];
                    pairs[1] = colors[glyph >> 2 & 3u];
                    pairs[2] = colors[glyph >> 4 & 3u];
                    pairs[3] = colors[glyph >> 6];
                    pairs += 4;
                }
            }
            return true;
        }
        case COMPOSITE_160x200x16:
        case COMPOSITE_160x200x16_force:
        case CGA_320x200x4:
        case CGA_320x200x4_BW:
            base = (0x8000u + (state.vram_offset << 1) +
                    (row >> 1) * 80u + ((row & 1u) << 13)) & 65535u;
            if (base + 80u > 65536u) return false;
            source = view.vram + base;
#pragma GCC unroll 2
            for (unsigned x = 0; x < 80u; ++x) {
                const uint8_t bits = (uint8_t)*source++;
                pairs[0] = even[bits >> 6];
                pairs[1] = odd[bits >> 4 & 3u];
                pairs[2] = even[bits >> 2 & 3u];
                pairs[3] = odd[bits & 3u];
                pairs += 4;
            }
            return true;
        case CGA_640x200x2: {
            base = (0x8000u + (state.vram_offset << 1) +
                    (row >> 1) * 80u + ((row & 1u) << 13)) & 65535u;
            if (base + 80u > 65536u) return false;
            source = view.vram + base;
            const uint32_t fg = luma[state.foreground & 15u], bg = luma[0];
            const uint32_t colors[4] = {
                bg | bg << 16, bg | fg << 16,
                fg | bg << 16, fg | fg << 16
            };
#pragma GCC unroll 2
            for (unsigned x = 0; x < 80u; ++x) {
                const uint8_t bits = (uint8_t)*source++;
                pairs[0] = colors[bits >> 6];
                pairs[1] = colors[bits >> 4 & 3u];
                pairs[2] = colors[bits >> 2 & 3u];
                pairs[3] = colors[bits & 3u];
                pairs += 4;
            }
            return true;
        }
        case TGA_160x200x16: {
            base = (state.tga_offset + (row >> 1) * 80u + ((row & 1u) << 13)) & 65535u;
            if (base + 80u > 65536u) return false;
            source = view.vram + base;
            // A Tandy pixel owns both phase pairs. Put them next to each
            // other once per row so the loop can use paired loads/stores,
            // with the color-zero replacement already folded into entry 0.
            pc_ntsc_color_pair_t colors[16];
#pragma GCC unroll 16
            for (unsigned i = 0; i < 16u; ++i) {
                const unsigned color = i ? i : state.foreground & 15u;
                colors[i] = (pc_ntsc_color_pair_t){even[color], odd[color]};
            }
            pc_ntsc_color_pair_t *color_output = (pc_ntsc_color_pair_t *)output;
#pragma GCC unroll 2
            for (unsigned x = 0; x < 80u; ++x) {
                const uint8_t bits = (uint8_t)*source++;
                color_output[0] = colors[bits >> 4];
                color_output[1] = colors[bits & 15u];
                color_output += 2;
            }
            return true;
        }
        case TGA_320x200x16:
            base = (state.tga_offset + ((row & 3u) << 13) + (row >> 2) * 160u) & 65535u;
            if (base + 160u > 65536u) return false;
            source = view.vram + base;
#pragma GCC unroll 8
            for (unsigned x = 0; x < 160u; ++x) {
                const uint8_t bits = (uint8_t)*source++;
                pairs[0] = even[bits >> 4];
                pairs[1] = odd[bits & 15u];
                pairs += 2;
            }
            return true;
        case EGA_320x200x16x4:
            source = view.vram + row * 40u;
#pragma GCC unroll 2
            for (unsigned x = 0; x < 40u; ++x) {
                const uint32_t planes = *source++;
                const uint32_t pixels = pc_pack_planes(planes);
                pairs[0] = even[pixels >> 28];
                pairs[1] = odd[pixels >> 24 & 15u];
                pairs[2] = even[pixels >> 20 & 15u];
                pairs[3] = odd[pixels >> 16 & 15u];
                pairs[4] = even[pixels >> 12 & 15u];
                pairs[5] = odd[pixels >> 8 & 15u];
                pairs[6] = even[pixels >> 4 & 15u];
                pairs[7] = odd[pixels & 15u];
                pairs += 8;
            }
            return true;
        case VGA_320x200x256x4:
            source = view.vram + row * 80u;
#pragma GCC unroll 8
            for (unsigned x = 0; x < 80u; ++x) {
                const uint32_t pixels = *source++;
                pairs[0] = even[(uint8_t)pixels];
                pairs[1] = odd[(uint8_t)(pixels >> 8)];
                pairs[2] = even[(uint8_t)(pixels >> 16)];
                pairs[3] = odd[pixels >> 24];
                pairs += 4;
            }
            return true;
        case VGA_320x200x256:
            source = view.vram + row * 320u;
#pragma GCC unroll 2
            for (unsigned x = 0; x < 160u; ++x) {
                pairs[0] = even[(uint8_t)source[0]];
                pairs[1] = odd[(uint8_t)source[1]];
                source += 2;
                pairs += 2;
            }
            return true;
        default:
            return false;
    }
}
