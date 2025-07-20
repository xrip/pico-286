#include <pthread.h>
#include <unistd.h>
#include <cstring>
#include <signal.h>
#include <sys/time.h>
#include <cstdio>
#include "MiniFB.h"
#include "emulator/emulator.h"
#include "emulator/includes/font8x16.h"
#include "emulator/includes/font8x8.h"
#include "emu8950.h"

static uint32_t ALIGN(4, SCREEN[640 * 480]);
uint8_t ALIGN(4, DEBUG_VRAM[80 * 10]) = { 0 };

int cursor_blink_state = 0;
uint8_t log_debug = 0;

extern OPL *emu8950_opl;

#define AUDIO_BUFFER_LENGTH ((SOUND_FREQUENCY / 10))
static int16_t audio_buffer[AUDIO_BUFFER_LENGTH * 2] = {};
static int sample_index = 0;

extern "C" void adlib_getsample(int16_t *sndptr, intptr_t numsamples);

extern "C" void adlib_getsample(int16_t *sndptr, intptr_t numsamples) {
    for (intptr_t i = 0; i < numsamples; i++) {
        get_sound_sample(0, &sndptr[i * 2]);
        sndptr[i * 2 + 1] = sndptr[i * 2];
    }
}

extern "C" void _putchar(char character) {
    putchar(character);
    static int x = 0, y = 0;

    if (y == 10) {
        y = 9;
        memmove(DEBUG_VRAM, DEBUG_VRAM + 80, 80 * 9);
        memset(DEBUG_VRAM + 80 * 9, 0, 80);
    }
    uint8_t *vidramptr = DEBUG_VRAM + y * 80 + x;

    if ((unsigned)character >= 32) {
        if (character >= 96) character -= 32;
        *vidramptr = ((character - 32) & 63) | 0 << 6;
        if (x == 80) {
            x = 0;
            y++;
        } else
            x++;
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

static inline void renderer() {
    static uint8_t v = 0;
    if (v != videomode) {
        printf("videomode %x %x\n", videomode, v);
        v = videomode;
    }

    uint8_t *vidramptr = VIDEORAM + 0x8000 + ((vram_offset & 0xffff) << 1);
    uint8_t cols = 80;
    for (int y = 0; y < 480; y++) {
        if (y >= 399)
            port3DA = 8;
        else
            port3DA = 0;

        if (y & 1)
            port3DA |= 1;

        uint32_t *pixels = SCREEN + y * 640;

        switch (videomode) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x07: {
                uint16_t *vram = (uint16_t *) (vidramptr + ((y >> 4) * cols + 0) * 2);
                for (int x = 0; x < 640; x += 8) {
                    uint16_t ch_attr = vram[(x >> 3)];
                    uint8_t ch = ch_attr & 0xff;
                    uint8_t attr = ch_attr >> 8;
                    const uint8_t *fontline = &font_8x16[(ch * 16) + (y & 15)];
                    uint8_t fontbits = *fontline;
                    uint32_t fg = vga_palette[(attr & 15) + (videomode == 7 ? 0 : 16)];
                    uint32_t bg = vga_palette[((attr >> 4) & 7) + (videomode == 7 ? 0 : 16)];

                    if ((attr & 0x80) && cursor_blink_state)
                        fontbits = 0xff;

                    for (int b = 0; b < 8; b++) {
                        pixels[x + b] = (fontbits & (0x80 >> b)) ? fg : bg;
                    }
                }
                break;
            }

            case 0x04:
            case 0x05:
            case 0x06: {
                if (y < 200) {
                    for (int x = 0; x < 640; x += 2) {
                        uint8_t pixels2 = vidramptr[0x2000 * ((y & 1) ^ 1) + (y >> 1) * 80 + (x >> 3)];
                        uint8_t b1 = (pixels2 >> (6 - ((x >> 1) & 6))) & 3;
                        uint8_t b2 = (pixels2 >> (6 - (((x >> 1) + 1) & 6))) & 3;
                        pixels[x] = vga_palette[b1 + 16];
                        pixels[x + 1] = vga_palette[b2 + 16];
                    }
                } else {
                    memset(pixels, 0, 640 * 4);
                }
                break;
            }

            case 0x08:
            case 0x09:
            case 0x0A: {
                if (y < 200) {
                    for (int x = 0; x < 640; x += 4) {
                        uint8_t pixels4 = vidramptr[0x2000 * ((y & 1) ^ 1) + (y >> 1) * 80 + (x >> 2)];
                        for (int b = 0; b < 4; b++) {
                            uint8_t pixel = (pixels4 >> (6 - (b << 1))) & 3;
                            pixels[x + b] = vga_palette[pixel + 16];
                        }
                    }
                } else {
                    memset(pixels, 0, 640 * 4);
                }
                break;
            }

            case 0x0D: {
                if (y < 200) {
                    for (int x = 0; x < 320; x++) {
                        uint8_t pixel = vidramptr[y * 40 + (x >> 3)] >> (7 - (x & 7)) & 1;
                        pixels[x * 2] = pixels[x * 2 + 1] = vga_palette[pixel ? 31 : 16];
                    }
                } else {
                    memset(pixels, 0, 640 * 4);
                }
                break;
            }

            case 0x0E: {
                if (y < 200) {
                    for (int x = 0; x < 640; x += 8) {
                        for (int p = 0; p < 4; p++) {
                            uint8_t plane_data = vidramptr[p * 0x10000 + y * 80 + (x >> 3)];
                            for (int b = 0; b < 8; b++) {
                                if (plane_data & (0x80 >> b)) {
                                    pixels[x + b] = vga_palette[p + 16];
                                }
                            }
                        }
                    }
                } else {
                    memset(pixels, 0, 640 * 4);
                }
                break;
            }

            case 0x10: {
                if (y < 200) {
                    for (int x = 0; x < 320; x++) {
                        uint8_t pixel = 0;
                        for (int p = 0; p < 4; p++) {
                            uint8_t plane_data = vidramptr[p * 0x10000 + y * 40 + (x >> 3)];
                            if (plane_data & (0x80 >> (x & 7))) {
                                pixel |= (1 << p);
                            }
                        }
                        pixels[x * 2] = pixels[x * 2 + 1] = vga_palette[pixel + 16];
                    }
                } else {
                    memset(pixels, 0, 640 * 4);
                }
                break;
            }

            case 0x12: {
                if (y < 480) {
                    for (int x = 0; x < 640; x += 8) {
                        for (int p = 0; p < 4; p++) {
                            uint8_t plane_data = vidramptr[p * 0x10000 + y * 80 + (x >> 3)];
                            for (int b = 0; b < 8; b++) {
                                if (plane_data & (0x80 >> b)) {
                                    pixels[x + b] = vga_palette[p + 16];
                                }
                            }
                        }
                    }
                }
                break;
            }

            case 0x13: {
                if (y < 200) {
                    for (int x = 0; x < 320; x++) {
                        uint8_t pixel = vidramptr[y * 320 + x];
                        pixels[x * 2] = pixels[x * 2 + 1] = vga_palette[pixel];
                    }
                } else {
                    memset(pixels, 0, 640 * 4);
                }
                break;
            }

            default: {
                memset(pixels, 0, 640 * 4);
                break;
            }
        }
    }

    if (log_debug) {
        for (int y = 0; y < 10; y++) {
            uint32_t *pixels = SCREEN + y * 640;
            for (int x = 0; x < 80; x++) {
                uint8_t ch = DEBUG_VRAM[y * 80 + x];
                if (ch == 0) ch = 32;
                const uint8_t *fontline = &font_8x8[(ch * 8) + (y & 7)];
                uint8_t fontbits = *fontline;
                for (int b = 0; b < 8; b++) {
                    pixels[x * 8 + b] = (fontbits & (0x80 >> b)) ? 0xFFFFFF : 0x000000;
                }
            }
        }
    }
}

extern "C" void HandleInput(unsigned int keycode, int isKeyDown) {
    // Convert X11 keycode to PC scancode
    unsigned char scancode = 0;
    
    switch (keycode) {
        case 27: scancode = 0x01; break;  // Escape
        case 49: scancode = 0x02; break; // 1
        case 50: scancode = 0x03; break; // 2
        case 51: scancode = 0x04; break; // 3
        case 52: scancode = 0x05; break; // 4
        case 53: scancode = 0x06; break; // 5
        case 54: scancode = 0x07; break; // 6
        case 55: scancode = 0x08; break; // 7
        case 56: scancode = 0x09; break; // 8
        case 57: scancode = 0x0A; break; // 9
        case 48: scancode = 0x0B; break; // 0
        case 65: scancode = 0x1E; break; // A
        case 66: scancode = 0x30; break; // B
        case 67: scancode = 0x2E; break; // C
        case 68: scancode = 0x20; break; // D
        case 69: scancode = 0x12; break; // E
        case 70: scancode = 0x21; break; // F
        case 71: scancode = 0x22; break; // G
        case 72: scancode = 0x23; break; // H
        case 73: scancode = 0x17; break; // I
        case 74: scancode = 0x24; break; // J
        case 75: scancode = 0x25; break; // K
        case 76: scancode = 0x26; break; // L
        case 77: scancode = 0x32; break; // M
        case 78: scancode = 0x31; break; // N
        case 79: scancode = 0x18; break; // O
        case 80: scancode = 0x19; break; // P
        case 81: scancode = 0x10; break; // Q
        case 82: scancode = 0x13; break; // R
        case 83: scancode = 0x1F; break; // S
        case 84: scancode = 0x14; break; // T
        case 85: scancode = 0x16; break; // U
        case 86: scancode = 0x2F; break; // V
        case 87: scancode = 0x11; break; // W
        case 88: scancode = 0x2D; break; // X
        case 89: scancode = 0x15; break; // Y
        case 90: scancode = 0x2C; break; // Z
        case 32: scancode = 0x39; break; // Space
        case 13: scancode = 0x1C; break; // Enter
        case 8:  scancode = 0x0E; break; // Backspace
        case 9:  scancode = 0x0F; break; // Tab
        case 37: scancode = 0x4B; break; // Left
        case 38: scancode = 0x48; break; // Up
        case 39: scancode = 0x4D; break; // Right
        case 40: scancode = 0x50; break; // Down
        case 112: scancode = 0x3B; break; // F1
        case 113: scancode = 0x3C; break; // F2
        case 114: scancode = 0x3D; break; // F3
        case 115: scancode = 0x3E; break; // F4
        case 116: scancode = 0x3F; break; // F5
        case 117: scancode = 0x40; break; // F6
        case 118: scancode = 0x41; break; // F7
        case 119: scancode = 0x42; break; // F8
        case 120: scancode = 0x43; break; // F9
        case 121: scancode = 0x44; break; // F10
        case 122: scancode = 0x57; break; // F11
        case 123: scancode = 0x58; break; // F12
        case 16: scancode = 0x2A; break; // Shift
        case 17: scancode = 0x1D; break; // Ctrl
        case 18: scancode = 0x38; break; // Alt
        default: scancode = 0; break;
    }
    
    if (!isKeyDown && scancode != 0) {
        scancode |= 0x80;
    }
    
    port60 = scancode;
    port64 |= 2;
    doirq(1);
}

extern "C" void HandleMouse(int x, int y, int buttons) {
    static int prev_x = 0, prev_y = 0;
    sermouseevent(buttons, x - prev_x, y - prev_y);
    prev_y = y;
    prev_x = x;
}

extern "C" int HanldeMenu(int menu_id, int checked) {
    switch (menu_id) {
        case 1:
            return !checked;
        case 2:
            return !checked;
        default:
            return 0;
    }
}

static volatile int running = 1;

void signal_handler(int sig) {
    running = 0;
}

pthread_mutex_t update_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t update_cond = PTHREAD_COND_INITIALIZER;
volatile int update_ready = 0;

void *sound_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&update_mutex);
        while (!update_ready && running) {
            pthread_cond_wait(&update_cond, &update_mutex);
        }
        if (!running) {
            pthread_mutex_unlock(&update_mutex);
            break;
        }
        update_ready = 0;
        pthread_mutex_unlock(&update_mutex);
        
        // Process audio buffer here if needed
        usleep(1000);
    }
    return NULL;
}

void *ticks_thread(void *arg) {
    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    uint64_t elapsed_system_timer = 0;
    uint64_t elapsed_blink_tics = 0;
    uint64_t elapsed_frame_tics = 0;
    uint64_t last_dss_tick = 0;
    uint64_t last_sb_tick = 0;
    uint64_t last_sound_tick = 0;
    
    int16_t last_dss_sample = 0;
    int16_t last_sb_sample = 0;
    
    const uint64_t hostfreq = 1000000000; // nanoseconds
    
    while (running) {
        clock_gettime(CLOCK_MONOTONIC, &current);
        
        uint64_t elapsedTime = (current.tv_sec - start.tv_sec) * hostfreq + (current.tv_nsec - start.tv_nsec);
        
        // Timer interrupt (~18.2 Hz)
        if (elapsedTime - elapsed_system_timer >= hostfreq / timer_period) {
            doirq(0);
            elapsed_system_timer = elapsedTime;
        }
        
        // Disney Sound Source frequency ~7KHz
        if (elapsedTime - last_dss_tick >= hostfreq / 7000) {
            last_dss_sample = dss_sample();
            last_dss_tick = elapsedTime;
        }
        
        // Sound Blaster
        if (elapsedTime - last_sb_tick >= hostfreq / 22050) {
            last_sb_sample = blaster_sample();
            last_sb_tick = elapsedTime;
        }
        
        // Audio samples
        if (elapsedTime - last_sound_tick >= hostfreq / SOUND_FREQUENCY) {
            get_sound_sample(last_dss_sample + last_sb_sample, &audio_buffer[sample_index]);
            sample_index += 2;
            
            if (sample_index >= AUDIO_BUFFER_LENGTH) {
                pthread_mutex_lock(&update_mutex);
                update_ready = 1;
                pthread_cond_signal(&update_cond);
                pthread_mutex_unlock(&update_mutex);
                sample_index = 0;
            }
            
            last_sound_tick = elapsedTime;
        }
        
        // Cursor blink
        if (elapsedTime - elapsed_blink_tics >= 333333333) { // ~3Hz
            cursor_blink_state ^= 1;
            elapsed_blink_tics = elapsedTime;
        }
        
        // Frame rendering (60 FPS)
        if (elapsedTime - elapsed_frame_tics >= 16666666) { // ~60Hz
            renderer();
            elapsed_frame_tics = elapsedTime;
        }
        
        usleep(1000); // 1ms sleep
    }
    return NULL;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!mfb_open("Pico-286 Emulator", 640, 480, 1)) {
        printf("Failed to open window\n");
        return -1;
    }

    memset(SCREEN, 0, sizeof(SCREEN));
    emu8950_opl = OPL_new(3579552, SOUND_FREQUENCY);
    blaster_reset();
    sn76489_reset();
    reset86();
    pthread_t sound_tid, ticks_tid;
    pthread_create(&sound_tid, NULL, sound_thread, NULL);
    pthread_create(&ticks_tid, NULL, ticks_thread, NULL);
    
    while (running) {
        exec86(32768);
        if (mfb_update(SCREEN, 0) < 0) {
            running = 0;
            break;
        }
    }

    pthread_cancel(sound_tid);
    pthread_cancel(ticks_tid);
    pthread_join(sound_tid, NULL);
    pthread_join(ticks_tid, NULL);
    
    mfb_close();
    return 0;
}