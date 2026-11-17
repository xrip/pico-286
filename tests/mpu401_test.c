// Host test for the MPU-401 byte parser, with the winmm calls replaced by recorders.
// Build and run from the project root (Windows, MinGW):
//   gcc -std=gnu17 -fms-extensions -O2 -Isrc -Isrc/emu8950 -Isrc/printf tests/mpu401_test.c -o tests/mpu401_test.exe && tests/mpu401_test.exe
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

static uint32_t sent[64];
static int sent_count, opens, sysex_count, sysex_length;

static MMRESULT open_device(HMIDIOUT *device, UINT_PTR id, DWORD_PTR callback, DWORD_PTR data, DWORD flags) {
    opens++;
    *device = (HMIDIOUT) 1;
    return MMSYSERR_NOERROR;
}
static MMRESULT send_short(HMIDIOUT device, DWORD message) {
    sent[sent_count++] = message;
    return MMSYSERR_NOERROR;
}
static MMRESULT send_long(HMIDIOUT device, MIDIHDR *header, UINT size) {
    sysex_count++;
    sysex_length = header->dwBufferLength;
    header->dwFlags |= MHDR_DONE;
    return MMSYSERR_NOERROR;
}
static MMRESULT do_nothing(HMIDIOUT device, MIDIHDR *header, UINT size) { return MMSYSERR_NOERROR; }
static MMRESULT reset_device(HMIDIOUT device) { return MMSYSERR_NOERROR; }

#define midiOutOpen open_device
#define midiOutReset reset_device
#define midiOutShortMsg send_short
#define midiOutLongMsg send_long
#define midiOutPrepareHeader do_nothing
#define midiOutUnprepareHeader do_nothing
#define MIDI_WINMM 1
#include "emulator/audio/mpu401.c.inl"

#define DATA 0x330
#define COMMAND 0x331

static void bytes(const uint8_t *list, int count) {
    for (int i = 0; i < count; i++) mpu401_write(DATA, list[i]);
}
#define SEND(...) bytes((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static void expect(const uint32_t *list, int count) {
    assert(sent_count == count);
    for (int i = 0; i < count; i++) assert(sent[i] == list[i]);
    sent_count = 0;
}
#define EXPECT(...) expect((const uint32_t[]){__VA_ARGS__}, sizeof((const uint32_t[]){__VA_ARGS__}) / 4)
#define EXPECT_NONE() assert(sent_count == 0)

int main(void) {
    mpu401_write(COMMAND, 0x3F); // UART mode, no reset first
    assert(mpu401_read(COMMAND) == STATUS_READY && mpu401_read(DATA) == 0xFE);
    assert(mpu401_read(COMMAND) == STATUS_INPUT_NOT_READY);

    SEND(0x90, 0x3C, 0x40, 0x3E, 0x41); // note on, then running status
    EXPECT(0x403C90, 0x413E90);
    assert(opens == 1);

    SEND(0x91, 0x3C, 0xF8, 0x40); // a clock byte inside a message
    EXPECT(0xF8, 0x403C91);
    SEND(0x45, 0x50);             // running status still kept
    EXPECT(0x504591);

    SEND(0xC0, 0x05, 0x06); // two-byte message, running status
    EXPECT(0x05C0, 0x06C0);

    SEND(0xF0, 0x41, 0x10, 0x42, 0xF7, 0x22); // SysEx, then a stray data byte
    assert(sysex_count == 1 && sysex_length == 5);
    EXPECT_NONE();

    SEND(0xF6, 0x33); // system common ends running status
    EXPECT(0xF6);

    mpu401_write(COMMAND, 0xDF); // must not send the old SysEx buffer
    assert(sysex_count == 1);

    SEND(0x90, 0x3C);
    mpu401_write(COMMAND, 0xFF); // reset in the middle of a message
    SEND(0x40);
    EXPECT_NONE();

    puts("mpu401_test: all passed");
    return 0;
}
