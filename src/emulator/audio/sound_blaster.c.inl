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
	Emulation of the Sound Blaster 2.0
*/

// #define DEBUG_BLASTER

#include "../emulator.h"
#define SB_READ_BUFFER 16

// Sound Blaster DSP I/O port offsets
#define DSP_RESET           0x6
#define DSP_READ            0xA
#define DSP_WRITE           0xC
#define DSP_WRITE_STATUS    0xC
#define DSP_READ_STATUS     0xE

// Sound Blaster DSP commands.
#define DSP_DMA_HS_SINGLE       0x91
#define DSP_DMA_HS_AUTO         0x90
#define DSP_DMA_ADPCM           0x7F    //creative ADPCM 8bit to 3bit
#define DSP_DMA_SINGLE          0x14    //follosed by length
#define DSP_DMA_AUTO            0X1C    //length based on 48h
#define DSP_DMA_BLOCK_SIZE      0x48    //block size for highspeed/dma
//#define DSP_DMA_DAC 0x14
#define DSP_DIRECT_DAC          0x10
#define DSP_DIRECT_ADC          0x20
#define DSP_MIDI_READ_POLL      0x30
#define DSP_MIDI_WRITE_POLL     0x38
#define DSP_SET_TIME_CONSTANT   0x40
#define DSP_DMA_PAUSE           0xD0
#define DSP_DMA_PAUSE_DURATION  0x80    //Used by Tryrian
#define DSP_ENABLE_SPEAKER      0xD1
#define DSP_DISABLE_SPEAKER     0xD3
#define DSP_DMA_RESUME          0xD4
#define DSP_SPEAKER_STATUS      0xD8
#define DSP_IDENTIFICATION      0xE0
#define DSP_VERSION             0xE1
#define DSP_WRITETEST           0xE4
#define DSP_READTEST            0xE8
#define DSP_SINE                0xF0
#define DSP_IRQ                 0xF2
#define DSP_CHECKSUM            0xF4

typedef  struct sound_blaster_s {
    int16_t current_audio_sample;

    uint8_t speaker_enabled;

    uint8_t read_buffer_length;
    uint32_t dma_transfer_length;
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

//#define DEBUG_BLASTER
uint16_t timeconst = 22;
uint64_t sb_samplerate = 22050;

#define SB_IRQ 3
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
    //    printf("SB command %x : %x        %d\r\n", sb.lastcmd, value, i++);
    switch (sound_blaster.current_dsp_command) {
        case DSP_DIRECT_DAC: //direct DAC, 8-bit
            sound_blaster.current_audio_sample = (command_byte - 128) << 6;
            sound_blaster.current_dsp_command = 0;
            return;
        case DSP_DMA_SINGLE: //DMA DAC, 8-bit
        case 0x24:
        case DSP_DMA_HS_SINGLE:
            if (sound_blaster.parameter_byte_index == 0) {
                sound_blaster.dma_transfer_length = command_byte;
                sound_blaster.parameter_byte_index = 1;
            } else {
                sound_blaster.dma_transfer_length |= (uint32_t) command_byte << 8;
                sound_blaster.dma_transfer_length++;
                sound_blaster.current_dsp_command = 0;
                sound_blaster.dma_bytes_processed = 0;
                sound_blaster.silence_mode_active = 0;
                sound_blaster.auto_init_mode_enabled = 0;
                sound_blaster.recording_mode_active = (sound_blaster.current_dsp_command == 0x24) ? 1 : 0;
                sound_blaster.dma_transfer_enabled = 1;
#ifdef DEBUG_BLASTER
                printf("[BLASTER] Begin DMA transfer mode with 0x%04X  byte blocks\r\n", sb.dmalen);
#endif
            }
            return;
        case DSP_SET_TIME_CONSTANT: //set time constant
            timeconst = 256 - command_byte;
            sb_samplerate = 1000000 / timeconst;
            sound_blaster.current_dsp_command = 0;
#ifdef DEBUG_BLASTER
            printf("[BLASTER] Set time constant: %u (Sample rate: %lu Hz)\r\n", value, 1000000 / (256 - value));
#endif
            return;
        case DSP_DMA_BLOCK_SIZE: //set DMA block size
            if (sound_blaster.parameter_byte_index == 0) {
                sound_blaster.dma_transfer_length = command_byte;
                sound_blaster.parameter_byte_index = 1;
            } else {
                sound_blaster.dma_transfer_length |= (uint32_t) command_byte << 8;
                sound_blaster.dma_transfer_length++;
                sound_blaster.current_dsp_command = 0;
            }
#ifdef DEBUG_BLASTER
            printf("[NOTICE] Sound Blaster DSP block transfer size set to %u\n", sb.dmalen);
#endif
            return;
        case DSP_DMA_PAUSE_DURATION: //silence DAC
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
        case 0xE2: //DMA identification write
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
        case DSP_WRITETEST: //write test register
            sound_blaster.dsp_test_register = command_byte;
            sound_blaster.current_dsp_command = 0;
            return;
    }

    switch (command_byte) {
        case 0x10: //direct DAC, 8-bit
            break;
        case 0x14: //DMA DAC, 8-bit
        case 0x24:
            sound_blaster.parameter_byte_index = 0;
            break;
        case DSP_DMA_AUTO: //auto-initialize DMA DAC, 8-bit
        case 0x2C:
            sound_blaster.dma_bytes_processed = 0;
            sound_blaster.silence_mode_active = 0;
            sound_blaster.auto_init_mode_enabled = 1;
            sound_blaster.recording_mode_active = (command_byte == 0x2C) ? 1 : 0;
            sound_blaster.dma_transfer_enabled = 1;
#ifdef DEBUG_BLASTER
            printf("[BLASTER] Begin auto-init DMA transfer mode with %d byte blocks\r\n", sb.dmacount);
#endif
            break;
        case DSP_DIRECT_ADC: //direct ADC, 8-bit record
            blaster_write_buffer(128); //Silence, though I might add actual recording support later.
            break;
        case 0x40: //set time constant
            break;
        case 0x91:
        case 0x48: //set DMA block size
            sound_blaster.parameter_byte_index = 0;
            break;
        case 0x80: //silence DAC
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
        case 0xDA: //exit auto-initialize DMA operation, 8-bit
            sound_blaster.dma_transfer_enabled = 0;
            sound_blaster.auto_init_mode_enabled = 0;
            break;
        case 0xE0: //DSP identification (returns bitwise NOT of data byte)
            break;
        case DSP_VERSION: //DSP version (SB 2.0 is DSP 2.01)
            blaster_write_buffer(2);
            blaster_write_buffer(1);
            break;
        case 0xE2: //DMA identification write
            break;
        case 0xE4: //write test register
            break;
        case DSP_READTEST: //read test register
            sound_blaster.read_buffer_length = 0;
            blaster_write_buffer(sound_blaster.dsp_test_register);
            break;
        case DSP_IRQ: //trigger 8-bit IRQ
            doirq(SB_IRQ);
            break;
        case 0xF8: //Undocumented
            sound_blaster.read_buffer_length = 0;
            blaster_write_buffer(0);
            break;
        default:
            break;
//            printf("[BLASTER] Unrecognized command: 0x%02X\r\n", value);
    }

    sound_blaster.current_dsp_command = command_byte;
}

static INLINE void blaster_write(const uint16_t port, const uint8_t value) {
#ifdef DEBUG_BLASTER
    printf("[BLASTER] Write %03X: %02X\r\n", portnum, value);
#endif
    switch (port & 0xF) {
        case DSP_RESET:
            blaster_reset();
            break;
        case DSP_WRITE: //DSP write (command/data)
            blaster_command(value);
            break;
    }
}

// TODO: Consider renaming to handle_dsp_port_read for clarity
static INLINE uint8_t blaster_read(const uint16_t port) {
#ifdef DEBUG_BLASTER
    printf("[BLASTER] Read %03X\r\n", portnum);
#endif

    switch (port & 0xF) {
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