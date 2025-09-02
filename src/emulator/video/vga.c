#include "emulator/emulator.h"
#if PICO_ON_DEVICE
#include "graphics.h"
#endif

static uint8_t color_index = 0, read_color_index = 0, vga_register, sequencer_register = 0, graphics_control_register = 0;
uint32_t vga_plane_offset = 0;
uint8_t vga_planar_mode = 0;

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
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000 // 248-255
};
#if PICO_ON_DEVICE
bool ega_vga_enabled = true;
#endif

// vga_mem.h  -- minimal VGA planar memory handling (packed as uint32_t per offset).
// Target: RP2040 / RP2350 (32-bit), optimized hot path using bitwise and multiplications.
// Each vram entry: 0xP3P2P1P0 (plane0 in lowest byte).


// Latches (32-bit).
static uint32_t vga_latch32 = 0;

// Utility: replicate an 8-bit value into all four bytes of a 32-bit word
static inline uint32_t expand_to_u32(const uint8_t value) {
    // multiplication trick: x * 0x01010101
    return (uint32_t) value * 0x01010101u;
}

// Rotate right 8-bit
static inline uint8_t ror8(const uint8_t value, unsigned count) {
    count &= 7;
    return (uint8_t) ((value >> count) | (value << ((8 - count) & 7)));
}

// Expand 4-bit nibble into per-plane byte mask (0xFF if bit set, else 0x00)
// nibble bit0 -> plane0 (low byte), bit1 -> plane1, bit2 -> plane2, bit3 -> plane3
static inline uint32_t expand_nibble_to_planes(const uint8_t nibble) {
    return (nibble & 1 ? 0x000000FFu : 0) |
           (nibble & 2 ? 0x0000FF00u : 0) |
           (nibble & 4 ? 0x00FF0000u : 0) |
           (nibble & 8 ? 0xFF000000u : 0);
}


// Expand 4-bit value (0..15) into per-plane byte value 0x00/0xFF placed in each plane byte
// Equivalent to plane_mask32 for map_mask bits
static inline uint32_t plane_mask_from_mapmask(const uint8_t map_mask) {
    return expand_nibble_to_planes(map_mask & 0x0Fu);
}

// Build set_reset32 where each plane's byte is 0xFF if set_reset bit for that plane = 1, else 0x00
static inline uint32_t setreset32_from_setreset(const uint8_t set_reset) {
    return expand_nibble_to_planes(set_reset & 0x0Fu);
}

// Build enable_set_reset32 same as above
static inline uint32_t enablesr32_from(const uint8_t enable_set_reset) {
    return expand_nibble_to_planes(enable_set_reset & 0x0Fu);
}

// color_compare/don't care: each is 4-bit; expand into 0x00/0xFF per plane byte
static inline uint32_t cc32_from(const uint8_t cc4) { return expand_nibble_to_planes(cc4 & 0x0Fu); }
static inline uint32_t ndc32_from(const uint8_t ndc4) { return expand_nibble_to_planes(ndc4 & 0x0Fu); } // don't care mask

// Precomputed derived register cache to accelerate hot path
typedef struct {
    // raw registers (we keep raw bytes for reads and debug)
    uint8_t sequencer[8]; // sequencer indices we care about (0..4)
    uint8_t graphics_controller[16]; // graphics controller registers 0..8 used; keep up to 15 for future use

    // derived frequently-used masks/values
    uint32_t map_mask32; // plane enable mask replicated to bytes
    uint32_t set_reset32; // set/reset expanded
    uint32_t enable_set_reset32; // ESR expanded
    uint32_t bit_mask32; // bit mask replicated across bytes
    uint32_t color_compare32; // color compare replicated to bytes (as 0x00/0xFF per plane)
    uint32_t color_dontcare32; // don't care mask replicated (0xFF means compare, 0x00 means ignore)
    uint8_t data_rotate_counter; // rotate count 0..7
    uint8_t logical_operation; // logical function 0..3 (as in data_rotate reg low 2 bits)
    uint8_t read_map_select; // which plane byte to return on read mode 0
    uint8_t write_mode; // write mode 0..3
    uint8_t read_mode; // read mode 0 or 1

    // any other cached flags...
} vga_cache_t;

volatile uint8_t chain4; // sequencer memory_mode bit for chain4
static vga_cache_t vga;

// Initialize the regs and derived cache
static inline void vga_reset_cache(void) {
    memset(&vga, 0, sizeof(vga));
    // sensible defaults: map_mask = 0x0F (all planes enabled)
    vga.sequencer[2] = 0x0F; // map mask
    // vga_cache.graphics_controller[4] = 0x0F; // read map select//
    vga.graphics_controller[8] = 0xFF; // bit mask


    vga.map_mask32 = plane_mask_from_mapmask(0x0F);
    vga.read_map_select = 0;
    vga.bit_mask32 = expand_to_u32(0xFF); // default all bits allowed
    vga.set_reset32 = setreset32_from_setreset(0);
    vga.enable_set_reset32 = enablesr32_from(0);
    vga.color_compare32 = cc32_from(0);
    vga.color_dontcare32 = ndc32_from(0x0F); // default compare all planes
}

// Call whenever sequencer reg 2 or memory_mode changed
static inline void vga_update_seq_cache(void) {
    vga.map_mask32 = plane_mask_from_mapmask(vga.sequencer[2] & 0x0F);
    // memory_mode in seq[4] bit2 typically is chain4
    chain4 = !!(vga.sequencer[4] & 0x04u);
    vga_planar_mode = !(vga.sequencer[4] & 8) || !(vga.sequencer[4] & 6);
}

// Call whenever GC registers that affect derived masks change
static inline void vga_update_gc_cache(void) {
    // set_reset: reg 0 (lower 4 bits)
    vga.set_reset32 = setreset32_from_setreset(vga.graphics_controller[0] & 0x0Fu);
    vga.enable_set_reset32 = enablesr32_from(vga.graphics_controller[1] & 0x0Fu);
    vga.color_compare32 = cc32_from(vga.graphics_controller[2] & 0x0Fu);
    // color don't care: reg 7 low 4 bits indicates which planes to consider?
    // In VGA GC reg7 is color_dont_care; typically a 4-bit mask where 1 = compare
    vga.color_dontcare32 = ndc32_from(vga.graphics_controller[7] & 0x0Fu);
    vga.data_rotate_counter = vga.graphics_controller[3] & 0x07u;
    vga.logical_operation = (vga.graphics_controller[3] >> 3) & 0x03u; // low 2 bits of upper nibble (func)
    vga.read_map_select = vga.graphics_controller[4] & 0x03u;
    // mode reg 5 bits: write mode bits 0..1, read mode bit 3
    vga.write_mode = (vga.graphics_controller[5] & 0x03u);
    vga.read_mode = ((vga.graphics_controller[5] & 0x08u) ? 1 : 0);
    // bit mask: reg 8
    vga.bit_mask32 = expand_to_u32(vga.graphics_controller[8]);
}

// ---------------------- Read path ----------------------

// Read a byte from VGA memory (emulates CPU byte read from VGA window).
// Performs latch update on read.
uint8_t vga_mem_read(const uint32_t address) {
    vga_latch32 = VIDEORAM[address & 0xFFFF];

    if (vga.read_mode == 0) {
        // return the selected byte from latch
        const uint32_t shift = (vga.read_map_select & 3u) << 3;
        return (uint8_t) (vga_latch32 >> shift & 0xFF);
    }

    // read mode 1: color compare against color_compare + color_dont_care
    // compute per-plane mismatches:
    // tmp32 = ((lat ^ color_compare32) & color_dontcare32)
    const uint32_t tmp = ((vga_latch32 ^ vga.color_compare32) & vga.color_dontcare32);
    // OR across plane-bytes into single byte: (tmp | tmp>>8 | tmp>>16 | tmp>>24) & 0xFF
    const uint32_t folded = (tmp | tmp >> 8 | tmp >> 16 | tmp >> 24) & 0xFFu;
    return (uint8_t) (~folded & 0xFFu);
}

// ---------------------- Write path ----------------------

// Core write implementation (CPU writes a byte to VGA memory)
void vga_mem_write(uint32_t address, const uint8_t cpu_data) {
    address &= 0xFFFF;
    // uint8_t imposed4;
    // addr_xlate(address, &address, &imposed4);

    // Compose the effective plane mask32
    const uint32_t plane_mask32 = vga.map_mask32;;
    // Compose the effective plane mask32
    // const uint32_t plane_mask32 = vga_cache.map_mask32;

    // Grab latch (VGA hardware latches on read; on write mode 1 we use the latched value)
    const uint32_t latch = vga_latch32;
    // For other modes latch is still used for ALU combos and blending
    // Compute bit mask replicated to 32-bit
    const uint32_t bitmask32 = vga.bit_mask32;

    // Precompute rotated source and replicated 32-bit source
    const uint8_t rotated = ror8(cpu_data, vga.data_rotate_counter);
    uint32_t data32 = expand_to_u32(rotated);

    uint32_t result32 = VIDEORAM[address]; // read current mem for blending

    switch (vga.write_mode) {
        case 0: {
            // Mode 0: normal write using set/reset + ALU function + bitmask + plane map
            // First apply enable_set_reset: where enable == 1, take set_reset; else take source bits
            data32 = (data32 & ~vga.enable_set_reset32) | (vga.set_reset32 & vga.enable_set_reset32);
            uint32_t alu;
            // ALU function:
            switch (vga.logical_operation) {
                default:
                case 0: alu = data32;
                    break; // Replace (SRsel)
                case 1: alu = data32 & latch;
                    break; // AND with latch
                case 2: alu = data32 | latch;
                    break; // OR with latch
                case 3: alu = data32 ^ latch;
                    break; // XOR with latch
            }
            // Apply bit mask: where bitmask is 1 -> take from alu, else from latch
            const uint32_t blended = (alu & bitmask32) | (latch & ~bitmask32);
            // Finally apply plane enable mask
            result32 = (result32 & ~plane_mask32) | (blended & plane_mask32);
            break;
        }
        case 1: {
            // Mode 1: write latch back (no rotate/ALU / data ignored). Write latched 32 bits to enabled planes.
            result32 = (result32 & ~plane_mask32) | (latch & plane_mask32);
            break;
        }

        case 2: {
            // Mode 2: "color expand" set color to all planes
            // data32 = expand_to_u32(cpu_data); // e.g. 0xA5 -> 0xA5A5A5A5
            data32 = expand_nibble_to_planes(cpu_data);

            // Step 3: use src32 as ALU source (set/reset is ignored; rotation ignored)
            uint32_t alu;
            switch (vga.logical_operation) {
                default:
                case 0: alu = data32;
                    break; // PASS
                case 1: alu = data32 & result32;
                    break; // AND
                case 2: alu = data32 | result32;
                    break; // OR
                case 3: alu = data32 ^ result32;
                    break; // XOR
            }

            // Step 4: apply GC bitmask (reg 8) — this selects bit positions within bytes
            const uint32_t blended = (alu & bitmask32) | (result32 & ~bitmask32);

            // Step 5: write only to planes enabled by sequencer map_mask & addressing-imposed mask
            result32 = (result32 & ~plane_mask32) | (blended & plane_mask32);

            break;
        }

        case 3: {
            // Mode 3: Transparent set/reset writes
            // Selection mask is rot8 & bitmask (only bits with mask=1 affected). We must expand selection mask to 32-bit bytes.
            uint8_t sel8 = (uint8_t) (rotated & vga.graphics_controller[8]);
            // gc[8] is bit_mask, but we already set bitmask32; still compute sel8
            // Expand sel8 into 32-bit where each byte equals sel8 (and then AND with per-byte 0xFF/0x00)
            // Simpler: create sel32 = expand_to_u32(sel8) & bitmask32
            uint32_t sel32 = expand_to_u32(sel8) & bitmask32;
            // blend set_reset where sel32=1 else latch
            uint32_t blended = (vga.set_reset32 & sel32) | (latch & ~sel32);
            result32 = (result32 & ~plane_mask32) | (blended & plane_mask32);
            // result32 = 0xF0F0F0F;
            break;
        }
        default: {
            // fallback safe: do nothing
            break;
        }
    }

    // Write back
    VIDEORAM[address] = result32;
    // Note: hardware doesn't automatically reload latch on write; latch remains last read.
}

static uint8_t seq_index = 0;
static uint8_t gc_index = 0;

static inline void out_0x3C4_seq_index(const uint8_t value) { seq_index = value & 0x07u; }

static inline void out_0x3C5_seq_data(const uint8_t value) {
    // store raw
    vga.sequencer[seq_index] = value;
    // update derived cache for changes that matter
    if (seq_index == 2 || seq_index == 4) {
        vga_update_seq_cache();
    }
    // other seq regs could be handled here if desired
}

static inline uint8_t in_0x3C5_seq_data() { return vga.sequencer[seq_index]; }

static inline void out_0x3CE_gc_index(const uint8_t value) { gc_index = value & 0x0Fu; }

static inline void out_0x3CF_gc_data(const uint8_t value) {
    vga.graphics_controller[gc_index] = value;
    // If register affects derived cache, update
    if (gc_index <= 8 || gc_index == 0 || gc_index == 1 || gc_index == 2 || gc_index == 3 ||
        gc_index == 4 || gc_index == 5 || gc_index == 7 || gc_index == 8) {
        vga_update_gc_cache();
    }
}

static inline uint8_t in_0x3CF_gc_data() { return vga.graphics_controller[gc_index]; }

// ---------------------- Initialization ----------------------
void vga_init(void) {
    // memset(VIDEORAM, 0, sizeof(VIDEORAM));
    vga_reset_cache();
    vga_update_seq_cache();
    vga_update_gc_cache();
    vga_latch32 = 0;
    seq_index = gc_index = 0;
}


void vga_portout(uint16_t portnum, uint16_t value) {
    //    http://www.techhelpmanual.com/900-video_graphics_array_i_o_ports.html
    //    if (portnum != 0x3c8 && portnum != 0x3c9)
    //        printf("vga_portout %x %x\n", portnum, value);

    switch (portnum) {
        /* Attribute Address Register */
        case 0x3C0: {
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
                } else if (vga_register == 0x10) {
                    // Attribute Mode Control
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
            out_0x3C4_seq_index(value);
            break;
        case 0x3C5:
            out_0x3C5_seq_data(value);
            break;
        case 0x3C7:
            read_color_index = value & 0xff;
            break;
        case 0x3C8: //color index register (write operations)
            color_index = value & 0xff;
            break;
        case 0x3C9: {
            //RGB data register
            static uint8_t RGB[3] = {0, 0, 0};
            static uint8_t rgb_index = 0;

            RGB[rgb_index++] = value << 2;
            if (rgb_index == 3) {
                vga_palette[color_index++] = rgb(RGB[0], RGB[1], RGB[2]);
#if PICO_ON_DEVICE
                graphics_set_palette(color_index - 1, vga_palette[color_index - 1]);
#endif
                rgb_index = 0;
            }
            break;
        }

        // http://www.osdever.net/FreeVGA/vga/graphreg.htm
        case 0x3CE:
            out_0x3CE_gc_index(value);
            break;
        case 0x3CF:
            out_0x3CF_gc_data(value);
            break;
    }
}

uint16_t vga_portin(uint16_t portnum) {
    //printf("vga_portin %x\n", portnum);

    switch (portnum) {
        case 0x3C5: return in_0x3C5_seq_data();
        case 0x3CF: return in_0x3CF_gc_data();
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
