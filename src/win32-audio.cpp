#include "win32-audio.h"
#include <windows.h>
#include <cstdio>

#define NUM_BUFFERS 4

static HWAVEOUT hWaveOut = NULL;
static WAVEHDR waveHeaders[NUM_BUFFERS];
static int16_t* audio_buffers[NUM_BUFFERS] = {0};
static volatile int current_buffer_idx = 0;
static int g_audio_channels = 0;
static int buffer_size_bytes = 0;
static HANDLE wave_event = NULL;

void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        SetEvent(wave_event);
    }
}

int audio_init(int freq, int channels, int buffer_size) {
    g_audio_channels = channels;
    buffer_size_bytes = buffer_size * channels * sizeof(int16_t);

    wave_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (wave_event == NULL) {
        printf("Audio: CreateEvent failed\n");
        return -1;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = channels;
    wfx.nSamplesPerSec = freq;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    MMRESULT res = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        printf("Audio: waveOutOpen failed with error %u\n", res);
        return -1;
    }

    for (int i = 0; i < NUM_BUFFERS; ++i) {
        audio_buffers[i] = (int16_t*)malloc(buffer_size_bytes);
        if (!audio_buffers[i]) {
            printf("Audio: Failed to allocate audio buffer\n");
            for (int j = 0; j < i; ++j) {
                free(audio_buffers[j]);
            }
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
            return -1;
        }

        ZeroMemory(&waveHeaders[i], sizeof(WAVEHDR));
        waveHeaders[i].lpData = (LPSTR)audio_buffers[i];
        waveHeaders[i].dwBufferLength = buffer_size_bytes;

        res = waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        if (res != MMSYSERR_NOERROR) {
            printf("Audio: waveOutPrepareHeader failed with error %u\n", res);
            for (int j = 0; j <= i; ++j) {
                free(audio_buffers[j]);
            }
            waveOutClose(hWaveOut);
            hWaveOut = NULL;
            return -1;
        }
    }

    return 0;
}

void audio_close() {
    if (hWaveOut) {
        waveOutReset(hWaveOut);
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
            free(audio_buffers[i]);
        }
        waveOutClose(hWaveOut);
        hWaveOut = NULL;
    }
    if (wave_event) {
        CloseHandle(wave_event);
        wave_event = NULL;
    }
}

int audio_write(const int16_t* buffer, size_t samples) {
    if (!hWaveOut || samples == 0) {
        return -1;
    }

    WAVEHDR* currentHeader = &waveHeaders[current_buffer_idx];

    // Wait for the current buffer to be free.
    // WHDR_INQUEUE is set by waveOutWrite and cleared by the system when the buffer is done.
    while (currentHeader->dwFlags & WHDR_INQUEUE) {
        WaitForSingleObject(wave_event, 100); // Wait for any buffer to complete
    }

    size_t bytes_to_copy = samples * g_audio_channels * sizeof(int16_t);
    if (bytes_to_copy > (size_t)buffer_size_bytes) {
        bytes_to_copy = buffer_size_bytes;
    }
    memcpy(currentHeader->lpData, buffer, bytes_to_copy);
    currentHeader->dwBufferLength = bytes_to_copy;

    // The buffer is already prepared. waveOutWrite will clear WHDR_DONE and set WHDR_INQUEUE.
    MMRESULT result = waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        printf("Audio: waveOutWrite failed with error %u\n", result);
        return -1;
    }

    current_buffer_idx = (current_buffer_idx + 1) % NUM_BUFFERS;

    return 0;
}
