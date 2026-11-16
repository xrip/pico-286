#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "flash_config.h"
#include "flash_config_test_port.h"

static uint32_t clock_us;
static uint8_t sr1, sr2, last_written_sr2;
static unsigned writes, begin_count, end_count, busy_reads;
static bool write_enabled, reject_write, stuck_busy, quad_selected, reset_seen;
static jmp_buf reset_point;

uint32_t flash_io_begin(void) { ++begin_count; return 0x1234; }

void flash_io_transfer(const uint8_t *tx, uint8_t *rx, size_t count) {
    assert(begin_count == 1 && end_count == 0);
    memset(rx, 0, count);
    switch (tx[0]) {
        case 0x05:
            assert(count == 2);
            if ((sr1 & 1) && !stuck_busy && busy_reads && --busy_reads == 0) sr1 &= ~3u;
            rx[1] = sr1;
            break;
        case 0x35:
            assert(count == 2 && !(sr1 & 1));
            rx[1] = sr2;
            break;
        case 0x06:
            assert(count == 1 && !(sr1 & 1));
            if (write_enabled) sr1 |= 2;
            break;
        case 0x04:
            assert(count == 1 && !(sr1 & 1));
            sr1 &= ~2u;
            break;
        case 0x31:
            assert(count == 2 && (sr1 & 3) == 2);
            ++writes;
            last_written_sr2 = tx[1];
            if (!reject_write) sr2 = tx[1];
            sr1 |= 1;
            busy_reads = 3;
            break;
        default: assert(!"unexpected flash command");
    }
}

uint32_t flash_io_time_us(void) { clock_us += 1000; return clock_us; }
void flash_io_busy_timeout(void) { reset_seen = true; longjmp(reset_point, 1); }
void flash_io_end(uint32_t saved, bool quad) {
    assert(saved == 0x1234 && !(sr1 & 1));
    ++end_count;
    quad_selected = quad;
}

static void setup(uint8_t status1, uint8_t status2) {
    sr1 = status1; sr2 = status2;
    clock_us = writes = begin_count = end_count = busy_reads = 0;
    write_enabled = true;
    reject_write = stuck_busy = quad_selected = reset_seen = false;
    last_written_sr2 = 0;
}

int main(void) {
    for (unsigned status2 = 0; status2 < 256; ++status2) {
        if (status2 & 2u) continue;
        const unsigned wanted = status2 | 2u;
        setup(0x54, wanted);
        assert(flash_config_init() && writes == 0 && sr2 == wanted && sr1 == 0x54);
        assert(begin_count == 1 && end_count == 1 && quad_selected);

        setup(0x54, status2);
        assert(flash_config_init() && writes == 1 && sr2 == wanted && sr1 == 0x54);
        assert(last_written_sr2 == wanted && quad_selected);
        begin_count = end_count = 0;
        assert(flash_config_init() && writes == 1); // Repeat boot does not write again.
    }

    setup(0, 0);
    write_enabled = false;
    assert(!flash_config_init() && writes == 0 && end_count == 1 && !quad_selected);
    setup(0x44, 0x60);
    reject_write = true;
    assert(!flash_config_init() && writes == 1 && end_count == 1 && !quad_selected);
    assert(sr1 == 0x44 && sr2 == 0x60);

    setup(0, 0);
    stuck_busy = true;
    if (setjmp(reset_point) == 0) {
        (void)flash_config_init();
        assert(!"busy flash must not return into XIP");
    }
    assert(reset_seen && writes == 1 && end_count == 0 && clock_us >= 100000);

    setup(1, 0); // An initially busy chip must not receive WREN.
    stuck_busy = true;
    if (setjmp(reset_point) == 0) {
        (void)flash_config_init();
        assert(!"initial busy flash must not return into XIP");
    }
    assert(reset_seen && writes == 0 && end_count == 0);
    puts("Flash QE: automatic check, no-op, all preserved SR2 bits, failures, timeout and repeat boot passed");
}
