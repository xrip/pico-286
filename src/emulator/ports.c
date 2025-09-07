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

    samples[1] = samples[0] += (int32_t)(speaker_sample() + other_sample + covox_sample + sn76489_sample() + midi_sample());
    cms_samples(samples);
#endif
}

struct ports_s {
	uint32_t start;
	uint32_t size;
	uint8_t(*readcb)(void* udata, uint16_t addr);
	uint16_t(*readcbW)(void* udata, uint16_t addr);
	uint32_t(*readcbL)(void* udata, uint16_t addr);
	void (*writecb)(void* udata, uint16_t addr, uint8_t value);
	void (*writecbW)(void* udata, uint16_t addr, uint16_t value);
	void (*writecbL)(void* udata, uint16_t addr, uint32_t value);
	void* udata;
	int used;
} ports[64];

int lastportmap = -1;

FUNC_INLINE int getportmap(uint32_t addr32) {
	int i;
	for (i = lastportmap; i >= 0; i--) {
		if (ports[i].used) {
			if ((addr32 >= ports[i].start) && (addr32 < (ports[i].start + ports[i].size))) {
				return i;
			}
		}
	}
	return -1;
}

void port_write(uint16_t portnum, uint8_t value) {
	int map;
#ifdef DEBUG_PORTS
	debug_log(DEBUG_DETAIL, "port_write @ %03X <- %02X\r\n", portnum, value);
#endif
	//portnum &= 0x0FFF;

	map = getportmap(portnum);

	/*if (portnum == 0x80) {
		debug_log(DEBUG_DETAIL, "Diagnostic port out: %02X\r\n", value);
		//if (value == 0xA) showops = 1;
	}
	else*/ if (portnum == 0x92) {
		a20_enabled = (value & 2) ? 1 : 0;
		return;
	}
	/*if (ports_cbWriteB[portnum] != NULL) {
		(*ports_cbWriteB[portnum])(ports_udata[portnum], portnum, value);
		return;
	}*/

	if (map != -1) {
		if (ports[map].writecb != NULL) {
			(*ports[map].writecb)(ports[map].udata, portnum, value);
			return;
		}
	}

}

void port_writew(uint16_t portnum, uint16_t value) {
	int map;
	//portnum &= 0x0FFF;
	map = getportmap(portnum);

	if (portnum == 0x80) {
		debug_log(DEBUG_DETAIL, "Diagnostic port out: %04X\r\n", value);
	}
	/*if (ports_cbWriteW[portnum] != NULL) {
		(*ports_cbWriteW[portnum])(ports_udata[portnum], portnum, value);
		return;
	}*/
	if (map != -1) {
		if (ports[map].writecbW != NULL) {
			(*ports[map].writecbW)(ports[map].udata, portnum, value);
			return;
		}
	}

	port_write(portnum, (uint8_t)value);
	port_write(portnum + 1, (uint8_t)(value >> 8));
}

void port_writel(uint16_t portnum, uint32_t value) {
	int map;
	//portnum &= 0x0FFF;
	map = getportmap(portnum);

	if (portnum == 0x80) {
		debug_log(DEBUG_DETAIL, "Diagnostic port out: %08X\r\n", value);
	}
	/*if (ports_cbWriteL[portnum] != NULL) {
		(*ports_cbWriteL[portnum])(ports_udata[portnum], portnum, value);
		return;
	}*/

	if (map != -1) {
		if (ports[map].writecbL != NULL) {
			(*ports[map].writecbL)(ports[map].udata, portnum, value);
			return;
		}
	}


#ifdef FAKE_PCI
	if (portnum == 0xCF8) {
		pci_write_0xcf8(value);
		return;
	}
#endif

	port_write(portnum, (uint8_t)value);
	port_write(portnum + 1, (uint8_t)(value >> 8));
	port_write(portnum + 2, (uint8_t)(value >> 16));
	port_write(portnum + 3, (uint8_t)(value >> 24));
}

uint8_t port_read(uint16_t portnum) {
	int map;
#ifdef DEBUG_PORTS
	if ((portnum != 0x3BA) && (portnum != 0x3DA)) debug_log(DEBUG_DETAIL, "port_read @ %03X\r\n", portnum);
#endif
	if (showops) {
		if ((portnum != 0x3BA) && (portnum != 0x3DA)) debug_log(DEBUG_DETAIL, "port_read @ %03X\r\n", portnum);
	}
	//portnum &= 0x0FFF;
	map = getportmap(portnum);

#ifdef FAKE_PCI
	if (portnum == 0x92) {
		return cpu->a20_gate ? 2 : 0;
	}
	if (portnum == 0xCFC) {
		return pci_read_0xcfc() & 0xFF;
	}
	if (portnum == 0xCFD) {
		return pci_read_0xcfc() >> 8;
	}
	if (portnum == 0xCFE) {
		return pci_read_0xcfc() >> 16;
	}
	if (portnum == 0xCFF) {
		return pci_read_0xcfc() >> 24;
	}
#endif

	/*if (ports_cbReadB[portnum] != NULL) {
		return (*ports_cbReadB[portnum])(ports_udata[portnum], portnum);
	}*/
	if (map != -1) {
		if (ports[map].readcb != NULL) {
			return (*ports[map].readcb)(ports[map].udata, portnum);
		}
	}

	return 0xFF;
}

uint16_t port_readw(uint16_t portnum) {
	int map;
	uint16_t ret;
	//portnum &= 0x0FFF;
	map = getportmap(portnum);

	/*if (ports_cbReadW[portnum] != NULL) {
		return (*ports_cbReadW[portnum])(ports_udata[portnum], portnum);
	}*/
	if (map != -1) {
		if (ports[map].readcbW != NULL) {
			return (*ports[map].readcbW)(ports[map].udata, portnum);
		}
	}

#ifdef FAKE_PCI
	if (portnum == 0xCFC) {
		return pci_read_0xcfc() & 0xFFFF;
	}
	if (portnum == 0xCFE) {
		return pci_read_0xcfc() >> 16;
	}
#endif

	ret = port_read(portnum);
	ret |= (uint16_t)port_read(portnum + 1) << 8;
	return ret;
}

uint32_t port_readl(uint16_t portnum) {
	int map;
	uint32_t ret;
	//portnum &= 0x0FFF;
	map = getportmap(portnum);

	/*if (ports_cbReadL[portnum] != NULL) {
		return (*ports_cbReadL[portnum])(ports_udata[portnum], portnum);
	}*/
	if (map != -1) {
		if (ports[map].readcbL != NULL) {
			return (*ports[map].readcbL)(ports[map].udata, portnum);
		}
	}


#ifdef FAKE_PCI
	if (portnum == 0xCF8) {
		return pci_read_0xcf8();
	}
	else if (portnum == 0xCFC) {
		return pci_read_0xcfc();
	}
#endif

	ret = port_read(portnum);
	ret |= (uint32_t)port_read(portnum + 1) << 8;
	ret |= (uint32_t)port_read(portnum + 2) << 16;
	ret |= (uint32_t)port_read(portnum + 3) << 24;
	return ret;
}

void ports_cbRegister(uint32_t start, uint32_t count, uint8_t (*readb)(void*, uint16_t), uint16_t (*readw)(void*, uint16_t), void (*writeb)(void*, uint16_t, uint8_t), void (*writew)(void*, uint16_t, uint16_t), void* udata) {
/*	uint32_t i;
	for (i = 0; i < count; i++) {
		if ((start + i) >= PORTS_COUNT) {
			break;
		}
		ports_cbReadB[start + i] = readb;
		ports_cbReadW[start + i] = readw;
		ports_cbWriteB[start + i] = writeb;
		ports_cbWriteW[start + i] = writew;
		ports_udata[start + i] = udata;
	}*/

	uint8_t i;
	uint32_t j;
	for (i = 0; i < 64; i++) {
		if (ports[i].used == 0) break;
	}
	if (i == 64) {
		debug_log(DEBUG_ERROR, "[PORTS] Out of port map structs!\n");
		while(1);
	}
	ports[i].readcb = readb;
	ports[i].writecb = writeb;
	ports[i].readcbW = readw;
	ports[i].writecbW = writew;
	ports[i].readcbL = NULL;
	ports[i].writecbL = NULL;
	ports[i].start = start;
	ports[i].size = count;
	ports[i].udata = udata;
	ports[i].used = 1;
	lastportmap = i;

	/*for (int j = 0; j < 64; j++) {
		if (!ports[j].used) continue;

		uint16_t start_a = ports[j].start;
		uint16_t end_a = start_a + ports[j].size - 1;
		uint16_t start_b = start;
		uint16_t end_b = start + count - 1;

		if ((start_b <= end_a) && (end_b >= start_a)) {
			printf("[PORTS WARNING] Port range %04X-%04X overlaps with existing %04X-%04X\n",
				start_b, end_b, start_a, end_a);
		}
	}*/

}

void ports_init() {
	uint32_t i;
	/*for (i = 0; i < PORTS_COUNT; i++) {
		ports_cbReadB[i] = NULL;
		ports_cbReadW[i] = NULL;
		ports_cbReadL[i] = NULL;
		ports_cbWriteB[i] = NULL;
		ports_cbWriteW[i] = NULL;
		ports_cbWriteL[i] = NULL;
		ports_udata[i] = NULL;
	}*/
	for (i = 0; i < 64; i++) {
		ports[i].readcb = NULL;
		ports[i].writecb = NULL;
		ports[i].readcbW = NULL;
		ports[i].writecbW = NULL;
		ports[i].readcbL = NULL;
		ports[i].writecbL = NULL;
		ports[i].used = 0;
	}

	//memcpy(pci_config_space, piix4_config, 64);

/*	pci_config_space[0] = 0x86;
	pci_config_space[1] = 0x80;
	pci_config_space[2] = 0x11;
	pci_config_space[3] = 0x71;
	pci_config_space[4] = 0x05;
	pci_config_space[8] = 0x81;
	pci_config_space[9] = 0x80;
	pci_config_space[0xA] = 0x01;
	pci_config_space[0xB] = 0x01;

	// BAR0 = 0x000001F1 (I/O)
	pci_config_space[0x10] = 0xF1;
	pci_config_space[0x11] = 0x01;
	pci_config_space[0x12] = 0x00;
	pci_config_space[0x13] = 0x00;

	// BAR1 = 0x000003F5
	pci_config_space[0x14] = 0xF5;
	pci_config_space[0x15] = 0x03;
	pci_config_space[0x16] = 0x00;
	pci_config_space[0x17] = 0x00;

	// BAR2 = 0x00000171
	pci_config_space[0x18] = 0x71;
	pci_config_space[0x19] = 0x01;
	pci_config_space[0x1A] = 0x00;
	pci_config_space[0x1B] = 0x00;

	// BAR3 = 0x00000375
	pci_config_space[0x1C] = 0x75;
	pci_config_space[0x1D] = 0x03;
	pci_config_space[0x1E] = 0x00;
	pci_config_space[0x1F] = 0x00;

	pci_config_space[0x3C] = 0x0E; // IRQ line = 14
	pci_config_space[0x3D] = 0x01; // IRQ pin = INTA#
	*/
}
