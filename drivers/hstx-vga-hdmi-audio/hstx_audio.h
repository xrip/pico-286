#pragma once
// HDMI audio for the HSTX output: 44.1 kHz 16-bit stereo.

#include <stdbool.h>
#include <stdint.h>

enum { HSTX_AUDIO_SAMPLE_RATE = 44100 };

// For the sound loop on the scanout core. Write a sample while there is room:
// the HDMI clock then sets the sample rate.
bool hstx_audio_has_room(void);
void hstx_audio_write(int16_t left, int16_t right);

// For hstx_video.c.
void hstx_audio_init(void);
const uint32_t *hstx_audio_island(unsigned line); // the data island of output line `line` (0 = first blank line)
