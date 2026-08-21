#pragma once
#if PICO_ON_DEVICE
#include "printf/printf.h"
#include <pico.h>
#else
#endif
#include <stdint.h>
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

#if PICO_ON_DEVICE
extern uint32_t butter_psram_size;
extern bool PSRAM_AVAILABLE;
#include "psram_spi.h"
#define VIDEORAM_SIZE (64 << 10)
#if PICO_RP2350

#define RAM_SIZE (640 << 10)

#else
//#define RAM_SIZE (146 << 10)

#ifdef TOTAL_VIRTUAL_MEMORY_KBS
#define RAM_SIZE (72 << 10)
#else
#define RAM_SIZE (112 << 10)
#endif

#endif
#else
#include "printf/printf.h"
#define VIDEORAM_SIZE (64 << 10)
#define RAM_SIZE (640 << 10)
#define butter_psram_size 1
#define PSRAM_AVAILABLE 1
#endif
#ifdef HARDWARE_SOUND
#define SOUND_FREQUENCY (44100)
#else
    #if PICO_RP2040
#define SOUND_FREQUENCY (22050)
#else
#define SOUND_FREQUENCY (44100)
#endif
#endif
#define rgb(r, g, b) (((r)<<16) | ((g) << 8 ) | (b) )

#define VIDEORAM_START (0xA0000)
#define VIDEORAM_END (0xC0000)

#define VBIOS_START (0xC0000)
#define VBIOS_END (0xC8000)

#define EMS_START (0xC0000)
#define EMS_END   (0xD0000)

#define UMB_START (0xD0000)
#define UMB_END (0xFC000)

#define HMA_START (0x100000)
#define HMA_END (0x110000-16)

#define BIOS_START (0xFE000)

#define EMS_MEMORY_SIZE (2048 << 10) // 2 MB
#define XMS_MEMORY_SIZE (4096 << 10) // 4 MB

#define BIOS_MEMORY_SIZE                0x413
#define BIOS_TRUE_MEMORY_SIZE           0x415
#define BIOS_CRTCPU_PAGE        0x48A
extern uint8_t log_debug;


extern uint8_t RAM[RAM_SIZE];
extern uint32_t VIDEORAM[VIDEORAM_SIZE];
extern uint8_t UMB[UMB_END - UMB_START];
extern uint8_t HMA[HMA_END - HMA_START];
// for non-butter-psram modes
#define SRAM_BLOCK_SIZE 0x2C000
extern uint8_t SRAM[SRAM_BLOCK_SIZE];
#define FIRST_RAM_PAGE (butter_psram_size ? RAM : SRAM)

extern uint32_t dwordregs[8];
#define byteregs ((uint8_t*)dwordregs)
#define wordregs ((uint16_t*)dwordregs)

typedef union {
    uint32_t value;
    struct {
        unsigned CF : 1;  // 0 bit of value
        unsigned _1 : 1;  // 1
        unsigned PF : 1;  // 2
        unsigned _3 : 1;  // 3
        unsigned AF : 1;  // 4
        unsigned _5 : 1;  // 5
        unsigned ZF : 1;  // 6
        unsigned SF : 1;  // 7
        unsigned TF : 1;  // 8
        unsigned IF : 1;  // 9
        unsigned DF : 1;  // 10
        unsigned OF : 1;  // 11
        unsigned _12 : 1;
        unsigned _13 : 1;
        unsigned _14 : 1;
        unsigned _15 : 1;
        unsigned _16 : 1;
        unsigned _17 : 1;
        unsigned AC : 1; // 18 (Alignment Check)	Проверка выравнивания (включается в CPL=3 при CR0.AM=1)
                         // (Alignment Check Exception) — INT 17 (11h)
        unsigned VIF : 1; // 19 (Virtual Interrupt Flag)	Виртуальный IF для виртуализации (введён в 486, но зарезервирован с 386)
        unsigned VIP : 1; // 20 (Virtual Interrupt Pending)	Виртуальное прерывание ожидает (аналогично — введён в 486)
        unsigned ID : 1; // 21 (ID Flag)	Позволяет проверить поддержку CPUID инструкцией
    } bits;
} x86_flags_t;

extern x86_flags_t x86_flags;
extern uint32_t segregs32[6];

// i8259
#include "i8259.h"
#define doirq(irqnum) i8259_interrupt((uint8_t)(irqnum))

// Video
extern int videomode;
#define CURSOR_X FIRST_RAM_PAGE[0x450]
#define CURSOR_Y FIRST_RAM_PAGE[0x451]
extern uint8_t cursor_start, cursor_end;
extern uint32_t vga_palette[256];

// TGA
extern uint32_t tga_palette[16];
extern uint8_t tga_palette_map[16];

extern void tga_portout(uint16_t portnum, uint16_t value);

extern void tga_draw_char(uint8_t ch, int x, int y, uint8_t color);

extern void tga_draw_pixel(int x, int y, uint8_t color);

// CGA
extern const uint32_t cga_palette[16];
extern const uint8_t cga_gfxpal[3][2][4];
extern uint32_t cga_composite_palette[3][16];
extern uint8_t cga_intensity, cga_colorset, cga_foreground_color, cga_blinking, cga_blinking_lock, cga_hires;

void cga_portout(uint16_t portnum, uint16_t value);

uint16_t cga_portin(uint16_t portnum);

// EGA/VGA
#define vga_plane_size (16000)
extern uint32_t vga_plane_offset;
extern uint8_t vga_planar_mode;

#if PICO_ON_DEVICE
    extern bool ega_vga_enabled;
#else
#define ega_vga_enabled (1)
#endif

void OpFpu(uint8_t opcode);

// Memory
typedef void (*write86_t)(uint32_t address, uint8_t value);
typedef void (*write86w_t)(uint32_t address, uint16_t value);
typedef void (*write86dw_t)(uint32_t address, uint32_t value);
typedef uint8_t (*read86_t)(uint32_t address);
typedef uint16_t (*read86w_t)(uint32_t address);
typedef uint32_t (*read86dw_t)(uint32_t address);
extern read86_t read86;
extern read86w_t readw86;
extern read86dw_t readdw86;
extern write86_t write86;
extern write86w_t writew86;
extern write86dw_t writedw86;
// on-board (butter) psram
void write86_ob(const uint32_t address, const uint8_t value);
void writew86_ob(uint32_t address, uint16_t value);
void writedw86_ob(uint32_t address, uint32_t value);
uint8_t read86_ob(uint32_t address);
uint16_t readw86_ob(uint32_t address);
uint32_t readdw86_ob(uint32_t address);
// murmulator-psram
void write86_mp(const uint32_t address, const uint8_t value);
void writew86_mp(uint32_t address, uint16_t value);
void writedw86_mp(uint32_t address, uint32_t value);
uint8_t read86_mp(uint32_t address);
uint16_t readw86_mp(uint32_t address);
uint32_t readdw86_mp(uint32_t address);
// swap
void write86_sw(const uint32_t address, const uint8_t value);
void writew86_sw(uint32_t address, uint16_t value);
void writedw86_sw(uint32_t address, uint32_t value);
uint8_t read86_sw(uint32_t address);
uint16_t readw86_sw(uint32_t address);
uint32_t readdw86_sw(uint32_t address);

// Ports
void vga_portout(uint16_t portnum, uint16_t value);
uint16_t vga_portin(uint16_t portnum);
extern void portout(uint16_t portnum, uint16_t value);
extern void portout16(uint16_t portnum, uint16_t value);
extern uint16_t portin(uint16_t portnum);
extern uint16_t portin16(uint16_t portnum);

extern uint8_t port60, port61, port64;
extern volatile uint8_t port3DA;
extern uint32_t vram_offset;
extern uint32_t tga_offset;

// CPU
extern void exec86(uint32_t execloops);

extern void reset86();

// i8253
#include "i8253.h"

// Mouse
extern void sermouseevent(uint8_t buttons, int8_t xrel, int8_t yrel);

extern uint8_t mouse_portin(uint16_t portnum);

extern void mouse_portout(uint16_t portnum, uint8_t value);

extern void tandy_write(uint16_t reg, uint8_t value);

extern void adlib_write_d(uint16_t reg, uint8_t value);

extern void cms_write(uint16_t reg, uint8_t value);

int16_t dss_sample();

extern void sn76489_reset();

// static int16_t sn76489_sample();

// extern void cms_samples(int16_t *output);

#define XMS_FN_CS 0x0000
#define XMS_FN_IP 0x03FF

extern uint8_t xms_handler();

#include "i8237.h"

void blaster_reset();

// uint8_t blaster_read(uint16_t portnum);
// void blaster_write(uint16_t portnum, uint8_t value);
int16_t blaster_sample();

void outadlib(uint16_t portnum, uint8_t value);

uint8_t inadlib(uint16_t portnum);

int16_t adlibgensample();

extern void out_ems(uint16_t port, uint8_t data);

extern int16_t covox_sample;

#if !PICO_ON_DEVICE
#define __fast_mul(x,y) (x*y)
#define __not_in_flash(x)
#define __time_critical_func(x)

#endif

#ifndef INLINE
#if defined(_MSC_VER)
#define likely(x)       (x)
#define unlikely(x)     (x)
#define INLINE __inline
#define ALIGN(x, y) __declspec(align(x)) y
#elif defined(__GNUC__)
#define INLINE inline
///__not_in_flash("was_inline")
#if PICO_ON_DEVICE
#define ALIGN(x, y) y __attribute__((aligned(x)))
#else
#define ALIGN(x, y) y __attribute__((aligned(x)))
#endif
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define INLINE inline
#define ALING(x, y) y __attribute__((aligned(x)))
#endif
#endif
static INLINE int16_t speaker_sample() {
    if (!speakerenabled) return 0;
    static uint32_t speakerfullstep, speakerhalfstep, speakercurstep = 0;
    int16_t speakervalue;
    speakerfullstep = SOUND_FREQUENCY / i8253_frequency(2);
    if (speakerfullstep < 2)
        speakerfullstep = 2;
    speakerhalfstep = speakerfullstep >> 1;
    if (speakercurstep < speakerhalfstep) {
        speakervalue = 4096;
    } else {
        speakervalue = -4096;
    }
    speakercurstep = (speakercurstep + 1) % speakerfullstep;
    return speakervalue;
}

extern void get_sound_sample(int16_t other_sample, int16_t *samples);
#ifdef __cplusplus
}
#endif


#ifdef TOTAL_VIRTUAL_MEMORY_KBS
#include "swap.h"
static INLINE void write8psram(uint32_t address, uint8_t value) {
    swap_write(address, value);
}
static INLINE void write16psram(uint32_t address, uint16_t value) {
    swap_write16(address, value);
}
static INLINE void write32psram(uint32_t address, uint32_t value) {
    swap_write32(address, value);
}
static INLINE uint8_t read8psram(uint32_t address) {
    return swap_read(address);
}
static INLINE uint16_t read16psram(uint32_t address) {
    return swap_read16(address);
}
static INLINE uint32_t read32psram(uint32_t address) {
    return swap_read32(address);
}
#endif

void vga_init(void);
void vga_mem_write(uint32_t address, uint8_t cpu_data);
void vga_mem_write16(uint32_t address, uint16_t cpu_data_x2);
uint8_t vga_mem_read(uint32_t address);
uint16_t vga_mem_read16(uint32_t address);
