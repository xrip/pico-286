#include "hid_app.h"
#include "tusb.h"
#include "class/hid/hid.h"
#include "pico/util/queue.h"

#define GAMEPAD_MAX_DEVICES 2

int nespad_state;

typedef struct {
    uint16_t id;
    uint32_t type;
    uint8_t index;
    uint8_t hat_state;
    uint32_t button_state;
} gamepad_t;

uint8_t const keycode_to_ascii_table[128][2] = {HID_KEYCODE_TO_ASCII};

static hid_keyboard_report_t prev_report = {0, 0, {0}};

static gamepad_t gamepads[GAMEPAD_MAX_DEVICES];
static int8_t gamepads_count = 0;

static void gamepad_state_update(uint8_t index, uint8_t hat_state, uint32_t button_state);

// The main emulator wants XT ("set 1") scancodes. Who are we to disappoint them?
// ref1: adafrhit_hid/keycode.py
// ref2: https://www.scs.stanford.edu/10wi-cs140/pintos/specs/kbd/scancodes-9.html
// ref3: https://kbdlayout.info/kbdusx/scancodes?arrangement=ANSI104
typedef struct { const char *d[2]; } usb_to_xt;
static const usb_to_xt conversion[] = {
    [ 53] = { "\x29"          , "\xa9"           }, // ` ~
    [ 30] = { "\x02"          , "\x82"           }, // 1 !
    [ 31] = { "\x03"          , "\x83"           }, // 2 @
    [ 32] = { "\x04"          , "\x84"           }, // 3 #
    [ 33] = { "\x05"          , "\x85"           }, // 4 $
    [ 34] = { "\x06"          , "\x86"           }, // 5 % E
    [ 35] = { "\x07"          , "\x87"           }, // 6 ^
    [ 36] = { "\x08"          , "\x88"           }, // 7 &
    [ 37] = { "\x09"          , "\x89"           }, // 8 *
    [ 38] = { "\x0a"          , "\x8a"           }, // 9 (
    [ 39] = { "\x0b"          , "\x8b"           }, // 0 )
    [ 45] = { "\x0c"          , "\x8c"           }, // - _
    [ 46] = { "\x0d"          , "\x8d"           }, // = +
    [ 42] = { "\x0e"          , "\x8e"           }, // Backspace
    [ 43] = { "\x0f"          , "\x8f"           }, // Tab
    [ 20] = { "\x10"          , "\x90"           }, // Q
    [ 26] = { "\x11"          , "\x91"           }, // W
    [  8] = { "\x12"          , "\x92"           }, // E
    [ 21] = { "\x13"          , "\x93"           }, // R
    [ 23] = { "\x14"          , "\x94"           }, // T
    [ 28] = { "\x15"          , "\x95"           }, // Y
    [ 24] = { "\x16"          , "\x96"           }, // U
    [ 12] = { "\x17"          , "\x97"           }, // I
    [ 18] = { "\x18"          , "\x98"           }, // O
    [ 19] = { "\x19"          , "\x99"           }, // P
    [ 47] = { "\x1a"          , "\x9a"           }, // [ {
    [ 48] = { "\x1b"          , "\x9b"           }, // ] }
    [ 49] = { "\x2b"          , "\xab"           }, // \ |
    [ 57] = { "\x3a"          , "\xba"           }, // CapsLock
    [  4] = { "\x1e"          , "\x9e"           }, // A
    [ 22] = { "\x1f"          , "\x9f"           }, // S
    [  7] = { "\x20"          , "\xa0"           }, // D
    [  9] = { "\x21"          , "\xa1"           }, // F
    [ 10] = { "\x22"          , "\xa2"           }, // G
    [ 11] = { "\x23"          , "\xa3"           }, // H
    [ 13] = { "\x24"          , "\xa4"           }, // J
    [ 14] = { "\x25"          , "\xa5"           }, // K
    [ 15] = { "\x26"          , "\xa6"           }, // L
    [ 51] = { "\x27"          , "\xa7"           }, // ; :
    [ 52] = { "\x28"          , "\xa8"           }, // ' "
    [ 50] = { "\x00"          , "\x80"           }, // non-US-1
    [ 40] = { "\x1c"          , "\x9c"           }, // Enter
    [225] = { "\x2a"          , "\xaa"           }, // LShift
    [ 29] = { "\x2c"          , "\xac"           }, // Z
    [ 27] = { "\x2d"          , "\xad"           }, // X
    [  6] = { "\x2e"          , "\xae"           }, // C
    [ 25] = { "\x2f"          , "\xaf"           }, // V
    [  5] = { "\x30"          , "\xb0"           }, // B
    [ 17] = { "\x31"          , "\xb1"           }, // N
    [ 16] = { "\x32"          , "\xb2"           }, // M
    [ 54] = { "\x33"          , "\xb3"           }, // , <
    [ 55] = { "\x34"          , "\xb4"           }, // . >
    [ 56] = { "\x35"          , "\xb5"           }, // / ?
    [229] = { "\x36"          , "\xb6"           }, // RShift
    [224] = { "\x1d"          , "\x9d"           }, // LCtrl
    [226] = { "\x38"          , "\xb8"           }, // LAlt
    [ 44] = { "\x39"          , "\xb9"           }, // space
    [230] = { "\xe0\x38"      , "\xe0\xb8"       }, // RAlt
    [228] = { "\xe0\x1d"      , "\xe0\x9d"       }, // RCtrl
    [ 73] = { "\xe0\x52"      , "\xe0\xd2"       }, // Insert
    [ 76] = { "\xe0\x53"      , "\xe0\xd3"       }, // Delete
    [ 74] = { "\xe0\x47"      , "\xe0\xc7"       }, // Home
    [ 77] = { "\xe0\x4f"      , "\xe0\xcf"       }, // End
    [ 75] = { "\xe0\x49"      , "\xe0\xc9"       }, // PgUp
    [ 78] = { "\xe0\x51"      , "\xe0\xd1"       }, // PgDn
    [ 80] = { "\xe0\x4b"      , "\xe0\xcb"       }, // Left
    [ 82] = { "\xe0\x48"      , "\xe0\xc8"       }, // Up
    [ 81] = { "\xe0\x50"      , "\xe0\xd0"       }, // Down
    [ 79] = { "\xe0\x4d"      , "\xe0\xcd"       }, // Right
    [ 83] = { "\x45"          , "\xc5"           }, // NumLock
    [ 95] = { "\x47"          , "\xc7"           }, // KP-7 / Home
    [ 92] = { "\x4b"          , "\xcb"           }, // KP-4 / Left
    [ 89] = { "\x4f"          , "\xcf"           }, // KP-1 / End
    [ 84] = { "\xe0\x35"      , "\xe0\xb5"       }, // KP-/
    [ 96] = { "\x48"          , "\xc8"           }, // KP-8 / Up
    [ 93] = { "\x4c"          , "\xcc"           }, // KP-5
    [ 90] = { "\x50"          , "\xd0"           }, // KP-2 / Down
    [ 98] = { "\x52"          , "\xd2"           }, // KP-0 / Ins
    [ 85] = { "\x37"          , "\xb7"           }, // KP-*
    [ 97] = { "\x49"          , "\xc9"           }, // KP-9 / PgUp
    [ 94] = { "\x4d"          , "\xcd"           }, // KP-6 / Right
    [ 91] = { "\x51"          , "\xd1"           }, // KP-3 / PgDn
    [ 99] = { "\x53"          , "\xd3"           }, // KP-. / Del
    [ 86] = { "\x4a"          , "\xca"           }, // KP--
    [ 87] = { "\x4e"          , "\xce"           }, // KP-+
    [ 88] = { "\xe0\x1c"      , "\xe0\x9c"       }, // KP-Enter
    [ 41] = { "\x01"          , "\x81"           }, // Esc
    [ 58] = { "\x3b"          , "\xbb"           }, // F1
    [ 59] = { "\x3c"          , "\xbc"           }, // F2
    [ 60] = { "\x3d"          , "\xbd"           }, // F3
    [ 61] = { "\x3e"          , "\xbe"           }, // F4
    [ 62] = { "\x3f"          , "\xbf"           }, // F5
    [ 63] = { "\x40"          , "\xc0"           }, // F6
    [ 64] = { "\x41"          , "\xc1"           }, // F7
    [ 65] = { "\x42"          , "\xc2"           }, // F8
    [ 66] = { "\x43"          , "\xc3"           }, // F9
    [ 67] = { "\x44"          , "\xc4"           }, // F10
    [ 68] = { "\x57"          , "\xd7"           }, // F11
    [ 69] = { "\x58"          , "\xd8"           }, // F12
    [ 70] = { "\xe0\x37"      , "\xe0\xb7"       }, // PrtScr
    [154] = { "\x54"          , "\xd4"           }, // Alt+SysRq
    [ 71] = { "\x46"          , "\xc6"           }, // ScrollLock
    [ 72] = { "\xe1\x1d\x45\xe1\x9d\xc5", ""     }, // Pause
    [227] = { "\xe0\x5b"      , "\xe0\xdb"       }, // LWin (USB: LGUI)
    [231] = { "\xe0\x5c"      , "\xe0\xdc"       }, // RWin (USB: RGUI)
};

static queue_t kq;
void keyboard_init(void) {
    queue_init(&kq, /* element_size */ sizeof(uint8_t), /* element_count */ 32);
}

void mouse_init() {
}

int16_t keyboard_send(uint8_t i) {
    printf("keyboard_send %u unimplemented\n", i);
    return 0;
}

static bool ctrl_pressed;

static void kbd_add_sequence(const uint8_t *sequence) {
    if (!sequence)
        return;
    while(*sequence) {
        queue_try_add(&kq, sequence++);
    }
}

static void kbd_raw_key(int usb_code, int is_release) {
    const uint8_t *sequence = (const uint8_t*)conversion[usb_code].d[is_release];
    kbd_add_sequence(sequence);
}

static void kbd_raw_key_down(int usb_code) {    
    if (usb_code == 126 && ctrl_pressed) {
        kbd_add_sequence("\xe0\x46");
    } else {
        kbd_raw_key(usb_code, 0);
    }
}

static void kbd_raw_key_up(int usb_code) {    
    if (usb_code == 126 && ctrl_pressed) {
        kbd_add_sequence("\xe0\xc6"); // This is a guess
    } else {
        kbd_raw_key(usb_code, 1);
    }
}

static inline bool find_key_in_report(hid_keyboard_report_t const* report, uint8_t keycode) {
    for (uint8_t i = 0; i < 6; i++) {
        if (report->keycode[i] == keycode) {
            return true;
        }
    }
    return false;
}

static void process_kbd_report(hid_keyboard_report_t const* r1, hid_keyboard_report_t const* r2,
                               void (*kbd_raw_key_cb)(int code)) {

    for(int bit = 8; bit--;) {
        int weight = 1 << bit;
        if((r1->modifier & weight) && !(r2->modifier & weight)) {
            kbd_raw_key_cb(bit + 0xe0);
        }
    }

    // Process keycodes
    for (int i = 0; i < 6; i++) {
        if (r1->keycode[i]) {
            int keycode = r1->keycode[i];
            if (!find_key_in_report(r2, keycode)) {
                kbd_raw_key_cb(keycode);
            }
        }
    }
}

static void find_pressed_keys(hid_keyboard_report_t const* report) {
    process_kbd_report(report, &prev_report, &kbd_raw_key_down);
}

static void find_released_keys(hid_keyboard_report_t const* report) {
    process_kbd_report(&prev_report, report, &kbd_raw_key_up);
}

static gamepad_t* find_gamepad(uint16_t id) {
    for (int i = 0; i < gamepads_count; i++) {
        if (gamepads[i].id == id) {
            return &gamepads[i];
        }
    }
    return NULL;
}

// Support for Xbox One controller (04284001)
static uint8_t get_hat_state_04284001(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0x0F;
    
    switch (dpad) {
        case 0x1:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0xC:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x9:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x0:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_04284001(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[3] & 0x10) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[3] & 0x20) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[3] & 0x40) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[3] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[3] & 0x01) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[3] & 0x02) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

// Support for PS3 controller (05832060)
static uint8_t get_hat_state_05832060(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_05832060(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[3] & 0x40) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[3] & 0x20) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[3] & 0x10) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[3] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[3] & 0x08) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[3] & 0x04) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

// Support for PS4 controller (054C0CDA)
static uint8_t get_hat_state_054C0CDA(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[5] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_054C0CDA(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[5] & 0x10) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[5] & 0x20) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[5] & 0x40) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[5] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[6] & 0x01) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[6] & 0x02) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

static uint8_t get_hat_state_0079181C(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0xF;

    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }

    return hat_state;
}

static uint32_t get_button_state_0079181C(uint8_t const* report) {
    uint32_t button_state = 0;

    if (report[0] & 0x01) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[0] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[0] & 0x08) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[0] & 0x10) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[0] & 0x40) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[0] & 0x80) {
        button_state |= GAMEPAD_BUTTON_TR;
    }

    return button_state;
}

// Support for 8BitDo SN30 Pro controller (007918D2)
static uint8_t get_hat_state_007918D2(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[2] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_007918D2(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[0] & 0x01) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[0] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[0] & 0x08) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[0] & 0x10) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[0] & 0x40) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[0] & 0x80) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

// Support for Switch Pro controller (07382217)
static uint8_t get_hat_state_07382217(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = (report[5] >> 4) & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_07382217(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[3] & 0x04) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[3] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[3] & 0x08) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[3] & 0x01) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[3] & 0x40) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[3] & 0x80) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

static uint8_t get_hat_state_081FE401(uint8_t const* report) {
    uint8_t hat_state = 0;

    switch (report[0] << 8 | report[1]) {
        case 0x7F7F:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;

        case 0x7F00:
            hat_state = GAMEPAD_HAT_UP;
            break;

        case 0xFF00:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;

        case 0xFF7F:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;

        case 0xFFFF:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;

        case 0x7FFF:
            hat_state = GAMEPAD_HAT_DOWN;
            break;

        case 0x00FF:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;

        case 0x007F:
            hat_state = GAMEPAD_HAT_LEFT;
            break;

        case 0x0000:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;

        default:
            break;
    }

    return hat_state;
}

static uint32_t get_button_state_081FE401(uint8_t const* report) {
    uint32_t button_state = 0;

    if (report[5] & 0x20) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[5] & 0x40) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[5] & 0x10) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[5] & 0x80) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[6] & 0x01) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[6] & 0x02) {
        button_state |= GAMEPAD_BUTTON_TR;
    }

    return button_state;
}

// Support for Generic USB controller (1C59002X)
static uint8_t get_hat_state_1C59002X(uint8_t const* report) {
    uint8_t hat_state = 0;
    uint8_t dpad = report[0] & 0x0F;
    
    switch (dpad) {
        case 0x0:
            hat_state = GAMEPAD_HAT_UP;
            break;
        case 0x1:
            hat_state = GAMEPAD_HAT_UP_RIGHT;
            break;
        case 0x2:
            hat_state = GAMEPAD_HAT_RIGHT;
            break;
        case 0x3:
            hat_state = GAMEPAD_HAT_DOWN_RIGHT;
            break;
        case 0x4:
            hat_state = GAMEPAD_HAT_DOWN;
            break;
        case 0x5:
            hat_state = GAMEPAD_HAT_DOWN_LEFT;
            break;
        case 0x6:
            hat_state = GAMEPAD_HAT_LEFT;
            break;
        case 0x7:
            hat_state = GAMEPAD_HAT_UP_LEFT;
            break;
        case 0x8:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
        default:
            hat_state = GAMEPAD_HAT_CENTERED;
            break;
    }
    
    return hat_state;
}

static uint32_t get_button_state_1C59002X(uint8_t const* report) {
    uint32_t button_state = 0;
    
    if (report[1] & 0x01) {
        button_state |= GAMEPAD_BUTTON_A;
    }
    if (report[1] & 0x02) {
        button_state |= GAMEPAD_BUTTON_B;
    }
    if (report[1] & 0x04) {
        button_state |= GAMEPAD_BUTTON_X;
    }
    if (report[1] & 0x08) {
        button_state |= GAMEPAD_BUTTON_Y;
    }
    if (report[1] & 0x10) {
        button_state |= GAMEPAD_BUTTON_TL;
    }
    if (report[1] & 0x20) {
        button_state |= GAMEPAD_BUTTON_TR;
    }
    
    return button_state;
}

static void process_gamepad_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    uint16_t id = dev_addr << 8 | instance;
    gamepad_t* gamepad = find_gamepad(id);

    switch (gamepad->type) {
        case 0x04284001: // Xbox One controller
            if (len != 11) {
                return;
            }
            gamepad->hat_state = get_hat_state_04284001(report);
            gamepad->button_state = get_button_state_04284001(report);
            break;
            
        case 0x05832060: // PS3 controller
            if (len != 8) {
                return;
            }
            gamepad->hat_state = get_hat_state_05832060(report);
            gamepad->button_state = get_button_state_05832060(report);
            break;
            
        case 0x054C0CDA: // PS4 controller
            if (len != 10) {
                return;
            }
            gamepad->hat_state = get_hat_state_054C0CDA(report);
            gamepad->button_state = get_button_state_054C0CDA(report);
            break;
            
        case 0x0079181C:
            if (len != 9) {
                return;
            }
            gamepad->hat_state = get_hat_state_0079181C(report);
            gamepad->button_state = get_button_state_0079181C(report);
            break;
            
        case 0x007918D2: // 8BitDo SN30 Pro
            if (len != 9) {
                return;
            }
            gamepad->hat_state = get_hat_state_007918D2(report);
            gamepad->button_state = get_button_state_007918D2(report);
            break;
            
        case 0x07382217: // Switch Pro controller
            if (len != 10) {
                return;
            }
            gamepad->hat_state = get_hat_state_07382217(report);
            gamepad->button_state = get_button_state_07382217(report);
            break;
            
        case 0x081FE401:
            if (len != 8) {
                return;
            }
            gamepad->hat_state = get_hat_state_081FE401(report);
            gamepad->button_state = get_button_state_081FE401(report);
            break;

        case 0x1C590020: // Generic USB controller
        case 0x1C590021:
        case 0x1C590022:
        case 0x1C590023:
        case 0x1C590024:
        case 0x1C590025:
            if (len != 6) {
                return;
            }
            gamepad->hat_state = get_hat_state_1C59002X(report);
            gamepad->button_state = get_button_state_1C59002X(report);
            break;

        default:
            return;
    }

    gamepad_state_update(gamepad->index, gamepad->hat_state, gamepad->button_state);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_len;
    (void)desc_report;

    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            break;

        case HID_ITF_PROTOCOL_NONE:
            if (gamepads_count < GAMEPAD_MAX_DEVICES) {
                gamepad_t* gamepad = &gamepads[gamepads_count];
                gamepad->id = dev_addr << 8 | instance;
                gamepad->type = vid << 16 | pid;
                gamepad->index = gamepads_count;
                gamepad->hat_state = 0;
                gamepad->button_state = 0;
                gamepads_count++;

                printf("Gamepad connected: VID: %04X PID: %04X\n", vid, pid);
            }
            break;

        default:
            break;
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    (void)instance;
    (void)len;

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            const hid_keyboard_report_t *kbd_report = (const hid_keyboard_report_t*)report;
            printf("keyboard report: modifier=%02x keycode=[%3d %3d %3d %3d %3d %3d]\n",
                kbd_report->modifier, kbd_report->keycode[0],
                kbd_report->keycode[1], kbd_report->keycode[2], kbd_report->keycode[3], kbd_report->keycode[4], kbd_report->keycode[5]);
            ctrl_pressed = kbd_report->modifier & 0x11;
            find_pressed_keys(kbd_report);
            find_released_keys(kbd_report);
            memcpy(&prev_report, report, sizeof(hid_keyboard_report_t));
            break;

        case HID_ITF_PROTOCOL_NONE:
            process_gamepad_report(dev_addr, instance, report, len);
            break;

        default:
            break;
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    (void)instance;
}

int nespad_state;

static const int hat_translate[] = {
    0,
    DPAD_UP, DPAD_UP | DPAD_RIGHT,
    DPAD_RIGHT, DPAD_RIGHT | DPAD_DOWN,
    DPAD_DOWN, DPAD_DOWN | DPAD_DOWN | DPAD_LEFT,
    DPAD_LEFT, DPAD_LEFT | DPAD_UP,
};

void gamepad_state_update(uint8_t index, uint8_t hat_state, uint32_t button_state) {
    nespad_state = 0;
    if (hat_state < count_of(hat_translate)) {
        nespad_state = hat_translate[hat_state];
    }

    if (button_state & 0x55) {
        nespad_state |= DPAD_A;
    }
    if (button_state & 0xaa) {
        nespad_state |= DPAD_B;
    }
}


void keyboard_tick(void) {
    tuh_task();
    uint8_t xt_code;
    if (queue_try_remove(&kq, &xt_code)) {
        handleScancode(xt_code);
    }
}
