#ifndef VGA_H
#define VGA_H

#include <stdint.h>

extern uint8_t vga_sequencer[5];
extern uint8_t vga_graphics_control[9];
// vga_planar_mode is in emulator.h
// vga_plane_size is a macro in emulator.h
// vga_plane_offset is in emulator.h

#endif // VGA_H
