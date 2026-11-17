// Shared RIFF DLS parser + DLS-domain conversions.
// Included (not linked) by both gm_dls_player.c (golden reference) and
// dls_pack.c (offline soundbank packer) so the parsing logic has a single
// source of truth. All symbols are static; include exactly once per TU.
#pragma once

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DLS_DRUM_BANK 0x80000000u

#define CONN_SRC_NONE 0x000
#define CONN_SRC_LFO 0x001
#define CONN_SRC_KEYONVELOCITY 0x002
#define CONN_SRC_KEYNUMBER 0x003
#define CONN_SRC_EG1 0x004
#define CONN_SRC_EG2 0x005
#define CONN_SRC_PITCHWHEEL 0x006
#define CONN_SRC_CC1 0x081
#define CONN_SRC_CC7 0x087
#define CONN_SRC_CC10 0x08a
#define CONN_SRC_CC11 0x08b
#define CONN_SRC_RPN0 0x100
#define CONN_DST_ATTENUATION 0x001
#define CONN_DST_PITCH 0x003
#define CONN_DST_PAN 0x004
#define CONN_DST_CHORUS 0x080
#define CONN_DST_REVERB 0x081
#define CONN_DST_LFO_FREQUENCY 0x104
#define CONN_DST_LFO_STARTDELAY 0x105
#define CONN_DST_EG1_ATTACKTIME 0x206
#define CONN_DST_EG1_DECAYTIME 0x207
#define CONN_DST_EG1_RELEASETIME 0x209
#define CONN_DST_EG1_SUSTAINLEVEL 0x20A
#define CONN_DST_EG2_ATTACKTIME 0x30A
#define CONN_DST_EG2_DECAYTIME 0x30B
#define CONN_DST_EG2_RELEASETIME 0x30D
#define CONN_DST_EG2_SUSTAINLEVEL 0x30E
#define CONN_DST_FILTER_CUTOFF 0x500
#define CONN_DST_FILTER_Q 0x501

// ---- art1 connection census ----------------------------------------------
// Records every (source, control, destination) triple seen while parsing, so
// offline tools can report which GM.DLS metadata the pipeline consumes and
// which it silently drops. Guides extraction work with data, not guesses.
typedef struct {
    uint16_t source, control, destination;
    uint32_t count;
    int32_t scale_min, scale_max;
} dls_conn_stat_t;

static dls_conn_stat_t g_dls_conn_census[96];
static size_t g_dls_conn_census_count;

static void dls_census_connection(uint16_t source, uint16_t control, uint16_t destination, int32_t scale) {
    for (size_t i = 0; i < g_dls_conn_census_count; ++i) {
        dls_conn_stat_t *s = &g_dls_conn_census[i];
        if (s->source == source && s->control == control && s->destination == destination) {
            s->count++;
            if (scale < s->scale_min) s->scale_min = scale;
            if (scale > s->scale_max) s->scale_max = scale;
            return;
        }
    }
    if (g_dls_conn_census_count < sizeof(g_dls_conn_census) / sizeof(g_dls_conn_census[0])) {
        dls_conn_stat_t *s = &g_dls_conn_census[g_dls_conn_census_count++];
        s->source = source;
        s->control = control;
        s->destination = destination;
        s->count = 1;
        s->scale_min = scale;
        s->scale_max = scale;
    }
}

typedef struct {
    uint8_t *data;
    size_t size;
} file_blob_t;

typedef struct {
    uint16_t format_tag;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    const uint8_t *data;
    uint32_t data_size;
    uint32_t frame_count;
    bool valid;

    bool has_wsmp;
    uint16_t unity_note;
    int16_t fine_tune;
    int32_t attenuation;
    bool looped;
    uint32_t loop_start;
    uint32_t loop_length;
} dls_wave_t;

typedef struct {
    bool has_eg1;
    bool has_attack;
    bool has_decay;
    bool has_release;
    bool has_sustain;
    bool has_lfo_frequency;
    bool has_lfo_delay;
    bool has_filter_cutoff;
    bool has_filter_q;
    int32_t attack_time;
    int32_t decay_time;
    int32_t release_time;
    int32_t sustain_level;
    // Key-number scaling of EG1 times (timecents across the 0..128 key range).
    int32_t decay_keyscale;
    int32_t hold_keyscale;
    int32_t attack_keyscale;
    // Key-on velocity scaling of EG1 attack (timecents across 0..128 velocity).
    int32_t attack_velscale;
    int32_t lfo_frequency;
    int32_t lfo_delay;
    int32_t filter_cutoff;
    int32_t filter_q;
    int32_t reverb_send;
    int32_t chorus_send;
    bool has_pan;
    int32_t pan;                 // DLS pan, 0.1% units in 16.16 (-500..500 = full L..R)
    double lfo_pitch_cents;
    double mod_lfo_pitch_cents;
    bool has_lfo_gain;
    int32_t lfo_gain_scale;      // LFO->attenuation (tremolo), 0.1 dB units in 16.16
    // EG2 (pitch envelope), timecents/permille 16.16 like EG1.
    bool has_eg2;
    bool has_eg2_attack, has_eg2_decay, has_eg2_release, has_eg2_sustain;
    int32_t eg2_attack_time, eg2_decay_time, eg2_release_time, eg2_sustain_level;
    int32_t eg2_decay_keyscale;  // KEYNUMBER -> EG2 decay time
    int32_t eg2_attack_velscale; // KEYONVELOCITY -> EG2 attack time
    int32_t eg2_pitch_scale;     // EG2 -> pitch depth, cents 16.16
} dls_articulation_t;

typedef struct {
    uint16_t key_low;
    uint16_t key_high;
    uint16_t vel_low;
    uint16_t vel_high;
    uint16_t options;
    uint16_t key_group;
    uint32_t wave_index;

    bool has_wsmp;
    uint16_t unity_note;
    int16_t fine_tune;
    int32_t attenuation;
    bool looped;
    uint32_t loop_start;
    uint32_t loop_length;

    bool has_articulation;       // region carries its own lart (overrides instrument)
    dls_articulation_t articulation;
} dls_region_t;

typedef struct {
    uint32_t bank;
    uint32_t program;
    char name[64];
    dls_articulation_t articulation;
    dls_region_t *regions;
    size_t region_count;
    size_t region_capacity;
} dls_instrument_t;

typedef struct {
    file_blob_t blob;
    uint32_t *pool_offsets;
    size_t pool_count;
    dls_wave_t *waves;
    size_t wave_count;
    dls_instrument_t *instruments;
    size_t instrument_count;
    size_t instrument_capacity;
} dls_bank_t;

static uint16_t rd_u16le(const uint8_t *p) {
    return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static int16_t rd_i16le(const uint8_t *p) {
    return (int16_t) rd_u16le(p);
}

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static int32_t rd_i32le(const uint8_t *p) {
    return (int32_t) rd_u32le(p);
}

static uint16_t rd_u16be(const uint8_t *p) {
    return ((uint16_t) p[0] << 8) | (uint16_t) p[1];
}

static uint32_t rd_u32be(const uint8_t *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

static bool fourcc_is(const uint8_t *p, const char id[4]) {
    return memcmp(p, id, 4) == 0;
}

static bool read_entire_file(const char *path, file_blob_t *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long end = ftell(f);
    if (end < 0) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    uint8_t *data = malloc((size_t) end);
    if (!data) {
        fclose(f);
        fprintf(stderr, "out of memory reading %s\n", path);
        return false;
    }

    if (fread(data, 1, (size_t) end, f) != (size_t) end) {
        free(data);
        fclose(f);
        fprintf(stderr, "read %s failed\n", path);
        return false;
    }

    fclose(f);
    out->data = data;
    out->size = (size_t) end;
    return true;
}

static void free_file_blob(file_blob_t *blob) {
    free(blob->data);
    blob->data = NULL;
    blob->size = 0;
}

static bool reserve_region(dls_instrument_t *instrument) {
    if (instrument->region_count < instrument->region_capacity) return true;
    size_t next_capacity = instrument->region_capacity ? instrument->region_capacity * 2 : 8;
    dls_region_t *next = realloc(instrument->regions, next_capacity * sizeof(*next));
    if (!next) return false;
    instrument->regions = next;
    instrument->region_capacity = next_capacity;
    return true;
}

static bool reserve_instrument(dls_bank_t *bank) {
    if (bank->instrument_count < bank->instrument_capacity) return true;
    size_t next_capacity = bank->instrument_capacity ? bank->instrument_capacity * 2 : 64;
    dls_instrument_t *next = realloc(bank->instruments, next_capacity * sizeof(*next));
    if (!next) return false;
    bank->instruments = next;
    bank->instrument_capacity = next_capacity;
    return true;
}

static size_t riff_next(size_t off, uint32_t size) {
    return off + 8u + (size_t) size + (size_t) (size & 1u);
}

static bool parse_wsmp(const uint8_t *data, size_t size, dls_region_t *region, dls_wave_t *wave) {
    if (size < 20) return false;

    uint16_t unity_note = rd_u16le(data + 4);
    int16_t fine_tune = rd_i16le(data + 6);
    int32_t attenuation = rd_i32le(data + 8);
    uint32_t loop_count = rd_u32le(data + 16);
    bool looped = false;
    uint32_t loop_start = 0;
    uint32_t loop_length = 0;

    if (loop_count > 0 && size >= 36) {
        uint32_t loop_type = rd_u32le(data + 24);
        loop_start = rd_u32le(data + 28);
        loop_length = rd_u32le(data + 32);
        looped = loop_type == 0 && loop_length > 1;
    }

    if (region) {
        region->has_wsmp = true;
        region->unity_note = unity_note;
        region->fine_tune = fine_tune;
        region->attenuation = attenuation;
        region->looped = looped;
        region->loop_start = loop_start;
        region->loop_length = loop_length;
    }

    if (wave) {
        wave->has_wsmp = true;
        wave->unity_note = unity_note;
        wave->fine_tune = fine_tune;
        wave->attenuation = attenuation;
        wave->looped = looped;
        wave->loop_start = loop_start;
        wave->loop_length = loop_length;
    }

    return true;
}

static void parse_lart(const uint8_t *file, size_t file_size, size_t lart_off, dls_articulation_t *articulation);

static bool parse_region(const uint8_t *file, size_t file_size, size_t region_off, dls_instrument_t *instrument) {
    if (region_off + 12 > file_size || !fourcc_is(file + region_off, "LIST")) return false;
    uint32_t list_size = rd_u32le(file + region_off + 4);
    size_t data_off = region_off + 8u;
    size_t end = data_off + list_size;
    if (end > file_size || list_size < 4) return false;
    if (!fourcc_is(file + data_off, "rgn ") && !fourcc_is(file + data_off, "rgn2")) return true;

    dls_region_t region = {
        .key_low = 0,
        .key_high = 127,
        .vel_low = 0,
        .vel_high = 127,
        .wave_index = UINT32_MAX,
        .unity_note = 60,
    };

    for (size_t off = data_off + 4u; off + 8u <= end;) {
        uint32_t size = rd_u32le(file + off + 4);
        size_t payload = off + 8u;
        size_t next = riff_next(off, size);
        if (payload + size > file_size || payload + size > end || next <= off) return false;

        if (fourcc_is(file + off, "rgnh") && size >= 12) {
            region.key_low = rd_u16le(file + payload);
            region.key_high = rd_u16le(file + payload + 2);
            region.vel_low = rd_u16le(file + payload + 4);
            region.vel_high = rd_u16le(file + payload + 6);
            region.options = rd_u16le(file + payload + 8);
            region.key_group = rd_u16le(file + payload + 10);
        } else if (fourcc_is(file + off, "wsmp")) {
            if (!parse_wsmp(file + payload, size, &region, NULL)) return false;
        } else if (fourcc_is(file + off, "wlnk") && size >= 12) {
            region.wave_index = rd_u32le(file + payload + 8);
        } else if (fourcc_is(file + off, "LIST") && size >= 4 && fourcc_is(file + payload, "lart")) {
            parse_lart(file, file_size, off, &region.articulation);
            region.has_articulation = true;
        }

        off = next;
    }

    if (region.wave_index == UINT32_MAX) return true;
    if (!reserve_region(instrument)) return false;
    instrument->regions[instrument->region_count++] = region;
    return true;
}

static void parse_info_name(const uint8_t *file, size_t file_size, size_t info_off, char out[64]) {
    if (info_off + 12 > file_size || !fourcc_is(file + info_off, "LIST")) return;
    uint32_t list_size = rd_u32le(file + info_off + 4);
    size_t data_off = info_off + 8u;
    size_t end = data_off + list_size;
    if (end > file_size || list_size < 4 || !fourcc_is(file + data_off, "INFO")) return;

    for (size_t off = data_off + 4u; off + 8u <= end;) {
        uint32_t size = rd_u32le(file + off + 4);
        size_t payload = off + 8u;
        size_t next = riff_next(off, size);
        if (payload + size > file_size || payload + size > end || next <= off) return;

        if (fourcc_is(file + off, "INAM")) {
            size_t copy_len = size;
            if (copy_len >= 64) copy_len = 63;
            memcpy(out, file + payload, copy_len);
            out[copy_len] = '\0';
            for (size_t i = 0; out[i]; ++i) {
                if ((unsigned char) out[i] < 32) {
                    out[i] = '\0';
                    break;
                }
            }
            return;
        }

        off = next;
    }
}

static void parse_lart(const uint8_t *file, size_t file_size, size_t lart_off, dls_articulation_t *articulation) {
    if (lart_off + 12 > file_size || !fourcc_is(file + lart_off, "LIST")) return;
    uint32_t list_size = rd_u32le(file + lart_off + 4);
    size_t data_off = lart_off + 8u;
    size_t end = data_off + list_size;
    if (end > file_size || list_size < 4 || !fourcc_is(file + data_off, "lart")) return;

    for (size_t off = data_off + 4u; off + 8u <= end;) {
        uint32_t size = rd_u32le(file + off + 4);
        size_t payload = off + 8u;
        size_t next = riff_next(off, size);
        if (payload + size > file_size || payload + size > end || next <= off) return;

        if (fourcc_is(file + off, "art1") && size >= 8) {
            uint32_t connection_count = rd_u32le(file + payload + 4);
            size_t connection_off = payload + 8u;
            size_t connection_end = payload + size;
            for (uint32_t i = 0; i < connection_count && connection_off + 12u <= connection_end; ++i) {
                uint16_t source = rd_u16le(file + connection_off);
                uint16_t control = rd_u16le(file + connection_off + 2);
                uint16_t destination = rd_u16le(file + connection_off + 4);
                int32_t scale = rd_i32le(file + connection_off + 8);
                connection_off += 12u;
                dls_census_connection(source, control, destination, scale);

                if (source == CONN_SRC_NONE && control == CONN_SRC_NONE) {
                    switch (destination) {
                        case CONN_DST_PAN:
                            articulation->pan = scale;
                            articulation->has_pan = true;
                            break;
                        case CONN_DST_LFO_FREQUENCY:
                            articulation->lfo_frequency = scale;
                            articulation->has_lfo_frequency = true;
                            break;
                        case CONN_DST_LFO_STARTDELAY:
                            articulation->lfo_delay = scale;
                            articulation->has_lfo_delay = true;
                            break;
                        case CONN_DST_EG1_ATTACKTIME:
                            articulation->attack_time = scale;
                            articulation->has_attack = true;
                            articulation->has_eg1 = true;
                            break;
                        case CONN_DST_EG1_DECAYTIME:
                            articulation->decay_time = scale;
                            articulation->has_decay = true;
                            articulation->has_eg1 = true;
                            break;
                        case CONN_DST_EG1_RELEASETIME:
                            articulation->release_time = scale;
                            articulation->has_release = true;
                            articulation->has_eg1 = true;
                            break;
                        case CONN_DST_EG1_SUSTAINLEVEL:
                            articulation->sustain_level = scale;
                            articulation->has_sustain = true;
                            articulation->has_eg1 = true;
                            break;
                        case CONN_DST_EG2_ATTACKTIME:
                            articulation->eg2_attack_time = scale;
                            articulation->has_eg2_attack = true;
                            articulation->has_eg2 = true;
                            break;
                        case CONN_DST_EG2_DECAYTIME:
                            articulation->eg2_decay_time = scale;
                            articulation->has_eg2_decay = true;
                            articulation->has_eg2 = true;
                            break;
                        case CONN_DST_EG2_RELEASETIME:
                            articulation->eg2_release_time = scale;
                            articulation->has_eg2_release = true;
                            articulation->has_eg2 = true;
                            break;
                        case CONN_DST_EG2_SUSTAINLEVEL:
                            articulation->eg2_sustain_level = scale;
                            articulation->has_eg2_sustain = true;
                            articulation->has_eg2 = true;
                            break;
                        case CONN_DST_FILTER_CUTOFF:
                            articulation->filter_cutoff = scale;
                            articulation->has_filter_cutoff = true;
                            break;
                        case CONN_DST_FILTER_Q:
                            articulation->filter_q = scale;
                            articulation->has_filter_q = true;
                            break;
                        case CONN_DST_REVERB:
                            articulation->reverb_send = scale;
                            break;
                        case CONN_DST_CHORUS:
                            articulation->chorus_send = scale;
                            break;
                        default:
                            break;
                    }
                } else if (source == CONN_SRC_LFO && destination == CONN_DST_PITCH) {
                    if (control == CONN_SRC_NONE) {
                        articulation->lfo_pitch_cents += (double) scale / 65536.0;
                    } else if (control == CONN_SRC_CC1) {
                        articulation->mod_lfo_pitch_cents += (double) scale / 65536.0;
                    }
                } else if (source == CONN_SRC_LFO && control == CONN_SRC_NONE &&
                           destination == CONN_DST_ATTENUATION) {
                    articulation->lfo_gain_scale = scale;
                    articulation->has_lfo_gain = true;
                } else if (source == CONN_SRC_EG2 && control == CONN_SRC_NONE &&
                           destination == CONN_DST_PITCH) {
                    articulation->eg2_pitch_scale = scale;
                } else if (source == CONN_SRC_KEYNUMBER && control == CONN_SRC_NONE) {
                    // Key-number scaling of envelope times (higher notes -> faster).
                    switch (destination) {
                        case CONN_DST_EG1_DECAYTIME: articulation->decay_keyscale = scale; break;
                        case CONN_DST_EG1_ATTACKTIME: articulation->attack_keyscale = scale; break;
                        case CONN_DST_EG2_DECAYTIME: articulation->eg2_decay_keyscale = scale; break;
                        default: break;
                    }
                } else if (source == CONN_SRC_KEYONVELOCITY && control == CONN_SRC_NONE) {
                    if (destination == CONN_DST_EG1_ATTACKTIME) articulation->attack_velscale = scale;
                    if (destination == CONN_DST_EG2_ATTACKTIME) articulation->eg2_attack_velscale = scale;
                }
            }
        }

        off = next;
    }
}

static bool parse_instrument(const uint8_t *file, size_t file_size, size_t ins_off, dls_bank_t *bank) {
    if (ins_off + 12 > file_size || !fourcc_is(file + ins_off, "LIST")) return false;
    uint32_t list_size = rd_u32le(file + ins_off + 4);
    size_t data_off = ins_off + 8u;
    size_t end = data_off + list_size;
    if (end > file_size || list_size < 4 || !fourcc_is(file + data_off, "ins ")) return false;

    dls_instrument_t instrument = {0};
    instrument.program = UINT32_MAX;

    for (size_t off = data_off + 4u; off + 8u <= end;) {
        uint32_t size = rd_u32le(file + off + 4);
        size_t payload = off + 8u;
        size_t next = riff_next(off, size);
        if (payload + size > file_size || payload + size > end || next <= off) {
            free(instrument.regions);
            return false;
        }

        if (fourcc_is(file + off, "insh") && size >= 12) {
            instrument.bank = rd_u32le(file + payload + 4);
            instrument.program = rd_u32le(file + payload + 8) & 127u;
        } else if (fourcc_is(file + off, "LIST") && size >= 4 && fourcc_is(file + payload, "lrgn")) {
            for (size_t r = payload + 4u; r + 8u <= payload + size;) {
                uint32_t region_size = rd_u32le(file + r + 4);
                size_t next_region = riff_next(r, region_size);
                if (r + 8u + region_size > file_size || r + 8u + region_size > payload + size || next_region <= r) {
                    free(instrument.regions);
                    return false;
                }
                if (fourcc_is(file + r, "LIST")) {
                    if (!parse_region(file, file_size, r, &instrument)) {
                        free(instrument.regions);
                        return false;
                    }
                }
                r = next_region;
            }
        } else if (fourcc_is(file + off, "LIST") && size >= 4 && fourcc_is(file + payload, "INFO")) {
            parse_info_name(file, file_size, off, instrument.name);
        } else if (fourcc_is(file + off, "LIST") && size >= 4 && fourcc_is(file + payload, "lart")) {
            parse_lart(file, file_size, off, &instrument.articulation);
        }

        off = next;
    }

    if (instrument.program == UINT32_MAX || instrument.region_count == 0) {
        free(instrument.regions);
        return true;
    }

    if (!reserve_instrument(bank)) {
        free(instrument.regions);
        return false;
    }
    bank->instruments[bank->instrument_count++] = instrument;
    return true;
}

static bool parse_lins(const uint8_t *file, size_t file_size, size_t lins_off, dls_bank_t *bank) {
    if (lins_off + 12 > file_size || !fourcc_is(file + lins_off, "LIST")) return false;
    uint32_t list_size = rd_u32le(file + lins_off + 4);
    size_t data_off = lins_off + 8u;
    size_t end = data_off + list_size;
    if (end > file_size || list_size < 4 || !fourcc_is(file + data_off, "lins")) return false;

    for (size_t off = data_off + 4u; off + 8u <= end;) {
        uint32_t size = rd_u32le(file + off + 4);
        size_t payload = off + 8u;
        size_t next = riff_next(off, size);
        if (payload + size > file_size || payload + size > end || next <= off) return false;
        if (fourcc_is(file + off, "LIST") && size >= 4 && fourcc_is(file + payload, "ins ")) {
            if (!parse_instrument(file, file_size, off, bank)) return false;
        }
        off = next;
    }

    return true;
}

static bool parse_ptbl(const uint8_t *file, size_t file_size, size_t ptbl_off, dls_bank_t *bank) {
    if (ptbl_off + 16 > file_size || !fourcc_is(file + ptbl_off, "ptbl")) return false;
    uint32_t size = rd_u32le(file + ptbl_off + 4);
    size_t payload = ptbl_off + 8u;
    if (payload + size > file_size || size < 8) return false;

    uint32_t cb_size = rd_u32le(file + payload);
    uint32_t cue_count = rd_u32le(file + payload + 4);
    if (cb_size < 8 || cue_count == 0 || cue_count > 65536 || size < 8u + cue_count * 4u) return false;

    bank->pool_offsets = calloc(cue_count, sizeof(*bank->pool_offsets));
    if (!bank->pool_offsets) return false;
    bank->pool_count = cue_count;

    for (uint32_t i = 0; i < cue_count; ++i) {
        bank->pool_offsets[i] = rd_u32le(file + payload + 8u + i * 4u);
    }

    return true;
}

static bool parse_wave(const uint8_t *file, size_t file_size, size_t wave_off, dls_wave_t *wave) {
    if (wave_off + 12 > file_size || !fourcc_is(file + wave_off, "LIST")) return false;
    uint32_t list_size = rd_u32le(file + wave_off + 4);
    size_t data_off = wave_off + 8u;
    size_t end = data_off + list_size;
    if (end > file_size || list_size < 4 || !fourcc_is(file + data_off, "wave")) return false;

    memset(wave, 0, sizeof(*wave));

    for (size_t off = data_off + 4u; off + 8u <= end;) {
        uint32_t size = rd_u32le(file + off + 4);
        size_t payload = off + 8u;
        size_t next = riff_next(off, size);
        if (payload + size > file_size || payload + size > end || next <= off) return false;

        if (fourcc_is(file + off, "fmt ") && size >= 16) {
            wave->format_tag = rd_u16le(file + payload);
            wave->channels = rd_u16le(file + payload + 2);
            wave->sample_rate = rd_u32le(file + payload + 4);
            wave->block_align = rd_u16le(file + payload + 12);
            wave->bits_per_sample = rd_u16le(file + payload + 14);
        } else if (fourcc_is(file + off, "data")) {
            wave->data = file + payload;
            wave->data_size = size;
        } else if (fourcc_is(file + off, "wsmp")) {
            if (!parse_wsmp(file + payload, size, NULL, wave)) return false;
        }

        off = next;
    }

    if (wave->format_tag != 1 || wave->channels == 0 || wave->block_align == 0 ||
        (wave->bits_per_sample != 8 && wave->bits_per_sample != 16) || !wave->data) {
        return true;
    }

    wave->frame_count = wave->data_size / wave->block_align;
    wave->valid = wave->frame_count > 1;
    if (wave->loop_start >= wave->frame_count) wave->looped = false;
    if (wave->loop_start + wave->loop_length > wave->frame_count) {
        wave->loop_length = wave->frame_count - wave->loop_start;
    }
    if (wave->loop_length <= 1) wave->looped = false;

    return true;
}

static bool parse_wvpl(const uint8_t *file, size_t file_size, size_t wvpl_off, dls_bank_t *bank) {
    if (wvpl_off + 12 > file_size || !fourcc_is(file + wvpl_off, "LIST")) return false;
    uint32_t list_size = rd_u32le(file + wvpl_off + 4);
    size_t data_off = wvpl_off + 8u;
    size_t end = data_off + list_size;
    if (end > file_size || list_size < 4 || !fourcc_is(file + data_off, "wvpl")) return false;
    if (!bank->pool_offsets || bank->pool_count == 0) return false;

    size_t wave_base = data_off + 4u;
    bank->waves = calloc(bank->pool_count, sizeof(*bank->waves));
    if (!bank->waves) return false;
    bank->wave_count = bank->pool_count;

    for (size_t i = 0; i < bank->pool_count; ++i) {
        size_t wave_off = wave_base + bank->pool_offsets[i];
        if (wave_off + 12 > end) continue;
        if (!parse_wave(file, file_size, wave_off, &bank->waves[i])) return false;
    }

    return true;
}

static void dls_bank_free(dls_bank_t *bank) {
    for (size_t i = 0; i < bank->instrument_count; ++i) {
        free(bank->instruments[i].regions);
    }
    free(bank->instruments);
    free(bank->pool_offsets);
    free(bank->waves);
    free_file_blob(&bank->blob);
    memset(bank, 0, sizeof(*bank));
}

static bool dls_bank_load(const char *path, dls_bank_t *bank) {
    memset(bank, 0, sizeof(*bank));
    if (!read_entire_file(path, &bank->blob)) return false;

    const uint8_t *file = bank->blob.data;
    size_t file_size = bank->blob.size;
    if (file_size < 12 || !fourcc_is(file, "RIFF") || !fourcc_is(file + 8, "DLS ")) {
        fprintf(stderr, "%s is not a RIFF DLS file\n", path);
        dls_bank_free(bank);
        return false;
    }

    uint32_t riff_size = rd_u32le(file + 4);
    size_t riff_end = 8u + (size_t) riff_size;
    if (riff_end > file_size) riff_end = file_size;

    size_t lins_off = 0;
    size_t ptbl_off = 0;
    size_t wvpl_off = 0;

    for (size_t off = 12u; off + 8u <= riff_end;) {
        uint32_t size = rd_u32le(file + off + 4);
        size_t payload = off + 8u;
        size_t next = riff_next(off, size);
        if (payload + size > file_size || payload + size > riff_end || next <= off) {
            fprintf(stderr, "bad RIFF chunk in %s at 0x%zx\n", path, off);
            dls_bank_free(bank);
            return false;
        }

        if (fourcc_is(file + off, "ptbl")) {
            ptbl_off = off;
        } else if (fourcc_is(file + off, "LIST") && size >= 4) {
            if (fourcc_is(file + payload, "lins")) lins_off = off;
            if (fourcc_is(file + payload, "wvpl")) wvpl_off = off;
        }

        off = next;
    }

    if (!lins_off || !ptbl_off || !wvpl_off) {
        fprintf(stderr, "%s misses required DLS chunks\n", path);
        dls_bank_free(bank);
        return false;
    }

    if (!parse_ptbl(file, file_size, ptbl_off, bank) ||
        !parse_wvpl(file, file_size, wvpl_off, bank) ||
        !parse_lins(file, file_size, lins_off, bank)) {
        fprintf(stderr, "failed to parse %s\n", path);
        dls_bank_free(bank);
        return false;
    }

    fprintf(stderr, "DLS: %zu instruments, %zu waves loaded from %s\n",
            bank->instrument_count, bank->wave_count, path);
    return true;
}

static const dls_instrument_t *dls_find_instrument(const dls_bank_t *bank, uint32_t dls_bank, uint32_t program) {
    const dls_instrument_t *fallback_program = NULL;
    const dls_instrument_t *fallback_piano = NULL;
    for (size_t i = 0; i < bank->instrument_count; ++i) {
        const dls_instrument_t *instrument = &bank->instruments[i];
        if (instrument->bank == dls_bank && instrument->program == program) return instrument;
        if (instrument->bank == 0 && instrument->program == program) fallback_program = instrument;
        if (instrument->bank == 0 && instrument->program == 0) fallback_piano = instrument;
    }

    if (dls_bank & DLS_DRUM_BANK) {
        for (size_t i = 0; i < bank->instrument_count; ++i) {
            const dls_instrument_t *instrument = &bank->instruments[i];
            if (instrument->bank == DLS_DRUM_BANK && instrument->program == 0) return instrument;
        }
    }

    return fallback_program ? fallback_program : fallback_piano;
}

static const dls_region_t *dls_find_region(const dls_instrument_t *instrument, uint8_t note, uint8_t velocity) {
    const dls_region_t *nearest = NULL;
    int nearest_distance = 1000;

    for (size_t i = 0; i < instrument->region_count; ++i) {
        const dls_region_t *region = &instrument->regions[i];
        if (note >= region->key_low && note <= region->key_high &&
            velocity >= region->vel_low && velocity <= region->vel_high) {
            return region;
        }

        int distance = 0;
        if (note < region->key_low) distance = (int) region->key_low - note;
        if (note > region->key_high) distance = note - (int) region->key_high;
        if (!nearest || distance < nearest_distance) {
            nearest = region;
            nearest_distance = distance;
        }
    }

    return nearest;
}

static double dls_attenuation_gain(int32_t attenuation) {
    return pow(10.0, (double) attenuation / (655360.0 * 20.0));
}

static double dls_timecents_to_seconds(int32_t timecents_16_16) {
    if (timecents_16_16 <= INT32_MIN + 1) return 0.0;
    double timecents = (double) timecents_16_16 / 65536.0;
    if (timecents < -12000.0) return 0.0;
    if (timecents > 12000.0) timecents = 12000.0;
    return pow(2.0, timecents / 1200.0);
}

static double dls_absolute_cents_to_hz(int32_t cents_16_16) {
    double cents = (double) cents_16_16 / 65536.0;
    if (cents < -12000.0) cents = -12000.0;
    if (cents > 16000.0) cents = 16000.0;
    return 8.176 * pow(2.0, cents / 1200.0);
}

static double dls_sustain_to_gain(int32_t sustain_16_16) {
    double sustain = (double) sustain_16_16 / 65536.0;
    if (sustain < 0.0) sustain = 0.0;
    if (sustain > 1000.0) sustain = 1000.0;
    return sustain / 1000.0;
}

// DLS EG decay/release are linear in dB: a constant slope covering the full
// 96 dB range in `seconds`. As a per-sample amplitude multiplier that is
// 10^(-96/20 / (seconds*rate)). The engine applies it directly to the envelope
// (linear-in-dB ramp), clamping at sustain (decay) or freeing near -80 dB
// (release) — NOT an asymptotic approach toward sustain.
static double decay_coef_for_seconds(double seconds, uint32_t sample_rate) {
    if (seconds <= 0.0) return 0.0;
    return pow(10.0, -4.8 / (seconds * (double) sample_rate));
}

static double wave_read_channel(const dls_wave_t *wave, uint32_t frame, uint16_t channel) {
    if (frame >= wave->frame_count) return 0.0;
    if (channel >= wave->channels) channel = 0;

    const uint8_t *p = wave->data + (size_t) frame * wave->block_align;
    if (wave->bits_per_sample == 8) {
        return ((double) p[channel] - 128.0) / 128.0;
    }

    const uint8_t *sample = p + (size_t) channel * 2u;
    return (double) rd_i16le(sample) / 32768.0;
}
