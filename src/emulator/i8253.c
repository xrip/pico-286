#include "emulator.h"

#define PIT_MODE_LATCHCOUNT  0
#define PIT_MODE_LOBYTE 1
#define PIT_MODE_HIBYTE 2
#define PIT_MODE_TOGGLE 3

#if PICO_ON_DEVICE
#include <hardware/pwm.h>
#include <hardware/structs/clocks.h>
#include <hardware/clocks.h>
static pwm_config config;
#endif

struct i8253_s i8253_controller = { 0 };
int speakerenabled = 0;
int timer_period = 54925;

void init8253() {
    memset(&i8253_controller, 0, sizeof(i8253_controller));
}

void out8253(uint16_t portnum, uint8_t value) {
    portnum &= 3;
    
    if (portnum <= 2) { // channel data
        const uint8_t access_mode = i8253_controller.channel_access_mode[portnum];
        const uint8_t byte_toggle = i8253_controller.channel_byte_toggle[portnum];
        
        // Determine which byte to write (low or high)
        const uint8_t current_byte_selector = 
            (access_mode == PIT_MODE_LOBYTE || (access_mode == PIT_MODE_TOGGLE && byte_toggle == 0)) ? 0 : 1;

        // Update reload value
        if (current_byte_selector == 0) {
            i8253_controller.channel_reload_value[portnum] = 
                (i8253_controller.channel_reload_value[portnum] & 0xFF00) | value;
        } else {
            i8253_controller.channel_reload_value[portnum] = 
                (i8253_controller.channel_reload_value[portnum] & 0x00FF) | ((uint16_t)value << 8);
        }

        const uint16_t reload_value = i8253_controller.channel_reload_value[portnum];
        
        if (reload_value == 0) {
            i8253_controller.channel_effective_count[portnum] = 65536;
#if I2S_SOUND || HARDWARE_SOUND || !PICO_ON_DEVICE
            speakerenabled = 0;
#else
            pwm_set_gpio_level(PWM_BEEPER, 0);
#endif
        } else {
            i8253_controller.channel_effective_count[portnum] = reload_value;
            
#if I2S_SOUND || HARDWARE_SOUND || !PICO_ON_DEVICE
            speakerenabled = (port61 & 2) ? 1 : 0;
#else
            pwm_config_set_wrap(&config, reload_value);
            pwm_init(pwm_gpio_to_slice_num(PWM_BEEPER), &config, true);
            pwm_set_gpio_level(PWM_BEEPER, (port61 & 2) ? 127 : 0);
#endif
        }

        i8253_controller.channel_active[portnum] = 1;
        
        if (access_mode == PIT_MODE_TOGGLE) {
            i8253_controller.channel_byte_toggle[portnum] = (~byte_toggle) & 1;
        }

        // Calculate frequency
        i8253_controller.channel_frequency[portnum] = 1193182 / i8253_controller.channel_effective_count[portnum];

        // Update timer period for channel 0
        if (portnum == 0) {
#if PICO_ON_DEVICE
            timer_period = 1000000 / i8253_controller.channel_frequency[portnum];
#else
            timer_period = i8253_controller.channel_frequency[portnum];
#endif
        }
    } else { // portnum == 3: mode/command
        const uint8_t channel = value >> 6;
        const uint8_t access_mode = (value >> 4) & 3;
        
        i8253_controller.channel_access_mode[channel] = access_mode;
        
        if (access_mode == PIT_MODE_TOGGLE) {
            i8253_controller.channel_byte_toggle[channel] = 0;
        }
    }
}

uint8_t in8253(uint16_t portnum) {
    portnum &= 3;
    
    if (portnum <= 2) { // channel data
        const uint8_t access_mode = i8253_controller.channel_access_mode[portnum];
        const uint8_t byte_toggle = i8253_controller.channel_byte_toggle[portnum];
        
        // Determine which byte to read (low or high)
        const uint8_t current_byte_selector = 
            (access_mode == 0 || access_mode == PIT_MODE_LOBYTE || 
             (access_mode == PIT_MODE_TOGGLE && byte_toggle == 0)) ? 0 : 1;

        // Update toggle state for relevant modes
        if (access_mode == 0 || access_mode == PIT_MODE_TOGGLE) {
            i8253_controller.channel_byte_toggle[portnum] = (~byte_toggle) & 1;
        }
        
        if (current_byte_selector == 0) {
            // Low byte - update counter if needed
            if (i8253_controller.channel_current_count[portnum] < 10) {
                i8253_controller.channel_current_count[portnum] = i8253_controller.channel_reload_value[portnum];
            }
            i8253_controller.channel_current_count[portnum] -= 10;
            return (uint8_t)i8253_controller.channel_current_count[portnum];
        } else {
            // High byte
            return (uint8_t)(i8253_controller.channel_current_count[portnum] >> 8);
        }
    }
    
    return 0;
}