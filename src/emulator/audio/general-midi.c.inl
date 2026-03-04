#pragma GCC optimize("Ofast")
#pragma once
#include "general-midi.h"

// #define DEBUG_MIDI

#if defined(DEBUG_MIDI)
#define debug_log(...) printf(__VA_ARGS__)
#else
#define debug_log(...)
#endif

#define MAX_MIDI_VOICES 32
#define MIDI_CHANNELS 16

typedef struct midi_voice_s {
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    uint8_t velocity_base;
    uint8_t decay_shift;
    uint8_t sustain_level;
    uint8_t attack_target;   // >0 = attack phase, target velocity

    int32_t frequency_m100;
    uint16_t sample_position;
} midi_voice_t;

typedef struct midi_channel_s {
    uint8_t program;
    uint8_t volume;
    int32_t pitch;
} midi_channel_t;

typedef struct __attribute__((packed)) {
    uint8_t command;
    uint8_t note;
    uint8_t velocity;
    uint8_t other;
} midi_command_t;

static midi_voice_t midi_voices[MAX_MIDI_VOICES] = {0};
static midi_channel_t midi_channels[MIDI_CHANNELS] = {
    {0, 100, 0}, {0, 100, 0}, {0, 100, 0}, {0, 100, 0},
    {0, 100, 0}, {0, 100, 0}, {0, 100, 0}, {0, 100, 0},
    {0, 100, 0}, {0, 100, 0}, {0, 100, 0}, {0, 100, 0},
    {0, 100, 0}, {0, 100, 0}, {0, 100, 0}, {0, 100, 0},
};

// Bitmask for active voices
static uint32_t active_voice_bitmask = 0;
// Bitmask for sustained channels
static uint32_t channels_sustain_bitmask = 0;

// Best seeds for different percussion characteristics
static uint32_t noise_seed = 0x7EC80000; // Best overall for drums

// Alternative excellent seeds:
// 0x7EC80000 - Optimal: Maximum length sequence, good spectral distribution
// 0xDEADBEEF - Classic: Good randomness, widely used
// 0x8B8B8B8B - Balanced: Equal bit distribution
// 0xAAAA5555 - Alternating: Good for metallic sounds
// 0x13579BDF - Fibonacci-like: Natural sounding randomness

#define SET_ACTIVE_VOICE(idx) (active_voice_bitmask |= (1U << (idx)))
#define CLEAR_ACTIVE_VOICE(idx) (active_voice_bitmask &= ~(1U << (idx)))
#define IS_ACTIVE_VOICE(idx) ((active_voice_bitmask & (1U << (idx))) != 0)

#define SET_CHANNEL_SUSTAIN(idx) (channels_sustain_bitmask |= (1U << (idx)))
#define CLEAR_CHANNEL_SUSTAIN(idx) (channels_sustain_bitmask &= ~(1U << (idx)))
#define IS_CHANNEL_SUSTAIN(idx) ((channels_sustain_bitmask & (1U << (idx))) != 0)

// bend: 0-16383, center 8192. Returns multiplier ×10000
static INLINE int32_t calc_pitch_bend(const int bend) {
    // Map bend (0..16383) to fixed-point position in table (0..24), scaled by 256 for lerp
    // pos = 24 * 256 * (bend - 0) / 16383 = 6144 * bend / 16383
    const int32_t pos = (int32_t) bend * 6144 / 16383; // 0..6144
    const int idx = pos >> 8; // 0..24
    if (idx >= 24) return pitch_bend_table[24];
    const int frac = pos & 255; // 0..255
    return pitch_bend_table[idx] + (((pitch_bend_table[idx + 1] - pitch_bend_table[idx]) * frac) >> 8);
}

#define SIN_STEP (SOUND_FREQUENCY * 100 / 4096)

static INLINE int32_t sine_lookup(const uint32_t angle) {
    const uint16_t index = (angle / SIN_STEP) & 4095; // TODO: Should it be & 4095 or % 4096???
    return index < 2048
               ? sin_m128[index < 1024 ? index : 2047 - index]
               : -sin_m128[index < 3072 ? index - 2048 : 4095 - index];
}

static INLINE int16_t generate_noise() {
    // Linear feedback shift register for white noise
    noise_seed = (noise_seed >> 1) ^ (-(noise_seed & 1) & 0xD0000001);
    return (int16_t) ((noise_seed & 0xFFFF) - 0x8000) >> 8;
}

// Drum-specific synthesis
static INLINE int16_t generate_drum_sample(const midi_voice_t *voice, const uint16_t sample_position) {
    int16_t sample = 0;
    const uint8_t note = voice->note;

    // Envelope calculation - drums need faster decay
    int32_t envelope = voice->velocity;

    // Lookup synthesis type: notes 35-81 use the map, rest default to generic
    const uint8_t dtype = (note >= 35 && note <= 81)
                              ? drum_type_map[note - 35]
                              : DRUM_T_GENERIC;

    switch (dtype) {
        case DRUM_T_KICK: {
            // KICK DRUM: Low frequency sine + pitch sweep + click
            const int32_t sweep_freq = voice->frequency_m100 * (100 + (200 >> (sample_position >> 7))) / 100;
            const int32_t sine_val = sine_lookup(__fast_mul(sweep_freq, sample_position));

            // Add click at the beginning
            if (sample_position < 64) {
                sample = __fast_mul(envelope * 2, generate_noise()) >> 8;
            }

            // Fast exponential decay
            envelope = envelope * (65536 - (sample_position << 4)) >> 16;
            if (envelope < 0) envelope = 0;

            sample += __fast_mul(envelope, sine_val) >> 1;
            break;
        }

        case DRUM_T_SNARE: {
            // SNARE DRUM: Mixed noise and tone
            const int32_t sine_val = sine_lookup(__fast_mul(20000, sample_position)); // 200Hz

            const int16_t noise = generate_noise();

            // Sharp attack, quick decay
            if (sample_position >= 100) {
                envelope = envelope * (65536 - ((sample_position - 100) << 7)) >> 16;
            }
            if (envelope < 0) envelope = 0;

            // 70% noise, 30% tone for snare
            sample = __fast_mul(envelope, (noise * 7 + sine_val * 3) / 10);
            break;
        }

        case DRUM_T_STICK: {
            // SIDE STICK / SHORT CLICK: Ultra-fast noise decay
            const int16_t noise = generate_noise();

            envelope = envelope * (65536 - (sample_position << 10)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, noise);
            break;
        }

        case DRUM_T_CLAP: {
            // HAND CLAP: Noise, medium-fast decay, no tonal component
            const int16_t noise = generate_noise();

            envelope = envelope * (65536 - (sample_position << 7)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, noise);
            break;
        }

        case DRUM_T_TOM: {
            // TOM: Tonal drum, pitch depends on note via voice->frequency_m100
            const int32_t sine_val = sine_lookup(__fast_mul(voice->frequency_m100, sample_position));
            const int16_t noise = generate_noise();

            // Fast decay
            envelope = envelope * (65536 - (sample_position << 5)) >> 16;
            if (envelope < 0) envelope = 0;

            // Mostly tone + a bit of noise for attack
            sample = __fast_mul(envelope, sine_val + (noise >> 2));
            break;
        }

        case DRUM_T_HIHAT_C: {
            // CLOSED HI-HAT: High-frequency noise, fast decay
            const int16_t noise1 = generate_noise();
            const int16_t noise2 = generate_noise();
            const int16_t filtered = noise1 ^ noise2; // XOR for bright character without static state

            // Very fast decay
            envelope = envelope * (65536 - (sample_position << 9)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, filtered);
            break;
        }

        case DRUM_T_HIHAT_O: {
            // OPEN HI-HAT: Longer decay high-frequency noise
            const int16_t noise1 = generate_noise();
            const int16_t noise2 = generate_noise();
            const int16_t filtered = noise1 ^ noise2; // XOR for bright character without static state

            // Slower decay than closed hi-hat
            envelope = envelope * (65536 - (sample_position << 6)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, filtered);
            break;
        }

        case DRUM_T_CRASH: {
            // CRASH CYMBAL: Complex noise with slow decay
            const int16_t noise1 = generate_noise();
            const int16_t noise2 = generate_noise();
            int16_t mixed = (noise1 + noise2) >> 1;

            // Ring modulation for metallic sound
            const int32_t modulator = sine_lookup(__fast_mul(80000, sample_position)); // 800Hz
            mixed = __fast_mul(mixed, modulator) >> 7;

            // Slow decay
            envelope = envelope * (65536 - (sample_position << 4)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, mixed);
            break;
        }

        case DRUM_T_RIDE: {
            // RIDE CYMBAL: Tonal component + noise
            const int32_t sine_val = sine_lookup(__fast_mul(50000, sample_position)); // 500Hz bell
            const int16_t noise = generate_noise();
            const int16_t mixed = (sine_val + noise) >> 1;

            // Medium decay
            envelope = envelope * (65536 - (sample_position << 5)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, mixed);
            break;
        }

        case DRUM_T_COWBELL: {
            // COWBELL: Pure high tone, fast decay
            const int32_t sine_val = sine_lookup(__fast_mul(80000, sample_position)); // ~800Hz

            envelope = envelope * (65536 - (sample_position << 6)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, sine_val);
            break;
        }

        default: {
            // GENERIC PERCUSSION: Noise burst
            const int16_t noise = generate_noise();

            // Fast decay
            envelope = envelope * (65536 - (sample_position << 7)) >> 16;
            if (envelope < 0) envelope = 0;

            sample = __fast_mul(envelope, noise);
            break;
        }
    }

    return sample >> 2; // Scale down to prevent clipping
}

// Modified midi_sample function
static INLINE int16_t midi_sample() {
    if (__builtin_expect(!active_voice_bitmask, 0)) return 0;

    int32_t sample = 0;
    uint32_t active_voices = active_voice_bitmask;

    do {
        const uint32_t voice_index = __builtin_ctz(active_voices);
        const uint32_t voice_bit = 1U << voice_index;
        active_voices ^= voice_bit;

        midi_voice_t *__restrict voice = &midi_voices[voice_index];
        const uint16_t sample_position = voice->sample_position++;

        // Check if this is a drum channel (channel 9)
        if (voice->channel == 9) {
            sample += generate_drum_sample(voice, sample_position);
        } else {
            // Melodic synthesis with exponential envelope

            // Envelope update every 256 samples (~172 Hz update rate)
            if (__builtin_expect((sample_position & 255) == 0 && sample_position > 0, 0)) {
                if (voice->attack_target) {
                    // Attack phase: rise toward target
                    voice->velocity += ((voice->attack_target - voice->velocity) >> voice->decay_shift) | 1;
                    if (voice->velocity >= voice->attack_target) {
                        voice->velocity = voice->attack_target;
                        voice->attack_target = 0;
                        // Restore decay parameters from program table
                        const gm_envelope_t *env = &gm_envelopes[midi_channels[voice->channel].program];
                        voice->decay_shift = env->decay_shift;
                    }
                } else {
                    const uint8_t target = voice->sustain_level;
                    if (voice->velocity > target) {
                        voice->velocity -= ((voice->velocity - target) >> voice->decay_shift) | 1;
                        if (voice->velocity <= 1 && target == 0) {
                            active_voice_bitmask &= ~voice_bit;
                            continue;
                        }
                    }
                }
            }

            const int32_t sine_val = sine_lookup(__fast_mul(voice->frequency_m100, sample_position));
            sample += __fast_mul(voice->velocity, sine_val);
        }
    } while (active_voices);

    return sample >> 2;
}

// Optimized pitch bend calculation with lookup table or approximation
static INLINE int32_t apply_pitch(const int32_t base_frequency, const int32_t cents) {
    // Optimized: avoid division if cents is zero
    return __builtin_expect(cents == 0, 1) ? base_frequency : (base_frequency * cents + 5000) / 10000;
}


static INLINE void parse_midi(const midi_command_t *message) {
    const uint8_t channel = message->command & 0xf;

    switch (message->command >> 4) {
        case 0x9: // Note ON
            if (__builtin_expect(message->velocity != 0, 1)) {
                // Kill previous voice with same note+channel (MIDI spec: re-trigger)
                {
                    uint32_t voices_to_check = active_voice_bitmask;
                    while (voices_to_check) {
                        const uint32_t vs = __builtin_ctz(voices_to_check);
                        voices_to_check &= ~(1U << vs);
                        if (midi_voices[vs].channel == channel && midi_voices[vs].note == message->note) {
                            CLEAR_ACTIVE_VOICE(vs);
                            break;
                        }
                    }
                }

                // Find free voice slot using bit operations
                const uint32_t free_voices = ~active_voice_bitmask;
                if (__builtin_expect(free_voices != 0, 1)) {
                    const uint32_t voice_slot = __builtin_ctz(free_voices);
                    if (voice_slot < MAX_MIDI_VOICES) {
                        midi_voice_t *__restrict voice = &midi_voices[voice_slot];

                        // Initialize voice data
                        voice->sample_position = 0;
                        voice->channel = channel;
                        voice->note = message->note;
                        voice->velocity_base = message->velocity;

                        // Apply pitch bend and volume in one go
                        voice->frequency_m100 = apply_pitch(
                            note_frequencies_m_100[message->note],
                            midi_channels[channel].pitch
                        );

                        const uint8_t ch_volume = midi_channels[channel].volume;
                        voice->velocity = (ch_volume * message->velocity) >> 7;

                        // Set envelope from GM program table
                        const gm_envelope_t *env = &gm_envelopes[midi_channels[channel].program];
                        voice->sustain_level = env->sustain_level;
                        if (env->attack_shift) {
                            // Slow attack: start from zero, rise to target
                            voice->attack_target = voice->velocity;
                            voice->velocity = 0;
                            voice->decay_shift = env->attack_shift;
                        } else {
                            // Instant attack
                            voice->attack_target = 0;
                            voice->decay_shift = env->decay_shift;
                        }

                        SET_ACTIVE_VOICE(voice_slot);
                        break;
                    }
                }
            } // else do note off
        case 0x8: // Note OFF
            /* Probably we should
             * Find the first and last entry in the voices list with matching channel, key and look up the smallest play position
             */
            if (!IS_CHANNEL_SUSTAIN(channel)) {
                // Optimized voice search with early termination
                uint32_t voices_to_check = active_voice_bitmask;
                while (voices_to_check) {
                    const uint32_t voice_slot = __builtin_ctz(voices_to_check);
                    voices_to_check &= ~(1U << voice_slot);

                    midi_voice_t *__restrict voice = &midi_voices[voice_slot];
                    if (voice->channel == channel && voice->note == message->note) {
                        if (__builtin_expect(channel == 9, 0)) {
                            CLEAR_ACTIVE_VOICE(voice_slot);
                        } else {
                            // Switch to release: fast exponential decay to zero
                            voice->attack_target = 0;
                            voice->sustain_level = 0;
                            voice->decay_shift = 2;
                        }
                        break;
                    }
                }
            }
            break;


        case 0xB: // Controller Change
            switch (message->note) {
                case 0x7: // Volume change
                    debug_log("[MIDI] Channel %i volume %i\n", channel, midi_channels[channel].volume);
                    midi_channels[channel].volume = message->velocity;
                    for (int voice_slot = 0; voice_slot < MAX_MIDI_VOICES; ++voice_slot) {
                        if (midi_voices[voice_slot].channel == channel) {
                            const uint8_t new_vel = message->velocity * midi_voices[voice_slot].velocity_base >> 7;
                            midi_voices[voice_slot].velocity = new_vel;
                            if (midi_voices[voice_slot].attack_target)
                                midi_voices[voice_slot].attack_target = new_vel;
                        }
                    }
                    break;
                /*
                case 0x0A: //  Left-right pan
                    break;
                */
                case 0x40: // Sustain
                    if (message->velocity & 64) {
                        SET_CHANNEL_SUSTAIN(channel);
                    } else {
                        CLEAR_CHANNEL_SUSTAIN(channel);

                        for (int voice_slot = 0; voice_slot < MAX_MIDI_VOICES; ++voice_slot)
                            if (midi_voices[voice_slot].channel == channel) {
                                if (channel == 9) {
                                    CLEAR_ACTIVE_VOICE(voice_slot);
                                } else {
                                    // Switch to release: fast decay to zero
                                    midi_voices[voice_slot].attack_target = 0;
                                    midi_voices[voice_slot].sustain_level = 0;
                                    midi_voices[voice_slot].decay_shift = 2;
                                }
                            }
                    }
                    debug_log("[MIDI] Channel %i sustain %i\n", channel, message->velocity);

                    break;
                case 0x78: // All Sound Off
                case 0x7b: // All Notes Off
                    active_voice_bitmask = 0;
                    channels_sustain_bitmask = 0;
                    memset(midi_voices, 0, sizeof(midi_voices));
                    /*
                                    for (int voice_number = 0; voice_number < MAX_MIDI_VOICES; ++voice_number)
                                        midi_voices[voice_number].playing = 0;
                    */
                    break;
                case 0x79: // all controllers off
                    memset(midi_channels, 0, sizeof(midi_channel_t) * MIDI_CHANNELS);
                    for (int i = 0; i < MIDI_CHANNELS; i++)
                        midi_channels[i].volume = 100;
                    break;
                default:
                    debug_log("[MIDI] Unknown channel %i controller %02x %02x\n", channel, message->note,
                              message->velocity);
            }
            break;

        case 0xC: // Channel Program
            midi_channels[channel].program = message->note;

            for (int voice_slot = 0; voice_slot < MAX_MIDI_VOICES; ++voice_slot)
                if (midi_voices[voice_slot].channel == channel) {
                    CLEAR_ACTIVE_VOICE(voice_slot);
                }

            debug_log("[MIDI] Channel %i program %i\n", message->command & 0xf, message->note);
            break;
        case 0xE: {
            const int pitch_bend = (message->velocity * 128 + message->note);
            const int cents = calc_pitch_bend(pitch_bend);
            midi_channels[channel].pitch = cents;
            debug_log("[MIDI] Channel %i pitch_bend %i cents %i 44000->%i\n", channel, pitch_bend - 8192, cents,
                      apply_pitch(44000, cents));
            for (int voice_slot = 0; voice_slot < MAX_MIDI_VOICES; ++voice_slot)
                if (midi_voices[voice_slot].channel == channel) {
                    midi_voices[voice_slot].frequency_m100 = apply_pitch(
                        note_frequencies_m_100[midi_voices[voice_slot].note], cents);
                }
            break;
        }
        default:
            debug_log("[MIDI] Unknown channel %i command %x message %04x \n", channel, message->command >> 4,
                      midi_command);
            break;
    }
}
