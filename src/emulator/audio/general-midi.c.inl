#pragma GCC optimize("Ofast")
#pragma once
#include "general-midi.h"

// #define DEBUG_MIDI

#if defined(DEBUG_MIDI)
#define debug_log(...) printf(__VA_ARGS__)
#else
#define debug_log(...)
#endif

// #define USE_SAMPLES
#if defined(USE_SAMPLES)
#include "emulator/drum/drum.h"
#include "emulator/acoustic/acoustic.h"
#endif

#define MAX_MIDI_VOICES 32
#define MIDI_CHANNELS 16

#define RELEASE_DURATION (SOUND_FREQUENCY / 8) // Duration for note release

typedef struct midi_voice_s {
    uint8_t voice_slot;
    // uint8_t playing;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    uint8_t velocity_base;

    uint8_t *sample;
    int32_t frequency_m100;
    uint16_t sample_position;
    uint16_t release_position;
} midi_voice_t;

typedef struct midi_channel_s {
    uint8_t program;
    uint8_t volume;
    int pitch;
} midi_channel_t;

typedef struct __attribute__((packed)) {
    uint8_t command;
    uint8_t note;
    uint8_t velocity;
    uint8_t other;
} midi_command_t;

static midi_voice_t midi_voices[MAX_MIDI_VOICES] = {0};
static midi_channel_t midi_channels[MIDI_CHANNELS] = {0};

// Bitmask for active voices
static uint32_t active_voice_bitmask = 0;
// Bitmask for sustained channels
static uint32_t channels_sustain_bitmask = 0;


#define SET_ACTIVE_VOICE(idx) (active_voice_bitmask |= (1U << (idx)))
#define CLEAR_ACTIVE_VOICE(idx) (active_voice_bitmask &= ~(1U << (idx)))
#define IS_ACTIVE_VOICE(idx) ((active_voice_bitmask & (1U << (idx))) != 0)

#define SET_CHANNEL_SUSTAIN(idx) (channels_sustain_bitmask |= (1U << (idx)))
#define CLEAR_CHANNEL_SUSTAIN(idx) (channels_sustain_bitmask &= ~(1U << (idx)))
#define IS_CHANNEL_SUSTAIN(idx) ((channels_sustain_bitmask & (1U << (idx))) != 0)


#define SIN_STEP (SOUND_FREQUENCY * 100 / 4096)
static INLINE int32_t sine_lookup(const uint32_t angle) {
    const uint16_t index = angle / SIN_STEP & 4095; // TODO: Should it be & 4095 or % 4096???
    return index < 2048
               ? sin_m128[index < 1024 ? index : 2047 - index]
               : -sin_m128[index < 3072 ? index - 2048 : 4095 - index];
}
static ///INLINE
 int16_t __time_critical_func()
 midi_sample() {
    if (!active_voice_bitmask) return 0;

    int32_t sample = 0;
    uint32_t active_voices = active_voice_bitmask;

    while (active_voices) {
        const uint32_t voice_index = __builtin_ctz(active_voices); // Index of the active voice
        active_voices &= ~(1U << voice_index); // Clear the bit

        struct midi_voice_s *voice = &midi_voices[voice_index];

        const uint16_t sample_position = voice->sample_position++;

        // Poor man's ADSR
        if (sample_position == SOUND_FREQUENCY / 2) {
            // Sustain state
            voice->velocity -= voice->velocity >> 2;
        } else if (sample_position && sample_position == voice->release_position) {
            // Release state
            CLEAR_ACTIVE_VOICE(voice->voice_slot);
            // continue; // Skip to next voice. Important.
        }

        {
            sample += __fast_mul(voice->velocity, sine_lookup(__fast_mul(voice->frequency_m100, sample_position)));
        }
    }

    return (int32_t) (sample >> 2); // todo add pass filter
}

// todo: validate is it correct??
static INLINE int32_t apply_pitch(const int32_t base_frequency, const int cents) {
    return cents ? (base_frequency * cents + 5000) / 10000 : base_frequency;
}


static INLINE void parse_midi(const midi_command_t *message) {
    const uint8_t channel = message->command & 0xf;

    switch (message->command >> 4) {
        case 0x9: // Note ON
            if (message->velocity) {
                for (int voice_slot = 0; voice_slot < MAX_MIDI_VOICES; ++voice_slot) {
                    if (!IS_ACTIVE_VOICE(voice_slot)) {
                        struct midi_voice_s *voice = &midi_voices[voice_slot];
                        // voice->playing = 1;
                        voice->voice_slot = voice_slot;
                        voice->sample_position = 0;
                        voice->release_position = 0;
                        voice->channel = channel;
                        voice->note = message->note;

                        voice->frequency_m100 = apply_pitch(
                            note_frequencies_m_100[message->note], midi_channels[channel].pitch
                        );
                        voice->velocity_base = message->velocity;
                        voice->velocity = midi_channels[voice->channel].volume
                                              ? midi_channels[channel].volume * message->velocity >> 7
                                              : message->velocity;

#if defined(USE_SAMPLES)
        if (voice->channel == 9) {
            switch (voice->note) {
                case 35: voice->sample = __35_raw; break;
                case 36: voice->sample = __36_raw; break;
                case 37: voice->sample = __37_raw; break;
                case 38: voice->sample = __38_raw; break;
                case 39: voice->sample = __39_raw; break;
                case 40: voice->sample = __40_raw; break;
                case 41: voice->sample = __41_raw; break;
                case 42: voice->sample = __42_raw; break;
                case 43: voice->sample = __43_raw; break;
                case 44: voice->sample = __44_raw; break;
                case 45: voice->sample = __45_raw; break;
                case 46: voice->sample = __46_raw; break;
                case 47: voice->sample = __47_raw; break;
                case 48: voice->sample = __48_raw; break;
                case 49: voice->sample = __49_raw; break;
                case 50: voice->sample = __50_raw; break;
                case 51: voice->sample = __51_raw; break;
                case 52: voice->sample = __52_raw; break;
                case 53: voice->sample = __53_raw; break;
                case 54: voice->sample = __54_raw; break;
                case 55: voice->sample = __55_raw; break;
                case 56: voice->sample = __56_raw; break;
                case 57: voice->sample = __57_raw; break;
                case 58: voice->sample = __58_raw; break;
                case 59: voice->sample = __59_raw; break;
                case 60: voice->sample = __60_raw; break;
                case 61: voice->sample = __61_raw; break;
                case 62: voice->sample = __62_raw; break;
                case 63: voice->sample = __63_raw; break;
                case 64: voice->sample = __64_raw; break;
                case 65: voice->sample = __65_raw; break;
                case 66: voice->sample = __66_raw; break;
                case 67: voice->sample = __67_raw; break;
                case 68: voice->sample = __68_raw; break;
                case 69: voice->sample = __69_raw; break;
                case 70: voice->sample = __70_raw; break;
                case 71: voice->sample = __71_raw; break;
                case 72: voice->sample = __72_raw; break;
                case 73: voice->sample = __73_raw; break;
                case 74: voice->sample = __74_raw; break;
                case 75: voice->sample = __75_raw; break;
                case 76: voice->sample = __76_raw; break;
                case 77: voice->sample = __77_raw; break;
                case 78: voice->sample = __78_raw; break;
                case 79: voice->sample = __79_raw; break;
                case 80: voice->sample = __80_raw; break;
                case 81: voice->sample = __81_raw; break;
                default: voice->sample = __35_raw; break;
            }
        } else {
            voice->sample = NULL;
        }
#endif

                        SET_ACTIVE_VOICE(voice_slot);
                        return;
                    }
                }

                break;
            } // else do note off
        case 0x8: // Note OFF
            /* Probably we should
             * Find the first and last entry in the voices list with matching channel, key and look up the smallest play position
             */
            if (!IS_CHANNEL_SUSTAIN(channel))
                for (int voice_slot = 0; voice_slot < MAX_MIDI_VOICES; ++voice_slot) {
                    const struct midi_voice_s *voice = &midi_voices[voice_slot];
                    if (IS_ACTIVE_VOICE(voice_slot) && voice->channel == channel && voice->note == message->note) {
                        if (channel == 9) {
                            CLEAR_ACTIVE_VOICE(voice_slot);
                        } else {
                            midi_voices[voice_slot].velocity /= 2;
                            midi_voices[voice_slot].release_position =
                                    midi_voices[voice_slot].sample_position + RELEASE_DURATION;
                        }
                        return;
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
                            midi_voices[voice_slot].velocity =
                                    message->velocity * midi_voices[voice_slot].velocity_base >> 7;
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
                                    midi_voices[voice_slot].velocity /= 2;
                                    midi_voices[voice_slot].release_position =
                                            midi_voices[voice_slot].sample_position + RELEASE_DURATION;
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
        case 0xE:
            const int pitch_bend = (message->velocity * 128 + message->note);
            const int cents = pitch_pows[pitch_bend];
            midi_channels[channel].pitch = cents;
            debug_log("[MIDI] Channel %i pitch_bend %i cents %i 44000->%i\n", channel, pitch_bend - 8192, cents,
                      apply_pitch(44000, cents));
            for (int voice_slot = 0; voice_slot < MAX_MIDI_VOICES; ++voice_slot)
                if (midi_voices[voice_slot].channel == channel) {
                    midi_voices[voice_slot].frequency_m100 = apply_pitch(
                        midi_voices[voice_slot].frequency_m100, cents);
                }
            break;
        default:
            debug_log("[MIDI] Unknown channel %i command %x message %04x \n", channel, message->command >> 4,
                      midi_command);
            break;
    }
}
