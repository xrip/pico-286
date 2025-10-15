#pragma once

#include <stdint.h>

#define DPAD_LEFT   0x001000
#define DPAD_RIGHT  0x004000
#define DPAD_DOWN   0x000400
#define DPAD_UP     0x000100
#define DPAD_START  0x000040
#define DPAD_SELECT 0x000010
#define DPAD_B      0x000004   //Y on SNES
#define DPAD_A      0x000001   //B on SNES
extern int nespad_state;

void keyboard_init(void);
void mouse_init(void);
static inline void nespad_read() {}
int16_t keyboard_send(uint8_t data);
