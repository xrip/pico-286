// On-flash GM wavetable soundbank format, shared by the offline packer
// (dls_pack.c, writes it) and the real-time engine (general-midi engine, reads
// it directly from flash via XIP).
//
// Design constraints:
//  * Position-independent: every cross-reference is a byte offset from the blob
//    start, so the blob can be .incbin'd at any flash address.
//  * Little-endian, fixed-width fields.
//  * NATURAL alignment, NOT packed. Cortex-M0+ (ARMv6-M) faults on unaligned
//    LDR/LDRH, so struct fields are ordered largest-first, every struct size is
//    a multiple of 4, and the packer aligns each table + the PCM block to 4.
//    The blob itself must be placed at a 4-byte-aligned flash address.
//  * PCM is mono int16; sample N lives at (pcm_base)[wave.pcm_offset + N].
#pragma once

#include <stdint.h>

// DLS drum-kit marker in gm_instrument_t.bank (high bit). Defined here so the
// real-time engine needs only gm_bank.h, not the host RIFF parser; dls_parse.inl
// defines the same value, which is a harmless identical redefinition.
#ifndef DLS_DRUM_BANK
#define DLS_DRUM_BANK 0x80000000u
#endif

#define GM_BANK_MAGIC0 'G'
#define GM_BANK_MAGIC1 'M'
#define GM_BANK_MAGIC2 'W'
#define GM_BANK_MAGIC3 'B'
#define GM_BANK_VERSION 4u  // v4: region tremolo (LFO->gain) + EG2 pitch envelope

// Fixed-point scales.
#define GM_Q16 16          // gain, envelope coeffs, sustain: Q16 (65536 = 1.0)
#define GM_STEP_Q 16       // playback step: Q16.16 frames-per-output-sample
#define GM_ONE_Q16 65536

typedef struct {
    uint32_t pcm_offset;    // first sample index into the PCM block
    uint32_t frame_count;   // total mono frames
    uint32_t base_step_q16; // wave_rate / output_rate in Q16.16 (pre-pitch)
} gm_wave_t;                // 12 bytes

// Region flags
#define GM_RGN_LOOPED        0x01u  // loop_start/loop_length are valid
#define GM_RGN_ROOT_FROM_NOTE 0x02u // no wsmp: engine uses played note as root
#define GM_RGN_HAS_LFO       0x04u  // lfo_* fields drive vibrato and/or tremolo
#define GM_RGN_HAS_EG2       0x08u  // eg2_* fields drive the pitch envelope

typedef struct {
    uint32_t gain_q16;          // baked attenuation gain (Q16)
    uint32_t attack_step_q16;   // linear env increment per sample (Q16)
    uint32_t decay_coef_q16;    // per-sample decay multiplier (Q16, <1.0)
    uint32_t release_coef_q16;  // per-sample release multiplier (Q16, <1.0)
    uint32_t sustain_q16;       // sustain level (Q16)
    uint32_t loop_start;        // frames (valid if GM_RGN_LOOPED)
    uint32_t loop_length;       // frames (valid if GM_RGN_LOOPED)
    uint32_t lfo_phase_inc;     // vibrato LFO phase step per sample (full turn = 2^32)
    uint32_t lfo_delay;         // samples before the LFO starts
    int32_t  lfo_depth_q8;      // base vibrato depth, cents in Q8 (cents*256)
    int32_t  lfo_mod_depth_q8;  // extra depth scaled by mod wheel, cents Q8
    int32_t  lfo_gain_depth_q8; // tremolo depth, log2-amplitude cents Q8 (0 = none)
    uint32_t eg2_attack_step_q16;  // EG2 (pitch env): linear increment per sample
    uint32_t eg2_decay_coef_q16;   // per-sample decay multiplier (Q16, <1.0)
    uint32_t eg2_release_coef_q16; // per-sample release multiplier (Q16, <1.0)
    uint32_t eg2_sustain_q16;      // EG2 sustain level (Q16)
    int32_t  eg2_pitch_cents;      // EG2 -> pitch depth at full level, cents
    int16_t  fine_cents;        // pitch fine tune, cents
    uint16_t wave_index;        // index into wave table
    uint8_t  key_low;
    uint8_t  key_high;
    uint8_t  vel_low;
    uint8_t  vel_high;
    uint8_t  root_key;          // unity note (ignored if GM_RGN_ROOT_FROM_NOTE)
    uint8_t  key_group;         // DLS exclusive group (0 = none)
    int8_t   pan;               // region pan offset from DLS art1, MIDI units (-64..63)
    uint8_t  flags;             // GM_RGN_* bits
} gm_region_t;                  // 80 bytes

typedef struct {
    uint32_t bank;          // DLS bank number; high bit (0x80000000) = drum bank
    uint32_t region_first;  // index into region table
    uint16_t program;       // 0..127
    uint16_t region_count;
} gm_instrument_t;          // 12 bytes

typedef struct {
    char     magic[4];          // 'G','M','W','B'
    uint32_t version;
    uint32_t output_rate;       // engine sample rate the coeffs were baked for
    uint32_t instrument_count;
    uint32_t region_count;
    uint32_t wave_count;
    uint32_t off_instruments;   // byte offset to gm_instrument_t[]
    uint32_t off_regions;       // byte offset to gm_region_t[]
    uint32_t off_waves;         // byte offset to gm_wave_t[]
    uint32_t off_pcm;           // byte offset to int16 PCM block (4-aligned)
    uint32_t pcm_samples;       // number of int16 samples in the PCM block
} gm_bank_header_t;             // 44 bytes

_Static_assert(sizeof(gm_wave_t) == 12, "gm_wave_t layout");
_Static_assert(sizeof(gm_region_t) == 80, "gm_region_t layout");
_Static_assert(sizeof(gm_instrument_t) == 12, "gm_instrument_t layout");
_Static_assert(sizeof(gm_bank_header_t) == 44, "gm_bank_header_t layout");

// Typed views into a loaded blob. All pointers reference flash/blob memory.
typedef struct {
    const gm_bank_header_t *header;
    const gm_instrument_t  *instruments;
    const gm_region_t      *regions;
    const gm_wave_t        *waves;
    const int16_t          *pcm;
} gm_bank_view_t;

static inline int gm_bank_view(const void *blob, gm_bank_view_t *out) {
    const uint8_t *base = (const uint8_t *) blob;
    const gm_bank_header_t *h = (const gm_bank_header_t *) base;
    if (h->magic[0] != GM_BANK_MAGIC0 || h->magic[1] != GM_BANK_MAGIC1 ||
        h->magic[2] != GM_BANK_MAGIC2 || h->magic[3] != GM_BANK_MAGIC3 ||
        h->version != GM_BANK_VERSION) {
        return 0;
    }
    out->header = h;
    out->instruments = (const gm_instrument_t *) (base + h->off_instruments);
    out->regions = (const gm_region_t *) (base + h->off_regions);
    out->waves = (const gm_wave_t *) (base + h->off_waves);
    out->pcm = (const int16_t *) (base + h->off_pcm);
    return 1;
}
