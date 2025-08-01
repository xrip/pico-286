#ifndef WIN32_AUDIO_H
#define WIN32_AUDIO_H

#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio API functions
int audio_init(int sample_rate, int channels, int buffer_size);
int audio_start();
int audio_write(const int16_t* buffer, size_t samples);
void audio_stop();
void audio_close();

#ifdef __cplusplus
}
#endif

#endif // WIN32_AUDIO_H
