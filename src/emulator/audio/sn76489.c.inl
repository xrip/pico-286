/****************************************************************************

  emu76489.c -- SN76489 emulator by Mitsutaka Okazaki 2001-2016

  2001 08-13 : Version 1.00
  2001 10-03 : Version 1.01 -- Added SNG_set_quality().
  2004 05-23 : Version 1.10 -- Implemented GG stereo mode by RuRuRu
  2004 06-07 : Version 1.20 -- Improved the noise emulation.
  2015 12-13 : Version 1.21 -- Changed own integer types to C99 stdint.h types.
  2016 09-06 : Version 1.22 -- Support per-channel output.

  References:
    SN76489 data sheet
    sn76489.c   -- from MAME
    sn76489.txt -- from http://www.smspower.org/

*****************************************************************************/
// https://github.com/mamedev/mame/blob/master/src/devices/sound/sn76496.cpp
// https://www.zeridajh.org/articles/me_sn76489_sound_chip_details/index.html
#include "../emulator.h"

#define GETA_BITS 24
#define clock_increment_base (uint32_t) ((double) 3579545 * (1 << GETA_BITS) / (16 * SOUND_FREQUENCY))

static const uint8_t noise_parity_lookup[10] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0};
static uint32_t tone_counter[3];
static uint32_t tone_volume[3];
static uint32_t tone_frequency[3];
static uint32_t tone_output_state[3];
static uint32_t channel_mute[3];

static uint32_t noise_lfsr_seed;
static uint32_t noise_counter;
static uint32_t noise_frequency;
static uint32_t noise_volume;
static uint32_t noise_type_mode;
static uint32_t noise_uses_tone2_freq;

static uint32_t master_counter;

/* rate converter */
static uint32_t sample_rate_step;
static uint32_t emulator_time;
static uint32_t emulator_step;

static uint32_t register_address;

static uint32_t stereo_mask;

// Pre-scaled volume table (eliminates << 4 in hot path)
static const uint16_t volume_table_scaled[16] = {
    0xff0, 0xcb0, 0xa10, 0x800, 0x650, 0x500, 0x400, 0x330,
    0x280, 0x200, 0x190, 0x140, 0x100, 0x0c0, 0x0a0, 0x000
};

void sn76489_reset() {
    for (int channel_index = 0; channel_index < 3; channel_index++) {
        tone_counter[channel_index] = 0;
        tone_frequency[channel_index] = 0;
        tone_output_state[channel_index] = 0;
        tone_volume[channel_index] = 0x0f;
        channel_mute[channel_index] = 0;
    }

    register_address = 0;

    noise_lfsr_seed = 0x8000;
    noise_counter = 0;
    noise_frequency = 0;
    noise_volume = 0x0f;
    noise_type_mode = 0;
    noise_uses_tone2_freq = 0;

    stereo_mask = 0xFF;
    /*
     *The SN76489 is connected to a clock signal, which is commonly 3579545Hz for NTSC systems and 3546893Hz for PAL/SECAM systems (these are based on the associated TV colour subcarrier frequencies, and are common master clock speeds for many systems). It divides this clock by 16 to get its internal clock. The datasheets specify a maximum of 4MHz.
    */
}

static INLINE void sn76489_out(const uint16_t register_value) {
    if (register_value & 0x80) {
        //printf("OK");
        register_address = (register_value & 0x70) >> 4;
        switch (register_address) {
            case 0: // tone 0: frequency
            case 2: // tone 1: frequency
            case 4: // tone 2: frequency
                tone_frequency[register_address >> 1] = (tone_frequency[register_address >> 1] & 0x3F0) | (register_value & 0x0F);
                break;

            case 1: // tone 0: volume
            case 3: // tone 1: volume
            case 5: // tone 2: volume
                tone_volume[(register_address - 1) >> 1] = register_value & 0xF;
                break;

            case 6: // noise: frequency, mode
                noise_type_mode = (register_value & 4) >> 2;

                if ((register_value & 0x03) == 0x03) {
                    noise_frequency = tone_frequency[2];
                    noise_uses_tone2_freq = 1;
                } else {
                    noise_frequency = 32 << (register_value & 0x03);
                    noise_uses_tone2_freq = 0;
                }

                if (noise_frequency == 0)
                    noise_frequency = 1;

                noise_lfsr_seed = 0x8000;
                break;

            case 7: // noise: volume
                noise_volume = register_value & 0x0f;
                break;
        }
    } else {
        tone_frequency[register_address >> 1] = ((register_value & 0x3F) << 4) | (tone_frequency[register_address >> 1] & 0x0F);
    }
}

static INLINE int16_t sn76489_sample() {
    master_counter += clock_increment_base;
    const uint32_t clock_cycles = master_counter >> GETA_BITS;
    master_counter &= (1 << GETA_BITS) - 1;

    int32_t mixed_sample = 0; // Accumulate all channels here

    /* Noise */
    noise_counter += clock_cycles;
    if (noise_counter & 0x400) {
        if (noise_type_mode) /* White */
            noise_lfsr_seed = (noise_lfsr_seed >> 1) | (noise_parity_lookup[noise_lfsr_seed & 0x0009] << 15);
        else /* Periodic */
            noise_lfsr_seed = (noise_lfsr_seed >> 1) | ((noise_lfsr_seed & 1) << 15);

        if (noise_uses_tone2_freq)
            noise_counter -= tone_frequency[2];
        else
            noise_counter -= noise_frequency;
    }

    if (noise_lfsr_seed & 1) {
        mixed_sample += volume_table_scaled[noise_volume];
    }

    /* Tone */
#pragma GCC unroll(4)
    for (int channel_index = 0; channel_index < 3; channel_index++) {
        tone_counter[channel_index] += clock_cycles;
        if (tone_counter[channel_index] & 0x400) {
            if (tone_frequency[channel_index] > 1) {
                tone_output_state[channel_index] = !tone_output_state[channel_index];
                tone_counter[channel_index] -= tone_frequency[channel_index];
            } else {
                tone_output_state[channel_index] = 1;
            }
        }

        if (tone_output_state[channel_index] && !channel_mute[channel_index]) {
            mixed_sample += volume_table_scaled[tone_volume[channel_index]];
        }
    }

    // Final mixing and scaling (single operation instead of per-channel)
    return (int16_t) (mixed_sample >> 2); // Divide by 4 for proper scaling
}
