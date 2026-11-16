#include <stdio.h>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/stdio_usb.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>
#include <hardware/structs/pads_qspi.h>
#include "ff.h"

// Use the real RAM command session; the test also reads its status registers.
#include "../../drivers/flash-config/flash_config.c"

enum { TEST_MAGIC = 0x51455043 };

static void __no_inline_not_in_flash_func(read_chip)(uint32_t *id, uint8_t *sr1, uint8_t *sr2) {
    uint32_t irq = flash_io_begin();
    uint8_t tx[4] = {0x9f, 0, 0, 0}, rx[4];
    flash_io_transfer(tx, rx, 4);
    *id = (uint32_t)rx[1] << 16 | (uint32_t)rx[2] << 8 | rx[3];
    *sr1 = read_status(READ_STATUS1);
    *sr2 = read_status(READ_STATUS2);
    flash_io_end(irq, false);
}

static uint32_t __no_inline_not_in_flash_func(flash_checksum)(void) {
    const volatile uint32_t *flash = (const volatile uint32_t *)XIP_NOCACHE_NOALLOC_BASE;
    uint32_t sum = 0;
    for (unsigned i = 0; i < 16384; ++i) sum = ((sum << 5) | (sum >> 27)) ^ flash[i];
    return sum;
}

static void stage(unsigned number, const char *name) {
    watchdog_hw->scratch[1] = number;
    printf("STAGE %u: %s\n", number, name);
    stdio_flush();
    sleep_ms(50);
    watchdog_update();
}

int main(void) {
    bool recovered = watchdog_caused_reboot() && watchdog_hw->scratch[0] == TEST_MAGIC;
    unsigned failed_stage = watchdog_hw->scratch[1];
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(200);
    printf("BOOT sys=%lu Hz recovered=%u stage=%u\n", (unsigned long)clock_get_hz(clk_sys), recovered, failed_stage);
    if (!recovered) {
        watchdog_hw->scratch[0] = TEST_MAGIC;
        watchdog_enable(4000, true);
        stage(1, "read flash at boot clock");
        uint32_t id;
        uint8_t sr1, sr2;
        read_chip(&id, &sr1, &sr2);
        printf("JEDEC=%06lx SR1=%02x SR2=%02x\n", (unsigned long)id, sr1, sr2);
        stage(2, "automatic QE and quad XIP");
        bool ok = flash_config_init();
        printf("QE=%u QMI timing=%08lx rfmt=%08lx rcmd=%08lx padclk=%08lx\n", ok,
               (unsigned long)qmi_hw->m[0].timing, (unsigned long)qmi_hw->m[0].rfmt,
               (unsigned long)qmi_hw->m[0].rcmd, (unsigned long)pads_qspi_hw->io[0]);
        if (ok) {
            uint32_t before = flash_checksum();
            stage(3, "flash divider 6 and RX delay 6");
            qmi_hw->m[0].timing = 0x60007000u | 6u << QMI_M0_TIMING_RXDELAY_LSB | 6u << QMI_M0_TIMING_CLKDIV_LSB;
            stage(4, "VREG 1.60V and CPU 504MHz");
            vreg_disable_voltage_limit();
            vreg_set_voltage(VREG_VOLTAGE_1_60);
            sleep_ms(10);
            set_sys_clock_khz(504000, true);
            stage(5, "uncached flash read checks");
            unsigned bad = 0;
            for (unsigned i = 0; i < 100; ++i) {
                if (flash_checksum() != before) ++bad;
                watchdog_update();
            }
            printf("RESULT sys=%lu Hz checksum=%08lx bad=%u/100\n", (unsigned long)clock_get_hz(clk_sys), (unsigned long)before, bad);
            stage(6, "read saved clock settings without applying them");
            FATFS fs;
            FRESULT mounted = f_mount(&fs, "0", 1);
            printf("SD mount=%u\n", mounted);
            if (mounted == FR_OK) {
                FIL config;
                FRESULT opened = f_open(&config, "/config.286", FA_READ);
                if (opened != FR_OK) opened = f_open(&config, "/xt/config.286", FA_READ);
                printf("CONFIG open=%u\n", opened);
                if (opened == FR_OK) {
                    char line[256];
                    while (f_gets(line, sizeof line, &config)) {
                        if (strstr(line, "CPU") || strstr(line, "VREG") ||
                            strstr(line, "FLASH") || strstr(line, "PSRAM")) printf("CONFIG %s", line);
                        watchdog_update();
                    }
                    f_close(&config);
                }
            }
            puts("DONE");
        }
        watchdog_disable();
        watchdog_hw->scratch[0] = 0;
    }
    for (;;) {
        if (getchar_timeout_us(10000) == 'b') reset_usb_boot(0, 0);
    }
}
