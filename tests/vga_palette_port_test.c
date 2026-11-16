// gcc -std=c17 -O2 -DPICO_ON_DEVICE=1 -DPICO_RP2350=1 -Itests/vga-port-stubs \
//   tests/vga_palette_port_test.c src/emulator/video/vga.c -o vga_palette_port_test
#include <assert.h>
#include <stdio.h>
#include "emulator/emulator.h"

uint32_t VIDEORAM[65536];
uint8_t cga_blinking;
static unsigned callbacks;
static uint8_t last_index;
static uint32_t last_color;

void graphics_set_palette(uint8_t index, uint32_t color) {
    ++callbacks;
    last_index = index;
    last_color = color;
    assert(color == vga_palette[index]);
}

static void write_color(uint8_t red, uint8_t green, uint8_t blue) {
    const unsigned before = callbacks;
    vga_portout(0x3c9, red);
    vga_portout(0x3c9, green);
    assert(callbacks == before);
    vga_portout(0x3c9, blue);
    assert(callbacks == before + 1);
    assert(last_color == rgb(red << 2, green << 2, blue << 2));
}

int main(void) {
    vga_portout(0x3c8, 255);
    write_color(63, 17, 5);
    assert(last_index == 255 && vga_palette[255] == 0xfc4414);
    write_color(2, 3, 4);
    assert(last_index == 0 && vga_palette[0] == 0x080c10);

    vga_portout(0x3c8, 0);
    for (unsigned i = 0; i < 256; ++i) {
        write_color(i & 63u, (i + 7u) & 63u, (i + 19u) & 63u);
        assert(last_index == i);
    }
    write_color(6, 7, 8);
    assert(last_index == 0);

    vga_portout(0x3c7, 255);
    assert(vga_portin(0x3c9) == 63);
    assert(vga_portin(0x3c9) == 6);
    assert(vga_portin(0x3c9) == 18);
    assert(vga_portin(0x3c9) == 6);
    assert(vga_portin(0x3c9) == 7);
    assert(vga_portin(0x3c9) == 8);
    puts("VGA DAC ports: index 255, wrap to zero, 256-color upload and readback passed");
}
