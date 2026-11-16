#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(HDMI_SOUND)
#include "hstx_data_island.h"
#endif

enum {
    HSTX_PACKET_FRONT = 16,
    HSTX_PACKET_SYNC = 96,
    HSTX_PACKET_BACK = 48,
    HSTX_PACKET_PIXELS = 640,
#if defined(HDMI_SOUND)
    HSTX_PACKET_ISLAND = 7, // commands[] index of the data island
    HSTX_PACKET_HEADER_WORDS = 56,
#else
    HSTX_PACKET_HEADER_WORDS = 9,
#endif
    HSTX_PACKET_PIXEL_WORDS = HSTX_PACKET_PIXELS / 2,
    HSTX_PACKET_WORDS = HSTX_PACKET_HEADER_WORDS + HSTX_PACKET_PIXEL_WORDS,
    HSTX_CMD_RAW = 0x0000,
    HSTX_CMD_RAW_REPEAT = 0x1000,
    HSTX_CMD_TMDS = 0x2000,
    HSTX_CMD_NOP = 0xf000,
    HSTX_SYNC_V0_H0 = 0x354u | (0x354u << 10) | (0x354u << 20),
    HSTX_SYNC_V0_H1 = 0x0abu | (0x354u << 10) | (0x354u << 20),
    HSTX_SYNC_V1_H0 = 0x154u | (0x354u << 10) | (0x354u << 20),
    HSTX_SYNC_V1_H1 = 0x2abu | (0x354u << 10) | (0x354u << 20)
};

// Every line has the same transfer length and FIFO consumption schedule.
// Active payload words hold two BGR555 pixels. Blank payload words hold a
// 30-bit sync symbol consumed twice (RAW_N_SHIFTS=2, RAW_SHIFT=0); with
// HDMI_SOUND once (RAW_N_SHIFTS=1), as each data island word is one symbol.
typedef struct {
    uint32_t commands[HSTX_PACKET_HEADER_WORDS];
    union {
        uint16_t pixels[HSTX_PACKET_PIXELS];
        uint32_t pairs[HSTX_PACKET_PIXEL_WORDS];
    } payload;
} hstx_video_packet_t;

_Static_assert(offsetof(hstx_video_packet_t, payload) == HSTX_PACKET_HEADER_WORDS * sizeof(uint32_t),
               "HSTX payload must immediately follow its header");
_Static_assert(sizeof(hstx_video_packet_t) == HSTX_PACKET_WORDS * sizeof(uint32_t),
               "HSTX packet must be contiguous DMA words");

#if defined(HDMI_SOUND)
// HDMI 1.4 5.2: a data island in the sync pulse, and a video preamble and
// guard band before the pixels. The island words are left for hstx_video.c,
// which writes them for each line.
static inline void hstx_packet_init(hstx_video_packet_t *packet, bool active, bool vsync) {
    enum {
        CTRL_00 = 0x354u,
        CTRL_01 = 0x0abu,
        ISLAND_PREAMBLE = CTRL_01 << 10 | CTRL_01 << 20,
        VIDEO_PREAMBLE = CTRL_01 << 10 | CTRL_00 << 20,
        VIDEO_GUARD_BAND = 0x2ccu | 0x133u << 10 | 0x2ccu << 20,
        VIDEO_PREAMBLE_SYMBOLS = 8,
        VIDEO_GUARD_BAND_SYMBOLS = 2,
        BACK_CONTROL = HSTX_PACKET_BACK - VIDEO_PREAMBLE_SYMBOLS - VIDEO_GUARD_BAND_SYMBOLS
    };
    const uint32_t idle = vsync ? HSTX_SYNC_V0_H1 : HSTX_SYNC_V1_H1;
    const uint32_t sync = vsync ? HSTX_SYNC_V0_H0 : HSTX_SYNC_V1_H0;
    uint32_t *command = packet->commands;
    *command++ = HSTX_CMD_RAW_REPEAT | HSTX_PACKET_FRONT;
    *command++ = idle;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | HSTX_DI_PREAMBLE;
    *command++ = (sync & 0x3ffu) | ISLAND_PREAMBLE;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW | HSTX_DI_WORDS;
    command += HSTX_DI_WORDS;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | (HSTX_PACKET_SYNC - HSTX_DI_PREAMBLE - HSTX_DI_WORDS);
    *command++ = sync;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | BACK_CONTROL;
    *command++ = idle;
    *command++ = HSTX_CMD_NOP;
    *command++ = HSTX_CMD_RAW_REPEAT | VIDEO_PREAMBLE_SYMBOLS;
    *command++ = active ? (idle & 0x3ffu) | VIDEO_PREAMBLE : idle;
    *command++ = HSTX_CMD_NOP;
    if (active) {
        *command++ = HSTX_CMD_RAW_REPEAT | VIDEO_GUARD_BAND_SYMBOLS;
        *command++ = VIDEO_GUARD_BAND;
        *command++ = HSTX_CMD_TMDS | HSTX_PACKET_PIXELS;
    } else {
        // One symbol per payload word: the rest of the line comes first.
        *command++ = HSTX_CMD_RAW_REPEAT | (VIDEO_GUARD_BAND_SYMBOLS + HSTX_PACKET_PIXELS - HSTX_PACKET_PIXEL_WORDS);
        *command++ = idle;
        *command++ = HSTX_CMD_RAW | HSTX_PACKET_PIXEL_WORDS;
    }
    for (unsigned i = 0; i < HSTX_PACKET_PIXEL_WORDS; ++i) {
        packet->payload.pairs[i] = active ? 0u : idle;
    }
}

static inline void hstx_packet_set_island(hstx_video_packet_t *packet, const uint32_t *island) {
    for (unsigned i = 0; i < HSTX_DI_WORDS; ++i) {
        packet->commands[HSTX_PACKET_ISLAND + i] = island[i];
    }
}
#else
static inline void hstx_packet_init(hstx_video_packet_t *packet, bool active, bool vsync) {
    const uint32_t idle = vsync ? HSTX_SYNC_V0_H1 : HSTX_SYNC_V1_H1;
    const uint32_t sync = vsync ? HSTX_SYNC_V0_H0 : HSTX_SYNC_V1_H0;
    packet->commands[0] = HSTX_CMD_RAW_REPEAT | HSTX_PACKET_FRONT;
    packet->commands[1] = idle;
    packet->commands[2] = HSTX_CMD_NOP;
    packet->commands[3] = HSTX_CMD_RAW_REPEAT | HSTX_PACKET_SYNC;
    packet->commands[4] = sync;
    packet->commands[5] = HSTX_CMD_NOP;
    packet->commands[6] = HSTX_CMD_RAW_REPEAT | HSTX_PACKET_BACK;
    packet->commands[7] = idle;
    packet->commands[8] = (active ? HSTX_CMD_TMDS : HSTX_CMD_RAW) | HSTX_PACKET_PIXELS;
    for (unsigned i = 0; i < HSTX_PACKET_PIXEL_WORDS; ++i) {
        packet->payload.pairs[i] = active ? 0u : idle;
    }
}
#endif

// Repeated rows keep their complete packet. Rotate on actual writes so every
// return, including a repeated row, retains at least four callback lifetimes.
static inline unsigned hstx_packet_next_slot(unsigned slot) {
    return (slot + 1u) & 3u;
}
