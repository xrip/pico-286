#include "win32-audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

#pragma comment(lib, "winmm.lib")

#define NUM_BUFFERS 8 // Increased for smoother playback

static HWAVEOUT hWaveOut = NULL;
static WAVEHDR waveHeaders[NUM_BUFFERS];
static int16_t* audio_buffers[NUM_BUFFERS] = {0};
static volatile int current_buffer_idx = 0;
static int buffer_size_samples = 0;
static int buffer_size_bytes = 0;
static volatile LONG buffers_in_use = 0;

static HANDLE wave_event;

int audio_init(int sample_rate, int channels, int buffer_size) {
    if (hWaveOut) {
        return 0; // Already initialized
    }

    buffer_size_samples = buffer_size;
    buffer_size_bytes = buffer_size_samples * channels * sizeof(int16_t);

    wave_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (wave_event == NULL) {
        printf("Audio: Failed to create wave event\n");
        return -1;
    }

    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = (channels * format.wBitsPerSample) / 8;
    format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
    format.cbSize = 0;

    MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &format, (DWORD_PTR)wave_event, 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        printf("Audio: Failed to open wave output device. Error: %u\n", result);
        CloseHandle(wave_event);
        return -1;
    }

    for (int i = 0; i < NUM_BUFFERS; i++) {
        audio_buffers[i] = (int16_t*)malloc(buffer_size_bytes);
        if (!audio_buffers[i]) {
            printf("Audio: Failed to allocate audio buffer %d\n", i);
            for (int j = 0; j < i; j++) free(audio_buffers[j]);
            waveOutClose(hWaveOut);
            CloseHandle(wave_event);
            return -1;
        }
        memset(audio_buffers[i], 0, buffer_size_bytes);

        waveHeaders[i] = {0};
        waveHeaders[i].lpData = (LPSTR)audio_buffers[i];
        waveHeaders[i].dwBufferLength = buffer_size_bytes;

        result = waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            printf("Audio: Failed to prepare wave header %d. Error: %u\n", i, result);
            for (int j = 0; j <= i; j++) free(audio_buffers[j]);
            waveOutClose(hWaveOut);
            CloseHandle(wave_event);
            return -1;
        }
    }

    current_buffer_idx = 0;
    buffers_in_use = 0;

    printf("Audio: Win32 WaveOut initialized (%d Hz, %d channels, %d buffer size)\n", sample_rate, channels, buffer_size);
    return 0;
}

int audio_start() {
    // Not strictly needed for this model, but here for API compatibility.
    return 0;
}

int audio_write(const int16_t* buffer, size_t samples) {
    if (!hWaveOut || samples == 0) {
        return -1;
    }

    WAVEHDR* currentHeader = &waveHeaders[current_buffer_idx];

    // Wait for the current buffer to be free
    while (!(currentHeader->dwFlags & WHDR_DONE) && (currentHeader->dwFlags != 0)) {
        WaitForSingleObject(wave_event, 100);
    }

    // The WaveOut API might have marked the header as done, but we need to unprepare it before writing new data
    if (currentHeader->dwFlags & WHDR_PREPARED) {
        MMRESULT res = waveOutUnprepareHeader(hWaveOut, currentHeader, sizeof(WAVEHDR));
        if (res != MMSYSERR_NOERROR) {
            printf("Audio: waveOutUnprepareHeader failed with error %u\n", res);
        }
    }

    size_t bytes_to_copy = samples * 2 * sizeof(int16_t); // channels * sizeof(sample)
    if (bytes_to_copy > (size_t)buffer_size_bytes) {
        bytes_to_copy = buffer_size_bytes;
    }
    memcpy(currentHeader->lpData, buffer, bytes_to_copy);
    currentHeader->dwBufferLength = bytes_to_copy;

    MMRESULT result = waveOutPrepareHeader(hWaveOut, currentHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        printf("Audio: waveOutPrepareHeader failed with error %u\n", result);
        return -1;
    }

    result = waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        printf("Audio: waveOutWrite failed with error %u\n", result);
        return -1;
    }

    current_buffer_idx = (current_buffer_idx + 1) % NUM_BUFFERS;

    return 0;
}

void audio_stop() {
    if (!hWaveOut) return;
    waveOutReset(hWaveOut);
}

void audio_close() {
    if (!hWaveOut) return;

    audio_stop();

    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (waveHeaders[i].dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        }
        if (audio_buffers[i]) {
            free(audio_buffers[i]);
            audio_buffers[i] = NULL;
        }
    }

    waveOutClose(hWaveOut);
    hWaveOut = NULL;

    if (wave_event) {
        CloseHandle(wave_event);
        wave_event = NULL;
    }

    printf("Audio: Closed\n");
}
