#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/stdio_usb.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/exception.h>
#include <hardware/irq.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/pads_qspi.h>
#include <hardware/structs/timer.h>
#if TEST_PSRAM
#include <hardware/structs/xip_ctrl.h>
#include <hardware/sync.h>
#endif
#include "flash_config.h"
#include "fault_trace.h"
#if TEST_HSTX
#include <hardware/resets.h>
#include <hardware/structs/clocks.h>
#include "hstx_video.h"
#endif

#define LUT_VALUE(n) (((uint32_t)(n) * 0x9e3779b9u) ^ ((uint32_t)(n) * 0x45d9f3bu + 0xa53c91e7u))
#define LUT16(n) LUT_VALUE(n),LUT_VALUE(n+1),LUT_VALUE(n+2),LUT_VALUE(n+3),LUT_VALUE(n+4),LUT_VALUE(n+5),LUT_VALUE(n+6),LUT_VALUE(n+7),LUT_VALUE(n+8),LUT_VALUE(n+9),LUT_VALUE(n+10),LUT_VALUE(n+11),LUT_VALUE(n+12),LUT_VALUE(n+13),LUT_VALUE(n+14),LUT_VALUE(n+15)
#define LUT256(n) LUT16(n),LUT16(n+16),LUT16(n+32),LUT16(n+48),LUT16(n+64),LUT16(n+80),LUT16(n+96),LUT16(n+112),LUT16(n+128),LUT16(n+144),LUT16(n+160),LUT16(n+176),LUT16(n+192),LUT16(n+208),LUT16(n+224),LUT16(n+240)
#define LUT4096(n) LUT256(n),LUT256(n+256),LUT256(n+512),LUT256(n+768),LUT256(n+1024),LUT256(n+1280),LUT256(n+1536),LUT256(n+1792),LUT256(n+2048),LUT256(n+2304),LUT256(n+2560),LUT256(n+2816),LUT256(n+3072),LUT256(n+3328),LUT256(n+3584),LUT256(n+3840)
static const uint32_t flash_lookup[16384] = {LUT4096(0),LUT4096(4096),LUT4096(8192),LUT4096(12288)};
#if TEST_PSRAM
#define ram_lookup ((volatile uint32_t *)0x11000000u)
#elif TEST_CORE0_RAM
static uint32_t ram_lookup[16384];
#endif

enum { SEEDS = 8, WORDS = 64, ALARM = 2, IRQ_PERIOD_US = 32 };
_Static_assert(TEST_CPU_MHZ % TEST_FLASH_MHZ == 0, "Flash divider must be exact");
static uint32_t expected[2][SEEDS];
static volatile struct {
    uint32_t rounds, checksum, mismatches, seed_index;
} work[2];
static volatile uint32_t irq_count, irq_last_sp, core1_ready;
#if TEST_PSRAM
static uint32_t psram_verify_errors;
#endif

#if TEST_PSRAM
// Same QPI command/formats/timing calculation as pico-main.c, with GPIO8,
// the selected clock target, and no memory-size probe. Direct mode is IRQ-safe.
static void __no_inline_not_in_flash_func(test_psram_init)(void) {
    const uint32_t saved = save_and_disable_interrupts();
    gpio_set_function(8, GPIO_FUNC_XIP_CS1);
    qmi_hw->direct_csr = 10u << QMI_DIRECT_CSR_CLKDIV_LSB |
            QMI_DIRECT_CSR_EN_BITS | QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) tight_loop_contents();
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x35u;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) tight_loop_contents();

    // Compile-time arithmetic avoids calls into flash while QMI is direct.
    const unsigned clock_hz = TEST_CPU_MHZ * 1000000u;
    const unsigned target_hz = TEST_PSRAM_MHZ * 1000000u;
    const unsigned divisor = (clock_hz + target_hz - 1u) / target_hz;
    const unsigned rxdelay = divisor + (clock_hz / divisor > 100000000u);
    const unsigned clock_period_fs = 1000000000000000ull / clock_hz;
    const unsigned max_select = 125000000u / clock_period_fs;
    const unsigned min_deselect = (18000000u + clock_period_fs - 1u) /
            clock_period_fs - (divisor + 1u) / 2u;
    qmi_hw->m[1].timing = 1u << QMI_M1_TIMING_COOLDOWN_LSB |
            QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
            max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
            min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
            rxdelay << QMI_M1_TIMING_RXDELAY_LSB | divisor << QMI_M1_TIMING_CLKDIV_LSB;
    qmi_hw->m[1].rfmt = QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
            QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
            QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
            QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
            QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
            QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
            6u << QMI_M0_RFMT_DUMMY_LEN_LSB;
    qmi_hw->m[1].rcmd = 0xebu;
    qmi_hw->m[1].wfmt = QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
            QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
            QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
            QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
            QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
            QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;
    qmi_hw->m[1].wcmd = 0x38u;
    qmi_hw->direct_csr = 0;
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
    restore_interrupts(saved);
}
#endif

static inline __attribute__((always_inline)) uint32_t work_body(
        uint32_t seed, const volatile uint32_t *lookup) {
    _Alignas(8) uint32_t a[WORDS], b[WORDS];
    unsigned char *bytes = (unsigned char *)a;
    // Explicit memset/copy loops stay in this function; no ROM or SRAM libc
    // call changes the instruction-fetch path being tested.
    for (unsigned i = 0; i < sizeof a; ++i) bytes[i] = (uint8_t)seed;
    for (unsigned i = 0; i < WORDS; ++i) b[i] = a[i];
    uint32_t random = seed;
    for (unsigned pass = 0; pass < 8; ++pass) {
        for (unsigned i = 0; i < WORDS; ++i) {
            random = random * 1664525u + 1013904223u;
            const uint32_t value = b[i] ^ lookup[(random >> 8) & 16383u];
            a[i] = ((value << 7) | (value >> 25)) + random;
        }
        for (unsigned i = 0; i < WORDS; ++i) b[i] = a[i];
    }
    uint32_t sum = seed;
    for (unsigned i = 0; i < WORDS; ++i) sum = ((sum << 5) | (sum >> 27)) ^ b[i];
    return sum;
}

#if TEST_CORE0_RAM
#define CORE0_SECTION ".time_critical.flash_round"
#else
#define CORE0_SECTION ".text.flash_round"
#endif
uint32_t __attribute__((noipa, section(CORE0_SECTION))) flash_round(uint32_t seed) {
#if TEST_CORE0_RAM
    return work_body(seed, ram_lookup);
#else
    return work_body(seed, flash_lookup);
#endif
}

#if TEST_PROBE_RAM
#define PROBE_SECTION ".time_critical.probe_round"
#else
#define PROBE_SECTION ".text.probe_round"
#endif
uint32_t __attribute__((noipa, section(PROBE_SECTION))) probe_round(uint32_t seed) {
    return work_body(seed, flash_lookup);
}

static uint32_t seed_for(unsigned core, unsigned index) {
    return 0x12345678u ^ (core * 0x62d37a91u) ^ (index * 0x8712f3a5u);
}

#if !TEST_HSTX
static void __no_inline_not_in_flash_func(timer_irq_handler)(void) {
    timer0_hw->intr = 1u << ALARM;
    ++irq_count;
    __asm volatile("mov %0, sp" : "=r"(irq_last_sp));
    timer0_hw->alarm[ALARM] = timer0_hw->timerawl + IRQ_PERIOD_US;
}
#endif

static void core1_main(void) {
#if TEST_HSTX
    // No callback: the real driver sends its complete black active packet.
    core1_ready = hstx_video_init() && hstx_video_start() ? 1u : 2u;
#else
    irq_set_exclusive_handler(TIMER0_IRQ_2, timer_irq_handler);
    irq_set_priority(TIMER0_IRQ_2, 0);
    timer0_hw->intr = 1u << ALARM;
    hw_set_bits(&timer0_hw->inte, 1u << ALARM);
    irq_set_enabled(TIMER0_IRQ_2, true);
    timer0_hw->alarm[ALARM] = timer0_hw->timerawl + IRQ_PERIOD_US;
    core1_ready = 1;
#endif
    for (;;) {
        const unsigned index = work[1].rounds & (SEEDS - 1u);
        const uint32_t sum = probe_round(seed_for(1, index));
        if (sum != expected[1][index]) ++work[1].mismatches;
        work[1].checksum = sum;
        work[1].seed_index = index;
        ++work[1].rounds;
    }
}

static void print_status(void) {
    printf("irq=%lu ready=%lu sp=%08lx c0=%lu/%08lx/%lu c1=%lu/%08lx/%lu fault=%lu\n",
        (unsigned long)irq_count, (unsigned long)core1_ready, (unsigned long)irq_last_sp,
        (unsigned long)work[0].rounds, (unsigned long)work[0].checksum, (unsigned long)work[0].mismatches,
        (unsigned long)work[1].rounds, (unsigned long)work[1].checksum, (unsigned long)work[1].mismatches,
        (unsigned long)fault_trace[1].core_seen);
#if TEST_HSTX
    printf("hstx frames=%lu restarts=%lu run=%u phase=%lu\n",
        (unsigned long)hstx_video_frame_count, (unsigned long)hstx_video_restart_count,
        hstx_video_is_running(), (unsigned long)hstx_video_recovery_phase);
#endif
    if (fault_trace[1].core_seen) {
        const volatile fault_trace_record_t *f = &fault_trace[1];
        printf("fault pc=%08lx lr=%08lx sp=%08lx xpsr=%08lx exc=%08lx\n",
            (unsigned long)f->pc, (unsigned long)f->lr, (unsigned long)f->sp,
            (unsigned long)f->xpsr, (unsigned long)f->exc_return);
        printf("fault cfsr=%08lx hfsr=%08lx masks=%lu/%lu/%lu limit=%08lx vtor=%08lx\n",
            (unsigned long)f->cfsr, (unsigned long)f->hfsr, (unsigned long)f->primask,
            (unsigned long)f->basepri, (unsigned long)f->faultmask,
            (unsigned long)f->original_msplim, (unsigned long)f->vtor);
    }
}

int main(void) {
    // PSRAM shares the flash bus even when this test does not access it.
    gpio_put(8, true);
    gpio_set_dir(8, GPIO_OUT);
    gpio_set_function(8, GPIO_FUNC_SIO);
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, fault_trace_handler);
#if !TEST_HSTX
    hardware_alarm_claim(ALARM);
#endif
    stdio_init_all();
    if (!flash_config_init()) {
        for (;;) {
            puts("flash QE setup failed; staying at boot clock");
            sleep_ms(1000);
            if (getchar_timeout_us(0) == 'b') reset_usb_boot(0, 0);
        }
    }
    // Establish reference results at the ROM boot clock before any overclock.
#if TEST_CORE0_RAM && !TEST_PSRAM
    for (unsigned i = 0; i < 16384; ++i) ram_lookup[i] = flash_lookup[i];
#endif
    for (unsigned core = 0; core < 2; ++core) {
        for (unsigned i = 0; i < SEEDS; ++i)
#if TEST_PSRAM
            expected[core][i] = probe_round(seed_for(core, i));
#else
            expected[core][i] = flash_round(seed_for(core, i));
#endif
    }
    const unsigned divider = TEST_CPU_MHZ / TEST_FLASH_MHZ;
    const unsigned rx_delay = TEST_CPU_MHZ == 504 ? 6u : 3u;
    qmi_hw->m[0].timing = 0x60007000u |
            rx_delay << QMI_M0_TIMING_RXDELAY_LSB | divider << QMI_M0_TIMING_CLKDIV_LSB;
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);
    sleep_ms(10);
#if TEST_HSTX
    reset_block(RESETS_RESET_HSTX_BITS);
    clock_stop(clk_hstx);
    while (clocks_hw->clk[clk_hstx].ctrl & CLOCKS_CLK_HSTX_CTRL_ENABLED_BITS)
        tight_loop_contents();
#endif
    set_sys_clock_khz(TEST_CPU_MHZ * 1000u, true);
#if TEST_HSTX
    clock_configure_int_divider(clk_hstx, 0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        TEST_CPU_MHZ * 1000000u, TEST_CPU_MHZ / 126u);
    while (!(clocks_hw->clk[clk_hstx].ctrl & CLOCKS_CLK_HSTX_CTRL_ENABLED_BITS))
        tight_loop_contents();
    unreset_block_wait(RESETS_RESET_HSTX_BITS);
#endif
#if TEST_PSRAM
    test_psram_init();
    for (unsigned i = 0; i < 16384; ++i) ram_lookup[i] = flash_lookup[i];
    __dmb();
    for (unsigned i = 0; i < 16384; ++i)
        if (ram_lookup[i] != flash_lookup[i]) ++psram_verify_errors;
#endif
    multicore_launch_core1(core1_main);

    uint64_t next_status = 0;
    bool introduced = false;
    for (;;) {
        const unsigned index = work[0].rounds & (SEEDS - 1u);
        const uint32_t sum = flash_round(seed_for(0, index));
        if (sum != expected[0][index]) ++work[0].mismatches;
        work[0].checksum = sum;
        work[0].seed_index = index;
        ++work[0].rounds;
        if (!introduced && stdio_usb_connected()) {
            printf("IRQ stress CPU=%u flash=%u probe=%s qmi=%08lx padclk=%08lx\n",
                TEST_CPU_MHZ, TEST_FLASH_MHZ, TEST_PROBE_RAM ? "RAM" : "flash",
                (unsigned long)qmi_hw->m[0].timing, (unsigned long)pads_qspi_hw->io[0]);
            printf("source=%s core0=%s\n", TEST_HSTX ? "HSTX" : "timer", TEST_CORE0_RAM ? "RAM" : "flash");
#if TEST_CORE0_RAM
            printf("core0 lookup=%p\n", (void *)ram_lookup);
#endif
#if TEST_PSRAM
            printf("PSRAM GPIO8 clock=%uMHz m1timing=%08lx verify_bad=%lu/16384\n",
                TEST_PSRAM_MHZ, (unsigned long)qmi_hw->m[1].timing, (unsigned long)psram_verify_errors);
#endif
            printf("code core0=%p core1=%p irq=%p table=%p; keys v=status b=BOOTSEL\n",
                (void *)flash_round, (void *)probe_round,
                (void *)irq_get_vtable_handler(TEST_HSTX ? DMA_IRQ_1 : TIMER0_IRQ_2), (void *)flash_lookup);
            introduced = true;
        }
        const int command = getchar_timeout_us(0);
        if (command == 'b') reset_usb_boot(0, 0);
        const uint64_t now = time_us_64();
        if (command == 'v' || now >= next_status) {
            print_status();
            next_status = now + 1000000u;
        }
    }
}
