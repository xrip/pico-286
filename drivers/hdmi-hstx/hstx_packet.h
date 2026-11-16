#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    HSTX_PACKET_FRONT = 16,
    HSTX_PACKET_SYNC = 96,
    HSTX_PACKET_BACK = 48,
    HSTX_PACKET_PIXELS = 640,
    HSTX_PACKET_HEADER_WORDS = 9,
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
// 30-bit sync symbol consumed twice (RAW_N_SHIFTS=2, RAW_SHIFT=0).
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

// Repeated rows keep their complete packet. Rotate on actual writes so every
// return, including a repeated row, retains at least four callback lifetimes.
static inline unsigned hstx_packet_next_slot(unsigned slot) {
    return (slot + 1u) & 3u;
}
