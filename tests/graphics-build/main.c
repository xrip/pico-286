// A small stand-alone driver test, without PC audio, PSRAM or QMI setup.
// Its read-only VRAM lives in flash so RP2040 can test the full word-addressed
// PC layout without needing another 256 KiB of SRAM. It is not a speed test.
#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <graphics.h>
#include <graphics_modes.h>
#include <string.h>

#define MONO16 0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55
#define MONO_ROW MONO16,MONO16,MONO16,MONO16,MONO16
#define MONO8 MONO_ROW,MONO_ROW,MONO_ROW,MONO_ROW,MONO_ROW,MONO_ROW,MONO_ROW,MONO_ROW
#define MONO64 MONO8,MONO8,MONO8,MONO8,MONO8,MONO8,MONO8,MONO8
#define TEXT8 'A',0x1f,'B',0x2f,'C',0x3f,'D',0x4f,'E',0x5f,'F',0x6f,'G',0x7f,'H',0x8f
#define TEXT80 TEXT8,TEXT8,TEXT8,TEXT8,TEXT8,TEXT8,TEXT8,TEXT8,TEXT8,TEXT8

#if !PICO_RP2350
static const uint32_t vram[65536] = {
    MONO64, MONO64,
    [0x8000] = 'P',0x0f,'C',0x0f,' ',0x0f,'H',0x0f,'D',0x0f,'M',0x0f,'I',0x0f,
    [0x8000 + 160] = TEXT80,
    [0x8000 + 320] = TEXT80,
    [0x8000 + 480] = TEXT80
};
#else
static uint32_t vram[65536];
static enum graphics_mode_t visual_mode;
static bool visual_blink;
static const enum graphics_mode_t visual_modes[] = {
    TEXTMODE_80x25_COLOR, TEXTMODE_80x25_BW, VGA_640x480x16, EGA_640x350x16x4,
    EGA_640x200x16x4, VGA_640x480x2, TEXTMODE_40x25_COLOR,
    VGA_320x200x256
};

static void make_picture(enum graphics_mode_t mode) {
    visual_mode = mode;
    memset(vram, 0, sizeof vram);
    if (mode <= TEXTMODE_80x25_COLOR) {
        const unsigned cols = mode <= TEXTMODE_40x25_COLOR ? 40 : 80;
        for (unsigned y = 0; y < 25; ++y) {
            for (unsigned x = 0; x < cols; ++x) {
                const unsigned address = 0x8000 + (y * cols + x) * 2;
                vram[address] = 'A' + (x + y) % 26;
                vram[address + 1] = ((x / 5) & 15u) << 4 | (y & 15u);
            }
        }
        const char *title = cols == 80 ? "PC HDMI 80x25 - 640 PIXELS - CURSOR AND COLORS" : "PC HDMI 40x25 - COLORS";
        for (unsigned x = 0; title[x]; ++x) {
            vram[0x8000 + x * 2] = title[x];
            vram[0x8001 + x * 2] = 0x1f;
        }
        return;
    }
    if (mode == VGA_320x200x256) {
        for (unsigned y = 0; y < 200; ++y)
            for (unsigned x = 0; x < 320; ++x)
                vram[y * 320 + x] = ((x / 20) + (y / 13) * 16) & 255u;
        return;
    }
    const unsigned height = mode == EGA_640x350x16x4 ? 350 :
                            mode == EGA_640x200x16x4 ? 200 : 480;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < 640; ++x) {
            unsigned color = x / 40;
            if (x == 0 || x == 639 || y == 0 || y == height - 1) color = 15;
            else if ((y & 31u) == 16) color ^= 15;
            if (mode == VGA_640x480x2) color = (x ^ y) & 8 ? 15 : 0;
            for (unsigned plane = 0; plane < (mode == VGA_640x480x2 ? 1u : 4u); ++plane)
                if (color & (1u << plane))
                    vram[y * 80 + x / 8] |= 1u << (plane * 8 + 7 - (x & 7));
        }
    }
}
#endif
volatile uint8_t port3DA;

void __time_critical_func(graphics_pc_snapshot)(graphics_pc_state_t *state) {
    *state = (graphics_pc_state_t){.foreground = 15, .blink_mask = 255,
        .cursor_x = 5, .cursor_y = 2, .cursor_start = 12, .cursor_end = 13,
        .cursor_phase = true};
#if PICO_RP2350
    if (visual_mode == TEXTMODE_80x25_BW) {
        state->blink_mask = 0x7f;
        state->cursor_phase = visual_blink;
    }
#endif
}

uint32_t __time_critical_func(graphics_pc_ega_line)(unsigned line) {
    return line * (graphics_source_mode() == EGA_320x200x16x4 ? 40u : 80u);
}

uint32_t graphics_pc_palette_color(uint8_t index, uint32_t color, bool composite_output) {
    (void)index;
    (void)composite_output;
    return color;
}

int main(void) {
#if defined(HDMI_HSTX)
    graphics_prepare_clock();
    make_picture(TEXTMODE_80x25_COLOR);
#else
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_sys_clock_khz(378000, true);
#endif
    graphics_set_buffer((uint8_t *)vram, 320, 200);
    graphics_set_textbuffer((uint8_t *)(vram + 0x8000));
    graphics_set_mode(TEXTMODE_80x25_COLOR);
    graphics_init();
    for (unsigned index = 0; index < 256; ++index) {
        const uint32_t rgb = index < 16 ? GRAPHICS_CGA_RGB(index) :
                ((index & 0xe0u) << 16 | (index & 0x1cu) << 11 | (index & 3u) * 85u);
        graphics_set_palette((uint8_t)index, rgb);
    }
#if PICO_RP2350
    for (unsigned i = 0; i < sizeof visual_modes / sizeof visual_modes[0]; ++i) {
        graphics_set_buffer(NULL, 320, 200);
        make_picture(visual_modes[i]);
        graphics_set_mode(visual_modes[i]);
        graphics_set_buffer((uint8_t *)vram, 320, 200);
        for (unsigned tick = 0; tick < 40; ++tick) {
            visual_blink = !visual_blink;
            sleep_ms(250);
        }
    }
    // HSTX owns PLL_USB. Return through the boot ROM after one visual pass,
    // so another test can be loaded without a working application USB clock.
    reset_usb_boot(0, 0);
    for (;;) tight_loop_contents();
#else
    static const enum graphics_mode_t modes[] = {
        VGA_640x480x2, TEXTMODE_80x25_COLOR, VGA_320x200x256
    };
    for (;;) {
        for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
            graphics_set_mode(modes[i]);
            sleep_ms(5000);
        }
    }
#endif
}
