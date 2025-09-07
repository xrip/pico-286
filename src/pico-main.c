#pragma GCC optimize("Ofast")

#include <pico/time.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/pwm.h>
#include <hardware/exception.h>
#include <hardware/watchdog.h>

#include "psram_spi.h"
#include "emulator/swap.h"

#if PICO_RP2040
#include "../../memops_opt/memops_opt.h"
#else
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
#include <hardware/regs/sysinfo.h>
#endif

#include "emulator/emulator.h"
#include "audio.h"
#include "graphics.h"
#include "ps2.h"
#include "cpu.h"
#include "ff.h"
#include "nespad.h"
#include "emu8950.h"
#include "ps2_mouse.h"

#if HARDWARE_SOUND
#include "74hc595/74hc595.h"
#endif

uint8_t __attribute__((aligned (4), section(".cmos"))) cmos[4 << 10] = { 0 };

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
static const uint32_t SCANCODE_DEL_RELEASE = 0xD3;

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
        case SCANCODE_DEL_RELEASE:
            if (ctrlPressed && altPressed) {
                watchdog_enable(1, true);
            }
            break;
        case SCANCODE_KP_MULT_UP: // KP "*" up
            if (ctrlPressed && altPressed) {
                ega_vga_enabled = !ega_vga_enabled;
                printf("EGA/VGA: %s\n", ega_vga_enabled ? "ON" : "MCGA");
            }
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
                    default:
                        printf("\n\n\n\n\n\n\n\n\n\n");
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
                        delay = 0;
                        if (butter_psram_size)
                            printf("Model 70\n");
                        else
                            printf("Model 50\n");
                        break;
                    default:
                        printf("\n\n\n\n\n\n\n\n\n\n");
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

INLINE void _putchar(char character) {
    static uint8_t color = 0xf;
    static int x = 0, y = 0;

    // Handle screen scrolling
    if (y == 10) {
        y = 9;
        memmove(DEBUG_VRAM, DEBUG_VRAM + 80, 80 * 9);
        memset(DEBUG_VRAM + 80 * 9, 0, 80);
    }

    uint8_t *vidramptr = DEBUG_VRAM + __fast_mul(y, 80) + x;

    if ((unsigned)character >= 32) {
        // Convert to uppercase if lowercase
        if (character >= 96) {
            character -= 32;
        }
        *vidramptr = ((character - 32) & 63) | 0 << 6;

        if (x == 80) {
            x = 0;
            y++;
        } else {
            x++;
        }
    } else if (character == '\n') {
        x = 0;
        y++;
    } else if (character == '\r') {
        x = 0;
    } else if (character == 8 && x > 0) {
        x--;
        *vidramptr = 0;
    }
}

volatile int16_t last_sb_sample = 0;
volatile bool ask_to_blast = false;

/* Renderer loop on Pico's second core */
void __time_critical_func() second_core(void) {
    // Initialize graphics subsystem
    graphics_init();
    graphics_set_buffer((uint8_t *)VIDEORAM, 320, 200);
    graphics_set_textbuffer((uint8_t *)VIDEORAM + 32768*4);
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
            if (butter_psram_size || PSRAM_AVAILABLE)
                last_sb_sample = blaster_sample();
            else
                ask_to_blast = true; // protect swap from using from seconf core
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
                        if (vga_planar_mode) {
                            videomode = VGA_320x200x256x4;
                        }
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
uint32_t butter_psram_size = 0 ;
uint32_t BUTTER_PSRAM_GPIO = 0;
bool rp2350a = true;
#define MB16 (16ul << 20)
#define MB8 (8ul << 20)
#define MB4 (4ul << 20)
#define MB1 (1ul << 20)
inline static uint32_t __not_in_flash_func(_butter_psram_size)() {
    volatile uint8_t* PSRAM_DATA = (uint8_t*)0x11000000;
    for(register int i = MB8; i < MB16; ++i)
        PSRAM_DATA[i] = 16;
    for(register int i = MB4; i < MB8; ++i)
        PSRAM_DATA[i] = 8;
    for(register int i = MB1; i < MB4; ++i)
        PSRAM_DATA[i] = 4;
    for(register int i = 0; i < MB1; ++i)
        PSRAM_DATA[i] = 1;
    register uint32_t res = PSRAM_DATA[MB16 - 1];
    for (register int i = MB16 - MB1; i < MB16; ++i) {
        if (res != PSRAM_DATA[i])
            return 0;
    }
    return res << 20;
}
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

    // Set PSRAM timing
    const int max_psram_freq = PSRAM_FREQ_MHZ * MHZ;
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
    // detect a chip size
    butter_psram_size = _butter_psram_size();
}
#endif

void sigbus(void) {
    printf("SIGBUS exception caught...\n");
    // reset_usb_boot(0, 0);
}

void __attribute__((naked, noreturn)) __printflike(1, 0) dummy_panic(__unused const char *fmt, ...) {
    printf("*** PANIC ***");
    if (fmt) {
        printf(fmt);
    }
}

int cpu_mhz = CPU_FREQ_MHZ;
int flash_mhz = FLASH_FREQ_MHZ;
int psram_mhz = PSRAM_FREQ_MHZ;
uint new_flash_timings = 0;
uint new_psram_timings = 0;

void __not_in_flash() flash_timings() {
    if (!new_flash_timings) {
        const int max_flash_freq = flash_mhz * MHZ;
        const int clock_hz = cpu_mhz * MHZ;
        int divisor = (clock_hz + max_flash_freq - 1) / max_flash_freq;
        if (divisor == 1 && clock_hz > 100000000) {
            divisor = 2;
        }
        int rxdelay = divisor;
        if (clock_hz / divisor > 100000000) {
            rxdelay += 1;
        }
        qmi_hw->m[0].timing = 0x60007000 |
                            rxdelay << QMI_M0_TIMING_RXDELAY_LSB |
                            divisor << QMI_M0_TIMING_CLKDIV_LSB;
    } else {
        qmi_hw->m[0].timing = new_flash_timings;
    }
}

void __not_in_flash() psram_timings() {
    if (!new_psram_timings) {
        const int max_psram_freq = psram_mhz * MHZ;
        const int clock_hz = cpu_mhz * MHZ;
        int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
        if (divisor == 1 && clock_hz > 100000000) {
            divisor = 2;
        }
        int rxdelay = divisor;
        if (clock_hz / divisor > 100000000) {
            rxdelay += 1;
        }
        qmi_hw->m[1].timing = (qmi_hw->m[1].timing & ~0x000000FFF) |
                            rxdelay << QMI_M0_TIMING_RXDELAY_LSB |
                            divisor << QMI_M0_TIMING_CLKDIV_LSB;
    } else {
        qmi_hw->m[1].timing = new_psram_timings;
    }
}

static char* open_config(UINT* pbr) {
    FILINFO fileinfo;
    size_t file_size = 0;
    char * cfn = "/config.286";
    if (f_stat(cfn, &fileinfo) != FR_OK || (fileinfo.fattrib & AM_DIR)) {
        cfn = "/xt/config.286";
        if (f_stat(cfn, &fileinfo) != FR_OK || (fileinfo.fattrib & AM_DIR)) {
            return 0;
        } else {
            file_size = (size_t)fileinfo.fsize & 0xFFFFFFFF;
        }
    } else {
        file_size = (size_t)fileinfo.fsize & 0xFFFFFFFF;
    }

    FIL f;
    if(f_open(&f, cfn, FA_READ) != FR_OK) {
        return 0;
    }
    char* buff = (char*)calloc(file_size + 1, 1);
    if (f_read(&f, buff, file_size, pbr) != FR_OK) {
        printf("Failed to read config.286\n");
        free(buff);
        buff = 0;
    }
    f_close(&f);
    return buff;
}

inline static void tokenizeCfg(char* s, size_t sz) {
    size_t i = 0;
    for (; i < sz; ++i) {
        if (s[i] == '=' || s[i] == '\n' || s[i] == '\r') {
            s[i] = 0;
        }
    }
    s[i] = 0;
}

static char* next_token(char* t) {
    char *t1 = t + strlen(t);
    while(!*t1++);
    return t1 - 1;
}

static int new_cpu_mhz = CPU_FREQ_MHZ;
static int vreg = VREG_VOLTAGE_1_60;
static int new_vreg = VREG_VOLTAGE_1_60;

static void load_config_286() {
    UINT br;
    char* buff = open_config(&br);
    if (buff) {
        tokenizeCfg(buff, br);
        char *t = buff;
        while (t - buff < br) {
            if (strcmp(t, "CPU") == 0) {
                t = next_token(t);
                new_cpu_mhz = atoi(t);
                if (clock_get_hz(clk_sys) != new_cpu_mhz * MHZ) {
                    if (set_sys_clock_hz(new_cpu_mhz * MHZ, 0) ) {
                        cpu_mhz = new_cpu_mhz;
                    }
                }
            } else if (strcmp(t, "VREG") == 0) {
                t = next_token(t);
                new_vreg = atoi(t);
                if (new_vreg != vreg && new_vreg >= VREG_VOLTAGE_0_55 && new_vreg <= VREG_VOLTAGE_3_30) {
                    vreg = new_vreg;
                    vreg_set_voltage(vreg);
                }
            } else if (!new_flash_timings && strcmp(t, "FLASH") == 0) {
                t = next_token(t);
                int new_flash_mhz = atoi(t);
                if (flash_mhz != new_flash_mhz) {
                    flash_mhz = new_flash_mhz;
                    flash_timings();
                }
            } else if (strcmp(t, "FLASH_T") == 0) {
                t = next_token(t);
                char *endptr;
                new_flash_timings = (uint)strtol(t, &endptr, 16);
                if (*endptr == 0 && qmi_hw->m[0].timing != new_flash_timings) {
                    flash_timings();
                }
            } else if (!new_psram_timings && strcmp(t, "PSRAM") == 0) {
                t = next_token(t);
                int new_psram_mhz = atoi(t);
                if (psram_mhz != new_psram_mhz) {
                    psram_mhz = new_psram_mhz;
                    psram_timings();
                }
            } else if (strcmp(t, "PSRAM_T") == 0) {
                t = next_token(t);
                char *endptr;
                new_psram_timings = (uint)strtol(t, &endptr, 16);
                if (*endptr == 0 && qmi_hw->m[1].timing != new_psram_timings) {
                    psram_timings();
                }
            } else { // unknown token
                t = next_token(t);
            }
            t = next_token(t);
        }
        free(buff);
    }
}

int main(void) {
    // Platform-specific initialization
#if PICO_RP2350
    vreg_disable_voltage_limit();
    vreg_set_voltage(vreg);
    flash_timings();
    sleep_ms(100);
    if (!set_sys_clock_hz(cpu_mhz * MHZ, 0) ) {
        cpu_mhz = 378;
        set_sys_clock_hz(cpu_mhz * MHZ, 1); // fallback to failsafe clocks
    }
#else
    memcpy_wrapper_replace(NULL);
    hw_set_bits(&vreg_and_chip_reset_hw->vreg, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    set_sys_clock_hz(CPU_FREQ_MHZ * MHZ, true);
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

#if 0 // for future save settings in flash
    if (cmos[0] == 0) {
        printf("Empty CMOS\n");
    }
#endif
    // Mount SD card filesystem
    if (FR_OK != f_mount(&fs, "0", 1)) {
        sem_init(&vga_start_semaphore, 0, 1);
        multicore_launch_core1(second_core);
        sem_release(&vga_start_semaphore);
        printf("SD Card not inserted or SD Card error!");
        keyboard_init();
        while (1);
    }

    nespad_begin(NES_GPIO_CLK, NES_GPIO_DATA, NES_GPIO_LAT);
    sleep_ms(5);
    nespad_read();

    if (nespad_state & DPAD_SELECT) {
        // skip config
    } else {
        load_config_286();
    }

    // Initialize PSRAM
    rp2350a = (*((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1);
    int gp;
#ifdef MURM2
    gp = rp2350a ?  8 : 47;
#else
    gp = rp2350a ? 19 : 47;
#endif
    psram_init(gp);
    if (!butter_psram_size) {
        if (init_psram() ) {
            write86 = write86_mp;
            writew86 = writew86_mp;
            writedw86 = writedw86_mp;
            read86 = read86_mp;
            readw86 = readw86_mp;
            readdw86 = readdw86_mp;
        } else {
            init_swap();
            write86 = write86_sw;
            writew86 = writew86_sw;
            writedw86 = writedw86_sw;
            read86 = read86_sw;
            readw86 = readw86_sw;
            readdw86 = readdw86_sw;
        }
    } else {
        write86 = write86_ob;
        writew86 = writew86_ob;
        writedw86 = writedw86_ob;
        read86 = read86_ob;
        readw86 = readw86_ob;
        readdw86 = readdw86_ob;
    }

    // Initialize peripherals
    keyboard_init();

    // Check for mouse availability
#ifndef MURM2
    const uint8_t mouse_available = nespad_state;
    if (mouse_available)
#endif
        mouse_init();

    // Initialize semaphore and launch second core
    sem_init(&vga_start_semaphore, 0, 1);
    multicore_launch_core1(second_core);
    sem_release(&vga_start_semaphore);

    if (new_cpu_mhz != cpu_mhz) {
        printf("Failed to overclock to %d MHz\n", new_cpu_mhz);
    }
    printf("CPU: %d MHz\n", cpu_mhz);
    if (new_vreg < VREG_VOLTAGE_0_55 || new_vreg > VREG_VOLTAGE_3_30) {
        printf("Unexpected VREG value: %d\n", new_vreg);
    }
    printf("VREG: %d\n", vreg);
    if (new_flash_timings == qmi_hw->m[0].timing) {
        printf("FLASH [T%p]\n", new_flash_timings);
    } else {
        printf("FLASH max %d MHz [T%p]\n", flash_mhz, qmi_hw->m[0].timing);
    }
    if (butter_psram_size) {
        if (new_psram_timings == qmi_hw->m[1].timing) {
            printf("PSRAM [T%p]\n", new_psram_timings);
        } else {
            printf("PSRAM max %d MHz [T%p]\n", psram_mhz, qmi_hw->m[1].timing);
        }
        printf("On-Board-PSRAM mode (GP%d)\n", gp);
    } else if (write86 == write86_mp) {
        printf("Murmulator-Board-PSRAM mode\n");
    } else {
        printf("Swap-RAM mode (8 MB)\n");
    }

    // Initialize audio and reset emulator
    // adlib_init(SOUND_FREQUENCY);
    sn76489_reset();
    ///reset86();
    /// TODO: machine_init...
    wrcache_init();
    memory_init();
    cpu_reset();
    ports_init();

    // Initialize mouse control variables
    nespad_read();
    float mouse_throttle = 3.0f;
    bool left = nespad_state & DPAD_LEFT;
    bool right = nespad_state & DPAD_RIGHT;
    bool up = nespad_state & DPAD_UP;
    bool down = nespad_state & DPAD_DOWN;

    // Main emulation loop
    while (true) {
///        exec86(tormoz);
        cpu_exec(tormoz);
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
