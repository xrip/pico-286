#if PICO_ON_DEVICE
#include <pico/time.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#include "emulator.h"

i8253_s i8253 = {
    .channels = {
        {.latched_value = 0xFFFF},
        {.latched_value = 0xFFFF},
        {.latched_value = 0xFFFF},
    },
};

int speakerenabled = 0;
int timer_period = 54925;

uint64_t i8253_now_us(void) {
#if PICO_ON_DEVICE
    return time_us_64();
#elif defined(_WIN32)
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)((counter.QuadPart * 1000000ULL) / frequency.QuadPart);
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_nsec / 1000ULL;
#endif
}

void init8253(void) {
    memset(&i8253, 0, sizeof(i8253));
    for (uint8_t channel = 0; channel < 3; ++channel) {
        i8253.channels[channel].latched_value = 0xFFFF;
    }
}
