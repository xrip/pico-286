#ifndef LINUX_AUDIO_H
#define LINUX_AUDIO_H

#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio backend types
typedef enum {
    LINUX_AUDIO_NONE = 0,
    LINUX_AUDIO_OSS,
    LINUX_AUDIO_PULSE,
    LINUX_AUDIO_ALSA
} linux_audio_backend_t;

// Audio configuration
typedef struct {
    int sample_rate;
    int channels;
    int bits_per_sample;
    int buffer_size;
    int num_buffers;
} linux_audio_config_t;

// Audio buffer management
typedef struct {
    int16_t* data;
    size_t size;
    int ready;
} linux_audio_buffer_t;

// Main audio context
typedef struct {
    linux_audio_backend_t backend;
    linux_audio_config_t config;
    
    // OSS specific
    int audio_fd;
    
    // PulseAudio specific
    void* pulse_simple;
    
    // Buffer management
    linux_audio_buffer_t buffers[4];
    int current_buffer;
    int write_buffer;
    
    // Threading
    pthread_t audio_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t ready_cond;
    
    // Control
    int running;
    int initialized;
    
} linux_audio_context_t;

// Audio API functions
int linux_audio_init(int sample_rate, int channels, int buffer_size);
int linux_audio_start();
int linux_audio_write(const int16_t* buffer, size_t samples);
void linux_audio_stop();
void linux_audio_close();

// Get current backend
linux_audio_backend_t linux_audio_get_backend();
const char* linux_audio_get_backend_name();

#ifdef __cplusplus
}
#endif

#endif // LINUX_AUDIO_H