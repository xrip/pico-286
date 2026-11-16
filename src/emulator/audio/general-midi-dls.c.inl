#pragma once
#include <pico/util/queue.h>
#include "gm_bank.h"

// Voices belong to the audio core; the emulator core only queues commands.
#define WT_NO_WAVE_CACHE
#define WT_RAMFUNC(name) __not_in_flash_func(name)
#define parse_midi wt_parse_midi
#pragma push_macro("INLINE")
#undef INLINE
#define INLINE static inline
#include "wavetable.c.inl"
#pragma pop_macro("INLINE")
#undef parse_midi
#undef WT_RAMFUNC
#undef WT_NO_WAVE_CACHE

extern const uint8_t gm_bank_blob[];
static queue_t midi_queue;

void midi_dls_init(void) {
    if (!gm_bank_view(gm_bank_blob, &g_bank) ||
            g_bank.header->output_rate != SOUND_FREQUENCY)
        panic("GM.DLS bank format or sample rate mismatch");
    wt_set_bank(gm_bank_blob);
    if (!queue_init(&midi_queue, sizeof(midi_command_t), 128))
        panic("Cannot allocate MIDI command queue");
}

static INLINE void parse_midi(const midi_command_t *command) {
    queue_add_blocking(&midi_queue, command);
}

static INLINE void midi_dls_sample(int16_t samples[2]) {
    midi_command_t command;
    // Bound command work per sample, including a full queue at entry.
    for (unsigned i = 0; i < 128 && queue_try_remove(&midi_queue, &command); ++i) {
        if (command.command == 0xff) wt_engine_reset();
        else wt_parse_midi(&command);
    }
    midi_sample_stereo(&samples[0], &samples[1]);
}

static INLINE int16_t midi_sample(void) {
    int16_t midi[2];
    midi_dls_sample(midi);
    return (int16_t)(((int32_t)midi[0] + midi[1]) / 2);
}

static INLINE void midi_dls_mix(int16_t samples[2]) {
    int16_t midi[2];
    midi_dls_sample(midi);
    for (unsigned channel = 0; channel < 2; ++channel) {
        int32_t mixed = (int32_t)samples[channel] + midi[channel];
        if (mixed > INT16_MAX) mixed = INT16_MAX;
        if (mixed < INT16_MIN) mixed = INT16_MIN;
        samples[channel] = (int16_t)mixed;
    }
}
