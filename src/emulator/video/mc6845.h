#ifndef PC_MC6845_H
#define PC_MC6845_H

#include <stdint.h>
#include <string.h>

enum {
    MC6845_STORAGE_COUNT = 32,
    MC6845_OUTPUT_LINES = 480,
    MC6845_BEAM_LINES = 525,
};

typedef struct mc6845_state_s {
    uint8_t registers[MC6845_STORAGE_COUNT];
    uint8_t index;

    /* Geometry is cached on register writes; beam state moves once per output line. */
    uint16_t start_address;
    uint16_t cursor_address;
    uint16_t cursor_x;
    uint16_t cursor_y;
    uint16_t displayed_scanlines;
    uint16_t total_scanlines;
    uint16_t vertical_sync_start;
    uint16_t vertical_sync_end;
    uint16_t beam_scanline;
    uint16_t horizontal_total;
    int16_t vertical_position;
    int16_t horizontal_position;
    uint8_t horizontal_displayed;
    uint8_t horizontal_sync_start;
    uint8_t horizontal_sync_width;
    uint8_t vertical_displayed;
    uint8_t character_height;
    uint8_t character_height_shift;
    uint8_t beam_phase;
    uint8_t video_ii_registers;
} mc6845_state_s;

typedef struct mc6845_scanline_s {
    uint16_t logical_y;
    uint16_t row;
    uint8_t raster;
    uint8_t repeat_previous;
    uint8_t displayed;
} mc6845_scanline_s;

static inline void mc6845_recompute_addresses(mc6845_state_s *state) {
    state->start_address =
            (uint16_t) ((state->registers[12] & 0x3F) << 8) | state->registers[13];
    state->cursor_address =
            (uint16_t) ((state->registers[14] & 0x3F) << 8) | state->registers[15];

    if (state->horizontal_displayed) {
        const uint16_t cursor_offset =
                (state->cursor_address - state->start_address) & 0x3FFF;
        state->cursor_x = cursor_offset % state->horizontal_displayed;
        state->cursor_y = cursor_offset / state->horizontal_displayed;
    } else {
        state->cursor_x = UINT16_MAX;
        state->cursor_y = UINT16_MAX;
    }
}

static inline void mc6845_recompute_geometry(mc6845_state_s *state) {
    state->horizontal_total = (uint16_t) state->registers[0] + 1u;
    state->horizontal_displayed = state->registers[1];
    state->horizontal_sync_start = state->registers[2];
    state->horizontal_sync_width = state->registers[3] & 0x0F;
    if (!state->horizontal_sync_width) state->horizontal_sync_width = 16;
    state->vertical_displayed = state->video_ii_registers
            ? state->registers[6] : state->registers[6] & 0x7F;
    state->character_height = (state->registers[9] & 0x1F) + 1;
    switch (state->character_height) {
        case 1: state->character_height_shift = 0; break;
        case 2: state->character_height_shift = 1; break;
        case 4: state->character_height_shift = 2; break;
        case 8: state->character_height_shift = 3; break;
        case 16: state->character_height_shift = 4; break;
        case 32: state->character_height_shift = 5; break;
        default: state->character_height_shift = UINT8_MAX; break;
    }
    state->displayed_scanlines =
            (uint16_t) state->vertical_displayed * state->character_height;
    const uint16_t vertical_total_rows = (uint16_t) (state->video_ii_registers
            ? state->registers[4] : state->registers[4] & 0x7F) + 1u;
    state->total_scanlines = (uint16_t) (vertical_total_rows * state->character_height +
            (state->registers[5] & 0x1F));
    state->vertical_sync_start =
            (uint16_t) (state->registers[7] & 0x7F) * state->character_height;
    uint8_t vertical_sync_width = state->registers[3] >> 4;
    if (!vertical_sync_width) vertical_sync_width = 16;
    state->vertical_sync_end = state->vertical_sync_start + vertical_sync_width;
    if (state->beam_scanline >= state->total_scanlines) {
        state->beam_scanline = 0;
        state->beam_phase = 0;
    }
    const uint8_t horizontal_sync_reference = state->horizontal_displayed > 80 ? 94
            : state->horizontal_displayed <= 40 ? 45 : 90;
    state->horizontal_position = (int16_t) horizontal_sync_reference -
            state->horizontal_sync_start;
    uint8_t vertical_sync_reference = state->registers[7] & 0x7F;
    const uint8_t vertical_sync = state->registers[7] & 0x7F;
    if (state->vertical_displayed == 25 && state->character_height == 8 &&
        vertical_sync >= 26 && vertical_sync <= 30)
        vertical_sync_reference = 28;
    else if (state->vertical_displayed == 30 && state->character_height == 8 &&
             vertical_sync >= 28 && vertical_sync <= 32)
        vertical_sync_reference = 30;
    else if (state->vertical_displayed == 100 && state->character_height == 2 &&
             vertical_sync >= 110 && vertical_sync <= 114)
        vertical_sync_reference = 112;
    state->vertical_position = (int16_t) (vertical_sync_reference -
            vertical_sync) * state->character_height;
}

static inline void mc6845_recompute(mc6845_state_s *state) {
    mc6845_recompute_geometry(state);
    mc6845_recompute_addresses(state);
}

static inline void mc6845_reset(mc6845_state_s *state) {
    static const uint8_t cga_80x25[16] = {
            0x71, 0x50, 0x5A, 0x0A,
            0x1F, 0x06, 0x19, 0x1C,
            0x02, 0x07, 0x06, 0x07,
            0x00, 0x00, 0x00, 0x00,
    };

    memset(state, 0, sizeof(*state));
    memcpy(state->registers, cga_80x25, sizeof(cga_80x25));
    mc6845_recompute(state);
}

static inline void mc6845_set_cga_mode(mc6845_state_s *state, uint8_t video_mode) {
    mc6845_reset(state);

    if (video_mode == 0 || video_mode == 1 || video_mode == 4 || video_mode == 5 ||
        video_mode == 8 || video_mode == 9) {
        state->registers[0] = 0x38;
        state->registers[1] = 0x28;
        state->registers[2] = 0x2D;
    }

    if (video_mode >= 4 && video_mode <= 10) {
        state->registers[4] = 0x7F;
        state->registers[6] = 0x64;
        state->registers[7] = 0x70;
        state->registers[9] = 0x01;
    } else if (video_mode == 11) {
        /* Video II advances through one linear 320-byte row per scanline. */
        state->video_ii_registers = 1;
        state->registers[4] = 0xFF;
        state->registers[5] = 0x06;
        state->registers[6] = 0xC8;
        state->registers[7] = 0xE0;
        state->registers[9] = 0x00;
    }

    mc6845_recompute(state);
}

static inline void mc6845_set_video_ii(mc6845_state_s *state, uint8_t enabled) {
    enabled = enabled != 0;
    if (state->video_ii_registers == enabled) return;
    state->video_ii_registers = enabled;
    mc6845_recompute(state);
}

static inline void mc6845_write(mc6845_state_s *state, uint8_t index, uint8_t value) {
    if (index >= MC6845_STORAGE_COUNT) return;

    state->registers[index] = value;
    if (index <= 7 || index == 9) {
        mc6845_recompute_geometry(state);
        if (index == 1) mc6845_recompute_addresses(state);
    } else if (index >= 12 && index <= 15) {
        mc6845_recompute_addresses(state);
    }
}

static inline uint16_t mc6845_output_line(uint16_t output_y,
                                          uint8_t *repeat_previous) {
    *repeat_previous = output_y & 1u;
    return output_y >> 1;
}

static inline uint8_t mc6845_beam_status(const mc6845_state_s *state) {
    uint8_t vertical_sync;
    if (state->vertical_sync_end <= state->total_scanlines) {
        vertical_sync = state->beam_scanline >= state->vertical_sync_start &&
                state->beam_scanline < state->vertical_sync_end;
    } else {
        vertical_sync = state->beam_scanline >= state->vertical_sync_start ||
                state->beam_scanline < state->vertical_sync_end - state->total_scanlines;
    }
    return (uint8_t) (state->beam_phase | (vertical_sync << 3));
}

static inline uint8_t mc6845_advance_beam(mc6845_state_s *state) {
    state->beam_phase ^= 1u;
    if (state->beam_phase) return 0;
    if (++state->beam_scanline < state->total_scanlines) return 0;
    state->beam_scanline = 0;
    return 1;
}

static inline mc6845_scanline_s mc6845_scanline(const mc6845_state_s *state,
                                                 uint16_t output_y) {
    mc6845_scanline_s line;
    const uint8_t character_height = state->character_height ? state->character_height : 1;

    /* IBM CGA has no usable interlaced address generation; R8 does not alter scanout. */
    const int32_t logical_y = (int32_t) mc6845_output_line(
            output_y, &line.repeat_previous) - state->vertical_position;
    line.displayed = logical_y >= 0 && logical_y < state->displayed_scanlines;
    line.logical_y = line.displayed ? (uint16_t) logical_y : 0;
    if (state->character_height_shift != UINT8_MAX) {
        line.row = line.logical_y >> state->character_height_shift;
        line.raster = line.logical_y & (character_height - 1);
    } else {
        line.row = line.logical_y / character_height;
        line.raster = (uint8_t) (line.logical_y - line.row * character_height);
    }
    return line;
}

static inline uint8_t mc6845_graphics_line(const mc6845_state_s *state,
                                           uint16_t output_y,
                                           uint16_t *logical_y,
                                           uint8_t *repeat_previous) {
    const int32_t positioned_y = (int32_t) mc6845_output_line(
            output_y, repeat_previous) - state->vertical_position;
    if (positioned_y < 0 || positioned_y >= state->displayed_scanlines) return 0;
    *logical_y = (uint16_t) positioned_y;
    return 1;
}

static inline uint8_t mc6845_cursor_raster_visible(const mc6845_state_s *state,
                                                    uint8_t raster,
                                                    uint8_t blink_on) {
    const uint8_t cursor_mode = (state->registers[10] >> 5) & 3;
    if (cursor_mode == 1 || (cursor_mode >= 2 && !blink_on)) return 0;

    const uint8_t start = state->registers[10] & 0x1F;
    const uint8_t end = state->registers[11] & 0x1F;
    return start <= end ? raster >= start && raster <= end
                        : raster >= start || raster <= end;
}

static inline uint16_t mc6845_text_byte_offset(const mc6845_state_s *state,
                                               uint16_t row,
                                               uint8_t columns) {
    return (uint16_t) (((uint32_t) state->start_address * 2u +
                        (uint32_t) row * columns * 2u) & 0x3FFFu);
}

static inline uint16_t mc6845_two_bank_offset(const mc6845_state_s *state,
                                              uint16_t y,
                                              uint16_t bytes_per_line) {
    const uint16_t row_offset = (uint16_t) (((uint32_t) state->start_address * 2u +
                                             (uint32_t) (y >> 1) * bytes_per_line) & 0x1FFFu);
    return row_offset | (uint16_t) ((y & 1) << 13);
}

static inline uint16_t mc6845_four_bank_offset(const mc6845_state_s *state,
                                               uint16_t y,
                                               uint16_t bytes_per_line) {
    const uint16_t row_offset = (uint16_t) (((uint32_t) state->start_address * 2u +
                                             (uint32_t) (y >> 2) * bytes_per_line) & 0x1FFFu);
    return row_offset | (uint16_t) ((y & 3) << 13);
}

static inline uint16_t mc6845_linear_offset(const mc6845_state_s *state,
                                            uint16_t y,
                                            uint16_t bytes_per_line) {
    return (uint16_t) ((uint32_t) state->start_address * 2u +
                       (uint32_t) y * bytes_per_line);
}

#endif
