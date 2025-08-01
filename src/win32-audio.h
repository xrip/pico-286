#pragma once

#include <cstddef>
#include <cstdint>

int audio_init(int freq, int channels, int buffer_size);
void audio_close();
int audio_write(const int16_t* buffer, size_t samples);
