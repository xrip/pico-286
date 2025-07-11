#pragma once
#pragma GCC optimize("Ofast")
// http://qzx.com/pc-gpe/gameblst.txt

#define MASTER_CLOCK 7159090
#define NOISE_FREQ_256 (MASTER_CLOCK / 256)
#define NOISE_FREQ_512 (MASTER_CLOCK / 512)
#define NOISE_FREQ_1024 (MASTER_CLOCK / 1024)

static const uint16_t noise_freq_table[4] = {
    NOISE_FREQ_256, NOISE_FREQ_512, NOISE_FREQ_1024, 0 // 0 will be replaced by voice freq
};

// Volume LUT optimization - pre-calculated volume values
static const int16_t volume_lut[16] = {
    0,      // 0: (0 << 6) + (0 << 5) = 0
    96,     // 1: (1 << 6) + (1 << 5) = 64 + 32 = 96
    192,    // 2: (2 << 6) + (2 << 5) = 128 + 64 = 192
    288,    // 3: (3 << 6) + (3 << 5) = 192 + 96 = 288
    384,    // 4: (4 << 6) + (4 << 5) = 256 + 128 = 384
    480,    // 5: (5 << 6) + (5 << 5) = 320 + 160 = 480
    576,    // 6: (6 << 6) + (6 << 5) = 384 + 192 = 576
    672,    // 7: (7 << 6) + (7 << 5) = 448 + 224 = 672
    768,    // 8: (8 << 6) + (8 << 5) = 512 + 256 = 768
    864,    // 9: (9 << 6) + (9 << 5) = 576 + 288 = 864
    960,    // 10: (10 << 6) + (10 << 5) = 640 + 320 = 960
    1056,   // 11: (11 << 6) + (11 << 5) = 704 + 352 = 1056
    1152,   // 12: (12 << 6) + (12 << 5) = 768 + 384 = 1152
    1248,   // 13: (13 << 6) + (13 << 5) = 832 + 416 = 1248
    1344,   // 14: (14 << 6) + (14 << 5) = 896 + 448 = 1344
    1440    // 15: (15 << 6) + (15 << 5) = 960 + 480 = 1440
};

static int register_addresses[2] = {0};
static uint8_t cms_registers[2][32] = {0};
static uint16_t frequency_latch[2][6] = {0};
static int voice_frequency[2][6] = {0};
static int voice_counter[2][6] = {0};
static int voice_volume[2][6][2] = {0};
static int voice_state[2][6] = {0};
static uint16_t noise_shift_register[2][2] = {0};
static uint16_t cms_noise_frequency[2][2] = {0};
static int cms_noise_counter[2][2] = {0};
static uint8_t noise_type[2][2] = {0};

static uint8_t latched_data;

//} cms_t;
// static int16_t out_l = 0, out_r = 0;
static INLINE void cms_samples(int16_t *output) {
    for (int channel = 0; channel < 4; channel++) {
        const uint8_t chip_index = channel >> 1;
        const uint8_t noise_index = channel & 1;

        if (noise_type[chip_index][noise_index] < 3) {
            cms_noise_frequency[chip_index][noise_index] = noise_freq_table[noise_type[chip_index][noise_index]];
        } else {
            cms_noise_frequency[chip_index][noise_index] = voice_frequency[chip_index][noise_index ? 3 : 0];
        }

        if (channel < 2 && (cms_registers[channel][0x1C] & 1)) {
            for (int voice_index = 0; voice_index < 6; voice_index++) {
                // Use volume LUT for optimized volume calculation
                const int16_t volume_left = volume_lut[voice_volume[channel][voice_index][0]];
                const int16_t volume_right = volume_lut[voice_volume[channel][voice_index][1]];

                if (cms_registers[channel][0x14] & (1 << voice_index)) {
                    if (voice_state[channel][voice_index]) {
                        output[0] += volume_left;
                        output[1] += volume_right;
                    }
                    voice_counter[channel][voice_index] += voice_frequency[channel][voice_index];
                    if (voice_counter[channel][voice_index] >= 24000) {
                        voice_counter[channel][voice_index] -= 24000;
                        voice_state[channel][voice_index] ^= 1;
                    }
                } else if (cms_registers[channel][0x15] & (1 << voice_index)) {
                    if (noise_shift_register[channel][voice_index / 3] & 1) {
                        output[0] += volume_left;
                        output[1] += volume_right;
                    }
                }

                if (voice_index < 2) {
                    cms_noise_counter[channel][voice_index] += cms_noise_frequency[channel][voice_index];
                    while (cms_noise_counter[channel][voice_index] >= 24000) {
                        cms_noise_counter[channel][voice_index] -= 24000;
                        noise_shift_register[channel][voice_index] = noise_shift_register[channel][voice_index] << 1 | !(
                                                (noise_shift_register[channel][voice_index] & 0x4000) >> 8 ^ noise_shift_register[channel][voice_index] & 0x40);
                    }
                }
            }
        }
    }
}

static INLINE void cms_out(const uint16_t address, const uint16_t value) {
    int chip_select = (address & 2) >> 1;
    int voice_number;

    switch (address & 0xf) {
        case 1: register_addresses[0] = value & 31;
            break;
        case 3: register_addresses[1] = value & 31;
            break;

        case 0:
        case 2:
            cms_registers[chip_select][register_addresses[chip_select] & 31] = value;
            switch (register_addresses[chip_select] & 31) {
                case 0x00 ... 0x05: // Volume control
                    voice_number = register_addresses[chip_select] & 7;
                    voice_volume[chip_select][voice_number][0] = value & 0xf;
                    voice_volume[chip_select][voice_number][1] = value >> 4;
                    break;

                case 0x08 ... 0x0D: // Frequency control
                    voice_number = register_addresses[chip_select] & 7;
                    frequency_latch[chip_select][voice_number] = (frequency_latch[chip_select][voice_number] & 0x700) | value;
                    voice_frequency[chip_select][voice_number] = (MASTER_CLOCK / 512 << (frequency_latch[chip_select][voice_number] >> 8)) / (
                                            511 - (frequency_latch[chip_select][voice_number] & 255));
                    break;

                case 0x10 ... 0x12: // Octave control
                    voice_number = (register_addresses[chip_select] & 3) << 1;
                    frequency_latch[chip_select][voice_number] = (frequency_latch[chip_select][voice_number] & 0xFF) | ((value & 7) << 8);
                    frequency_latch[chip_select][voice_number + 1] = (frequency_latch[chip_select][voice_number + 1] & 0xFF) | ((value & 0x70) << 4);
                    voice_frequency[chip_select][voice_number] = (MASTER_CLOCK / 512 << (frequency_latch[chip_select][voice_number] >> 8)) / (
                                            511 - (frequency_latch[chip_select][voice_number] & 255));
                    voice_frequency[chip_select][voice_number + 1] = (MASTER_CLOCK / 512 << (frequency_latch[chip_select][voice_number + 1] >> 8)) / (
                                                511 - (frequency_latch[chip_select][voice_number + 1] & 255));
                    break;

                case 0x16: // Noise type
                    noise_type[chip_select][0] = value & 3;
                    noise_type[chip_select][1] = (value >> 4) & 3;
                    break;
            }
            break;

        case 0x6:
        case 0x7:
            latched_data = value;
            break;
    }
}

static INLINE uint8_t cms_in(const uint16_t address) {
    switch (address & 0xf) {
        case 0x1: return register_addresses[0];
        case 0x3: return register_addresses[1];
        case 0x4: return 0x7f;
        case 0xa:
        case 0xb: return latched_data;
    }
    return 0xff;
}