// https://www.recordingblogs.com/wiki/midi-voice-messages
// https://midi.org/summary-of-midi-1-0-messages
// https://www.blitter.com/~russtopia/MIDI/~jglatt/tech/midispec/messages.htm
// https://newt.phys.unsw.edu.au/jw/notes.html
// https://massimo-nazaria.github.io/midi-synth.html
#include <math.h>
#include <emulator/emulator.h>

#define EMULATED_MIDI

// #define DEBUG_MPU401 1

enum { STATUS_READY = 0, STATUS_OUTPUT_NOT_READY = 0x40, STATUS_INPUT_NOT_READY = 0x80 };

uint8_t mpu_status = STATUS_INPUT_NOT_READY;
uint8_t mpu_rx_data = 0;

static int midi_pos, midi_len;
static uint32_t midi_command;
static int midi_lengths[8] = {3, 3, 3, 3, 2, 2, 3, 1};
static int midi_insysex;


#if defined(EMULATED_MIDI)
#include <emulator/audio/general-midi.c.inl>
#else

static int midi_id = 0;
static HMIDIOUT midi_out_device = NULL;

static INLINE void midi_init_once() {
    if (midi_out_device != NULL) return;

    MMRESULT hr = midiOutOpen(&midi_out_device, midi_id, 0, 0, CALLBACK_NULL);
    if (hr != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("midiOutOpen error - %08X\n", hr);
#endif
        return;
    }
    midiOutReset(midi_out_device);
}

static INLINE void midi_close() {
    if (midi_out_device != NULL) {
        midiOutReset(midi_out_device);
        midiOutClose(midi_out_device);
        midi_out_device = NULL;
    }
}

static uint8_t midi_sysex_data[4096 + 2];
static INLINE void midi_send_sysex() {
    MIDIHDR hdr = {
        .lpData = (LPSTR) midi_sysex_data,
        .dwBufferLength = midi_pos,
        .dwFlags = 0
    };
    // Prepare the MIDI header for sending
    MMRESULT result = midiOutPrepareHeader(midi_out_device, &hdr, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("Error preparing SysEx header: %08X\n", result);
#endif
        return;
    }

    // Send the SysEx message
    result = midiOutLongMsg(midi_out_device, &hdr, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("Error sending SysEx message: %08X\n", result);
#endif
        midiOutUnprepareHeader(midi_out_device, &hdr, sizeof(MIDIHDR));
        return;
    }

    // Wait for the message to be sent
    while (!(hdr.dwFlags & MHDR_DONE)) {
        Sleep(1); // Allow time for the message to finish sending
    }

    // Unprepare the header after sending
    result = midiOutUnprepareHeader(midi_out_device, &hdr, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("Error unpreparing SysEx header: %08X\n", result);
#endif
    }

    // Reset the SysEx state
    midi_insysex = 0;
}
int16_t midi_sample() { return 0; }


#endif


static INLINE uint8_t mpu401_read(uint16_t portnum) {
    if (portnum & 1) {
        return mpu_status;
    }

    mpu_status = STATUS_INPUT_NOT_READY;
    return mpu_rx_data;
}

static INLINE void mpu401_write(uint16_t portnum, uint8_t value) {
    if (portnum & 1) {
        switch (value) {
            case 0xDF:
                mpu_rx_data = 0xFE; // Ack
#if !defined(EMULATED_MIDI)
                midi_send_sysex();
#endif
                break;
            case 0xD0: // Enter UART Mode
                mpu_rx_data = 0xFF; // ???
                break;
            case 0x3F: // Enter Intellegent UART mode
                mpu_rx_data = 0xFE; // Ack
                break;
            default:
#ifdef DEBUG_MPU401
                printf("[MIDI] Unknown %x %x\n", portnum, value);
#endif
            case 0xFF: // Reset
#if !defined(EMULATED_MIDI)
                midi_init_once();
#endif
                mpu_rx_data = 0xFE; // Ack
                break;
        }

        mpu_status = STATUS_READY;
        return;
    }

    if (value & 0x80 && !(value == 0xF7 && midi_insysex)) {
        midi_pos = 0;
        midi_len = midi_lengths[value >> 4 & 7];
        midi_command = 0;

        if (value == 0xF0) midi_insysex = 1;
    }

    if (midi_insysex) {
#if !defined(EMULATED_MIDI)
        midi_sysex_data[midi_pos++] = value;

        if (value == 0xF7 || midi_pos >= sizeof(midi_sysex_data)) {
            midi_send_sysex();
        }
#endif
        if (value == 0xF7) {
            midi_insysex = 0;
        }
        return;
    }

    if (midi_len) {
        midi_command |= value << midi_pos * 8;
        if (++midi_pos == midi_len) {
#if defined(EMULATED_MIDI)
            parse_midi((void *)&midi_command);
#else
            midiOutShortMsg(midi_out_device, midi_command);
#endif
        }
    }
}
