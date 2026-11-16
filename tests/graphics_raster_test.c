#include <assert.h>
#include <stdio.h>
#include "graphics_pc_raster.h"

// Reference ordering from the old dma_handler_VGA: blank, active, text/debug,
// odd-line reuse, rendered graphics. Mode 12 now renders all 480 lines.
static uint8_t old_vga_status(unsigned mode, unsigned line, bool rendered) {
    if (line >= (mode == 0x10 ? 350u : 480u)) return 9;
    if (!rendered) return 0;
    if (line >= 400 || mode <= 3) return 1;
    if (mode == 7 || mode == 0x1e || mode == 0x11 || mode == 0x10 || mode == 0x12) return 1;
    if (line & 1u) return 0;
    return 1;
}

int main(void) {
    const unsigned modes[] = {0,1,2,3,4,5,6,7,8,9,10,13,14,16,17,18,19,30,0x74,0x76,0xff};
    for (unsigned m = 0; m < sizeof modes / sizeof *modes; ++m) {
        const unsigned mode = modes[m];
        const unsigned total = mode == 0x10 ? 449u : 525u;
        unsigned blank = 0;
        for (unsigned line = 0; line < total; ++line) {
            for (unsigned rendered = 0; rendered < 2; ++rendered)
                assert(graphics_pc_raster_status(mode, line, rendered) ==
                       old_vga_status(mode, line, rendered));
            blank += !!(graphics_pc_raster_status(mode, line, true) & 8u);
        }
        assert(blank == (mode == 0x10 ? 99u : 45u));
    }
    // A guest waiting for end-of-blank then start-of-blank completes each frame.
    unsigned phase = 0, frames = 0;
    for (unsigned line = 0; line < 525u * 4u; ++line) {
        const unsigned status = graphics_pc_raster_status(VGA_320x200x256, line % 525u, true);
        if (!phase && !(status & 8u)) phase = 1;
        else if (phase && (status & 8u)) { ++frames; phase = 0; }
    }
    assert(frames == 4);
    puts("VGA raster status: all 21 modes, full blank intervals and guest wait loops passed");
}
