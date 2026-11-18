// Compile this decoder for each hardware pixel type. The mapping is folded
// into packed-pixel decoding, so PC scanout has no index staging/copy pass.
// There is no run-time output-format branch or per-pixel function pointer.
static inline __attribute__((always_inline)) void PC_FN(emit)(
        PC_PIXEL **output, uint8_t color, unsigned repeat, PC_PIXEL *origin,
        const PC_PALETTE *palette) {
    (void)origin; (void)palette;
    do {
        *(*output)++ = PC_MAP(color);
    } while (--repeat);
}

static inline __attribute__((always_inline)) void PC_FN(fill)(
        PC_PIXEL *output, unsigned width, PC_PIXEL *origin,
        const PC_PALETTE *palette) {
    if (width) PC_FN(emit)(&output, 0, width, origin, palette);
}

static inline __attribute__((always_inline)) void PC_FN(text_line)(
        const uint32_t *vram, const graphics_pc_state_t *state,
        enum graphics_mode_t mode, unsigned y, unsigned width, unsigned height,
        PC_PIXEL *output, const PC_PALETTE *palette) {
    PC_PIXEL *const origin = output;
    (void)origin; (void)palette;
    const unsigned columns = mode <= TEXTMODE_40x25_COLOR ? 40u : 80u;
    const unsigned cell_height = height == 480 ? 16u : 8u;
    const unsigned line = height == 480 ? y : y >> 1;
    const unsigned text_row = line / cell_height;
    const unsigned glyph_line = line % cell_height;
    const unsigned start = 0x8000u + (state->vram_offset << 1) + text_row * columns * 2u;
    const uint8_t *font = height == 480 ? font_8x16 : font_8x8;
    const bool cursor_row = pc_cursor_on_line(state, text_row, glyph_line, height);
    // Two pixels in each store, from the four pairs of the cell's two colors.
    // A row has long runs of one attribute, so the pairs are made again only when it changes.
    typedef struct { PC_PIXEL pixel[2]; } __attribute__((may_alias)) pair_t;
    pair_t pairs[4]; // [glyph bits 0 and 1]
    unsigned colors = ~0u; // the foreground and background of pairs[]
    for (unsigned column = 0; column < columns; ++column) {
        const uint8_t character = (uint8_t)pc_word(vram, start + column * 2u);
        const uint8_t attr = (uint8_t)pc_word(vram, start + column * 2u + 1u);
        uint8_t glyph = font[(unsigned)character * cell_height + glyph_line];
        if (state->blink_mask == 0x7fu && (attr & 0x80u) && state->cursor_phase)
            glyph = 0;
        if (cursor_row && column == state->cursor_x) glyph = 0xffu;
        const uint8_t foreground = attr & 15u;
        const uint8_t background = (attr & state->blink_mask) >> 4;
        if (width == 320 && columns == 80) {
            // Four samples per glyph. OR the pair so thin strokes remain
            // legible on a 320-pixel TFT instead of dropping alternate strokes.
            const PC_PIXEL foreground_pixel = PC_MAP(foreground);
            const PC_PIXEL background_pixel = PC_MAP(background);
            for (unsigned pair = 0; pair < 4; ++pair) {
                *output++ = glyph & 3u ? foreground_pixel : background_pixel;
                glyph >>= 2;
            }
            continue;
        }
        if ((unsigned)(foreground | background << 4) != colors) {
            colors = foreground | background << 4;
            const PC_PIXEL f = PC_MAP(foreground), b = PC_MAP(background);
            pairs[0] = (pair_t){{b, b}};
            pairs[1] = (pair_t){{f, b}};
            pairs[2] = (pair_t){{b, f}};
            pairs[3] = (pair_t){{f, f}};
        }
        pair_t *out = (pair_t *)output;
        if (width == 640 && columns == 40) { // each glyph bit is two pixels
            for (unsigned bit = 0; bit < 8; ++bit) {
                *out++ = pairs[glyph & 1u ? 3u : 0u];
                glyph >>= 1;
            }
        } else {
            *out++ = pairs[glyph & 3u];
            *out++ = pairs[(glyph >> 2) & 3u];
            *out++ = pairs[(glyph >> 4) & 3u];
            *out++ = pairs[glyph >> 6];
        }
        output = (PC_PIXEL *)out;
    }
}

static inline __attribute__((always_inline)) void PC_FN(render_line)(
        enum graphics_mode_t mode, const uint32_t *vram,
        const graphics_pc_state_t *state, unsigned y, unsigned width,
        unsigned height, PC_PIXEL *output, const PC_PALETTE *palette) {
    PC_PIXEL *const origin = output;
    const unsigned low_y = y >> 1;
    unsigned limit = 400;
    switch (mode) {
        case HERC_640x480x2: case VGA_640x480x2: case VGA_640x480x16:
        case VGA_320x200x256x4: // the CRTC sets the height
            limit = 480; break;
        case HERC_640x480x2_90: limit = 348; break;
        case EGA_640x350x16x4: limit = 350; break;
        default: break;
    }
    if (vram == NULL || y >= limit) {
        PC_FN(emit)(&output, 0, width, origin, palette);
        return;
    }
    switch (mode) {
        case TEXTMODE_40x25_BW: case TEXTMODE_40x25_COLOR:
        case TEXTMODE_80x25_BW: case TEXTMODE_80x25_COLOR:
            PC_FN(text_line)(vram, state, mode, y, width, height, output, palette);
            return;
        case CGA_320x200x4: case CGA_320x200x4_BW: {
            const unsigned base = 0x8000u + (state->vram_offset << 1) +
                    (low_y >> 1) * 80u + ((low_y & 1u) << 13);
            const unsigned repeat = width == 640 ? 2u : 1u;
            for (unsigned byte = 0; byte < 80; ++byte) {
                const uint8_t bits = (uint8_t)pc_word(vram, base + byte);
                PC_FN(emit)(&output, bits >> 6, repeat, origin, palette);
                PC_FN(emit)(&output, (bits >> 4) & 3u, repeat, origin, palette);
                PC_FN(emit)(&output, (bits >> 2) & 3u, repeat, origin, palette);
                PC_FN(emit)(&output, bits & 3u, repeat, origin, palette);
            }
            return;
        }
        case CGA_640x200x2: case HERC_640x480x2:
        case HERC_640x480x2_90: case VGA_640x480x2: {
            unsigned base;
            uint8_t foreground = 15;
            if (mode == CGA_640x200x2) {
                base = 0x8000u + (state->vram_offset << 1) +
                        (low_y >> 1) * 80u + ((low_y & 1u) << 13);
                foreground = state->foreground;
            } else if (mode == VGA_640x480x2) {
                base = y * 80u;
            } else {
                base = (y & 3u) * 8192u + (y >> 2) * 90u +
                        (mode == HERC_640x480x2_90 ? 5u : 0u);
            }
            const unsigned step = width == 640 ? 1u : 2u;
            for (unsigned byte = 0; byte < 80; ++byte) {
                const uint8_t bits = (uint8_t)pc_word(vram, base + byte);
                for (int bit = 7; bit >= 0; bit -= step)
                    PC_FN(emit)(&output, ((bits >> bit) & 1u) ? foreground : 0, 1, origin, palette);
            }
            return;
        }
        case COMPOSITE_160x200x16: case COMPOSITE_160x200x16_force:
        case TGA_160x200x16: case TGA_320x200x16: case TGA_640x200x16: {
            unsigned bank, offset = 0, bytes, repeat;
            const bool narrow = mode != TGA_320x200x16 && mode != TGA_640x200x16;
            if (narrow) {
                bank = state->tga_offset + ((low_y & 1u) << 13);
                offset = pc_bank_offset(state, (low_y >> 1) * 80u);
                bytes = 80;
                repeat = width == 640 ? 4u : 2u;
            } else if (mode == TGA_320x200x16) {
                bank = state->tga_offset + (low_y & 3u) * 8192u;
                offset = pc_bank_offset(state, (low_y >> 2) * 160u);
                bytes = 160;
                repeat = width == 640 ? 2u : 1u;
            } else {
                bank = low_y * 320u;
                bytes = 320;
                repeat = 1;
            }
            // A line that crosses the end of its 8K bank goes on at the bank start: two runs.
            unsigned address = bank + offset;
            unsigned run = offset + bytes > 0x2000u ? 0x2000u - offset : bytes;
            for (unsigned rest = bytes; rest; rest -= run, run = rest, address = bank)
            for (const unsigned end = address + run; address < end; ++address) {
                const uint8_t pixels = (uint8_t)pc_word(vram, address);
                uint8_t left = pixels >> 4;
                uint8_t right = pixels & 15u;
                if (mode == TGA_160x200x16) {
                    if (!left) left = state->foreground;
                    if (!right) right = state->foreground;
                }
                PC_FN(emit)(&output, left, repeat, origin, palette);
                if (mode != TGA_640x200x16 || width == 640)
                    PC_FN(emit)(&output, right, repeat, origin, palette);
            }
            return;
        }
        case EGA_320x200x16x4: case EGA_640x200x16x4:
        case EGA_640x350x16x4: case VGA_640x480x16: {
            const bool narrow = mode == EGA_320x200x16x4;
            const unsigned words = narrow ? 40u : 80u;
            const unsigned source_y = mode == EGA_640x350x16x4 || mode == VGA_640x480x16 ? y : low_y;
            const unsigned base = graphics_pc_ega_line(source_y);
            const unsigned step = !narrow && width == 320 ? 2u : 1u;
            const unsigned repeat = narrow && width == 640 ? 2u : 1u;
            for (unsigned word = 0; word < words; ++word) {
                const uint32_t packed = pc_pack_planes(pc_word(vram, base + word));
                for (int bit = 7; bit >= 0; bit -= step)
                    PC_FN(emit)(&output, (packed >> (bit * 4)) & 15u, repeat, origin, palette);
            }
            return;
        }
        case VGA_320x200x256x4: {
            const unsigned repeat = width == 640 ? 2u : 1u;
            unsigned words;
            const unsigned base = graphics_pc_vga_x_line(low_y, &words);
            const unsigned border = (80u - words) * 2u * repeat; // pixels on each side of a narrow picture
            if (border) PC_FN(emit)(&output, 0, border, origin, palette);
            for (unsigned word = 0; word < words; ++word) {
                const uint32_t pixels = pc_word(vram, base + word);
                PC_FN(emit)(&output, (uint8_t)pixels, repeat, origin, palette);
                PC_FN(emit)(&output, (uint8_t)(pixels >> 8), repeat, origin, palette);
                PC_FN(emit)(&output, (uint8_t)(pixels >> 16), repeat, origin, palette);
                PC_FN(emit)(&output, (uint8_t)(pixels >> 24), repeat, origin, palette);
            }
            if (border) PC_FN(emit)(&output, 0, border, origin, palette);
            return;
        }
        case VGA_320x200x256: {
            const unsigned repeat = width == 640 ? 2u : 1u;
            for (unsigned pixel = 0; pixel < 320; ++pixel)
                PC_FN(emit)(&output, (uint8_t)pc_word(vram, low_y * 320u + pixel), repeat, origin, palette);
            return;
        }
        default:
            PC_FN(emit)(&output, 0, width, origin, palette);
            return;
    }
}

static inline __attribute__((always_inline)) void PC_FN(render)(
        unsigned row, unsigned width, unsigned height, PC_PIXEL *indices, const PC_PALETTE *palette) {
    const graphics_source_state_t source = source_snapshot();
    const enum graphics_mode_t mode = source.mode;
    const uint32_t *vram = (const uint32_t *)source.buffer;
    const int shift_x = source.shift_x;
    const int shift_y = source.shift_y;
    if ((width != 320 && width != 640) || (height != 240 && height != 480)) {
        PC_FN(fill)(indices, width, indices, palette);
        return;
    }
    const int y = (int)(height == 480 ? row : row * 2u) - shift_y * 2;
    if (row >= height || y < 0 || y >= 480) {
        PC_FN(fill)(indices, width, indices, palette);
        return;
    }
    graphics_pc_state_t state;
    graphics_pc_snapshot(&state);
    const int shift = width == 640 ? shift_x * 2 : shift_x;
    PC_FN(render_line)(mode, vram, &state, (unsigned)y, width, height, indices, palette);
    if (shift >= (int)width || shift <= -(int)width) {
        PC_FN(fill)(indices, width, indices, palette);
    } else if (shift > 0) {
        memmove(indices + shift, indices, (width - (unsigned)shift) * sizeof(*indices));
        PC_FN(fill)(indices, (unsigned)shift, indices, palette);
    } else if (shift < 0) {
        memmove(indices, indices - shift, (width + shift) * sizeof(*indices));
        PC_FN(fill)(indices + width + shift, (unsigned)-shift, indices, palette);
    }
}
