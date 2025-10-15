#pragma GCC optimize("Ofast")

#include <pico/time.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/pwm.h>
#include <hardware/exception.h>

#ifndef ONBOARD_PSRAM_GPIO
#ifndef TOTAL_VIRTUAL_MEMORY_KBS
#include "psram_spi.h"
#endif
#endif

#if PICO_RP2040
#include "../../memops_opt/memops_opt.h"
#else
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
#endif

#include "tusb.h" 
#include "../lib/Pico-PIO-USB/src/pio_usb_configuration.h"

#include "emulator/emulator.h"
#include "audio.h"
#include "graphics.h"
#include "../drivers/usbhid/hid_app.h"
#include "ff.h"
#include "emu8950.h"

#if HARDWARE_SOUND
#include "74hc595/74hc595.h"
#endif

// Global variables
extern OPL *emu8950_opl;
extern uint16_t timeconst;
extern bool PSRAM_AVAILABLE;

FATFS fs;
struct semaphore vga_start_semaphore;
int cursor_blink_state = 0;

#if I2S_SOUND
i2s_config_t i2s_config;
#elif PWM_SOUND || HARDWARE_SOUND
pwm_config pwm;
#endif

// Debug VRAM for text output
uint8_t __aligned(4) DEBUG_VRAM[80 * 10] = {0};

// Function declarations
bool handleScancode(uint32_t ps2scancode);
void second_core(void);
void sigbus(void);
void __attribute__((naked, noreturn)) __printflike(1, 0) dummy_panic(__unused const char *fmt, ...);
#if PICO_RP2350
void __no_inline_not_in_flash_func(psram_init)(uint cs_pin);
#endif

static const uint32_t NO_TORMOZ = 32768;
#if !PICO_RP2040
static const uint32_t TORMOZ_TURBO_8 = 4;
static const uint32_t TORMOZ_PC_XT = 3;
static uint32_t delay = 0;
static const uint32_t DELAY_TURBO_8 = 8;
static const uint32_t DELAY_PC_XT = 10;
#else
static const uint32_t TORMOZ_TURBO_8 = 1024;
static const uint32_t TORMOZ_PC_XT = 512;
#endif
static uint32_t tormoz = NO_TORMOZ;

static bool ctrlPressed = false;
static bool altPressed = false;

static const uint32_t SCANCODE_CTRL_PRESS = 0x1D;
static const uint32_t SCANCODE_CTRL_RELEASE = 0x9D;
static const uint32_t SCANCODE_ALT_PRESS = 0x38;
static const uint32_t SCANCODE_ALT_RELEASE = 0xB8;
static const uint32_t SCANCODE_KP_MULT_UP = 0xB7;
static const uint32_t SCANCODE_KP_MINUS_UP = 0xCA;
static const uint32_t SCANCODE_KP_PLUS_UP = 0xCE;

bool handleScancode(uint32_t ps2scancode) {
    //printf("PS/2 SCANCODE: %d\n", ps2scancode);

    switch(ps2scancode) {
        case SCANCODE_CTRL_PRESS:
            ctrlPressed = true;
            break;
        case SCANCODE_CTRL_RELEASE:
            ctrlPressed = false;
            break;
        case SCANCODE_ALT_PRESS:
            altPressed = true;
            break;
        case SCANCODE_ALT_RELEASE:
            altPressed = false;
            break;
        case SCANCODE_KP_MULT_UP: // KP "*" up
            ega_vga_enabled = !ega_vga_enabled;
            printf("EGA/VGA: %s\n", ega_vga_enabled ? "ON" : "OFF");
            break;
        case SCANCODE_KP_MINUS_UP: // KP "-" up
            if (ctrlPressed && altPressed) {
                switch (tormoz) {
                    case NO_TORMOZ:
                        tormoz = TORMOZ_TURBO_8;
                        printf("TURBO 8\n"); // TODO: calibrate on other configurations
                        #if !PICO_RP2040
                            delay = DELAY_TURBO_8;
                        #endif
                        break;
                    case TORMOZ_TURBO_8:
                        tormoz = TORMOZ_PC_XT;
                        #if !PICO_RP2040
                            delay = DELAY_PC_XT;
                        #endif
                        printf("PC XT\n");
                        break;
                }
            }
            break;
        case SCANCODE_KP_PLUS_UP: // KP "+" up
            if (ctrlPressed && altPressed) {
                switch (tormoz) {
                    case TORMOZ_PC_XT:
                        tormoz = TORMOZ_TURBO_8;
                        printf("TURBO 8\n");
                        #if !PICO_RP2040
                            delay = DELAY_TURBO_8;
                        #endif
                        break;
                    case TORMOZ_TURBO_8:
                        tormoz = NO_TORMOZ;
                        #if !PICO_RP2040
                            delay = 0;
                            printf("Model 70\n");
                        #else
                            printf("Model 30\n");
                        #endif
                        break;
                }
            }
            break;
    }

    port60 = ps2scancode;
    port64 |= 2;
    doirq(1);
    return true;
}

/* Renderer loop on Pico's second core */
void __time_critical_func() second_core(void) {
    // Initialize graphics subsystem
    graphics_init();
    graphics_set_buffer(VIDEORAM, 320, 200);
    graphics_set_textbuffer(VIDEORAM + 32768);
    graphics_set_bgcolor(0);
    graphics_set_offset(0, 0);
    graphics_set_flashmode(true, true);

    // Set initial VGA palette
    for (uint8_t i = 0; i < 255; i++) {
        graphics_set_palette(i, vga_palette[i]);
    }

    // Initialize sound hardware
#if !HARDWARE_SOUND
    emu8950_opl = OPL_new(3579552, SOUND_FREQUENCY);
#endif

#if I2S_SOUND
    audio_dac_init();

    i2s_config = i2s_get_default_config();
    i2s_config.sample_freq = SOUND_FREQUENCY;
    i2s_config.dma_trans_count = 1;
    i2s_volume(&i2s_config, 0);
    i2s_init(&i2s_config);
    sleep_ms(100);

#elif PWM_SOUND
    pwm = pwm_get_default_config();

    gpio_set_function(PWM_LEFT_CHANNEL, GPIO_FUNC_PWM);
    gpio_set_function(PWM_RIGHT_CHANNEL, GPIO_FUNC_PWM);

    pwm_config_set_clkdiv(&pwm, 1.0f);
    pwm_config_set_wrap(&pwm, (1 << 12) - 1); // MAX PWM value

    pwm_init(pwm_gpio_to_slice_num(PWM_LEFT_CHANNEL), &pwm, true);
    pwm_init(pwm_gpio_to_slice_num(PWM_RIGHT_CHANNEL), &pwm, true);

    gpio_set_function(PWM_BEEPER, GPIO_FUNC_PWM);
    pwm_config_set_clkdiv(&pwm, 127);
    pwm_init(pwm_gpio_to_slice_num(PWM_BEEPER), &pwm, true);

#elif HARDWARE_SOUND
    init_74hc595();
    pwm = pwm_get_default_config();
    gpio_set_function(PCM_PIN, GPIO_FUNC_PWM);
    pwm_config_set_clkdiv(&pwm, 1.0f);
    pwm_config_set_wrap(&pwm, (1 << 12) - 1); // MAX PWM value
    pwm_init(pwm_gpio_to_slice_num(PCM_PIN), &pwm, true);
#endif

    // Timing variables
    uint64_t tick = time_us_64();
    uint64_t last_timer_tick = tick;
    uint64_t last_cursor_blink = tick;
    uint64_t last_sound_tick = tick;
    uint64_t last_frame_tick = tick;
    uint64_t last_dss_tick = 0;
    uint64_t last_sb_tick = 0;

    int16_t last_dss_sample = 0;
    int16_t last_sb_sample = 0;

    // Main render loop
    while (true) {
        // Timer interrupt handling
        if (tick >= last_timer_tick + timer_period) {
            doirq(0);
            last_timer_tick = tick;
        }

        // Cursor blink handling (333ms intervals)
        if (tick >= last_cursor_blink + 333333) {
            cursor_blink_state ^= 1;
            last_cursor_blink = tick;
        }

        // Dinse Sound Source frequency ~7kHz
        if (tick > last_dss_tick + (1000000 / 7000)) {
            last_dss_sample = dss_sample();
            last_dss_tick = tick;
        }

#if !PICO_RP2040
        // Sound Blaster sampling
        if (tick > last_sb_tick + timeconst) {
            last_sb_sample = blaster_sample();
            last_sb_tick = tick;
        }
#endif

        // Audio output at configured sample rate
        if (tick > last_sound_tick + (1000000 / SOUND_FREQUENCY)) {
            int16_t samples[2];
            get_sound_sample(last_dss_sample + last_sb_sample, samples);

#if I2S_SOUND
            i2s_dma_write(&i2s_config, samples);
#elif PWM_SOUND
            pwm_set_gpio_level(PWM_LEFT_CHANNEL, (uint16_t)((int32_t)samples[0] + 0x8000L) >> 4);
            pwm_set_gpio_level(PWM_RIGHT_CHANNEL, (uint16_t)((int32_t)samples[1] + 0x8000L) >> 4);
#endif
            last_sound_tick = tick;
        }

        // Video frame rendering (~60Hz)
        if (tick >= last_frame_tick + 16667) {
            static uint8_t old_video_mode;

            // Handle video mode changes
            if (old_video_mode != videomode) {
                switch (videomode) {
                    case TEXTMODE_80x25_BW:
                    case TEXTMODE_40x25_BW:
                        case TEXTMODE_80x25_COLOR:
                        case TEXTMODE_40x25_COLOR: {
                        for (uint8_t i = 0; i < 16; i++) {
                            graphics_set_palette(i, cga_palette[i]);
                        }
                    }
                        break;

                    case TGA_160x200x16:
                    case TGA_320x200x16:
                    case TGA_640x200x16:
                        for (uint8_t i = 0; i < 15; i++) {
                            graphics_set_palette(i, tga_palette[i]);
                        }
                        break;

                    case COMPOSITE_160x200x16:
#ifndef NTSC
                        for (uint8_t i = 0; i < 15; i++) {
                            graphics_set_palette(i, cga_composite_palette[0][i]);
                        }
                        break;
#endif

                    case COMPOSITE_160x200x16_force:
#ifndef NTSC
                        for (uint8_t i = 0; i < 15; i++) {
                            graphics_set_palette(i, cga_composite_palette[cga_intensity << 1][i]);
                        }
                        break;
#endif
                    case CGA_320x200x4_BW:
                    case CGA_320x200x4:
                        for (uint8_t i = 0; i < 4; i++) {
                            graphics_set_palette(i, cga_palette[cga_gfxpal[cga_colorset][cga_intensity][i]]);
                        }
                        break;

                    case VGA_320x200x256:
                    case VGA_320x200x256x4:
                    default:
                        for (uint8_t i = 0; i < 255; i++) {
                            graphics_set_palette(i, vga_palette[i]);
                        }
                        break;
                }
                graphics_set_mode(videomode);
                old_video_mode = videomode;
            }
#if defined(TFT)
            refresh_lcd();
            port3DA = 8;
            port3DA |= 1;
#endif
            last_frame_tick = tick;
        }
        tick = time_us_64();
        tight_loop_contents();
    }
    __unreachable();
}

#if PICO_RP2350
void __no_inline_not_in_flash_func(psram_init)(uint cs_pin) {
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    // Enable direct mode, PSRAM CS, clkdiv of 10
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB |
                         QMI_DIRECT_CSR_EN_BITS |
                         QMI_DIRECT_CSR_AUTO_CS1N_BITS;

    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
        tight_loop_contents();
    }

    // Enable QPI mode on the PSRAM
    const uint CMD_QPI_EN = 0x35;
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;

    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
        tight_loop_contents();
    }

    // Set PSRAM timing for APS6404
    const int max_psram_freq = 166000000;
    const int clock_hz = clock_get_hz(clk_sys);
    int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;

    if (divisor == 1 && clock_hz > 100000000) {
        divisor = 2;
    }

    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) {
        rxdelay += 1;
    }

    // Calculate timing parameters
    const int clock_period_fs = 1000000000000000ll / clock_hz;
    const int max_select = (125 * 1000000) / clock_period_fs;  // 125 = 8000ns / 64
    const int min_deselect = (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

    qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
                          QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
                          max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
                          min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
                          rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
                          divisor << QMI_M1_TIMING_CLKDIV_LSB;

    // Set PSRAM read format
    qmi_hw->m[1].rfmt = QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
                        QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
                        QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
                        QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
                        QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
                        QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
                        6 << QMI_M0_RFMT_DUMMY_LEN_LSB;

    qmi_hw->m[1].rcmd = 0xEB;

    // Set PSRAM write format
    qmi_hw->m[1].wfmt = QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
                        QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
                        QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
                        QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
                        QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
                        QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;

    qmi_hw->m[1].wcmd = 0x38;

    // Disable direct mode
    qmi_hw->direct_csr = 0;

    // Enable writes to PSRAM
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
}
#endif

void sigbus(void) {
    printf("SIGBUS exception caught...\n");
    // reset_usb_boot(0, 0);
}

#if 0
void __attribute__((naked, noreturn)) __printflike(1, 0) dummy_panic(__unused const char *fmt, ...) {
    puts("*** PANIC ***");
    if (fmt) {
        puts(fmt);
    }
}
#endif

int main(void) {
    // Platform-specific initialization
#if PICO_RP2350
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);

    qmi_hw->m[0].timing = 0x60007304; // 4x FLASH divisor

    sleep_ms(100);
    if (!set_sys_clock_hz(CPU_FREQ_MHZ * MHZ, 0) ) {
        set_sys_clock_hz(378 * MHZ, 1); // fallback to failsafe clocks
    }
    sleep_ms(100);
#else
    memcpy_wrapper_replace(NULL);
    hw_set_bits(&vreg_and_chip_reset_hw->vreg, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    set_sys_clock_hz(CPU_FREQ_MHZ * MHZ, true);
#endif

    stdio_init_all();
    stdio_puts("\n\nFruit Jam 286");
 
    // Initialize PSRAM
#ifdef ONBOARD_PSRAM_GPIO
    // Overclock psram
    stdio_puts("Init psram");
    psram_init(ONBOARD_PSRAM_GPIO);

#else
    #ifndef TOTAL_VIRTUAL_MEMORY_KBS
    init_psram();
    #endif
#endif

    // Set exception handler
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, sigbus);

    // Initialize onboard LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // LED startup sequence
    for (int i = 0; i < 6; i++) {
        sleep_ms(23);
        gpio_put(PICO_DEFAULT_LED_PIN, true);
        sleep_ms(23);
        gpio_put(PICO_DEFAULT_LED_PIN, false);
    }

    sleep_ms(50);

    // Initialize peripherals
    stdio_puts("keyboard_init");
    keyboard_init();
    sleep_ms(5);
    nespad_read();

    // Check for mouse availability
    stdio_puts("mouse_init");
#ifndef MURM2
    const uint8_t mouse_available = nespad_state;
    if (mouse_available)
#endif
        mouse_init();

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    _Static_assert(PIN_USB_HOST_DP + 1 == PIN_USB_HOST_DM || PIN_USB_HOST_DP - 1 == PIN_USB_HOST_DM, "Permitted USB D+/D- configuration");
    pio_cfg.pinout = PIN_USB_HOST_DP + 1 == PIN_USB_HOST_DM ? PIO_USB_PINOUT_DPDM : PIO_USB_PINOUT_DMDP;
    pio_cfg.pin_dp = PIN_USB_HOST_DP;
    pio_cfg.tx_ch = 9;
    pio_cfg.pio_tx_num = 1;
    pio_cfg.pio_rx_num = 1;

    #ifdef PIN_USB_HOST_VBUS
    printf("Enabling USB host VBUS power on GP%d\r\n", PIN_USB_HOST_VBUS);
    gpio_init(PIN_USB_HOST_VBUS);
    gpio_set_dir(PIN_USB_HOST_VBUS, GPIO_OUT);
    gpio_put(PIN_USB_HOST_VBUS, 1);
    #endif

    tuh_configure(CFG_TUH_RPI_PIO_USB, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    printf("Init USB...\n");
    printf("USB D+/D- on GP%d and GP%d\r\n", PIN_USB_HOST_DP, PIN_USB_HOST_DM);
    printf("TinyUSB Host HID Controller Example\r\n");

    tusb_init();

#if 1
puts("loopy\n");
    for(int i=0; i<2000; i++) {
        tuh_task();
        sleep_ms(1);
    }
puts("noopy\n");
#endif

    // Initialize semaphore and launch second core
    stdio_puts("launch core 1");
    sem_init(&vga_start_semaphore, 0, 1);
    multicore_launch_core1(second_core);
    sleep_ms(1000);
    sem_release(&vga_start_semaphore);

    stdio_puts("mount SD card");
    // Mount SD card filesystem
    if (FR_OK != f_mount(&fs, "0", 1)) {
        printf("SD Card not inserted or SD Card error!");
        while (1);
    }
    // adlib_init(SOUND_FREQUENCY);
#ifdef TOTAL_VIRTUAL_MEMORY_KBS
    init_swap();
#endif

    stdio_puts("init audio");
    // Initialize audio and reset emulator
    sn76489_reset();
    stdio_puts("reset86");
    reset86();

    stdio_puts("init mouse");
    // Initialize mouse control variables
    nespad_read();
    float mouse_throttle = 3.0f;
    bool left = nespad_state & DPAD_LEFT;
    bool right = nespad_state & DPAD_RIGHT;
    bool up = nespad_state & DPAD_UP;
    bool down = nespad_state & DPAD_DOWN;

    stdio_puts("main emulation loop");
    // Main emulation loop
    while (true) {
        tuh_task();
        exec86(tormoz);
#if !PICO_RP2040
        if (delay) sleep_us(delay);
#endif
        // Handle gamepad input for mouse emulation
#ifndef MURM2
        if (!mouse_available) {
#endif
            nespad_read();

            // Increase mouse speed when holding direction
            if ((left && (nespad_state & DPAD_LEFT)) ||
                (right && (nespad_state & DPAD_RIGHT)) ||
                (down && (nespad_state & DPAD_DOWN)) ||
                (up && (nespad_state & DPAD_UP))) {
                mouse_throttle += 0.2f;
            } else {
                mouse_throttle = 3.0f;
            }

            // Update direction states
            left = nespad_state & DPAD_LEFT;
            right = nespad_state & DPAD_RIGHT;
            up = nespad_state & DPAD_UP;
            down = nespad_state & DPAD_DOWN;

            // Send mouse event
            sermouseevent(nespad_state & DPAD_B | ((nespad_state & DPAD_A) != 0) << 1,
                         left ? -mouse_throttle : right ? mouse_throttle : 0,
                         down ? mouse_throttle : up ? -mouse_throttle : 0);
#ifndef MURM2
        }
#endif
        tight_loop_contents();
    }
    __unreachable();
}
