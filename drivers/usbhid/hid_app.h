#pragma once

extern void kbd_raw_key_down(int code);
extern void kbd_raw_key_up(int code);
extern void gamepad_state_update(uint8_t index, uint8_t hat_state, uint32_t button_state);

