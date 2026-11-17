// Offline GM.DLS -> flash soundbank packer.
//
//   dls_pack <gm.dls> <out.bin> <output_rate>
//
// Bakes everything the fixed-point engine needs into a position-independent
// blob (see gm_bank.h), so the device does no float math:
//   * waves downmixed to mono int16 PCM at native rate, with a base playback step
//   * regions with resolved wsmp tuning, attenuation, loops, EG1/EG2 and LFO
//     baked at <output_rate>
//   * instruments in bank/program order
#include "dls_parse.inl"
#include "../src/emulator/audio/gm_bank.h"

// ---- growable output buffers -------------------------------------------------

typedef struct {
    void  *data;
    size_t count;
    size_t cap;
    size_t elem;
} vec_t;

static void vec_init(vec_t *v, size_t elem) {
    v->data = NULL;
    v->count = 0;
    v->cap = 0;
    v->elem = elem;
}

static void *vec_push(vec_t *v) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 1024;
        void *p = realloc(v->data, next * v->elem);
        if (!p) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        v->data = p;
        v->cap = next;
    }
    void *slot = (uint8_t *) v->data + v->count * v->elem;
    v->count++;
    return slot;
}

// ---- fixed-point bakers ------------------------------------------------------

static uint32_t gain_to_q16(int32_t attenuation) {
    double g = dls_attenuation_gain(attenuation);
    if (g < 0.0) g = 0.0;
    double q = g * (double) GM_ONE_Q16;
    if (q > 4294967040.0) q = 4294967040.0; // keep inside uint32
    return (uint32_t) llround(q);
}

static uint32_t coef_to_q16(double coef) {
    if (coef < 0.0) coef = 0.0;
    if (coef > 0.99998) coef = 0.99998; // never reach exactly 1.0 (would never decay)
    return (uint32_t) llround(coef * (double) GM_ONE_Q16);
}

static uint32_t unit_to_q16(double value) {
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    return (uint32_t) llround(value * (double) GM_ONE_Q16);
}

static int16_t clamp_i16(long v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t) v;
}

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Bake the DLS LFO modulators (per-instrument articulation) into a region:
// LFO->pitch vibrato (with CC1-scaled extra depth) and LFO->attenuation
// tremolo. Pitch depth keeps the full DLS range (some SFX like "Wind" use
// ~725 cents).
static void bake_lfo(const dls_articulation_t *art, uint32_t rate, gm_region_t *out) {
    int pitch = art->lfo_pitch_cents != 0.0 || art->mod_lfo_pitch_cents != 0.0;
    int gain = art->has_lfo_gain && art->lfo_gain_scale != 0;
    if (!pitch && !gain) return;

    double freq = art->has_lfo_frequency ? dls_absolute_cents_to_hz(art->lfo_frequency) : 5.0;
    freq = clampd(freq, 0.01, 40.0);
    double delay_s = art->has_lfo_delay ? dls_timecents_to_seconds(art->lfo_delay) : 0.0;

    out->lfo_phase_inc = (uint32_t) llround(freq / (double) rate * 4294967296.0);
    out->lfo_delay = (uint32_t) llround(delay_s * (double) rate);
    out->lfo_depth_q8 = (int32_t) llround(art->lfo_pitch_cents * 256.0);
    out->lfo_mod_depth_q8 = (int32_t) llround(art->mod_lfo_pitch_cents * 256.0);
    if (gain) {
        // Tremolo depth: DLS 0.1 dB units (16.16) -> log2-amplitude "cents"
        // (1/1200 octave), so the engine reuses the pitch 2^x LUT for gain.
        double db = (double) art->lfo_gain_scale / 65536.0 / 10.0;
        out->lfo_gain_depth_q8 = (int32_t) llround(db / 6.0205999132796239 * 1200.0 * 256.0);
    }
    out->flags |= GM_RGN_HAS_LFO;
}

// ---- packing -----------------------------------------------------------------

// DLS connection sources are normalized to value/128, so a KEYNUMBER/VELOCITY
// connection adds scale_tc * (key|vel)/128 timecents (16.16) to the base time.
// GM.DLS uses this heavily on drums (higher note -> shorter decay); dropping it
// bakes multi-second decays where ~1-2s were intended.
static int32_t eg_time_tc(int32_t base_tc, int32_t keyscale_tc, int key,
                          int32_t velscale_tc, int vel) {
    int64_t tc = (int64_t) base_tc
               + (int64_t) keyscale_tc * key / 128
               + (int64_t) velscale_tc * vel / 128;
    if (tc < INT32_MIN) tc = INT32_MIN;
    if (tc > INT32_MAX) tc = INT32_MAX;
    return (int32_t) tc;
}

// Bake the region's DLS EG1 (amplitude envelope) with key/velocity time scaling
// resolved at (key, vel) — exact for drums (single-note regions), region-midpoint
// approximation for melodic ones. Missing pieces take the DLS defaults (instant
// attack/decay, sustain 100%); release falls back to a short 50ms anti-click ramp.
static void bake_eg1(const dls_articulation_t *art, uint32_t rate,
                     int key, int vel, gm_region_t *out) {
    double attack_s = art->has_attack
        ? dls_timecents_to_seconds(eg_time_tc(art->attack_time, art->attack_keyscale, key,
                                              art->attack_velscale, vel))
        : 0.0;
    double decay_s = art->has_decay
        ? dls_timecents_to_seconds(eg_time_tc(art->decay_time, art->decay_keyscale, key, 0, 0))
        : 0.0;
    double release_s = art->has_release ? dls_timecents_to_seconds(art->release_time) : 0.05;
    double sustain = art->has_sustain ? dls_sustain_to_gain(art->sustain_level) : 1.0;

    double attack_step = attack_s <= 0.0 ? 1.0 : 1.0 / (attack_s * (double) rate);
    out->attack_step_q16 = unit_to_q16(attack_step);
    out->decay_coef_q16 = coef_to_q16(decay_coef_for_seconds(decay_s, rate));
    out->release_coef_q16 = coef_to_q16(decay_coef_for_seconds(release_s, rate));
    out->sustain_q16 = unit_to_q16(sustain);
}

// Bake the DLS EG2 pitch envelope. Only meaningful when the EG2->PITCH depth is
// non-zero (GM.DLS defines EG2 timings on many instruments without routing them
// anywhere — those are skipped). Same key/velocity time scaling rules as EG1.
static void bake_eg2(const dls_articulation_t *art, uint32_t rate,
                     int key, int vel, gm_region_t *out) {
    int32_t depth_cents = (int32_t) llround((double) art->eg2_pitch_scale / 65536.0);
    if (!art->has_eg2 || depth_cents == 0) return;

    double attack_s = art->has_eg2_attack
        ? dls_timecents_to_seconds(eg_time_tc(art->eg2_attack_time, 0, 0,
                                              art->eg2_attack_velscale, vel))
        : 0.0;
    double decay_s = art->has_eg2_decay
        ? dls_timecents_to_seconds(eg_time_tc(art->eg2_decay_time, art->eg2_decay_keyscale, key, 0, 0))
        : 0.0;
    double release_s = art->has_eg2_release ? dls_timecents_to_seconds(art->eg2_release_time) : 0.0;
    double sustain = art->has_eg2_sustain ? dls_sustain_to_gain(art->eg2_sustain_level) : 1.0;

    double attack_step = attack_s <= 0.0 ? 1.0 : 1.0 / (attack_s * (double) rate);
    out->eg2_attack_step_q16 = unit_to_q16(attack_step);
    out->eg2_decay_coef_q16 = coef_to_q16(decay_coef_for_seconds(decay_s, rate));
    out->eg2_release_coef_q16 = coef_to_q16(decay_coef_for_seconds(release_s, rate));
    out->eg2_sustain_q16 = unit_to_q16(sustain);
    out->eg2_pitch_cents = depth_cents;
    out->flags |= GM_RGN_HAS_EG2;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <gm.dls> <out.bin> <output_rate>\n", argv[0]);
        return 2;
    }
    const char *dls_path = argv[1];
    const char *out_path = argv[2];
    char *endp = NULL;
    unsigned long rate_ul = strtoul(argv[3], &endp, 10);
    if (!endp || *endp || rate_ul < 8000 || rate_ul > 192000) {
        fprintf(stderr, "invalid output_rate: %s\n", argv[3]);
        return 2;
    }
    uint32_t output_rate = (uint32_t) rate_ul;

    dls_bank_t bank;
    if (!dls_bank_load(dls_path, &bank)) return 1;

    if (bank.wave_count > 65535) {
        fprintf(stderr, "too many waves (%zu) for uint16 wave_index\n", bank.wave_count);
        dls_bank_free(&bank);
        return 1;
    }

    vec_t waves, regions, instruments, pcm;
    vec_init(&waves, sizeof(gm_wave_t));
    vec_init(&regions, sizeof(gm_region_t));
    vec_init(&instruments, sizeof(gm_instrument_t));
    vec_init(&pcm, sizeof(int16_t));   // int16 PCM: 2 bytes/sample

    // Waves: 1:1 with bank pool indices so region wave_index stays valid.
    for (size_t i = 0; i < bank.wave_count; ++i) {
        const dls_wave_t *w = &bank.waves[i];
        gm_wave_t *gw = vec_push(&waves);
        gw->pcm_offset = (uint32_t) pcm.count;
        if (!w->valid) {
            gw->frame_count = 0;
            gw->base_step_q16 = 0;
            continue;
        }
        gw->frame_count = w->frame_count;
        gw->base_step_q16 = (uint32_t) llround((double) w->sample_rate / (double) output_rate * (double) GM_ONE_Q16);
        for (uint32_t f = 0; f < w->frame_count; ++f) {
            double mono = 0.0;
            for (uint16_t ch = 0; ch < w->channels; ++ch) mono += wave_read_channel(w, f, ch);
            mono /= (double) w->channels;
            int16_t *s = vec_push(&pcm);
            *s = clamp_i16(lrint(mono * 32768.0));
        }
    }

    // Instruments + regions, preserving bank/program order.
    for (size_t i = 0; i < bank.instrument_count; ++i) {
        const dls_instrument_t *ins = &bank.instruments[i];
        uint32_t region_first = (uint32_t) regions.count;
        uint16_t region_count = 0;

        for (size_t r = 0; r < ins->region_count; ++r) {
            const dls_region_t *rg = &ins->regions[r];
            if (rg->wave_index >= bank.wave_count) continue;
            const dls_wave_t *w = &bank.waves[rg->wave_index];

            gm_region_t *out = vec_push(&regions);
            memset(out, 0, sizeof(*out));
            out->wave_index = (uint16_t) rg->wave_index;
            out->key_low = (uint8_t) rg->key_low;
            out->key_high = (uint8_t) rg->key_high;
            out->vel_low = (uint8_t) rg->vel_low;
            out->vel_high = (uint8_t) rg->vel_high;
            out->key_group = (uint8_t) rg->key_group;
            out->flags = 0;

            // wsmp precedence: region -> wave -> defaults (root = played note).
            if (rg->has_wsmp) {
                out->root_key = (uint8_t) rg->unity_note;
                out->fine_cents = rg->fine_tune;
                out->gain_q16 = gain_to_q16(rg->attenuation);
            } else if (w->has_wsmp) {
                out->root_key = (uint8_t) w->unity_note;
                out->fine_cents = w->fine_tune;
                out->gain_q16 = gain_to_q16(w->attenuation);
            } else {
                out->root_key = 60;
                out->fine_cents = 0;
                out->gain_q16 = GM_ONE_Q16;
                out->flags |= GM_RGN_ROOT_FROM_NOTE;
            }

            // loop precedence: region, then wave (only if the region has no wsmp).
            uint32_t ls = 0, ll = 0;
            int looped = 0;
            if (rg->looped) {
                ls = rg->loop_start;
                ll = rg->loop_length;
                looped = 1;
            } else if (!rg->has_wsmp && w->looped) {
                ls = w->loop_start;
                ll = w->loop_length;
                looped = 1;
            }
            if (looped) {
                if (ls >= w->frame_count) looped = 0;
                else if (ls + ll > w->frame_count) ll = w->frame_count - ls;
                if (ll <= 1) looped = 0;
            }
            if (looped) {
                out->loop_start = ls;
                out->loop_length = ll;
                out->flags |= GM_RGN_LOOPED;
            }

            // DLS articulation: a region's own lart overrides the instrument's.
            const dls_articulation_t *art = rg->has_articulation ? &rg->articulation : &ins->articulation;
            int rep_key = ((int) rg->key_low + (int) rg->key_high) / 2;
            int rep_vel = ((int) rg->vel_low + (int) rg->vel_high) / 2;
            bake_eg1(art, output_rate, rep_key, rep_vel, out);
            bake_eg2(art, output_rate, rep_key, rep_vel, out);
            bake_lfo(art, output_rate, out);
            // DLS art1 pan (0.1% units, 16.16): -500..500 spans full L..R, i.e.
            // a -64..+64 offset on the 0..127 MIDI pan scale.
            if (art->has_pan) {
                long p = lround((double) art->pan / 65536.0 / 500.0 * 64.0);
                if (p < -64) p = -64;
                if (p > 63) p = 63;
                out->pan = (int8_t) p;
            }
            region_count++;
        }

        gm_instrument_t *gi = vec_push(&instruments);
        gi->bank = ins->bank;
        gi->program = (uint16_t) ins->program;
        gi->region_first = region_first;
        gi->region_count = region_count;
    }

    // Layout (all tables are 4-aligned by construction).
    gm_bank_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = GM_BANK_MAGIC0;
    hdr.magic[1] = GM_BANK_MAGIC1;
    hdr.magic[2] = GM_BANK_MAGIC2;
    hdr.magic[3] = GM_BANK_MAGIC3;
    hdr.version = GM_BANK_VERSION;
    hdr.output_rate = output_rate;
    hdr.instrument_count = (uint32_t) instruments.count;
    hdr.region_count = (uint32_t) regions.count;
    hdr.wave_count = (uint32_t) waves.count;
    hdr.off_instruments = (uint32_t) sizeof(hdr);
    hdr.off_regions = hdr.off_instruments + (uint32_t) (instruments.count * sizeof(gm_instrument_t));
    hdr.off_waves = hdr.off_regions + (uint32_t) (regions.count * sizeof(gm_region_t));
    hdr.off_pcm = hdr.off_waves + (uint32_t) (waves.count * sizeof(gm_wave_t));
    hdr.pcm_samples = (uint32_t) pcm.count;

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "open %s: %s\n", out_path, strerror(errno));
        dls_bank_free(&bank);
        return 1;
    }
    int ok = 1;
    ok &= fwrite(&hdr, sizeof(hdr), 1, f) == 1;
    ok &= fwrite(instruments.data, sizeof(gm_instrument_t), instruments.count, f) == instruments.count;
    ok &= fwrite(regions.data, sizeof(gm_region_t), regions.count, f) == regions.count;
    ok &= fwrite(waves.data, sizeof(gm_wave_t), waves.count, f) == waves.count;
    ok &= fwrite(pcm.data, sizeof(int16_t), pcm.count, f) == pcm.count;
    if (fclose(f) != 0) ok = 0;

    double pcm_mb = (double) (pcm.count * sizeof(int16_t)) / (1024.0 * 1024.0);
    double total_mb = (double) (hdr.off_pcm + pcm.count * sizeof(int16_t)) / (1024.0 * 1024.0);
    fprintf(stderr, "GMWB: %u instruments, %u regions, %u waves, %.2f MB PCM, %.2f MB total @ %u Hz -> %s\n",
            hdr.instrument_count, hdr.region_count, hdr.wave_count, pcm_mb, total_mb, output_rate, out_path);

    free(waves.data);
    free(regions.data);
    free(instruments.data);
    free(pcm.data);
    dls_bank_free(&bank);
    return ok ? 0 : 1;
}
