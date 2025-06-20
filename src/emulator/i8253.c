#include "emulator.h"
#define PIT_MODE_LATCHCOUNT  0
#define PIT_MODE_LOBYTE 1
#define PIT_MODE_HIBYTE 2
#define PIT_MODE_TOGGLE 3
#if PICO_ON_DEVICE
#include <hardware/pwm.h>
#include <hardware/structs/clocks.h>
#include <hardware/clocks.h>
pwm_config config;
#endif
struct i8253_s i8253_controller = { 0 };
int speakerenabled = 0;                    // speakerenabled -> speaker_enabled
int timer_period = 54925;           // timer_period -> system_timer_period

void init8253() {
    memset(&i8253_controller, 0, sizeof(i8253_controller));
}

void out8253(uint16_t portnum, uint8_t value) {
    uint8_t current_byte_selector = 0;      // curbyte -> current_byte_selector
    portnum &= 3;
    switch (portnum) {
        case 0:
        case 1:
        case 2: //channel data
            if ((i8253_controller.channel_access_mode[portnum] == PIT_MODE_LOBYTE) ||
                ((i8253_controller.channel_access_mode[portnum] == PIT_MODE_TOGGLE) && (i8253_controller.channel_byte_toggle[portnum] == 0))) {
                current_byte_selector = 0;
            } else if ((i8253_controller.channel_access_mode[portnum] == PIT_MODE_HIBYTE) ||
                       ((i8253_controller.channel_access_mode[portnum] == PIT_MODE_TOGGLE) && (i8253_controller.channel_byte_toggle[portnum] == 1))) {
                current_byte_selector = 1;
            }

            if (current_byte_selector == 0) {
                //low byte
                i8253_controller.channel_reload_value[portnum] = (i8253_controller.channel_reload_value[portnum] & 0xFF00) | value;
            } else {
                //high byte
                i8253_controller.channel_reload_value[portnum] = (i8253_controller.channel_reload_value[portnum] & 0x00FF) | ((uint16_t) value << 8);
            }

            if (i8253_controller.channel_reload_value[portnum] == 0) {
                i8253_controller.channel_effective_count[portnum] = 65536;
#if I2S_SOUND || HARDWARE_SOUND || !PICO_ON_DEVICE
                speakerenabled = 0;
#else
                pwm_set_gpio_level(PWM_BEEPER, 0);                      // set 0% (0) duty clcle ==> Sound output off
#endif
            } else {
                i8253_controller.channel_effective_count[portnum] = i8253_controller.channel_reload_value[portnum];
#if I2S_SOUND || HARDWARE_SOUND || !PICO_ON_DEVICE
                if (port61 & 2) {
                    speakerenabled = 1; // set 50% (127) duty cycle ==> Sound output on
                } else {
                    speakerenabled = 0; // set 0% (0) duty clcle ==> Sound output off
                }
#else
                pwm_config_set_wrap(&config, i8253.channel_effective_count[portnum]);
                pwm_init(pwm_gpio_to_slice_num(PWM_BEEPER), &config, true);

                if (port61 & 2) {
                    pwm_set_gpio_level(PWM_BEEPER, 127);                    // set 50% (127) duty cycle ==> Sound output on
                } else {
                    pwm_set_gpio_level(PWM_BEEPER, 0);                      // set 0% (0) duty clcle ==> Sound output off
                }
#endif
            }


            i8253_controller.channel_active[portnum] = 1;
            if (i8253_controller.channel_access_mode[portnum] == PIT_MODE_TOGGLE) {
                i8253_controller.channel_byte_toggle[portnum] = (~i8253_controller.channel_byte_toggle[portnum]) & 1;
            }

            i8253_controller.channel_frequency[portnum] = 1193182 / i8253_controller.channel_effective_count[portnum];
#if 1
            if (portnum == 0) {
                // Timer freq 1,193,180
#if PICO_ON_DEVICE
                system_timer_period =  1000000 / i8253.channel_frequency[portnum];
#else
                timer_period =  i8253_controller.channel_frequency[portnum];
#endif
            }
#endif
            break;
        case 3: //mode/command
            i8253_controller.channel_access_mode[value >> 6] = (value >> 4) & 3;
            if (i8253_controller.channel_access_mode[value >> 6] == PIT_MODE_TOGGLE) i8253_controller.channel_byte_toggle[value >> 6] = 0;
            break;
    }
}

uint8_t in8253(uint16_t portnum) {
    uint8_t current_byte_selector;          // curbyte -> current_byte_selector
    portnum &= 3;
    switch (portnum) {
        case 0:
        case 1:
        case 2: //channel data
            if ((i8253_controller.channel_access_mode[portnum] == 0) || (i8253_controller.channel_access_mode[portnum] == PIT_MODE_LOBYTE) ||
                ((i8253_controller.channel_access_mode[portnum] == PIT_MODE_TOGGLE) && (i8253_controller.channel_byte_toggle[portnum] == 0))) {
                current_byte_selector = 0;
            } else if ((i8253_controller.channel_access_mode[portnum] == PIT_MODE_HIBYTE) ||
                       ((i8253_controller.channel_access_mode[portnum] == PIT_MODE_TOGGLE) && (i8253_controller.channel_byte_toggle[portnum] == 1))) {
                current_byte_selector = 1;
            }

            if ((i8253_controller.channel_access_mode[portnum] == 0) || (i8253_controller.channel_access_mode[portnum] == PIT_MODE_TOGGLE))
                i8253_controller.channel_byte_toggle[portnum] = (~i8253_controller.channel_byte_toggle[portnum]) & 1;
            if (current_byte_selector == 0) {
                //low byte
                if (i8253_controller.channel_current_count[portnum] < 10) i8253_controller.channel_current_count[portnum] = i8253_controller.channel_reload_value[portnum];
                i8253_controller.channel_current_count[portnum] -= 10;
                return ((uint8_t) i8253_controller.channel_current_count[portnum]);
            } else {
                //high byte
                return ((uint8_t) (i8253_controller.channel_current_count[portnum] >> 8));
            }
    }
    return (0);
}