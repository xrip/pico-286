#pragma once
// HDMI data island packets for HSTX: TERC4 and BCH encoding, ACR and InfoFrames.
// From rh1tech/frank-quake2 drivers/hdmi/hstx_packet.h, itself from pico_hdmi by
// fliperama86 (public domain / Unlicense, see LICENSE.pico_hdmi). Names are
// hstx_di_* here: hstx_packet.h already has this driver's line packets.

#include <stdbool.h>
#include <stdint.h>

enum {
    HSTX_DI_GUARD_BAND = 2,
    HSTX_DI_PREAMBLE = 8,
    HSTX_DI_PACKET = 32,
    HSTX_DI_WORDS = HSTX_DI_GUARD_BAND + HSTX_DI_PACKET + HSTX_DI_GUARD_BAND // one word per symbol
};

typedef struct {
    uint8_t header[4];       // 3 bytes and the BCH parity
    uint8_t subpacket[4][8]; // 7 bytes and the BCH parity each
} hstx_di_packet_t;

typedef struct {
    uint32_t words[HSTX_DI_WORDS];
} hstx_data_island_t;

typedef struct {
    int16_t left;
    int16_t right;
} hstx_di_sample_t;

extern const uint16_t hstx_ter_c4[16];
extern const uint8_t hstx_bch_table[256];

void hstx_di_packet_set_acr(hstx_di_packet_t *packet, uint32_t n, uint32_t cts);
void hstx_di_packet_set_audio_infoframe(hstx_di_packet_t *packet, uint32_t sample_rate, uint8_t channels,
                                        uint8_t bits_per_sample);
void hstx_di_packet_set_avi_infoframe(hstx_di_packet_t *packet, uint8_t vic, uint8_t pixel_repetition);
int hstx_di_packet_set_audio_samples(hstx_di_packet_t *packet, const hstx_di_sample_t *samples, int num_samples,
                                     int frame_count);

// `vsync`/`hsync`: the island is inside that sync pulse.
void hstx_encode_data_island(hstx_data_island_t *out, const hstx_di_packet_t *packet, bool vsync, bool hsync);
