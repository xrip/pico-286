#pragma GCC optimize("Ofast")
#include <time.h>
#include "emulator.h"
#if PICO_ON_DEVICE
#include <74hc595.h>
#include <hardware/pwm.h>
extern int16_t keyboard_send(uint8_t data);
#include "nespad.h"
#endif

#include <emu8950.h>
OPL *emu8950_opl;
#include "audio/sn76489.c.inl"
#include "audio/cms.c.inl"
#include "audio/dss.c.inl"
#include "audio/mpu401.c.inl"
#include "audio/sound_blaster.c.inl"
#include "i8237.c.inl"

uint8_t crt_controller_idx, crt_controller[32];
uint8_t port60, port61, port64;
uint8_t cursor_start = 12, cursor_end = 13;
uint32_t vram_offset = 0x0;

int sound_chips_clock = 0;

static uint16_t adlibregmem[5], adlib_register = 0;
static uint8_t adlibstatus = 0;

static int8_t joystick_tick;
static INLINE void joystick_out() {
#if PICO_ON_DEVICE
    joystick_tick = -127;
#endif
}

static INLINE uint8_t joystick_in() {
    uint8_t data = 0xF0;
#if PICO_ON_DEVICE
    nespad_read();
    int8_t axis_x = nespad_state & DPAD_LEFT ? -127 : (nespad_state & DPAD_RIGHT) ? 127 : 0;
    int8_t axis_y = nespad_state & DPAD_UP ? -127 : (nespad_state & DPAD_DOWN) ? 127 : 0;
    joystick_tick++;

    if (joystick_tick < axis_x) data |= 1;
    if (joystick_tick < axis_y) data |= 2;
    if (nespad_state & DPAD_A) data ^= 0x10;
    if (nespad_state & DPAD_B) data ^= 0x20;
#endif
    return data;
}


static INLINE uint8_t rtc_read(uint16_t addr) {
    uint8_t ret = 0xFF;
    struct tm tdata;

    time((time_t*)&tdata);
    struct tm *t = localtime((const time_t*)&tdata);

    t->tm_year = 24;
    addr &= 0x1F;
    switch (addr) {
        case 1:
            ret = 0;
            break;
        case 2:
            ret = (uint8_t)t->tm_sec;
            break;
        case 3:
            ret = (uint8_t)t->tm_min;
            break;
        case 4:
            ret = (uint8_t)t->tm_hour;
            break;
        case 5:
            ret = (uint8_t)t->tm_wday;
            break;
        case 6:
            ret = (uint8_t)t->tm_mday;
            break;
        case 7:
            ret = (uint8_t)t->tm_mon + 1;
            break;
        case 9:
            ret = (uint8_t)t->tm_year % 100;
            break;
    }

    if (ret != 0xFF) {
        uint8_t rh, rl;
        rh = (ret / 10) % 10;
        rl = ret % 10;
        ret = (rh << 4) | rl;
    }

    return ret;
}

void portout(uint16_t portnum, uint16_t value) {
    switch (portnum) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
// i8237 DMA
            return i8237_writeport(portnum, value);
        case 0x20:
        case 0x21: // i8259 PIC
            return out8259(portnum, value);
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43: // i8253 PIT
            return out8253(portnum, value);
        case 0x61: // PC Speaker
            port61 = value;
            if ((value & 3) == 3) {
#if I2S_SOUND || HARDWARE_SOUND || !PICO_ON_DEVICE
                speakerenabled = 1;
#else
                pwm_set_gpio_level(PWM_BEEPER, 127);
#endif
            } else {
#if I2S_SOUND || HARDWARE_SOUND || !PICO_ON_DEVICE
                speakerenabled = 0;
#else
                pwm_set_gpio_level(PWM_BEEPER, 0);
#endif
            }

            break;
        case 0x64: // Keyboard Controller
#if PICO_ON_DEVICE
            keyboard_send(value);
#endif
            port64 = value;
            break;
// i8237 DMA
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x87:
// i8237 DMA Page Registers
            return i8237_writepage(portnum, value);

// A20 Gate
        case 0x92:
            a20_enabled = value & 1;
            printf("A20 W: %d\n", a20_enabled);
            return;
// Tandy 3-Voice Sound
        case 0x1E0:
        case 0x2C0:
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC4:
        case 0xC5:
        case 0xC6:
        case 0xC7:
#if HARDWARE_SOUND
        if (!sound_chips_clock) {
            clock_init(CLOCK_PIN, CLOCK_FREQUENCY);
            sound_chips_clock = 1;
        }
        return SN76489_write(value);
#else
        return sn76489_out(value);
#endif
// Joystick
        case 0x201:
            return joystick_out();
// Creative Music System / GameBlaster
        case 0x220:
        case 0x221:
        case 0x222:
        case 0x223:
#if HARDWARE_SOUND
if (sound_chips_clock) {
    clock_init(CLOCK_PIN, CLOCK_FREQUENCY * 2);
    sound_chips_clock = 0;
}
        switch (portnum & 3) {
            case 0:
                SAA1099_write(0, 0, value);
            break;
            case 1:
                SAA1099_write(1, 0, value);
            break;
            case 2:
                SAA1099_write(0, 1, value);
            break;
            case 3:
                SAA1099_write(1, 1, value);
            break;
        }
#else
        cms_out(portnum, value);
#endif
        case 0x224:
        case 0x225:
        case 0x226:
        case 0x227:
        case 0x228:
        case 0x229:
        case 0x22a:
        case 0x22b:
        case 0x22c:
        case 0x22d:
        case 0x22e:
        case 0x22f:
// Sound Blaster
#if !PICO_RP2040
            blaster_write(portnum, value);
#endif
        return;
        case 0x260:
        case 0x261:
        case 0x262:
        case 0x263:
// EMS
            return out_ems(portnum, value);

        case 0x278:
// Covox Speech Thing
            covox_sample = (int16_t) (value - 128 << 6);
            return;
        case 0x330:
        case 0x331:
// MPU-401
            return mpu401_write(portnum, value);
        case 0x378:
        case 0x37A:
// Disney Sound Source
            return dss_out(portnum, value);
// AdLib / OPL2
        case 0x388:
            adlib_register = value;
            break;
        case 0x389:
            if (adlib_register <= 4) {
                adlibregmem[adlib_register] = value;

                if (adlib_register == 4 && value & 0x80) {
                    adlibstatus = 0;
                    adlibregmem[4] = 0;
                }
            }
#if HARDWARE_SOUND
        if (!sound_chips_clock) {
            clock_init(CLOCK_PIN, CLOCK_FREQUENCY);
            sound_chips_clock = 1;
        }
            OPL2_write_byte(0, 0, adlib_register & 0xff);
            OPL2_write_byte(1, 0, value & 0xff);
        return;
#else
            return OPL_writeReg(emu8950_opl, adlib_register, value);
#endif
// EGA/VGA
        case 0x3C4:
        case 0x3CE:
        case 0x3C0:
        case 0x3C5:
        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
        case 0x3CF:
            return vga_portout(portnum, value);
// https://stanislavs.org/helppc/6845.html
// https://bitsavers.trailing-edge.com/components/motorola/_dataSheets/6845.pdf
// https://www.theoddys.com/acorn/the_6845_crtc/the_6845_crtc.html
// MC6845 CRTC
        case 0x3B0:
        case 0x3B2:
        case 0x3B4:
        case 0x3B6:
        case 0x3D0:
        case 0x3D2:
        case 0x3D4:
        case 0x3D6:
            crt_controller_idx = value & 31;
            break;
        case 0x3B1:
        case 0x3B3:
        case 0x3B5:
        case 0x3B7:
        case 0x3D1:
        case 0x3D3:
        case 0x3D5:
        case 0x3D7:
            switch (crt_controller_idx) {
                case 0x4: {
                    if (value == 0x3e/* && videomode == 1*/) {
                        videomode = 0x79;

                    }
                    break;
                }

                case 0x6:
//                    printf("!!! Y = %i\n", value);
                    // 160x100x16 or 160x200x16 mode TODO: Add more checks
                    if (value == 0x64 && (videomode <= 3)) {
                        videomode = cga_hires ? 0x76 : 0x77;
                    }

                    // 160x46x16 mode TODO: Add more checks
                    if (value == 0x2e/* && (videomode <= 3))*/) {
                        videomode = 0x87;
                    }
                    break;

                case 0x8:
                    break;
// Cursor pos
                case 0x0A:
                    cursor_start = value;
                    //cursor_visible = !(value & 0x20) && (cursor_start < 8);
                    break;
                case 0x0B:
                    cursor_end = value;
                    break;

// Screen offset
                case 0x0C: // Start address (MSB)
                    vram_offset = (value & 0xff) << 8;
                    // printf("vram offset %04X\n", vram_offset);
                    break;
                case 0x0D: // Start address (LSB)
                    vram_offset |= (value & 0xff);
                    // printf("vram offset %04X\n", vram_offset);
                    break;
            }

//            if ((crt_controller_idx != 0x03) && ((crt_controller_idx != 0x0E) && (crt_controller_idx != 0x0F) && (crt_controller_idx != 0x0c) && (crt_controller_idx != 0x0d)))
//                printf("CRT %x %x\n", crt_controller_idx, value);

            crt_controller[crt_controller_idx] = value;

            break;
        case 0x3B8:
        case 0x3BF:
        case 0x3D8:
        case 0x3D9:
// CGA
            return cga_portout(portnum, value);
        case 0x3DA:
        case 0x3DE:
        case 0x3DF:
// TGA
            return tga_portout(portnum, value);
        case 0x3F8:
        case 0x3F9:
        case 0x3FA:
        case 0x3FB:
        case 0x3FC:
        case 0x3FD:
        case 0x3FE:
        case 0x3FF:
// Serial Port (Mouse)
            return mouse_portout(portnum, value);
    }
}

uint16_t portin(uint16_t portnum) {
    switch (portnum) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
// i8237 DMA
            return i8237_readport(portnum);
        case 0x20:
        case 0x21: // i8259 PIC
            return in8259(portnum);
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43: // i8253 PIT
            return in8253(portnum);

// Keyboard
        case 0x60:
            return port60;
        case 0x61:
            return port61;
        case 0x64:
            return port64;
// i8237 DMA Page Registers
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x87:
            return i8237_readpage(portnum);
// A20 Gate
        case 0x92:
            printf("A20 R: %d\n", a20_enabled);
            return a20_enabled;
        case 0x201:
// Joystick
            return joystick_in();

        case 0x220:
        case 0x221:
        case 0x222:
        case 0x223:
        case 0x224:
        case 0x225:
        case 0x226:
        case 0x227:
        case 0x228:
        case 0x229:
        case 0x22a:
        case 0x22b:
        case 0x22c:
        case 0x22d:
        case 0x22e:
        case 0x22f:
// Sound Blaster / GameBlaster
#if !PICO_RP2040
            return blaster_read(portnum);
#else
            return cms_in(portnum);
#endif
// RTC
        case 0x240:
        case 0x241:
        case 0x242:
        case 0x243:
        case 0x244:
        case 0x245:
        case 0x246:
        case 0x247:
        case 0x248:
        case 0x249:
        case 0x24A:
        case 0x24B:
        case 0x24C:
        case 0x24D:
        case 0x24E:
        case 0x24F:
        case 0x250:
        case 0x251:
        case 0x252:
        case 0x253:
        case 0x254:
        case 0x255:
        case 0x256:
        case 0x257:
            return rtc_read(portnum);
        case 0x27A: // Covox Speech Thing
            return 0;
        case 0x330:
        case 0x331:
// MPU-401
            return mpu401_read(portnum);
        case 0x378:
        case 0x379:
// Disney Sound Source
            return dss_in(portnum);
        case 0x37A:
            return 0;
// AdLib
        case 0x388:
        case 0x389:
            if (!adlibregmem[4])
                adlibstatus = 0;
            else
                adlibstatus = 0x80;

            adlibstatus = adlibstatus + (adlibregmem[4] & 1) * 0x40 + (adlibregmem[4] & 2) * 0x10;
            return adlibstatus;
        case 0x3C1:
        case 0x3C2:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
// VGA
            return vga_portin(portnum);

        case 0x3D4:
// CRTC
            return crt_controller_idx;
        case 0x3D5:
// CRTC
            return crt_controller[crt_controller_idx];
        case 0x3DA:
// CGA
            return cga_portin(portnum);
        case 0x3F8:
        case 0x3F9:
        case 0x3FA:
        case 0x3FB:
        case 0x3FC:
        case 0x3FD:
        case 0x3FE:
        case 0x3FF:
// Serial Port (Mouse)
            return mouse_portin(portnum);
        default:
            return 0xFF;
    }
}


void portout16(uint16_t portnum, uint16_t value) {
    portout(portnum, (uint8_t) value);
    portout(portnum + 1, (uint8_t) (value >> 8));
}

uint16_t portin16(uint16_t portnum) {
    return portin(portnum) | portin(portnum + 1) << 8;
}


void get_sound_sample(const int16_t other_sample, int16_t *samples) {
#if HARDWARE_SOUND
    const int32_t sample = (speaker_sample() + other_sample + covox_sample + midi_sample());
    pwm_set_gpio_level(PCM_PIN, (uint16_t) ((int32_t) sample + 0x8000L) >> 4);
#else
    OPL_calc_buffer_linear(emu8950_opl, (int32_t *)samples, 1);

    samples[1] = samples[0] += (int32_t)(other_sample + covox_sample + sn76489_sample() + midi_sample());
    cms_samples(samples);
#endif

}