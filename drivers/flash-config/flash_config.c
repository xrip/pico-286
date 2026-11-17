#include "flash_config.h"

#include <stddef.h>
#include <stdint.h>

// Protocol based on fhoedemakers/flash_config and the named flash datasheets.
// Unlike flash_do_cmd(), one direct session spans WREN, WRSR2 and WIP polling.
// No boot2 replay can clear unrelated protection bits or read a busy flash.
#ifdef FLASH_CONFIG_TEST
#include "flash_config_test_port.h"
#define FLASH_RAM(name) name
#else
#include <pico.h>
#include <pico/bootrom.h>
#include <boot/picoboot_constants.h>
#include <hardware/structs/qmi.h>
#include <hardware/sync.h>
#include <hardware/timer.h>


#define FLASH_RAM(name) __no_inline_not_in_flash_func(name)

static uint32_t FLASH_RAM(flash_io_begin)(void) {
    const uint32_t saved = save_and_disable_interrupts();
    rom_connect_internal_flash_fn connect =
            (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn exit_xip =
            (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    connect();
    exit_xip();
    // At the normal 150 MHz boot clock this gives 5 MHz serial commands.
    qmi_hw->direct_csr = (30u << QMI_DIRECT_CSR_CLKDIV_LSB) | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {}
    return saved;
}

static void FLASH_RAM(flash_io_transfer)(const uint8_t *tx, uint8_t *rx, size_t count) {
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS0N_BITS);
    size_t sent = 0, received = 0;
    while (received < count) {
        const uint32_t flags = qmi_hw->direct_csr;
        if (sent < count && !(flags & QMI_DIRECT_CSR_TXFULL_BITS))
            qmi_hw->direct_tx = tx[sent++];
        if (!(flags & QMI_DIRECT_CSR_RXEMPTY_BITS))
            rx[received++] = (uint8_t)qmi_hw->direct_rx;
    }
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {}
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS0N_BITS);
}

static uint32_t FLASH_RAM(flash_io_time_us)(void) {
    return time_us_32();
}

static void __attribute__((noreturn)) FLASH_RAM(flash_io_busy_timeout)(void) {
    // The chip may still be programming its status register. No XIP restore,
    // printf, panic, or return through a flash-resident caller is safe here.
    rom_reboot_fn reboot = (rom_reboot_fn)rom_func_lookup_inline(ROM_FUNC_REBOOT);
    reboot(REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 1, 0, 0);
    for (;;) __asm volatile("nop");
}

static void FLASH_RAM(flash_io_end)(uint32_t saved, bool quad) {
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    // Keep the ROM's pad settings; timing is set from the boot profile later.
    rom_flash_flush_cache_fn flush =
            (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    rom_flash_select_xip_read_mode_fn select_mode =
            (rom_flash_select_xip_read_mode_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_SELECT_XIP_READ_MODE);
    flush();
    // This changes both CS windows. The caller must configure PSRAM later.
    select_mode(quad ? BOOTROM_XIP_MODE_EBH_QUAD : BOOTROM_XIP_MODE_03H_SERIAL, 12);
    restore_interrupts(saved);
}
#endif

enum {
    READ_STATUS1 = 0x05,
    READ_STATUS2 = 0x35,
    WRITE_ENABLE = 0x06,
    WRITE_DISABLE = 0x04,
    WRITE_STATUS2 = 0x31,
    STATUS_BUSY = 1,
    STATUS_WRITE_ENABLE = 2,
    STATUS_QUAD_ENABLE = 2,
    WRITE_TIMEOUT_US = 100000
};

static uint8_t FLASH_RAM(read_status)(uint8_t command) {
    uint8_t tx[2] = {command, 0};
    uint8_t rx[2];
    flash_io_transfer(tx, rx, sizeof(tx));
    return rx[1];
}

static void FLASH_RAM(send_command)(uint8_t command) {
    uint8_t rx;
    flash_io_transfer(&command, &rx, 1);
}

static void FLASH_RAM(wait_ready)(void) {
    const uint32_t start = flash_io_time_us();
    while (read_status(READ_STATUS1) & STATUS_BUSY) {
        if ((uint32_t)(flash_io_time_us() - start) >= WRITE_TIMEOUT_US)
            flash_io_busy_timeout();
    }
}

bool FLASH_RAM(flash_config_init)(void) {
    const uint32_t saved = flash_io_begin();
    wait_ready();
    const uint8_t sr1 = read_status(READ_STATUS1);
    const uint8_t sr2 = read_status(READ_STATUS2);
    if (sr2 & STATUS_QUAD_ENABLE) {
        flash_io_end(saved, true);
        return true;
    }
    const uint8_t wanted = sr2 | STATUS_QUAD_ENABLE;
    send_command(WRITE_ENABLE);
    if (!(read_status(READ_STATUS1) & STATUS_WRITE_ENABLE)) {
        flash_io_end(saved, false);
        return false;
    }
    uint8_t tx_write[2] = {WRITE_STATUS2, wanted};
    uint8_t rx_write[2];
    flash_io_transfer(tx_write, rx_write, sizeof(tx_write));
    wait_ready();
    send_command(WRITE_DISABLE);

    const bool verified = read_status(READ_STATUS2) == wanted &&
            (read_status(READ_STATUS1) & 0xfcu) == (sr1 & 0xfcu);
    flash_io_end(saved, verified);
    return verified;
}
