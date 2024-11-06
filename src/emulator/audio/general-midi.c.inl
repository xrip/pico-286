#pragma once
#pragma GCC optimize("Ofast")
#include "general-midi.h"
// #define USE_SAMPLES
#if defined(USE_SAMPLES)
#include "emulator/drum/drum.h"
#include "emulator/acoustic/acoustic.h"
#endif
#define MAX_MIDI_VOICES 24
#define MIDI_CHANNELS 16

struct midi_voice_s {
    uint8_t playing;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    int32_t frequency_m100;
    int32_t sample_position;
} midi_voices[MAX_MIDI_VOICES] = {0};
// Bitmask for active voices
uint32_t active_voice_bitmask = 0;

typedef struct midi_channel_s {
    uint8_t program;
    uint8_t volume;
    int pitch;
} midi_channel_t;
// Bitmask for sustained channels
uint32_t channels_sustain_bitmask = 0;

typedef struct __attribute__((packed)) {
    uint8_t command;
    uint8_t note;
    uint8_t velocity;
    uint8_t other;
} midi_command_t;

midi_channel_t midi_channels[MIDI_CHANNELS] = {0};




#define SET_ACTIVE_VOICE(idx) (active_voice_bitmask |= (1U << (idx)))
#define CLEAR_ACTIVE_VOICE(idx) (active_voice_bitmask &= ~(1U << (idx)))
#define IS_ACTIVE_VOICE(idx) ((active_voice_bitmask & (1U << (idx))) != 0)

// todo if multiple sustained channels available or only one?
#define SET_CHANNEL_SUSTAIN(idx) (channels_sustain_bitmask |= (1U << (idx)))
#define CLEAR_CHANNEL_SUSTAIN(idx) (channels_sustain_bitmask &= ~(1U << (idx)))
#define IS_CHANNEL_SUSTAIN(idx) ((channels_sustain_bitmask & (1U << (idx))) != 0)


static INLINE int8_t sin_m_128(size_t idx) {
    if (idx < 1024) return sin_m128[idx];
    if (idx < 2048) return sin_m128[2047 - idx];
    if (idx < (2048 + 1024)) return -sin_m128[idx - 2048];
    return -sin_m128[4095 - idx];
}

static INLINE int32_t sin100sf_m_128_t(int32_t a) {
    static const size_t step = SOUND_FREQUENCY * 100 / 4096;
    return sin_m_128((a / step) & 4095);
}

int16_t midi_sample() {
    if (!active_voice_bitmask && !channels_sustain_bitmask) return 0;

    int32_t sample = 0;
    struct midi_voice_s *voice = (struct midi_voice_s *) midi_voices;
    for (int voice_number = 0; voice_number < MAX_MIDI_VOICES; ++voice_number) {
        if (IS_ACTIVE_VOICE(voice_number) || IS_CHANNEL_SUSTAIN(voice->channel)) {
#if defined(USE_SAMPLES)
            if (channel->program < 7) {
                const int16_t* smpl = (int16_t*)_Yamaha_TG77_Ivory_Piano_C6_wav; /// _Casio_VZ_10M_Piano_C2_wav;
                size_t z = __fast_mul(voice->frequency_m100, voice->sample_position++) / 104650;
                if (z < 217022 / 2)
                    sample += __fast_mul(voice->velocity, smpl[z]);  /// C6 == 1046.5 Hz;  6541]; // C2 == 65.41Hz
                else
                    voice->playing = 0;
            } else if (voice->channel == 9) {
                const int8_t* smpl;
                switch (voice->note) {
                    case 35: smpl = _35_wav; break;
                    case 36: smpl = _36_wav; break;
                    case 37: smpl = _37_wav; break;
                    case 38: smpl = _38_wav; break;
                    case 39: smpl = _39_wav; break;
                    case 40: smpl = _40_wav; break;
                    case 41: smpl = _41_wav; break;
                    case 42: smpl = _42_wav; break;
                    case 43: smpl = _43_wav; break;
                    case 44: smpl = _44_wav; break;
                    case 45: smpl = _45_wav; break;
                    case 46: smpl = _46_wav; break;
                    case 47: smpl = _47_wav; break;
                    case 48: smpl = _48_wav; break;
                    case 49: smpl = _49_wav; break;
                    //case 50: smpl = _50_wav; break;
                    case 51: smpl = _51_wav; break;
                    case 52: smpl = _52_wav; break;
                    case 53: smpl = _53_wav; break;
                    case 54: smpl = _54_wav; break;
                    case 55: smpl = _55_wav; break;
                    case 56: smpl = _56_wav; break;
                    case 57: smpl = _57_wav; break;
                    case 58: smpl = _58_wav; break;
                    case 59: smpl = _59_wav; break;
                    case 60: smpl = _60_wav; break;
                    case 61: smpl = _61_wav; break;
                    case 62: smpl = _62_wav; break;
                    case 63: smpl = _63_wav; break;
                    case 64: smpl = _64_wav; break;
                    case 65: smpl = _65_wav; break;
                    case 66: smpl = _66_wav; break;
                    case 67: smpl = _67_wav; break;
                    case 68: smpl = _68_wav; break;
                    case 69: smpl = _69_wav; break;
                    case 70: smpl = _70_wav; break;
                    case 71: smpl = _71_wav; break;
                    case 72: smpl = _72_wav; break;
                    case 73: smpl = _73_wav; break;
                    case 74: smpl = _74_wav; break;
                    case 75: smpl = _75_wav; break;
                    case 76: smpl = _76_wav; break;
                    case 77: smpl = _77_wav; break;
                    case 78: smpl = _78_wav; break;
                    case 79: smpl = _79_wav; break;
                    case 80: smpl = _80_wav; break;
                    case 81: smpl = _81_wav; break;
                    default: smpl = _ZZ_Default_wav; break;
                }
                sample += __fast_mul(voice->velocity, smpl[voice->sample_position++]) << 8; // TODO: sz
            } else
#endif
            {
                sample += __fast_mul(voice->velocity, sin100sf_m_128_t(__fast_mul(voice->frequency_m100, voice->sample_position++))) << 8;
            }
        }
        voice++;
    }
    return sample >> 11; // / 128 * 32 + >> 8
}

// todo: validate is it correct??
static INLINE int32_t apply_pitch(const int32_t base_frequency, int cents) {
    return cents ? (base_frequency * cents + 5000) / 10000 : base_frequency;
}

static INLINE void parse_midi(const uint32_t midi_command) {
    const midi_command_t *message = (midi_command_t *) &midi_command;
    const uint8_t channel = message->command & 0xf;
    // struct midi_voice_s *channel = &midi_voices[message->command & 0xf];
    switch (message->command >> 4) {
        case 0x9: // Note ON
            if (message->velocity) {
                for (int voice_number = 0; voice_number < MAX_MIDI_VOICES; ++voice_number) {
                    struct midi_voice_s *voice = &midi_voices[voice_number];
                    if (!IS_ACTIVE_VOICE(voice_number)) {
                        voice->playing = 1;
                        voice->channel = channel;
                        voice->sample_position = 0;
                        voice->note = message->note;

                        voice->frequency_m100 = apply_pitch(
                            note_frequencies_m_100[message->note], midi_channels[channel].pitch
                        );

                        if (midi_channels[voice->channel].volume) {
                            voice->velocity = midi_channels[channel].volume * message->velocity >> 7;
                        } else {
                            voice->velocity = message->velocity;
                        }
                        SET_ACTIVE_VOICE(voice_number);
                        return;
                    }
                }

                break;
            } // else do note off
        case 0x8: // Note OFF
            for (int voice_number = 0; voice_number < MAX_MIDI_VOICES; ++voice_number) {
                struct midi_voice_s *voice = &midi_voices[voice_number];
                if (IS_ACTIVE_VOICE(voice_number) && voice->note == message->note) {
                    voice->playing = 0;
                    CLEAR_ACTIVE_VOICE(voice_number);
                    return;
                }
            }
            break;

        case 0xC: {
            midi_channels[channel].program = message->note;
            // printf("channel %i program %i\n", message->command & 0xf, message->note);
            break;
        }
        // MIDI Controller message
        case 0xB: {
            switch (message->note) {
                case 0x0A:
                    //  Left-right pan
                    break;
                case 0x7:
                    // printf("channel %i volume %i\n", channel, midi_channels[channel].volume);
                    midi_channels[channel].volume = message->velocity;
                    for (int voice_number = 0; voice_number < MAX_MIDI_VOICES; ++voice_number) {
                        if (midi_voices[voice_number].channel == channel) {
                            midi_voices[voice_number].velocity =
                                    message->velocity * midi_voices[voice_number].velocity >> 7;
                        }
                    }
                    break;
                case 0x40:
                    message->velocity >= 64 ? SET_CHANNEL_SUSTAIN(channel) : SET_CHANNEL_SUSTAIN(channel);
                    // printf("channel %i sustain %i\n", channel, message->velocity);
                    break;
                case 0x78:
                case 0x7b:
                    for (int voice_number = 0; voice_number < MAX_MIDI_VOICES; ++voice_number)
                        midi_voices[voice_number].playing = 0;

                    active_voice_bitmask = 0;
                    channels_sustain_bitmask = 0;
                    break;
                case 0x79: // all controllers off
                    memset(midi_channels, 0, sizeof(midi_channels));
                    break;
                default:
                    printf("unknown controller %x\n", message->note);
            }
            break;
        }
        case 0xE:
            const int pitch_bend = (message->velocity * 128 + message->note) - 8192;
            const int cents = pitch_bend >= 0
                                  ? pitch_pows[abs(pitch_bend)]
                                  : 10000 * pitch_pows[abs(pitch_bend)] / 10000;

            midi_channels[channel].pitch = cents;
        //printf("channel %i dev %i exponent_scaled %i multip %i  44000->%i\n", channel, devitation, cents, 1, apply_pitch_bend(44000, cents));

            for (int voice_number = 0; voice_number < MAX_MIDI_VOICES; ++voice_number)
                if (midi_voices[voice_number].channel == channel) {
                    midi_voices[voice_number].frequency_m100 = apply_pitch(midi_voices[voice_number].frequency_m100, cents);
                }
            break;

        default:
#ifdef DEBUG_MPU401
            printf("Unknown command %x message %04x \n", message->command >> 4, midi_command);
#endif
            break;
    }
    // printf("%x %x %x %x %x\n", message->command, message->note, message->velocity, message->other, midi_command );
}