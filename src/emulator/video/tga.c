#include "emulator/emulator.h"
#include "emulator/includes/font8x8.h"
#if PICO_ON_DEVICE
#include "graphics.h"
#endif
uint32_t tga_offset = 0x8000;

uint32_t tga_palette[16] = {
        0x000000, // Black
        0x0000AA, // Dark Blue
        0x00AA00, // Dark Green
        0x00AAAA, // Teal
        0xAA0000, // Dark Red
        0xAA00AA, // Purple
        0xAA5500, // Brown
        0xAAAAAA, // Light Gray
        0x555555, // Dark Gray
        0x5555FF, // Blue
        0x55FF55, // Green
        0x55FFFF, // Aqua
        0xFF5555, // Red
        0xFF55FF, // Magenta
        0xFFFF55, // Yellow
        0xFFFFFF // White
};

uint8_t tga_palette_map[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

void tga_portout(uint16_t portnum, uint16_t value) {
// http://archives.oldskool.org/pub/tvdog/tandy1000/faxback/02506.pdf
// https://ia803208.us.archive.org/15/items/Tandy_1000_Computer_Service_Manual_1985_Tandy/Tandy_1000_Computer_Service_Manual_1985_Tandy.pdf
    static uint8_t tga_register;


    switch (portnum) {
        case 0x3DA: // address
            tga_register = value & 0x1F;
            break;
        case 0x3DE: // data
            switch (tga_register & 0x1F) {
                case 0x01: /* Palette Mask Register */
                case 0x02: /* Border Color */
                    return;
                case 0x03: /* Mode Control */
                    if (value == 0x10) {
                        // TODO crt hires/lores
                        videomode = cga_hires ? 0x0A : 0x08; // hires 0x0A
                    }

                    if (value == 8) {
                        videomode = 0x8; // 640x200x4
                    }

                    return;
            }
            // Palette Registers 0x10-0x1F
            uint8_t palette = tga_register & 0xF;

            const uint8_t r = ((value >> 2 & 1) << 1) + (value >> 3 & 1);
            const uint8_t g = ((value >> 1 & 1) << 1) + (value >> 3 & 1);
            const uint8_t b = ((value >> 0 & 1) << 1) + (value >> 3 & 1);


            tga_palette[palette] = rgb(r * 85, g * 85, b * 85);
#if PICO_ON_DEVICE
            graphics_set_palette(palette, tga_palette[palette]);
#endif
            break;

        case 0x3DF:
// CRT/processor page register
// Bit 0-2: CRT page PG0-2
// In one- and two bank modes, bit 0-2 select the 16kB memory
// area of system RAM that is displayed on the screen.
// In 4-banked modes, bit 1-2 select the 32kB memory area.
// Bit 2 only has effect when the PCJR upgrade to 128k is installed.

// Bit 3-5: Processor page CPU_PG
// Selects the 16kB area of system RAM that is mapped to
// the B8000h IBM PC video memory window. Since A14-A16 of the
// processor are unconditionally replaced with these bits when
// B8000h is accessed, the 16kB area is mapped to the 32kB
// range twice in a row. (Scuba Venture writes across the boundary)

// CRT/processor page register
// See the comments on the PCJr version of this register.
// A difference to it is:
// Bit 3-5: Processor page CPU_PG
// The remapped range is 32kB instead of 16. Therefore CPU_PG bit 0
// appears to be ORed with CPU A14 (to preserve some sort of
// backwards compatibility?), resulting in odd pages being mapped
// as 2x16kB. Implemented in vga_memory.cpp Tandy handler.

// Bit 6-7: Video Address mode
// 0: CRTC addresses A0-12 directly, accessing 8k characters
//    (+8k attributes). Used in text modes (one bank).
//    PG0-2 in effect. 16k range.
// 1: CRTC A12 is replaced with CRTC RA0 (see max_scanline).
//    This results in the even/odd scanline two bank system.
//    PG0-2 in effect. 16k range.
// 2: Documented as unused. CRTC addresses A0-12, PG0 is replaced
//    with RA1. Looks like nonsense.
//    PG1-2 in effect. 32k range which cannot be used completely.
// 3: CRTC A12 is replaced with CRTC RA0, PG0 is replaced with
//    CRTC RA1. This results in the 4-bank mode.
//    PG1-2 in effect. 32k range
            if (videomode == 4) break;
            /*
             *             if ((memctrl & 0xc0) != 0xc0) {
                vram = ((memctrl & 0x06) << 14) + base;
                b8000 = ((memctrl & 0x30) << 11) + base;
                b8000_mask = 0x7fff; // 32kb
            } else {
                vram = ((memctrl & 0x07) << 14) + base;
                b8000 = ((memctrl & 0x38) << 11) + base;
                b8000_mask = 0x3fff; // 16kb
            }
             */
            if (value & 0xc0) {
                tga_offset = (value & 0x07) << 14;
                tga_offset -= 0x8000;
                tga_offset &= 0xc000;
                tga_offset = (value & 0x38) << 11;
//                tga_offset -= 0x8000;
                tga_offset &= 0xc000;
            } else {
                tga_offset = (value & 0x06) << 14;
                tga_offset -= 0x18000;
                tga_offset &= 0x8000;
                tga_offset = (value & 0x30) << 11;
                tga_offset -= 0x10000;
                tga_offset &= 0x8000;

            }
//                printf("%x tga_offset %x tga_offset %x\n",value, tga_offset, tga_offset);

            break;
    }
}

void tga_draw_char(uint8_t ch, int x, int y, uint8_t color) {
    uint16_t base_offset = tga_offset + (x << 2) + y * 320;

    // Iterate over the 8 rows of the font character
    const uint8_t * font_char = &font_8x8[ch << 3];  // Get the bitmap for the current row of the character
    for (int row = 0; row < 8; row++) {
        uint8_t font_row = *font_char++;
        uint16_t plane_offset = base_offset + ((row & 3) << 13);  // 8192-byte offset per bitplane

        VIDEORAM[0][plane_offset++] = (((font_row >> 0) & 1) * color << 4) | ((font_row >> 1) & 1) * color;
        VIDEORAM[0][plane_offset++] = (((font_row >> 2) & 1) * color << 4) | ((font_row >> 3) & 1) * color;
        VIDEORAM[0][plane_offset++] = (((font_row >> 4) & 1) * color << 4) | ((font_row >> 5) & 1) * color;
        VIDEORAM[0][plane_offset]   = (((font_row >> 6) & 1) * color << 4) | ((font_row >> 7) & 1) * color;

        if (row == 3) base_offset += 160;
    }
}

void tga_draw_pixel(int x, int y, uint8_t color) {
    uint8_t * pixel = &VIDEORAM[0][tga_offset + (x >> 1) + ((y >> 2) << 13)];
    if (x & 1) {
        *pixel = (*pixel & 0xF0) | (color & 0x0F);
    } else {
        *pixel = (*pixel & 0x0F) | (color << 4);
    }
}