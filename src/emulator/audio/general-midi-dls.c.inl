#pragma once
#include <pico/util/queue.h>
#include "gm_bank.h"
#include "wavetable.c.inl"

// Voices belong to the audio core; the emulator core only queues commands.
#define MIDI_QUEUE_LENGTH 128

extern const uint8_t gm_bank_blob[];
static queue_t midi_queue;

void midi_dls_init(void) {
    if (!gm_bank_view(gm_bank_blob, &g_bank) ||
            g_bank.header->output_rate != SOUND_FREQUENCY)
        panic("GM.DLS bank format or sample rate mismatch");
    wt_engine_reset();
    if (!queue_init(&midi_queue, sizeof(midi_command_t), MIDI_QUEUE_LENGTH))
        panic("Cannot allocate MIDI command queue");
}

static INLINE void parse_midi(const midi_command_t *command) {
    queue_add_blocking(&midi_queue, command);
}

static INLINE void midi_dls_sample(int16_t midi[2]) {
    midi_command_t command;
    // At most one full queue of commands per sample.
    for (unsigned i = 0; i < MIDI_QUEUE_LENGTH && queue_try_remove(&midi_queue, &command); ++i) {
        if (command.command == 0xff) wt_engine_reset();
        else wt_parse_midi(&command);
    }
    midi_sample_stereo(&midi[0], &midi[1]);
}

// For the mono HARDWARE_SOUND mix.
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
