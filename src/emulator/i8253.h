#pragma once

#include <stdint.h>
#include "i8259.h"

#if PICO_ON_DEVICE
#include <hardware/pwm.h>
#endif

#define PIT_MODE_LATCHCOUNT 0
#define PIT_MODE_LOBYTE 1
#define PIT_MODE_HIBYTE 2
#define PIT_MODE_TOGGLE 3
#define PIT_FREQUENCY 1193182u

typedef struct {
    uint16_t reload_value;
    uint8_t access_mode;
    uint8_t byte_toggle;
    uint8_t active;
    uint8_t latch_mode;
    uint16_t latched_value;
    uint64_t start_timestamp_us;
    uint8_t operating_mode;
} i8253_channel_s;

typedef struct {
    i8253_channel_s channels[3];
} i8253_s;

extern i8253_s i8253;
extern uint8_t port61;
extern int timer_period;
extern int speakerenabled;

void init8253(void);
uint64_t i8253_now_us(void);

static inline uint32_t i8253_divisor(const i8253_channel_s *channel) {
    return channel->reload_value ? channel->reload_value : 65536u;
}

static inline uint32_t i8253_frequency(const uint8_t channel) {
    return PIT_FREQUENCY / i8253_divisor(&i8253.channels[channel]);
}

static inline uint16_t i8253_get_current_count(const i8253_channel_s *channel) {
    const uint32_t reload = i8253_divisor(channel);
    const uint64_t ticks =
        ((i8253_now_us() - channel->start_timestamp_us) * PIT_FREQUENCY) / 1000000ULL;

    if ((channel->operating_mode & 7) == 0) {
        if (!channel->active || ticks >= reload) {
            return 0;
        }
        return (uint16_t)(reload - ticks);
    }

    return (uint16_t)(reload - 1 - ticks % reload);
}

static inline uint8_t i8253_read(const uint16_t port_number) {
    const uint8_t channel_index = port_number & 3;
    if (channel_index == 3) {
        return 0xFF;
    }

    i8253_channel_s *channel = &i8253.channels[channel_index];
    const uint16_t value = i8253_get_current_count(channel);

    switch (channel->access_mode) {
        case PIT_MODE_LOBYTE:
            return (uint8_t)value;
        case PIT_MODE_HIBYTE:
            return (uint8_t)(value >> 8);
        case PIT_MODE_TOGGLE:
            channel->byte_toggle ^= 1;
            return channel->byte_toggle ? (uint8_t)value : (uint8_t)(value >> 8);
        default:
            return 0xFF;
    }
}

static inline void i8253_write(const uint16_t port_number, const uint8_t data) {
    if (port_number <= 0x42) {
        const uint8_t channel_index = port_number & 3;
        i8253_channel_s *channel = &i8253.channels[channel_index];

        if (channel->access_mode == PIT_MODE_LOBYTE) {
            channel->reload_value = data;
        } else if (channel->access_mode == PIT_MODE_HIBYTE) {
            channel->reload_value = (uint16_t)data << 8;
        } else {
            if (channel->byte_toggle == 0) {
                channel->reload_value = (channel->reload_value & 0xFF00u) | data;
                channel->byte_toggle = 1;
                return;
            }
            channel->reload_value =
                (channel->reload_value & 0x00FFu) | ((uint16_t)data << 8);
            channel->byte_toggle = 0;
        }

        channel->active = 1;
        channel->start_timestamp_us = i8253_now_us();

        if (channel_index == 0) {
            const uint32_t divisor = i8253_divisor(channel);
            i8259.interrupt_request_register &= (uint8_t)~1u;
#if PICO_ON_DEVICE
            timer_period = (int)(((uint64_t)divisor * 1000000ULL) / PIT_FREQUENCY);
            if (timer_period < 1) {
                timer_period = 1;
            }
#else
            timer_period = (int)(PIT_FREQUENCY / divisor);
#endif
        } else if (channel_index == 2) {
#if I2S_SOUND || HARDWARE_SOUND || !PICO_ON_DEVICE
            speakerenabled = (port61 & 3) == 3;
#else
            pwm_config config = pwm_get_default_config();
            pwm_config_set_wrap(&config, channel->reload_value);
            pwm_init(pwm_gpio_to_slice_num(PWM_BEEPER), &config, true);
            pwm_set_gpio_level(PWM_BEEPER, (port61 & 3) == 3 ? 127 : 0);
#endif
        }
        return;
    }

    const uint8_t channel_index = data >> 6;
    if (channel_index == 3) {
        return;
    }

    const uint8_t access_mode = (data >> 4) & 3;
    i8253_channel_s *channel = &i8253.channels[channel_index];

    if (access_mode == PIT_MODE_LATCHCOUNT) {
        channel->latched_value = i8253_get_current_count(channel);
        channel->latch_mode = channel->access_mode;
        channel->byte_toggle = 0;
    } else {
        channel->access_mode = access_mode;
        channel->operating_mode = (data >> 1) & 7;
        channel->reload_value = 0;
        channel->active = 0;
        channel->latch_mode = 0;
        channel->byte_toggle = 0;
        channel->start_timestamp_us = 0;
    }
}
