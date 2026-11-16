#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"
#include "graphics_pc_hdmi.h"
#include "pico.h"
#ifndef BENCH_HOST
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#endif
#if BENCH_USB
#include "pico/stdio_usb.h"
#include "pico/bootrom.h"
#endif
#if BENCH_UART
#include "hardware/uart.h"
#endif

enum { BENCH_CLOCK_KHZ = 315000, BENCH_SAMPLES = 128, SYSTICK_MASK = 0xffffff };
#ifndef BENCH_HSTX_SOURCE
#define BENCH_HSTX_SOURCE 0
#endif
#ifndef BENCH_TEXT_STRESS
#define BENCH_TEXT_STRESS 0
#endif
uint32_t bench_vram[65536] __attribute__((aligned(4)));
graphics_pc_state_t bench_state;
#if !BENCH_HSTX_SOURCE
uint8_t *text_buffer;
#endif
static uint16_t palette[256];
uint16_t bench_text_pairs[1024], bench_ntsc_palette[1024] __attribute__((aligned(4)));
static uint32_t text_quad_pixels[4096];
static uint32_t ntsc_even[256], ntsc_odd[256];
static uint16_t ntsc_luma[256];
static uint32_t old_line[320], new_line[320];
static volatile uint32_t checksum;

typedef struct {
    uint32_t backend, mode, width, source_rows, output_bytes;
    uint32_t mismatches, first_bad_row, first_bad_byte;
    uint32_t old_min, old_max, old_total;
    uint32_t new_min, new_max, new_total;
} bench_result_t;

// Visible through SWD. status: 0=not started, 1=running, 2=complete, 3=clock error.
volatile uint32_t bench_status, bench_clock_hz, bench_timer_overhead;
volatile uint32_t bench_mismatch_count, bench_result_count;
volatile bench_result_t bench_results[BENCH_HSTX_SOURCE ? 61 : 44];

enum { BACKEND_VGA, BACKEND_VGA_TEXT, BACKEND_HDMI, BACKEND_NTSC, BACKEND_NTSC_TEXT, BACKEND_HSTX };

static const enum graphics_mode_t modes[] = {
    CGA_320x200x4, CGA_320x200x4_BW, CGA_640x200x2,
    HERC_640x480x2, HERC_640x480x2_90,
    TGA_160x200x16, TGA_320x200x16, TGA_640x200x16,
    EGA_320x200x16x4, EGA_640x200x16x4, EGA_640x350x16x4,
    VGA_640x480x2, VGA_640x480x16, VGA_320x200x256,
    VGA_320x200x256x4, COMPOSITE_160x200x16_force, COMPOSITE_160x200x16
};

static const enum graphics_mode_t text_modes[] = {0, 1, 2, 3};
static const enum graphics_mode_t hdmi_modes[] = {4, 5, 8, 9, 0x0d, 0x13, 0xff, 0x74, 0x76};
static const enum graphics_mode_t ntsc_modes[] = {4, 5, 6, 8, 9, 0x0d, 0x13, 0xff, 0x74, 0x76};

void __time_critical_func(graphics_pc_snapshot)(graphics_pc_state_t *out) {
    *out = bench_state;
}
// EGA lines at the standard width, start address 0: as the old loops
uint32_t __time_critical_func(graphics_pc_ega_line)(unsigned line) {
    return line * (graphics_source_mode() == EGA_320x200x16x4 ? 40u : 80u);
}

static unsigned source_rows(enum graphics_mode_t mode) {
    switch (mode) {
        case 0: case 1: case 2: case 3: return 400;
        case HERC_640x480x2: case VGA_640x480x2: case VGA_640x480x16: return 480;
        case HERC_640x480x2_90: return 348;
        case EGA_640x350x16x4: return 350;
        default: return 200;
    }
}

static unsigned output_row(unsigned row, unsigned rows) {
    return rows == 200 ? row * 2u : row;
}

static void __attribute__((noinline)) __time_critical_func(new_vga)(
        enum graphics_mode_t mode, unsigned row, const uint16_t *colors, uint8_t *output) {
    (void)mode;
    const unsigned width = graphics_source_native_width();
    if (width == 320)
        graphics_source_render_line16(row, 320, 480, (uint16_t *)output, colors);
    else
        graphics_source_render_vga8(row, 480, output, colors);
}

static void __attribute__((noinline)) __time_critical_func(new_text)(
        enum graphics_mode_t mode, unsigned row, const uint16_t *colors, uint8_t *output) {
    (void)mode; (void)colors;
    graphics_source_render_vga_text(row, 480, (uint16_t *)output, bench_text_pairs, text_quad_pixels);
}

static void __attribute__((noinline)) __time_critical_func(new_hdmi)(
        enum graphics_mode_t mode, unsigned row, const uint16_t *colors, uint8_t *output) {
    (void)mode;
    if (graphics_pc_hdmi_render(graphics_source_mode(), bench_vram, row >> 1, output)) return;
    graphics_source_render_line8(row >> 1, 320, 240, output, colors);
}

static void __attribute__((noinline)) __time_critical_func(new_ntsc)(
        enum graphics_mode_t mode, unsigned row, const uint16_t *colors, uint8_t *output) {
    (void)colors;
    if (graphics_source_render_ntsc_fast(row >> 1, (uint16_t *)output, ntsc_even, ntsc_odd, ntsc_luma)) return;
    if (mode == CGA_640x200x2)
        graphics_source_render_line16(row >> 1, 640, 240, (uint16_t *)output, ntsc_luma);
    else
        graphics_source_render_ntsc32(row >> 1, 320, 240, (uint32_t *)output, ntsc_even, ntsc_odd);
}

static void __attribute__((noinline)) __time_critical_func(new_ntsc_text)(
        enum graphics_mode_t mode, unsigned row, const uint16_t *colors, uint8_t *output) {
    (void)colors;
    if (graphics_source_render_ntsc_fast(row >> 1, (uint16_t *)output, ntsc_even, ntsc_odd, ntsc_luma)) return;
    if (mode <= TEXTMODE_40x25_COLOR)
        graphics_source_render_ntsc32(row >> 1, 320, 240, (uint32_t *)output, ntsc_even, ntsc_odd);
    else
        graphics_source_render_line16(row >> 1, 640, 240, (uint16_t *)output, ntsc_luma);
}

#if BENCH_HSTX_SOURCE
static void __attribute__((noinline)) __time_critical_func(new_hstx)(
        enum graphics_mode_t mode, unsigned row, const uint16_t *colors, uint8_t *output) {
    (void)mode;
    const unsigned width = graphics_source_native_width();
    graphics_source_render_line16(row, width, 480, (uint16_t *)output, colors);
}
#endif

typedef void (*render_fn)(enum graphics_mode_t, unsigned, const uint16_t *, uint8_t *);
static const render_fn old_kernels[] = {bench_old_vga, bench_old_text, bench_old_hdmi, bench_old_ntsc, bench_old_ntsc_text,
#if BENCH_HSTX_SOURCE
    bench_old_vga
#endif
};
static const render_fn new_kernels[] = {new_vga, new_text, new_hdmi, new_ntsc, new_ntsc_text,
#if BENCH_HSTX_SOURCE
    new_hstx
#endif
};

#ifndef BENCH_HOST
static uint32_t __attribute__((noinline)) __time_critical_func(measure)(render_fn render,
        enum graphics_mode_t mode, unsigned row, uint32_t *output) {
    const uint32_t saved = save_and_disable_interrupts();
    __compiler_memory_barrier();
    const uint32_t start = systick_hw->cvr;
    render(mode, row, palette, (uint8_t *)output);
    __compiler_memory_barrier();
    const uint32_t end = systick_hw->cvr;
    restore_interrupts(saved);
    checksum ^= output[0]; // Keep all render calls observable with LTO.
    return (start - end) & SYSTICK_MASK;
}

static void __attribute__((noinline)) __time_critical_func(empty_render)(
        enum graphics_mode_t mode, unsigned row, const uint16_t *colors, uint8_t *output) {
    (void)mode; (void)row; (void)colors;
    __asm volatile("" : : "r"(output) : "memory");
}
#endif

static void fill_source(unsigned seed, unsigned backend) {
    uint32_t random = seed + 1u;
    for (unsigned i = 0; i < 65536; ++i) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        if (backend == BACKEND_HDMI) {
            // Old Mode X could emit control indices as pixels; compare its
            // valid color range without carrying that defect into new code.
            bench_vram[i] = ((random & 255u) % 252u) |
                    ((((random >> 8) & 255u) % 252u) << 8) |
                    ((((random >> 16) & 255u) % 252u) << 16) |
                    (((random >> 24) % 252u) << 24);
        } else bench_vram[i] = random;
    }
    for (unsigned i = 0; i < 256; ++i) {
        // Equal phases give byte-for-byte comparison to old 640-wide output,
        // which always took the low palette byte. New code alternates phases.
        const uint16_t sample = 0xc0u | ((i * 37u + seed * 11u) & 63u);
        palette[i] = sample | (sample << 8);
        if (backend == BACKEND_HDMI) {
            const uint16_t code = i < 252u ? i : 0u;
            palette[i] = code | (code << 8);
        }
        for (unsigned phase = 0; phase < 4; ++phase)
            bench_ntsc_palette[i * 4u + phase] = 2u + ((i * 3u + phase + seed) & 7u);
        ntsc_even[i] = bench_ntsc_palette[i * 4] | ((uint32_t)bench_ntsc_palette[i * 4 + 1] << 16);
        ntsc_odd[i] = bench_ntsc_palette[i * 4 + 2] | ((uint32_t)bench_ntsc_palette[i * 4 + 3] << 16);
        const uint8_t fg = (uint8_t)palette[i & 15u];
        const uint8_t bg = (uint8_t)palette[i >> 4];
        bench_text_pairs[i * 4] = bg | ((uint16_t)bg << 8);
        bench_text_pairs[i * 4 + 1] = fg | ((uint16_t)bg << 8);
        bench_text_pairs[i * 4 + 2] = bg | ((uint16_t)fg << 8);
        bench_text_pairs[i * 4 + 3] = fg | ((uint16_t)fg << 8);
    }
    // The old loops ignore the CRTC start address in Tandy modes.
    const enum graphics_mode_t mode = graphics_source_mode();
    const bool tandy = mode == TGA_160x200x16 || mode == TGA_320x200x16 || mode == TGA_640x200x16 ||
                       mode == COMPOSITE_160x200x16 || mode == COMPOSITE_160x200x16_force;
    bench_state.vram_offset = tandy ? 0 : seed * 0x117u;
    // This is also built once outside scanout by the real VGA backend.
    for (unsigned attr = 0; attr < 256; ++attr) {
        const uint32_t bg = bench_text_pairs[attr * 4u] & 255u;
        const uint32_t fg = bench_text_pairs[attr * 4u + 3u] & 255u;
        for (unsigned pattern = 0; pattern < 16; ++pattern) {
            uint32_t pixels = 0;
            for (unsigned bit = 0; bit < 4; ++bit)
                pixels |= (pattern & (1u << bit) ? fg : bg) << (bit * 8u);
            text_quad_pixels[attr * 16u + pattern] = pixels;
        }
    }
    bench_state.tga_offset = seed * 0x4000u;
    bench_state.foreground = seed * 7u + 1u;
    if (backend == BACKEND_HDMI) bench_state.foreground = 0; // Old TGA160 did not substitute it.
    bench_state.blink_mask = seed & 1u ? 0x7f : 0xff;
    bench_state.cursor_phase = seed & 1u;
    bench_state.cursor_start = 32; // Cursor disable; old/new shape fixes are tested elsewhere.
    bench_state.cursor_end = 0;
    ntsc_luma[0] = 2;
    ntsc_luma[bench_state.foreground] = 6;
    if (backend == BACKEND_NTSC_TEXT) {
        static const uint8_t brightness[16] = {2,3,5,6,4,4,6,7,4,5,6,7,5,6,8,9};
        for (unsigned i = 0; i < 16; ++i) ntsc_luma[i] = brightness[i];
        // Old text ignored the display page and 40-column blink/cursor.
        bench_state.vram_offset = 0;
        bench_state.blink_mask = 255;
        bench_state.cursor_phase = false;
    }
#if BENCH_TEXT_STRESS
    if (backend == BACKEND_VGA_TEXT) {
        bench_state.cursor_phase = true;
#if BENCH_TEXT_STRESS == 1
        bench_state.blink_mask = 0x7f;
#else
        bench_state.blink_mask = 0xff;
        // Full block is identical in old/new cursor models. The default
        // cursor shape changed intentionally and cannot be this oracle.
        bench_state.cursor_start = 0;
        bench_state.cursor_end = 15;
        bench_state.cursor_x = 20;
        bench_state.cursor_y = 4;
#endif
    }
#endif
}

static void compare_mode(bench_result_t *result, unsigned seed) {
    fill_source(seed, result->backend);
    for (unsigned row = 0; row < result->source_rows; ++row) {
        memset(old_line, 0xa5, sizeof(old_line));
        memset(new_line, 0x5a, sizeof(new_line));
        const unsigned y = output_row(row, result->source_rows);
        old_kernels[result->backend](result->mode, y, palette, (uint8_t *)old_line);
        new_kernels[result->backend](result->mode, y, palette, (uint8_t *)new_line);
        if (result->backend == BACKEND_HSTX && result->width == 640) {
            // Comparison only: expand old VGA8 to the test palette's repeated
            // 16-bit values. Timing still uses untouched old VGA8; its work is
            // not equal to HSTX16 and its ratio is not a performance verdict.
            for (unsigned x = 640; x--;) {
                const uint16_t sample = ((uint8_t *)old_line)[x];
                ((uint16_t *)old_line)[x] = sample | (sample << 8);
            }
        }
        for (unsigned byte = 0; byte < result->output_bytes; ++byte) {
            if (((uint8_t *)old_line)[byte] != ((uint8_t *)new_line)[byte]) {
                if (result->mismatches == 0) {
                    result->first_bad_row = y;
                    result->first_bad_byte = byte;
                }
                ++result->mismatches;
            }
        }
    }
}

#ifndef BENCH_HOST
static void __time_critical_func(measure_mode)(bench_result_t *result) {
    const render_fn old_kernel = old_kernels[result->backend];
    const render_fn new_kernel = new_kernels[result->backend];
    result->old_min = result->new_min = UINT_MAX;
    // Warm code and data, then alternate order to reduce systematic bias.
    old_kernel(result->mode, 0, palette, (uint8_t *)old_line);
    new_kernel(result->mode, 0, palette, (uint8_t *)new_line);
    for (unsigned sample = 0; sample < BENCH_SAMPLES; ++sample) {
        unsigned row = (sample * 37u) % result->source_rows;
#if BENCH_TEXT_STRESS == 2
        if (result->backend == BACKEND_VGA_TEXT)
            row = bench_state.cursor_y * 16u + (sample & 15u);
#endif
        const unsigned y = output_row(row, result->source_rows);
        uint32_t old_cycles, new_cycles;
        if (sample & 1u) {
            new_cycles = measure(new_kernel, result->mode, y, new_line);
            old_cycles = measure(old_kernel, result->mode, y, old_line);
        } else {
            old_cycles = measure(old_kernel, result->mode, y, old_line);
            new_cycles = measure(new_kernel, result->mode, y, new_line);
        }
        if (old_cycles < result->old_min) result->old_min = old_cycles;
        if (old_cycles > result->old_max) result->old_max = old_cycles;
        if (new_cycles < result->new_min) result->new_min = new_cycles;
        if (new_cycles > result->new_max) result->new_max = new_cycles;
        result->old_total += old_cycles;
        result->new_total += new_cycles;
    }
}
#endif

#if BENCH_UART || BENCH_USB
static void report_puts(const char *message) {
#if BENCH_UART
    uart_puts(uart0, message);
#endif
#if BENCH_USB
    stdio_put_string(message, (int)strlen(message), false, false);
#endif
}

static void report_results(void) {
    char message[240];
    snprintf(message, sizeof(message), "clock=%lu samples=%u overhead=%lu mismatches=%lu\r\n",
             (unsigned long)bench_clock_hz, BENCH_SAMPLES,
             (unsigned long)bench_timer_overhead, (unsigned long)bench_mismatch_count);
    report_puts(message);
#ifdef BENCH_APP_FLAGS
    report_puts("profile=app-compile-flags\r\n");
#endif
#if BENCH_TEXT_STRESS == 1
    report_puts("profile=vga-text-hidden-blink\r\n");
#elif BENCH_TEXT_STRESS == 2
    report_puts("profile=vga-text-full-block-cursor\r\n");
#endif
#if BENCH_HSTX_SOURCE
    report_puts("profile=hstx-source-locks backend5=HSTX16 old-reference=VGA row-budget=10000-cycles\r\n");
#endif
    report_puts("backend,mode,width,rows,mismatches,old_min,old_max,old_total,new_min,new_max,new_total\r\n");
    for (unsigned i = 0; i < bench_result_count; ++i) {
        const bench_result_t result = bench_results[i];
        snprintf(message, sizeof(message), "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
            (unsigned long)result.backend, (unsigned long)result.mode, (unsigned long)result.width,
            (unsigned long)result.source_rows, (unsigned long)result.mismatches,
            (unsigned long)result.old_min, (unsigned long)result.old_max,
            (unsigned long)result.old_total, (unsigned long)result.new_min,
            (unsigned long)result.new_max, (unsigned long)result.new_total);
        report_puts(message);
    }
}
#endif

int main(void) {
    bench_status = 1;
#ifndef BENCH_HOST
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_sys_clock_khz(BENCH_CLOCK_KHZ, true);
    bench_clock_hz = clock_get_hz(clk_sys);
    systick_hw->csr = 0;
    systick_hw->rvr = SYSTICK_MASK;
    systick_hw->cvr = 0;
    systick_hw->csr = 5; // Core clock, counter on, no SysTick interrupt.
    busy_wait_us_32(10);
    const uint32_t timer_start = systick_hw->cvr;
    busy_wait_us_32(10);
    const uint32_t timer_delta = (timer_start - systick_hw->cvr) & SYSTICK_MASK;
    if (bench_clock_hz != BENCH_CLOCK_KHZ * 1000u || timer_delta < 2000u || timer_delta > 10000u) {
        bench_status = 3;
        while (true) tight_loop_contents();
    }
#endif
#if BENCH_HSTX_SOURCE
    graphics_source_init();
    graphics_text_init();
#endif
    graphics_source_set_buffer((uint8_t *)bench_vram, 320, 200);
    graphics_source_set_offset(0, 0);
#ifndef BENCH_HOST
    bench_timer_overhead = measure(empty_render, modes[0], 0, old_line);
#endif
    const enum graphics_mode_t *mode_lists[] = {modes, text_modes, hdmi_modes, ntsc_modes, text_modes, modes};
    const unsigned counts[] = {17, 4, 9, 10, 4, 17};
    for (unsigned backend = 0; backend < (BENCH_HSTX_SOURCE ? 6u : 5u); ++backend) {
      for (unsigned i = 0; i < counts[backend]; ++i) {
        const enum graphics_mode_t mode = mode_lists[backend][i];
        graphics_source_set_mode(mode);
        bench_result_t result = {
            .backend = backend, .mode = mode, .width = graphics_source_native_width(),
            .source_rows = backend == BACKEND_NTSC_TEXT ? 200u : source_rows(mode),
            .output_bytes = backend == BACKEND_HDMI ? 320u : backend >= BACKEND_NTSC ? 1280u : 640u
        };
        if (backend == BACKEND_HSTX) result.output_bytes = result.width * 2u;
        if (backend == BACKEND_NTSC_TEXT && mode <= TEXTMODE_40x25_COLOR) result.width = 320;
        for (unsigned seed = 0; seed < 3; ++seed) compare_mode(&result, seed);
#ifndef BENCH_HOST
        measure_mode(&result);
#else
        printf("backend=%u mode=0x%02x width=%u rows=%u mismatches=%u first=(%u,%u)\n",
               result.backend, result.mode, result.width, result.source_rows, result.mismatches,
               result.first_bad_row, result.first_bad_byte);
#endif
        bench_results[bench_result_count] = result;
        bench_mismatch_count += result.mismatches;
        ++bench_result_count;
      }
    }
    bench_status = 2;
#ifdef BENCH_HOST
    printf("All %u graphics modes: %u differing bytes\n",
           (unsigned)bench_result_count, (unsigned)bench_mismatch_count);
    return bench_mismatch_count ? 1 : 0;
#else
#if BENCH_UART
    uart_init(uart0, 115200);
    gpio_set_function(BENCH_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(BENCH_UART_RX_PIN, GPIO_FUNC_UART);
#endif
#if BENCH_USB
    stdio_usb_init(); // All timings are complete before USB starts.
#endif
    while (true) {
#if BENCH_UART || BENCH_USB
        report_results();
#endif
        // The host may send 'b' to enter BOOTSEL for the next test build.
        // Keep board flashing outside this program and outside worker tasks.
        for (unsigned wait = 0; wait < 200; ++wait) {
#if BENCH_USB
            if (getchar_timeout_us(0) == 'b') reset_usb_boot(0, 0);
#endif
            sleep_ms(10);
        }
    }
#endif
}
