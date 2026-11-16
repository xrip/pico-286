// gcc -std=c17 -Wall -Wextra -Werror drivers/vga/tests/timing_test.c -o timing_test
#include <assert.h>
#include <stdio.h>
#include "../vga_timing.h"

int main(void) {
    static const unsigned modes[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 0x1e, 8, 9, 0x0a,
        0x0d, 0x0e, 0x10, 0x11, 0x12, 0x13, 0xff, 0x74, 0x76
    };
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        const vga_timing_t timing = vga_timing_for_mode(modes[i], false);
        if (modes[i] == 0x10) {
            assert(timing.active_lines == 350);
            assert(timing.total_lines == 449);
            assert(timing.vsync_first_line == 387);
        } else {
            assert(timing.active_lines == 480);
            assert(timing.total_lines == 525);
            assert(timing.vsync_first_line == 490);
        }
        assert(timing.vsync_lines == 2);
        assert(timing.active_lines < timing.vsync_first_line);
        assert(timing.vsync_first_line + timing.vsync_lines <= timing.total_lines);
        const vga_timing_t rgbs = vga_timing_for_mode(modes[i], true);
        assert(rgbs.active_lines == 240 && rgbs.total_lines == 262);
        assert(rgbs.vsync_first_line == 243 && rgbs.vsync_lines == 3);
    }
    puts("VGA timing: all 21 PC modes and RGBs profiles passed");
}
