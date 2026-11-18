// Native PC rows: decode each VRAM word once and write complete pixel pairs.
// The caller has already checked row bounds and zero software offsets.
// PC_FAST_TYPE/PALETTE/PAIR and MAP0/1 specialize the hardware pixel format.
static inline __attribute__((always_inline)) void PC_FAST_FN(pair)(
        PC_FAST_TYPE **out, unsigned a, unsigned b, const PC_FAST_PALETTE *palette) {
    (*out)[0] = PC_FAST_MAP(a);
    (*out)[1] = PC_FAST_MAP(b);
    *out += 2;
}

static inline __attribute__((always_inline)) bool PC_FAST_FN(render)(
        unsigned row, unsigned width, unsigned height, PC_FAST_TYPE *output,
        const PC_FAST_PALETTE *palette) {
    graphics_pc_view_t view;
    graphics_source_pc_view(&view);
    if (view.vram == NULL || view.shift_x || view.shift_y ||
        (height != 240 && height != 480) || row >= height) return false;
    if (width != 320 && width != 640) return false;
    const unsigned y = height == 480 ? row : row * 2u;
    const unsigned low_y = y >> 1;
    const enum graphics_mode_t mode = view.mode;
    unsigned base, words;
    bool source_wide = true;
    unsigned foreground = 15;
    graphics_pc_state_t state;
    switch (mode) {
        case CGA_320x200x4: case CGA_320x200x4_BW:
        case CGA_640x200x2:
            if (low_y >= 200) return false;
            graphics_pc_snapshot(&state);
            base = 0x8000u + (state.vram_offset << 1) +
                    (low_y >> 1) * 80u + ((low_y & 1u) << 13);
            words = 80;
            source_wide = mode == CGA_640x200x2;
            foreground = state.foreground;
            break;
        case HERC_640x480x2: case HERC_640x480x2_90:
            if (y >= (mode == HERC_640x480x2_90 ? 348u : 480u)) return false;
            base = (y & 3u) * 8192u + (y >> 2) * 90u +
                    (mode == HERC_640x480x2_90 ? 5u : 0u);
            words = 80;
            break;
        case VGA_640x480x2:
            if (y >= 480) return false;
            base = y * 80;
            words = 80;
            break;
        case TGA_160x200x16: case COMPOSITE_160x200x16:
        case COMPOSITE_160x200x16_force: case TGA_320x200x16:
            if (low_y >= 200) return false;
            graphics_pc_snapshot(&state);
            if (mode == TGA_320x200x16) {
                words = 160;
                base = pc_bank_offset(&state, (low_y >> 2) * 160u);
                if (base + words > 0x2000u) return false; // the line wraps inside its bank
                base += state.tga_offset + (low_y & 3u) * 8192u;
            } else {
                words = 80;
                base = pc_bank_offset(&state, (low_y >> 1) * 80u);
                if (base + words > 0x2000u) return false;
                base += state.tga_offset + ((low_y & 1u) << 13);
            }
            foreground = state.foreground;
            source_wide = false;
            break;
        case TGA_640x200x16:
            if (low_y >= 200) return false;
            base = low_y * 320;
            words = 320;
            break;
        case EGA_320x200x16x4: case EGA_640x200x16x4:
            if (low_y >= 200) return false;
            source_wide = mode != EGA_320x200x16x4;
            words = source_wide ? 80 : 40;
            base = graphics_pc_ega_line(low_y);
            break;
        case EGA_640x350x16x4: case VGA_640x480x16:
            if (y >= (mode == EGA_640x350x16x4 ? 350u : 480u)) return false;
            base = graphics_pc_ega_line(y);
            words = 80;
            break;
        case VGA_320x200x256:
            if (low_y >= 200) return false;
            words = 320;
            base = low_y * words;
            source_wide = false;
            break;
        case VGA_320x200x256x4:
            base = graphics_pc_vga_x_line(low_y, &words);
            source_wide = false;
            break;
        default: return false;
    }
    // The 640-pixel form of a low-resolution row is uncommon (HSTX and VGA
    // repeat it in hardware). Keep it in the general renderer.
    if (!source_wide && width != 320) return false;
    base &= 65535u;
    if (base + words > 65536u) return false;
    const uint32_t *source = view.vram + base;
    switch (mode) {
        case CGA_320x200x4: case CGA_320x200x4_BW: {
            PC_FAST_PAIR pairs[16];
            for (unsigned a = 0; a < 4; ++a) {
                const PC_FAST_PAIR left = PC_FAST_MAP(a);
                pairs[a * 4] = left | (PC_FAST_PAIR)PC_FAST_MAP(0) << (8u * sizeof(PC_FAST_TYPE));
                pairs[a * 4 + 1] = left | (PC_FAST_PAIR)PC_FAST_MAP(1) << (8u * sizeof(PC_FAST_TYPE));
                pairs[a * 4 + 2] = left | (PC_FAST_PAIR)PC_FAST_MAP(2) << (8u * sizeof(PC_FAST_TYPE));
                pairs[a * 4 + 3] = left | (PC_FAST_PAIR)PC_FAST_MAP(3) << (8u * sizeof(PC_FAST_TYPE));
            }
            for (unsigned i = 0; i < 80; ++i) {
                const uint8_t bits = (uint8_t)*source++;
                PC_FAST_PAIR packed = pairs[bits >> 4];
                __builtin_memcpy(output, &packed, sizeof packed); output += 2;
                packed = pairs[bits & 15];
                __builtin_memcpy(output, &packed, sizeof packed); output += 2;
            }
            return true;
        }
        case CGA_640x200x2: case HERC_640x480x2:
        case HERC_640x480x2_90: case VGA_640x480x2: {
            const PC_FAST_PAIR lo0 = PC_FAST_MAP(0);
            const PC_FAST_PAIR lo1 = PC_FAST_MAP(foreground);
            const PC_FAST_PAIR hi0 = (PC_FAST_PAIR)PC_FAST_MAP(0) << (8u * sizeof(PC_FAST_TYPE));
            const PC_FAST_PAIR hi1 = (PC_FAST_PAIR)PC_FAST_MAP(foreground) << (8u * sizeof(PC_FAST_TYPE));
            const PC_FAST_PAIR pair[4] = {lo0 | hi0, lo0 | hi1, lo1 | hi0, lo1 | hi1};
            if (width == 640) {
                for (unsigned i = 0; i < 80; ++i) {
                    const uint8_t bits = (uint8_t)*source++;
                    PC_FAST_PAIR packed = pair[bits >> 6];
                    __builtin_memcpy(output, &packed, sizeof packed); output += 2;
                    packed = pair[(bits >> 4) & 3];
                    __builtin_memcpy(output, &packed, sizeof packed); output += 2;
                    packed = pair[(bits >> 2) & 3];
                    __builtin_memcpy(output, &packed, sizeof packed); output += 2;
                    packed = pair[bits & 3];
                    __builtin_memcpy(output, &packed, sizeof packed); output += 2;
                }
            } else {
                for (unsigned i = 0; i < 80; ++i) {
                    const uint8_t bits = (uint8_t)*source++;
                    PC_FAST_PAIR packed = pair[((bits >> 6) & 2) | ((bits >> 5) & 1)];
                    __builtin_memcpy(output, &packed, sizeof packed); output += 2;
                    packed = pair[((bits >> 2) & 2) | ((bits >> 1) & 1)];
                    __builtin_memcpy(output, &packed, sizeof packed); output += 2;
                }
            }
            return true;
        }
        case TGA_160x200x16: case COMPOSITE_160x200x16:
        case COMPOSITE_160x200x16_force:
            if (mode == TGA_160x200x16 && foreground) {
                for (unsigned i = 0; i < 80; ++i) {
                    const uint8_t bits = (uint8_t)*source++;
                    const unsigned a = bits >> 4 ? bits >> 4 : foreground;
                    const unsigned b = bits & 15 ? bits & 15 : foreground;
                    PC_FAST_FN(pair)(&output, a, a, palette);
                    PC_FAST_FN(pair)(&output, b, b, palette);
                }
            } else {
                for (unsigned i = 0; i < 80; ++i) {
                    const uint8_t bits = (uint8_t)*source++;
                    PC_FAST_FN(pair)(&output, bits >> 4, bits >> 4, palette);
                    PC_FAST_FN(pair)(&output, bits & 15, bits & 15, palette);
                }
            }
            return true;
        case TGA_320x200x16:
            #pragma GCC unroll 4
            for (unsigned i = 0; i < 160; ++i) {
                const uint8_t bits = (uint8_t)*source++;
                PC_FAST_FN(pair)(&output, bits >> 4, bits & 15, palette);
            }
            return true;
        case TGA_640x200x16:
            if (width == 640) {
                #pragma GCC unroll 4
                for (unsigned i = 0; i < 320; ++i) {
                    const uint8_t bits = (uint8_t)*source++;
                    PC_FAST_FN(pair)(&output, bits >> 4, bits & 15, palette);
                }
            } else {
                for (unsigned i = 0; i < 160; ++i) {
                    const uint8_t a = (uint8_t)*source++;
                    const uint8_t b = (uint8_t)*source++;
                    PC_FAST_FN(pair)(&output, a >> 4, b >> 4, palette);
                }
            }
            return true;
        case EGA_320x200x16x4: case EGA_640x200x16x4:
        case EGA_640x350x16x4: case VGA_640x480x16:
            if (!source_wide || width == 640) {
                for (unsigned i = 0; i < words; ++i) {
                    const uint32_t pixels = pc_pack_planes(*source++);
                    PC_FAST_FN(pair)(&output, pixels >> 28, (pixels >> 24) & 15, palette);
                    PC_FAST_FN(pair)(&output, (pixels >> 20) & 15, (pixels >> 16) & 15, palette);
                    PC_FAST_FN(pair)(&output, (pixels >> 12) & 15, (pixels >> 8) & 15, palette);
                    PC_FAST_FN(pair)(&output, (pixels >> 4) & 15, pixels & 15, palette);
                }
            } else {
                for (unsigned i = 0; i < words; ++i) {
                    const uint32_t pixels = pc_pack_planes(*source++);
                    PC_FAST_FN(pair)(&output, pixels >> 28, (pixels >> 20) & 15, palette);
                    PC_FAST_FN(pair)(&output, (pixels >> 12) & 15, (pixels >> 4) & 15, palette);
                }
            }
            return true;
        case VGA_320x200x256x4: {
            const unsigned border = 80 - words; // pixel pairs on each side of a narrow picture
            for (unsigned i = 0; i < border; ++i) PC_FAST_FN(pair)(&output, 0, 0, palette);
            #pragma GCC unroll 4
            for (unsigned i = 0; i < words; ++i) {
                const uint32_t pixels = *source++;
                PC_FAST_FN(pair)(&output, pixels & 255, (pixels >> 8) & 255, palette);
                PC_FAST_FN(pair)(&output, (pixels >> 16) & 255, pixels >> 24, palette);
            }
            for (unsigned i = 0; i < border; ++i) PC_FAST_FN(pair)(&output, 0, 0, palette);
            return true;
        }
        case VGA_320x200x256:
            for (unsigned i = 0; i < 160; ++i) {
                const unsigned a = *source++ & 255;
                const unsigned b = *source++ & 255;
                PC_FAST_FN(pair)(&output, a, b, palette);
            }
            return true;
        default: return false;
    }
}
