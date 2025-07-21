#include "linux-audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <dlfcn.h>

// OSS headers
#ifdef __linux__
#include <linux/soundcard.h>
#else
#include <sys/soundcard.h>
#endif

// PulseAudio simple API function pointers
static void* pulse_lib = NULL;
static void* (*pa_simple_new)(const char*, const char*, int, const char*, const char*, void*, void*, void*, int*) = NULL;
static int (*pa_simple_write)(void*, const void*, size_t, int*) = NULL;
static void (*pa_simple_free)(void*) = NULL;
static char* (*pa_strerror)(int) = NULL;

static linux_audio_context_t g_audio_ctx = {0};

// Forward declarations
static int oss_init(linux_audio_context_t* ctx);
static int pulse_init(linux_audio_context_t* ctx);
static void* audio_thread_func(void* arg);
static int setup_oss_format(int fd, linux_audio_config_t* config);
static int load_pulse_library();

int linux_audio_init(int sample_rate, int channels, int buffer_size) {
    if (g_audio_ctx.initialized) {
        return 0; // Already initialized
    }
    
    memset(&g_audio_ctx, 0, sizeof(g_audio_ctx));
    
    // Set up configuration
    g_audio_ctx.config.sample_rate = sample_rate;
    g_audio_ctx.config.channels = channels;
    g_audio_ctx.config.bits_per_sample = 16;
    g_audio_ctx.config.buffer_size = buffer_size;
    g_audio_ctx.config.num_buffers = 4;
    
    // Initialize synchronization
    if (pthread_mutex_init(&g_audio_ctx.mutex, NULL) != 0) {
        printf("Failed to initialize audio mutex\n");
        return -1;
    }
    
    if (pthread_cond_init(&g_audio_ctx.cond, NULL) != 0) {
        printf("Failed to initialize audio condition\n");
        pthread_mutex_destroy(&g_audio_ctx.mutex);
        return -1;
    }
    
    if (pthread_cond_init(&g_audio_ctx.ready_cond, NULL) != 0) {
        printf("Failed to initialize ready condition\n");
        pthread_mutex_destroy(&g_audio_ctx.mutex);
        pthread_cond_destroy(&g_audio_ctx.cond);
        return -1;
    }
    
    // Allocate audio buffers
    for (int i = 0; i < g_audio_ctx.config.num_buffers; i++) {
        size_t buffer_bytes = buffer_size * channels * sizeof(int16_t);
        g_audio_ctx.buffers[i].data = malloc(buffer_bytes);
        if (!g_audio_ctx.buffers[i].data) {
            printf("Failed to allocate audio buffer %d\n", i);
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                free(g_audio_ctx.buffers[j].data);
            }
            pthread_mutex_destroy(&g_audio_ctx.mutex);
            pthread_cond_destroy(&g_audio_ctx.cond);
            pthread_cond_destroy(&g_audio_ctx.ready_cond);
            return -1;
        }
        g_audio_ctx.buffers[i].size = buffer_bytes;
        g_audio_ctx.buffers[i].ready = 0;
        memset(g_audio_ctx.buffers[i].data, 0, buffer_bytes);
    }
    
    // Try PulseAudio first
    if (pulse_init(&g_audio_ctx) == 0) {
        g_audio_ctx.backend = LINUX_AUDIO_PULSE;
        g_audio_ctx.initialized = 1;
        printf("Audio: PulseAudio backend initialized (%d Hz, %d channels)\n", 
               sample_rate, channels);
        return 0;
    }
    
    // Try to initialize OSS as fallback
    if (oss_init(&g_audio_ctx) == 0) {
        g_audio_ctx.backend = LINUX_AUDIO_OSS;
        g_audio_ctx.initialized = 1;
        printf("Audio: OSS backend initialized (%d Hz, %d channels)\n", 
               sample_rate, channels);
        return 0;
    }
    
    // If OSS fails, clean up and return error
    printf("Audio: No working backend found\n");
    for (int i = 0; i < g_audio_ctx.config.num_buffers; i++) {
        free(g_audio_ctx.buffers[i].data);
    }
    pthread_mutex_destroy(&g_audio_ctx.mutex);
    pthread_cond_destroy(&g_audio_ctx.cond);
    pthread_cond_destroy(&g_audio_ctx.ready_cond);
    
    return -1;
}

static int oss_init(linux_audio_context_t* ctx) {
    // Try to open OSS device
    ctx->audio_fd = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
    if (ctx->audio_fd < 0) {
        // Try alternative OSS devices
        ctx->audio_fd = open("/dev/dsp0", O_WRONLY | O_NONBLOCK);
        if (ctx->audio_fd < 0) {
            ctx->audio_fd = open("/dev/audio", O_WRONLY | O_NONBLOCK);
            if (ctx->audio_fd < 0) {
                printf("Audio: Failed to open OSS device: %s\n", strerror(errno));
                return -1;
            }
        }
    }
    
    // Setup audio format
    if (setup_oss_format(ctx->audio_fd, &ctx->config) != 0) {
        close(ctx->audio_fd);
        ctx->audio_fd = -1;
        return -1;
    }
    
    // Set to blocking mode for actual audio output
    int flags = fcntl(ctx->audio_fd, F_GETFL);
    fcntl(ctx->audio_fd, F_SETFL, flags & ~O_NONBLOCK);
    
    return 0;
}

static int setup_oss_format(int fd, linux_audio_config_t* config) {
    int format, channels, sample_rate;
    
    // Set sample format (16-bit signed)
    format = AFMT_S16_LE;
    if (ioctl(fd, SNDCTL_DSP_SETFMT, &format) < 0) {
        printf("Audio: Failed to set sample format: %s\n", strerror(errno));
        return -1;
    }
    
    if (format != AFMT_S16_LE) {
        printf("Audio: Device doesn't support 16-bit signed format\n");
        return -1;
    }
    
    // Set number of channels
    channels = config->channels;
    if (ioctl(fd, SNDCTL_DSP_CHANNELS, &channels) < 0) {
        printf("Audio: Failed to set channels: %s\n", strerror(errno));
        return -1;
    }
    
    if (channels != config->channels) {
        printf("Audio: Device doesn't support %d channels (got %d)\n", 
               config->channels, channels);
        return -1;
    }
    
    // Set sample rate
    sample_rate = config->sample_rate;
    if (ioctl(fd, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
        printf("Audio: Failed to set sample rate: %s\n", strerror(errno));
        return -1;
    }
    
    // Allow some tolerance in sample rate
    if (abs(sample_rate - config->sample_rate) > config->sample_rate * 0.05) {
        printf("Audio: Sample rate mismatch: requested %d, got %d\n", 
               config->sample_rate, sample_rate);
        return -1;
    }
    
    config->sample_rate = sample_rate; // Update with actual rate
    
    printf("Audio: OSS format configured - %d Hz, %d channels, 16-bit\n",
           sample_rate, channels);
    
    return 0;
}

static int load_pulse_library() {
    pulse_lib = dlopen("libpulse-simple.so.0", RTLD_LAZY);
    if (!pulse_lib) {
        pulse_lib = dlopen("libpulse-simple.so", RTLD_LAZY);
        if (!pulse_lib) {
            printf("Audio: Failed to load PulseAudio library: %s\n", dlerror());
            return -1;
        }
    }
    
    pa_simple_new = dlsym(pulse_lib, "pa_simple_new");
    pa_simple_write = dlsym(pulse_lib, "pa_simple_write");
    pa_simple_free = dlsym(pulse_lib, "pa_simple_free");
    pa_strerror = dlsym(pulse_lib, "pa_strerror");
    
    if (!pa_simple_new || !pa_simple_write || !pa_simple_free || !pa_strerror) {
        printf("Audio: Failed to load PulseAudio functions\n");
        dlclose(pulse_lib);
        pulse_lib = NULL;
        return -1;
    }
    
    return 0;
}

static int pulse_init(linux_audio_context_t* ctx) {
    if (load_pulse_library() != 0) {
        return -1;
    }
    
    // PulseAudio sample spec (need to include pulse headers or define manually)
    struct {
        int format;     // PA_SAMPLE_S16LE = 3
        uint32_t rate;
        uint8_t channels;
    } sample_spec = {
        .format = 3,    // PA_SAMPLE_S16LE
        .rate = ctx->config.sample_rate,
        .channels = ctx->config.channels
    };
    
    int error;
    ctx->pulse_simple = pa_simple_new(
        NULL,                   // server
        "Pico-286 Emulator",    // name
        1,                      // PA_STREAM_PLAYBACK = 1
        NULL,                   // device
        "Audio Output",         // stream description
        &sample_spec,           // sample spec
        NULL,                   // channel map
        NULL,                   // buffer attributes
        &error                  // error code
    );
    
    if (!ctx->pulse_simple) {
        printf("Audio: Failed to create PulseAudio stream: %s\n", pa_strerror(error));
        dlclose(pulse_lib);
        pulse_lib = NULL;
        return -1;
    }
    
    printf("Audio: PulseAudio stream created (%d Hz, %d channels)\n",
           ctx->config.sample_rate, ctx->config.channels);
    
    return 0;
}

int linux_audio_start() {
    if (!g_audio_ctx.initialized || g_audio_ctx.running) {
        return -1;
    }
    
    g_audio_ctx.running = 1;
    g_audio_ctx.current_buffer = 0;
    g_audio_ctx.write_buffer = 0;
    
    // Start audio thread
    if (pthread_create(&g_audio_ctx.audio_thread, NULL, audio_thread_func, NULL) != 0) {
        printf("Failed to create audio thread\n");
        g_audio_ctx.running = 0;
        return -1;
    }
    
    printf("Audio: Started\n");
    return 0;
}

static void* audio_thread_func(void* arg) {
    linux_audio_context_t* ctx = &g_audio_ctx;
    
    while (ctx->running) {
        pthread_mutex_lock(&ctx->mutex);
        
        // Wait for a buffer to be ready
        while (ctx->running && !ctx->buffers[ctx->current_buffer].ready) {
            pthread_cond_wait(&ctx->cond, &ctx->mutex);
        }
        
        if (!ctx->running) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }
        
        // Get current buffer
        linux_audio_buffer_t* buffer = &ctx->buffers[ctx->current_buffer];
        
        pthread_mutex_unlock(&ctx->mutex);
        
        // Write to audio device (blocking)
        if (ctx->backend == LINUX_AUDIO_OSS) {
            ssize_t written = write(ctx->audio_fd, buffer->data, buffer->size);
            if (written < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    printf("Audio write error: %s\n", strerror(errno));
                }
            }
        } else if (ctx->backend == LINUX_AUDIO_PULSE) {
            int error;
            if (pa_simple_write(ctx->pulse_simple, buffer->data, buffer->size, &error) < 0) {
                printf("Audio write error: %s\n", pa_strerror(error));
            }
        }
        
        // Mark buffer as processed
        pthread_mutex_lock(&ctx->mutex);
        buffer->ready = 0;
        ctx->current_buffer = (ctx->current_buffer + 1) % ctx->config.num_buffers;
        
        // Signal that a buffer is now available
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->mutex);
    }
    
    return NULL;
}

int linux_audio_write(const int16_t* buffer, size_t samples) {
    if (!g_audio_ctx.initialized || !g_audio_ctx.running) {
        return -1;
    }
    
    pthread_mutex_lock(&g_audio_ctx.mutex);
    
    // Wait for a buffer to be available
    while (g_audio_ctx.running && g_audio_ctx.buffers[g_audio_ctx.write_buffer].ready) {
        pthread_cond_wait(&g_audio_ctx.ready_cond, &g_audio_ctx.mutex);
    }
    
    if (!g_audio_ctx.running) {
        pthread_mutex_unlock(&g_audio_ctx.mutex);
        return -1;
    }
    
    // Copy data to buffer
    linux_audio_buffer_t* audio_buf = &g_audio_ctx.buffers[g_audio_ctx.write_buffer];
    size_t bytes_to_copy = samples * g_audio_ctx.config.channels * sizeof(int16_t);
    
    if (bytes_to_copy > audio_buf->size) {
        bytes_to_copy = audio_buf->size;
    }
    
    memcpy(audio_buf->data, buffer, bytes_to_copy);
    audio_buf->ready = 1;
    
    // Move to next buffer
    g_audio_ctx.write_buffer = (g_audio_ctx.write_buffer + 1) % g_audio_ctx.config.num_buffers;
    
    // Signal audio thread
    pthread_cond_signal(&g_audio_ctx.cond);
    pthread_mutex_unlock(&g_audio_ctx.mutex);
    
    return 0;
}

void linux_audio_stop() {
    if (!g_audio_ctx.running) {
        return;
    }
    
    g_audio_ctx.running = 0;
    
    // Wake up audio thread
    pthread_cond_broadcast(&g_audio_ctx.cond);
    
    // Wait for thread to finish
    pthread_join(g_audio_ctx.audio_thread, NULL);
    
    printf("Audio: Stopped\n");
}

void linux_audio_close() {
    if (!g_audio_ctx.initialized) {
        return;
    }
    
    linux_audio_stop();
    
    // Close audio device
    if (g_audio_ctx.backend == LINUX_AUDIO_OSS && g_audio_ctx.audio_fd >= 0) {
        close(g_audio_ctx.audio_fd);
        g_audio_ctx.audio_fd = -1;
    } else if (g_audio_ctx.backend == LINUX_AUDIO_PULSE && g_audio_ctx.pulse_simple) {
        pa_simple_free(g_audio_ctx.pulse_simple);
        g_audio_ctx.pulse_simple = NULL;
        if (pulse_lib) {
            dlclose(pulse_lib);
            pulse_lib = NULL;
        }
    }
    
    // Free buffers
    for (int i = 0; i < g_audio_ctx.config.num_buffers; i++) {
        if (g_audio_ctx.buffers[i].data) {
            free(g_audio_ctx.buffers[i].data);
            g_audio_ctx.buffers[i].data = NULL;
        }
    }
    
    // Destroy synchronization objects
    pthread_mutex_destroy(&g_audio_ctx.mutex);
    pthread_cond_destroy(&g_audio_ctx.cond);
    pthread_cond_destroy(&g_audio_ctx.ready_cond);
    
    memset(&g_audio_ctx, 0, sizeof(g_audio_ctx));
    
    printf("Audio: Closed\n");
}

linux_audio_backend_t linux_audio_get_backend() {
    return g_audio_ctx.backend;
}

const char* linux_audio_get_backend_name() {
    switch (g_audio_ctx.backend) {
        case LINUX_AUDIO_OSS: return "OSS";
        case LINUX_AUDIO_PULSE: return "PulseAudio";
        case LINUX_AUDIO_ALSA: return "ALSA";
        case LINUX_AUDIO_NONE: 
        default: return "None";
    }
}