// https://the.earth.li/~tfm/oldpage/sb_dsp.html
// http://qzx.com/pc-gpe/sbdsp.txt
// https://github.com/joncampbell123/dosbox-x/wiki/Hardware:Sound-Blaster:DSP-commands
/*
  XTulator: A portable, open-source 80186 PC emulator.
  Copyright (C)2020 Mike Chambers

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
	Emulation of the Sound Blaster Pro 2 (DSP 3.02, CT1345 mixer)
*/

// #define DEBUG_BLASTER

#include "../emulator.h"
#define SB_READ_BUFFER 16

// Sound Blaster I/O port offsets
#define MIXER_INDEX         0x4
#define MIXER_DATA          0x5
#define DSP_RESET           0x6
#define DSP_READ            0xA
#define DSP_WRITE           0xC
#define DSP_WRITE_STATUS    0xC
#define DSP_READ_STATUS     0xE

// Sound Blaster DSP commands.
#define DSP_DMA_HS_SINGLE       0x91
#define DSP_DMA_HS_AUTO         0x90
#define DSP_DMA_SINGLE          0x14    //follosed by length
#define DSP_DMA_AUTO            0X1C    //length based on 48h
#define DSP_DMA_ADC_SINGLE      0x24
#define DSP_DMA_ADC_AUTO        0x2C
#define DSP_DMA_BLOCK_SIZE      0x48    //block size for highspeed/dma
#define DSP_DIRECT_DAC          0x10
#define DSP_DIRECT_ADC          0x20
#define DSP_SET_TIME_CONSTANT   0x40
#define DSP_DMA_PAUSE           0xD0
#define DSP_DMA_SILENCE         0x80    //Used by Tryrian
#define DSP_ENABLE_SPEAKER      0xD1
#define DSP_DISABLE_SPEAKER     0xD3
#define DSP_DMA_RESUME          0xD4
#define DSP_DMA_EXIT_AUTO       0xDA
#define DSP_IDENTIFICATION      0xE0
#define DSP_VERSION             0xE1
#define DSP_DMA_IDENTIFICATION  0xE2
#define DSP_WRITETEST           0xE4
#define DSP_READTEST            0xE8
#define DSP_IRQ                 0xF2
#define DSP_UNDOCUMENTED_F8     0xF8

typedef  struct sound_blaster_s {
    int16_t current_audio_sample;

    uint8_t speaker_enabled;

    uint8_t read_buffer_length;
    uint32_t dma_transfer_length;
    uint32_t dma_block_size;
    uint8_t current_dsp_command;
    uint8_t parameter_byte_index;    // tracks whether we're reading low or high byte
    uint32_t dma_bytes_processed;
    uint8_t auto_init_mode_enabled;
    uint8_t dsp_test_register;
    uint8_t silence_mode_active;
    uint8_t recording_mode_active;
    uint8_t dma_transfer_enabled;
    uint8_t dsp_read_buffer[SB_READ_BUFFER];
} sound_blaster_s;

static sound_blaster_s sound_blaster = { 0 };

// SB Pro mixer (CT1345). Its own chip: a DSP reset does not touch it.
#define MIXER_RESET         0x00
#define MIXER_DEFAULTS { [0x04] = 0x99, [0x22] = 0x99, [0x26] = 0x99, [0x28] = 0x11, [0x2E] = 0x11 }
static const uint8_t mixer_defaults[256] = MIXER_DEFAULTS;
static uint8_t mixer_registers[256] = MIXER_DEFAULTS;
static uint8_t mixer_index;

uint16_t timeconst = 22;

#define SB_IRQ 5
#define SB_DMA_CHANNEL 1

static const int16_t dma_identification_lookup_table[9] = { 0x01, -0x02, -0x04, 0x08, -0x10, 0x20, 0x40, -0x80, -106 };

static INLINE void blaster_write_buffer(const uint8_t byte_value) {
    if (sound_blaster.read_buffer_length < SB_READ_BUFFER) {
        sound_blaster.dsp_read_buffer[sound_blaster.read_buffer_length++] = byte_value;
    }
}

static INLINE uint8_t blaster_read_buffer() {
    if (sound_blaster.read_buffer_length == 0) return 0;

    const uint8_t first_byte = sound_blaster.dsp_read_buffer[0];

    for (int buffer_index = 0; buffer_index < sound_blaster.read_buffer_length - 1; buffer_index++) {
        sound_blaster.dsp_read_buffer[buffer_index] = sound_blaster.dsp_read_buffer[buffer_index + 1];
    }

    sound_blaster.read_buffer_length--;

    return first_byte;
}

INLINE void blaster_reset() {
    memset(&sound_blaster, 0, sizeof(sound_blaster_s));
    sound_blaster.current_audio_sample = 0;
    blaster_write_buffer(0xAA);
}

// TODO: Consider renaming to process_dsp_command for clarity
static INLINE void blaster_command(const uint8_t command_byte) {
    //    printf("SB command %x : %x\r\n", sound_blaster.current_dsp_command, command_byte);
    switch (sound_blaster.current_dsp_command) {
        case DSP_DIRECT_DAC: //direct DAC, 8-bit
            sound_blaster.current_audio_sample = (command_byte - 128) << 6;
            sound_blaster.current_dsp_command = 0;
            return;
        case DSP_DMA_SINGLE: //DMA DAC, 8-bit
        case DSP_DMA_ADC_SINGLE:
            if (sound_blaster.parameter_byte_index == 0) {
                sound_blaster.dma_transfer_length = command_byte;
                sound_blaster.parameter_byte_index = 1;
            } else {
                sound_blaster.dma_transfer_length |= (uint32_t) command_byte << 8;
                sound_blaster.dma_transfer_length++;
                sound_blaster.recording_mode_active = (sound_blaster.current_dsp_command == DSP_DMA_ADC_SINGLE);
                sound_blaster.current_dsp_command = 0;
                sound_blaster.dma_bytes_processed = 0;
                sound_blaster.silence_mode_active = 0;
                sound_blaster.auto_init_mode_enabled = 0;
                sound_blaster.dma_transfer_enabled = 1;
#ifdef DEBUG_BLASTER
                printf("[BLASTER] Begin DMA transfer mode with %u byte blocks\r\n", (unsigned) sound_blaster.dma_transfer_length);
#endif
            }
            return;
        case DSP_SET_TIME_CONSTANT:
            timeconst = 256 - command_byte;
            sound_blaster.current_dsp_command = 0;
#ifdef DEBUG_BLASTER
            printf("[BLASTER] Set time constant: %u (Sample rate: %u Hz)\r\n", command_byte, 1000000u / timeconst);
#endif
            return;
        case DSP_DMA_BLOCK_SIZE:
            if (sound_blaster.parameter_byte_index == 0) {
                sound_blaster.dma_block_size = command_byte;
                sound_blaster.parameter_byte_index = 1;
            } else {
                sound_blaster.dma_block_size |= (uint32_t) command_byte << 8;
                sound_blaster.dma_block_size++;
                sound_blaster.current_dsp_command = 0;
#ifdef DEBUG_BLASTER
                printf("[NOTICE] Sound Blaster DSP block transfer size set to %u\n", (unsigned) sound_blaster.dma_block_size);
#endif
            }
            return;
        case DSP_DMA_SILENCE:
            if (sound_blaster.parameter_byte_index == 0) {
                sound_blaster.dma_transfer_length = command_byte;
                sound_blaster.parameter_byte_index = 1;
            } else {
                sound_blaster.dma_transfer_length |= (uint32_t) command_byte << 8;
                sound_blaster.dma_transfer_length++;
                sound_blaster.current_dsp_command = 0;
                sound_blaster.dma_bytes_processed = 0;
                sound_blaster.silence_mode_active = 1;
                sound_blaster.auto_init_mode_enabled = 0;
            }
            return;
        case DSP_IDENTIFICATION: //DSP identification (returns bitwise NOT of data byte)
            blaster_write_buffer(~command_byte);
            sound_blaster.current_dsp_command = 0;
            return;
        case DSP_DMA_IDENTIFICATION:
        {
            int16_t calculated_value = 0xAA;
            for (uint8_t bit_index = 0; bit_index < 8; bit_index++) {
                if (command_byte >> bit_index & 0x01) {
                    calculated_value += dma_identification_lookup_table[bit_index];
                }
            }
            calculated_value += dma_identification_lookup_table[8];
            i8237_write(SB_DMA_CHANNEL, calculated_value);
            sound_blaster.current_dsp_command = 0;
            return;
        }
        case DSP_WRITETEST:
            sound_blaster.dsp_test_register = command_byte;
            sound_blaster.current_dsp_command = 0;
            return;
    }

    switch (command_byte) {
        case DSP_DIRECT_DAC: //direct DAC, 8-bit
            break;
        case DSP_DMA_SINGLE: //DMA DAC, 8-bit
        case DSP_DMA_ADC_SINGLE:
            sound_blaster.parameter_byte_index = 0;
            break;
        case DSP_DMA_AUTO: //auto-initialize DMA DAC, 8-bit
        case DSP_DMA_HS_AUTO: //high-speed auto-initialize DMA DAC, 8-bit
        case DSP_DMA_ADC_AUTO:
        case DSP_DMA_HS_SINGLE: //high-speed single-cycle DMA DAC, 8-bit
            sound_blaster.dma_transfer_length = sound_blaster.dma_block_size;
            sound_blaster.dma_bytes_processed = 0;
            sound_blaster.silence_mode_active = 0;
            sound_blaster.auto_init_mode_enabled = (command_byte != DSP_DMA_HS_SINGLE);
            sound_blaster.recording_mode_active = (command_byte == DSP_DMA_ADC_AUTO);
            sound_blaster.dma_transfer_enabled = 1;
#ifdef DEBUG_BLASTER
            printf("[BLASTER] Begin DMA command 0x%02X with %u byte blocks\r\n", command_byte, (unsigned) sound_blaster.dma_transfer_length);
#endif
            break;
        case DSP_DIRECT_ADC: //direct ADC, 8-bit record
            blaster_write_buffer(128); //Silence, though I might add actual recording support later.
            break;
        case DSP_SET_TIME_CONSTANT:
            break;
        case DSP_DMA_BLOCK_SIZE:
            sound_blaster.parameter_byte_index = 0;
            break;
        case DSP_DMA_SILENCE:
            sound_blaster.parameter_byte_index = 0;
            break;
        case DSP_DMA_PAUSE: //halt DMA operation, 8-bit
            sound_blaster.dma_transfer_enabled = 0;
            break;
        case DSP_ENABLE_SPEAKER: //speaker on
            sound_blaster.speaker_enabled = 1;
            break;
        case DSP_DISABLE_SPEAKER: //speaker off
            sound_blaster.speaker_enabled = 0;
            break;
        case DSP_DMA_RESUME: //continue DMA operation, 8-bit
            sound_blaster.dma_transfer_enabled = 1;
            break;
        case DSP_DMA_EXIT_AUTO: //exit auto-initialize DMA operation, 8-bit
            sound_blaster.dma_transfer_enabled = 0;
            sound_blaster.auto_init_mode_enabled = 0;
            break;
        case DSP_IDENTIFICATION: //DSP identification (returns bitwise NOT of data byte)
            break;
        case DSP_VERSION: //DSP version (SB Pro 2 is DSP 3.02)
            blaster_write_buffer(3);
            blaster_write_buffer(2);
            break;
        case DSP_DMA_IDENTIFICATION:
            break;
        case DSP_WRITETEST:
            break;
        case DSP_READTEST: //read test register
            sound_blaster.read_buffer_length = 0;
            blaster_write_buffer(sound_blaster.dsp_test_register);
            break;
        case DSP_IRQ: //trigger 8-bit IRQ
            doirq(SB_IRQ);
            break;
        case DSP_UNDOCUMENTED_F8:
            sound_blaster.read_buffer_length = 0;
            blaster_write_buffer(0);
            break;
        default:
            break;
//            printf("[BLASTER] Unrecognized command: 0x%02X\r\n", command_byte);
    }

    sound_blaster.current_dsp_command = command_byte;
}

static INLINE void blaster_write(const uint16_t port, const uint8_t value) {
#ifdef DEBUG_BLASTER
    printf("[BLASTER] Write %03X: %02X\r\n", port, value);
#endif
    switch (port & 0xF) {
        case DSP_RESET:
            blaster_reset();
            break;
        case DSP_WRITE: //DSP write (command/data)
            blaster_command(value);
            break;
        case MIXER_INDEX:
            mixer_index = value;
            break;
        case MIXER_DATA:
            if (mixer_index == MIXER_RESET)
                memcpy(mixer_registers, mixer_defaults, sizeof(mixer_registers));
            else
                mixer_registers[mixer_index] = value;
            break;
    }
}

// TODO: Consider renaming to handle_dsp_port_read for clarity
static INLINE uint8_t blaster_read(const uint16_t port) {
#ifdef DEBUG_BLASTER
    printf("[BLASTER] Read %03X\r\n", port);
#endif

    switch (port & 0xF) {
        case MIXER_INDEX:
            return mixer_index;
        case MIXER_DATA:
            return mixer_registers[mixer_index];
        case DSP_READ:
            return blaster_read_buffer();
        case DSP_WRITE_STATUS:
            return 0x00;
        case DSP_READ_STATUS:
            return sound_blaster.read_buffer_length ? 0x80 : 0x00;
    }

    return 0xff;
}

// TODO: Consider renaming to generate_audio_sample for clarity - this function generates the audio sample for DMA mode
inline int16_t blaster_sample() { //for DMA mode
    int16_t generated_sample = 0;
    if (!sound_blaster.dma_transfer_enabled) return sound_blaster.speaker_enabled ? sound_blaster.current_audio_sample : 0;
    if (sound_blaster.silence_mode_active == 0) {
        if (sound_blaster.recording_mode_active == 0) {
            generated_sample = (i8237_read(SB_DMA_CHANNEL) - 128) << 6;
        } else {
            i8237_write(SB_DMA_CHANNEL, 128); //silence
        }
    }

    if (++sound_blaster.dma_bytes_processed == sound_blaster.dma_transfer_length) {
        sound_blaster.dma_bytes_processed = 0;
        doirq(SB_IRQ);
        sound_blaster.dma_transfer_enabled = sound_blaster.auto_init_mode_enabled;
    }

    return sound_blaster.speaker_enabled ? generated_sample : 0;
}