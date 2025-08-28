#include <graphics.h>
enum graphics_mode_t graphics_mode;
uint16_t ntsc_palette[4 * 256] __attribute__ ((aligned (4)));

void graphics_set_palette(const uint8_t index, const uint32_t rgb) {
    const uint8_t r = (rgb >> 16) & 0xFF;
    const uint8_t g = (rgb >> 8) & 0xFF;
    const uint8_t b = (rgb >> 0) & 0xFF;

    // ntsc_set_color expects parameters in order: (blue, red, green)
    ntsc_set_color(index, b, r, g);
}