#include "emulator/emulator.h"
#if PICO_ON_DEVICE
#include "graphics.h"
#endif

static uint8_t color_index = 0, read_color_index = 0, vga_register, sequencer_register = 0, graphics_control_register = 0;
uint32_t vga_plane_offset = 0;
uint8_t vga_planar_mode = 0;
uint8_t vga_sequencer[5];
uint8_t vga_graphics_control[9];

// https://wiki.osdev.org/VGA_Hardware

uint32_t vga_palette[256] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA, // 0-7
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF, // 8-15
        0x000000, 0x141414, 0x202020, 0x2C2C2C, 0x383838, 0x444444, 0x505050, 0x606060, // 16-23
        0x707070, 0x808080, 0x909090, 0xA0A0A0, 0xB4B4B4, 0xC8C8C8, 0xDCDCDC, 0xF0F0F0, // 24-31
        0x0000FF, 0x4100FF, 0x8200FF, 0xBE00FF, 0xFF00FF, 0xFF00BE, 0xFF0082, 0xFF0041, // 32-39
        0xFF0000, 0xFF4100, 0xFF8200, 0xFFBE00, 0xFFFF00, 0xBEFF00, 0x82FF00, 0x41FF00, // 40-47
        0x00FF00, 0x00FF41, 0x00FF82, 0x00FFBE, 0x00FFFF, 0x00BEFF, 0x0082FF, 0x0041FF, // 48-55
        0x8282FF, 0x9E82FF, 0xBE82FF, 0xDB82FF, 0xFF82FF, 0xFF82DB, 0xFF82BE, 0xFF829E, // 56-63
        0xFF8282, 0xFF9E82, 0xFFBE82, 0xFFDB82, 0xFFFF82, 0xDBFF82, 0xBEFF82, 0x9EFF82, // 64-71
        0x82FF82, 0x82FF9E, 0x82FFBE, 0x82FFDB, 0x82FFFF, 0x82DBFF, 0x82BEFF, 0x829EFF, // 72-79
        0xB6B6FF, 0xC6B6FF, 0xDBB6FF, 0xEBB6FF, 0xFFB6FF, 0xFFB6EB, 0xFFB6DB, 0xFFB6C6, // 80-87
        0xFFB6B6, 0xFFC6B6, 0xFFDBB6, 0xFFEBB6, 0xFFFFB6, 0xEBFFB6, 0xDBFFB6, 0xC6FFB6, // 88-95
        0xB6FFB6, 0xB6FFC6, 0xB6FFDB, 0xB6FFEB, 0xB6FFFF, 0xB6EBFF, 0xB6DBFF, 0xB6C6FF, // 96-103
        0x000071, 0x1C0071, 0x390071, 0x550071, 0x710071, 0x710055, 0x710039, 0x71001C, // 104-111
        0x710000, 0x711C00, 0x713900, 0x715500, 0x717100, 0x557100, 0x397100, 0x1C7100, // 112-119
        0x007100, 0x00711C, 0x007139, 0x007155, 0x007171, 0x005571, 0x003971, 0x001C71, // 120-127
        0x393971, 0x453971, 0x553971, 0x613971, 0x713971, 0x713961, 0x713955, 0x713945, // 128-135
        0x713939, 0x714539, 0x715539, 0x716139, 0x717139, 0x617139, 0x557139, 0x457139, // 136-143
        0x397139, 0x397145, 0x397155, 0x397161, 0x397171, 0x396171, 0x395571, 0x394571, // 144-151
        0x515171, 0x595171, 0x615171, 0x695171, 0x715171, 0x715169, 0x715161, 0x715159, // 152-159
        0x715151, 0x715951, 0x716151, 0x716951, 0x717151, 0x697151, 0x617151, 0x597151, // 160-167
        0x517151, 0x517159, 0x517161, 0x517169, 0x517171, 0x516971, 0x516171, 0x515971, // 168-175
        0x000041, 0x100041, 0x200041, 0x310041, 0x410041, 0x410031, 0x410020, 0x410010, // 176-183
        0x410000, 0x411000, 0x412000, 0x413100, 0x414100, 0x314100, 0x204100, 0x104100, // 184-191
        0x004100, 0x004110, 0x004120, 0x004131, 0x004141, 0x003141, 0x002041, 0x001041, // 192-199
        0x202041, 0x282041, 0x312041, 0x392041, 0x412041, 0x412039, 0x412031, 0x412028, // 200-207
        0x412020, 0x412820, 0x413120, 0x413920, 0x414120, 0x394120, 0x314120, 0x284120, // 208-215
        0x204120, 0x204128, 0x204131, 0x204139, 0x204141, 0x203941, 0x203141, 0x202841, // 216-223
        0x2D2D41, 0x312D41, 0x392D41, 0x3D2D41, 0x412D41, 0x412D3D, 0x412D39, 0x412D31, // 224-231
        0x412D2D, 0x41312D, 0x41392D, 0x413D2D, 0x41412D, 0x3D412D, 0x39412D, 0x31412D, // 232-239
        0x2D412D, 0x2D4131, 0x2D4139, 0x2D413D, 0x2D4141, 0x2D3D41, 0x2D3941, 0x2D3141, // 240-247
        0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000  // 248-255
};

 void vga_portout(uint16_t portnum, uint16_t value) {
//    http://www.techhelpmanual.com/900-video_graphics_array_i_o_ports.html
//    if (portnum != 0x3c8 && portnum != 0x3c9)
//        printf("vga_portout %x %x\n", portnum, value);

    switch (portnum) {
        /* Attribute Address Register */
        case 0x3C0:  {
            static uint8_t data_mode = 0; // 0 -- address, 1 -- data

            if (data_mode) {

                // Palette registers
                if (vga_register <= 0x0f) {
                    const uint8_t r = (((value >> 2) & 1) << 1) + (value >> 5 & 1);
                    const uint8_t g = (((value >> 1) & 1) << 1) + (value >> 4 & 1);
                    const uint8_t b = (((value >> 0) & 1) << 1) + (value >> 3 & 1);

                    vga_palette[vga_register] = rgb(r * 85, g * 85, b * 85);
#if PICO_ON_DEVICE
                    graphics_set_palette(vga_register, vga_palette[vga_register]);
#endif
                } else if (vga_register == 0x10) { // Attribute Mode Control
                    //printf("[VGA] value 0x%02x\r\n", value);
                    cga_blinking = (value >> 5) & 1 ? 0x7F : 0xFF;
                }
            } else {
                vga_register = value & 0b1111;
            }

            data_mode ^= 1;
            break;
        }
// http://www.osdever.net/FreeVGA/vga/seqreg.htm
// https://vtda.org/books/Computing/Programming/EGA-VGA-ProgrammersReferenceGuide2ndEd_BradleyDyckKliewer.pdf
        case 0x3C4:
//            printf("3C4 %x\n", value);
            sequencer_register = value & 0xff;
            break;
        case 0x3C5: {
            if (sequencer_register == 2) {
                switch (value & 0b1111) {
                    case 1:
                        vga_plane_offset = 0;
                        break;
                    case 2:
                        vga_plane_offset = vga_plane_size * 1;
                        break;
                    case 4:
                        vga_plane_offset = vga_plane_size * 2;
                        break;
                    case 8:
                        vga_plane_offset = vga_plane_size * 3;
                        break;
                    default:
                        vga_plane_offset = 0;
                        break;
                }

                //printf("vga_plane_offset %x\n",value);
            }
            if (sequencer_register == 4) {
                vga_planar_mode = value & 6;
//                printf("vga planar %i\n", vga_planar_mode);
            }
            //printf("sequencer %x %x\n", sequencer_register, value);
            vga_sequencer[sequencer_register] = value & 0xff;
            break;
        }
        case 0x3C7:
            read_color_index = value & 0xff;
            break;
        case 0x3C8: //color index register (write operations)
            color_index = value & 0xff;
            break;
        case 0x3C9: { //RGB data register
            static uint8_t RGB[3] = { 0, 0, 0 };
            static uint8_t rgb_index = 0;

            RGB[rgb_index++] = value << 2;
            if (rgb_index == 3) {
                vga_palette[color_index++] = rgb(RGB[0], RGB[1], RGB[2]);
#if PICO_ON_DEVICE
                graphics_set_palette(color_index-1, vga_palette[color_index-1]);
#endif
                rgb_index = 0;
            }
            break;
        }

            // http://www.osdever.net/FreeVGA/vga/graphreg.htm
        case 0x3CE: { // Graphics 1 and 2 Address Register
            /*
             * The Graphics 1 and 2 Address Register selects which register
                will appear at port 3cfh. The index number of the desired regis
                ter is written OUT to port 3ceh.
                Index Register
                0 Set/Reset
                1 Enable Set/Reset
                2 Color Compare
                3 Data Rotate
                4 Read Map Select
                5 Mode Register
                6 Miscellaneous
                7 Color Don't Care
                8 Bit Mask
             */
            graphics_control_register = value & 8;
//            printf("3CE %x\n", value);
        }
        case 0x3CF: { // Graphics 1 and 2 Address Register
//            printf("3CF %x\n", value);
            vga_graphics_control[graphics_control_register] = value & 0xff;
        }
    }

}

 uint16_t vga_portin(uint16_t portnum) {
    //printf("vga_portin %x\n", portnum);

    switch (portnum) {
        case 0x3C8:
            return read_color_index;
        case 0x3C9: {
            static uint8_t rgb_index = 0;
            switch (rgb_index++) {
                case 0:
                    return ((vga_palette[read_color_index] >> 18)) & 63;
                case 1:
                    return ((vga_palette[read_color_index] >> 10)) & 63;
                case 2:
                    rgb_index = 0;
                    return ((vga_palette[read_color_index++] >> 2)) & 63;
            }
        }
        default:
            return 0xff;
    }
}