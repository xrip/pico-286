#pragma once
#include <stdatomic.h>
#include "gm_bank.h"
#include "wavetable.c.inl"

extern const uint8_t gm_bank_blob[]; // CMake checks it is a GMWB v4 bank for 22050 Hz

void midi_dls_init(void) {
    gm_bank_view(gm_bank_blob, &g_bank);
    wt_engine_reset();
}

// Voices belong to the audio thread; the emulator thread only queues commands.
// One writer moves midi_queue_head, one reader moves midi_queue_tail.
#define MIDI_QUEUE_LENGTH 128 // a power of two
static midi_command_t midi_queue[MIDI_QUEUE_LENGTH];
static atomic_uint midi_queue_head, midi_queue_tail;

static INLINE void parse_midi(const midi_command_t *command) {
    const unsigned head = atomic_load_explicit(&midi_queue_head, memory_order_relaxed);
    while (head - atomic_load_explicit(&midi_queue_tail, memory_order_acquire) == MIDI_QUEUE_LENGTH) {} // full
    midi_queue[head % MIDI_QUEUE_LENGTH] = *command;
    atomic_store_explicit(&midi_queue_head, head + 1, memory_order_release);
}

// GM.DLS waves are 22050 Hz: make a new MIDI sample every second output sample and send it twice.
static INLINE void midi_dls_sample(int16_t midi[2]) {
    static int16_t held[2];
    static bool repeat_next;
    if (!repeat_next) {
        const unsigned head = atomic_load_explicit(&midi_queue_head, memory_order_acquire);
        unsigned tail = atomic_load_explicit(&midi_queue_tail, memory_order_relaxed);
        for (; tail != head; ++tail) {
            const midi_command_t *command = &midi_queue[tail % MIDI_QUEUE_LENGTH];
            if (command->command == 0xff) wt_engine_reset();
            else wt_parse_midi(command);
        }
        atomic_store_explicit(&midi_queue_tail, tail, memory_order_release);
        midi_sample_stereo(&held[0], &held[1]);
    }
    repeat_next = !repeat_next;
    midi[0] = held[0];
    midi[1] = held[1];
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
