#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <pico/stdio_usb.h>
#include <hardware/clocks.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/pads_qspi.h>
#include <hardware/structs/io_bank0.h>
#include <hardware/structs/pads_bank0.h>
#include <hardware/structs/sio.h>

#ifdef PICO_EMBED_XIP_SETUP
#define EMBED_VALUE PICO_EMBED_XIP_SETUP
#define EMBED_DEFINED 1
#else
#define EMBED_VALUE 0
#define EMBED_DEFINED 0
#endif

typedef struct {
    uint32_t sys, timing, rfmt, rcmd, wfmt, wcmd, pads[6];
    uint32_t gpio8_ctrl, gpio8_input, gpio8_pad;
} flash_state_t;

static inline __attribute__((always_inline)) void capture(flash_state_t *s) {
    s->timing = qmi_hw->m[0].timing;
    s->rfmt = qmi_hw->m[0].rfmt;
    s->rcmd = qmi_hw->m[0].rcmd;
    s->wfmt = qmi_hw->m[0].wfmt;
    s->wcmd = qmi_hw->m[0].wcmd;
    for (unsigned i = 0; i < 6; ++i) s->pads[i] = pads_qspi_hw->io[i];
    s->gpio8_ctrl = io_bank0_hw->io[8].ctrl;
    s->gpio8_input = (sio_hw->gpio_in >> 8) & 1u;
    s->gpio8_pad = pads_bank0_hw->io[8];
    s->sys = clock_get_hz(clk_sys);
}

static void print_state(const char *stage, const flash_state_t *s) {
    printf("%s sys=%lu timing=%08lx rfmt=%08lx rcmd=%08lx wfmt=%08lx wcmd=%08lx\n",
        stage, (unsigned long)s->sys, (unsigned long)s->timing, (unsigned long)s->rfmt,
        (unsigned long)s->rcmd, (unsigned long)s->wfmt, (unsigned long)s->wcmd);
    for (unsigned i = 0; i < 6; ++i)
        printf("%s pad%u=%08lx\n", stage, i, (unsigned long)s->pads[i]);
    printf("%s gpio8 func=%lu input=%lu pad=%08lx ctrl=%08lx\n", stage,
        (unsigned long)(s->gpio8_ctrl & IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS),
        (unsigned long)s->gpio8_input, (unsigned long)s->gpio8_pad, (unsigned long)s->gpio8_ctrl);
}

int main(void) {
    flash_state_t entry, after_stdio;
    capture(&entry); // Raw register reads are the first application action.
    stdio_init_all();
    capture(&after_stdio);
    bool sent = false;
    for (;;) {
        if (!sent && stdio_usb_connected()) {
            printf("profile=%s embed=%u macro_defined=%u; v=read b=BOOTSEL\n",
                TEST_EMBEDDED_W25 ? "forced-embedded-w25-NOT-default" : "sdk-default-ROM",
                EMBED_VALUE, EMBED_DEFINED);
            print_state("main-entry", &entry);
            print_state("after-stdio", &after_stdio);
            sent = true;
        }
        const int c = getchar_timeout_us(10000);
        if (c == 'v') {
            flash_state_t now;
            capture(&now);
            print_state("current", &now);
        }
        if (c == 'b') reset_usb_boot(0, 0);
    }
}
