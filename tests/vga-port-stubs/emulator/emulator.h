#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Keep unused ARM-only VRAM helpers out of this host port test.
#define __not_in_flash() static
#define __fast_mul(a, b) ((a) * (b))
extern uint32_t VIDEORAM[65536];
extern uint32_t vga_palette[256];
extern uint8_t cga_blinking;
void vga_portout(uint16_t port, uint16_t value);
uint16_t vga_portin(uint16_t port);

static inline uint32_t rgb(unsigned red, unsigned green, unsigned blue) {
    return red << 16 | green << 8 | blue;
}
