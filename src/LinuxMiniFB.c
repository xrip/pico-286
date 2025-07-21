#if !PICO_ON_DEVICE && __linux__

#include "MiniFB.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *s_display = NULL;
static Window s_window;
static GC s_gc;
static XImage *s_ximage = NULL;
static int s_close = 0;
static int s_width;
static int s_height;
static int s_scale = 1;
static void *s_buffer = NULL;
static char *s_image_data = NULL;
static char key_status[512] = {0};
static uint32_t s_palette[256];
static int s_screen;
static Visual *s_visual;
static int s_depth;

extern void HandleInput(unsigned int keycode, int isKeyDown);
extern void HandleMouse(int x, int y, int buttons);
extern int HanldeMenu(int menu_id, int checked);

static unsigned int translate_key(KeySym keysym) {
    switch (keysym) {
        case XK_Escape: return 27;
        case XK_Return: return 13;
        case XK_space: return 32;
        case XK_BackSpace: return 8;
        case XK_Tab: return 9;
        case XK_Left: return 37;
        case XK_Up: return 38;
        case XK_Right: return 39;
        case XK_Down: return 40;
        case XK_F1: return 112;
        case XK_F2: return 113;
        case XK_F3: return 114;
        case XK_F4: return 115;
        case XK_F5: return 116;
        case XK_F6: return 117;
        case XK_F7: return 118;
        case XK_F8: return 119;
        case XK_F9: return 120;
        case XK_F10: return 121;
        case XK_F11: return 122;
        case XK_F12: return 123;
        case XK_Shift_L: case XK_Shift_R: return 16;
        case XK_Control_L: case XK_Control_R: return 17;
        case XK_Alt_L: case XK_Alt_R: return 18;
        default:
            if (keysym >= XK_a && keysym <= XK_z) return keysym - XK_a + 65;
            if (keysym >= XK_A && keysym <= XK_Z) return keysym - XK_A + 65;
            if (keysym >= XK_0 && keysym <= XK_9) return keysym - XK_0 + 48;
            return keysym & 0xFF;
    }
}

static void convert_buffer_to_image() {
    if (!s_buffer || !s_image_data) return;
    
    uint32_t *src = (uint32_t *)s_buffer;
    
    if (s_depth == 32) {
        uint32_t *dst = (uint32_t *)s_image_data;
        for (int y = 0; y < s_height; y++) {
            for (int x = 0; x < s_width; x++) {
                uint32_t pixel = src[y * s_width + x];
                if (s_palette[0] != 0) {
                    uint8_t index = pixel & 0xFF;
                    pixel = s_palette[index];
                }
                dst[y * s_width + x] = pixel;
            }
        }
    } else if (s_depth == 24) {
        uint8_t *dst = (uint8_t *)s_image_data;
        for (int y = 0; y < s_height; y++) {
            for (int x = 0; x < s_width; x++) {
                uint32_t pixel = src[y * s_width + x];
                if (s_palette[0] != 0) {
                    uint8_t index = pixel & 0xFF;
                    pixel = s_palette[index];
                }
                int idx = (y * s_width + x) * 4;
                dst[idx + 0] = pixel & 0xFF;         // Red
                dst[idx + 1] = (pixel >> 8) & 0xFF;  // Green
                dst[idx + 2] = (pixel >> 16) & 0xFF; // Blue
                dst[idx + 3] = 0;                    // Padding
            }
        }
    } else {
        for (int y = 0; y < s_height; y++) {
            for (int x = 0; x < s_width; x++) {
                uint32_t pixel = src[y * s_width + x];
                if (s_palette[0] != 0) {
                    uint8_t index = pixel & 0xFF;
                    pixel = s_palette[index];
                }
                XPutPixel(s_ximage, x, y, pixel);
            }
        }
    }
}

int mfb_open(const char *title, int width, int height, int scale) {
    s_display = XOpenDisplay(NULL);
    if (!s_display) {
        fprintf(stderr, "Cannot open X display\n");
        return 0;
    }
    
    s_screen = DefaultScreen(s_display);
    s_visual = DefaultVisual(s_display, s_screen);
    s_depth = DefaultDepth(s_display, s_screen);
    
    s_width = width;
    s_height = height;
    s_scale = scale;
    
    XSetWindowAttributes attrs;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | 
                      ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                      StructureNotifyMask;
    attrs.background_pixel = BlackPixel(s_display, s_screen);
    
    s_window = XCreateWindow(s_display, RootWindow(s_display, s_screen),
                            100, 100, width * scale, height * scale, 0,
                            s_depth, InputOutput, s_visual,
                            CWEventMask | CWBackPixel, &attrs);
    
    XStoreName(s_display, s_window, title);
    XMapWindow(s_display, s_window);
    
    s_gc = XCreateGC(s_display, s_window, 0, NULL);
    
    if (s_depth >= 24) {
        s_image_data = malloc(width * height * 4);
        s_ximage = XCreateImage(s_display, s_visual, s_depth, ZPixmap, 0,
                               s_image_data, width, height, 32, width * 4);
    } else {
        s_ximage = XCreateImage(s_display, s_visual, s_depth, ZPixmap, 0,
                               NULL, width, height, 32, 0);
        s_ximage->data = malloc(s_ximage->bytes_per_line * height);
        s_image_data = s_ximage->data;
    }
    
    if (!s_ximage) {
        fprintf(stderr, "Cannot create XImage\n");
        XCloseDisplay(s_display);
        return 0;
    }
    
    memset(s_palette, 0, sizeof(s_palette));
    
    Atom wm_delete = XInternAtom(s_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(s_display, s_window, &wm_delete, 1);
    
    return 1;
}

void mfb_set_pallete_array(const uint32_t *new_palette, uint8_t start, uint8_t count) {
    for (int i = start; i < start + count && i < 256; i++) {
        s_palette[i] = new_palette[i - start];
    }
}

void mfb_set_pallete(const uint8_t color_index, const uint32_t color) {
    if (color_index < 256) {
        s_palette[color_index] = color;
    }
}

int mfb_update(void *buffer, int fps_limit) {
    static struct timeval last_time = {0, 0};
    XEvent event;
    
    s_buffer = buffer;
    
    while (XPending(s_display)) {
        XNextEvent(s_display, &event);
        
        switch (event.type) {
            case Expose:
                break;
                
            case KeyPress:
            case KeyRelease: {
                KeySym keysym = XLookupKeysym(&event.xkey, 0);
                unsigned int keycode = translate_key(keysym);
                int is_down = (event.type == KeyPress);
                
                HandleInput(keycode, is_down);
                if (keycode < 512) {
                    key_status[keycode] = is_down;
                }
                
                // if (keycode == 27 && is_down) {
                    // s_close = 1;
                // }
                break;
            }
            
            case ButtonPress:
            case ButtonRelease: {
                int buttons = 0;
                if (event.xbutton.button == Button1) buttons |= 0x02;
                if (event.xbutton.button == Button3) buttons |= 0x01;
                
                if (event.type == ButtonRelease) buttons = 0;
                
                HandleMouse(event.xbutton.x, event.xbutton.y, buttons);
                break;
            }
            
            case MotionNotify:
                HandleMouse(event.xmotion.x, event.xmotion.y, 0);
                break;
                
            case ClientMessage:
                s_close = 1;
                break;
        }
    }
    
    if (s_close) return -1;
    
    if (buffer) {
        convert_buffer_to_image();
        
        if (s_scale == 1) {
            XPutImage(s_display, s_window, s_gc, s_ximage, 0, 0, 0, 0, s_width, s_height);
        } else {
            for (int sy = 0; sy < s_height; sy++) {
                for (int dy = 0; dy < s_scale; dy++) {
                    XPutImage(s_display, s_window, s_gc, s_ximage, 
                             0, sy, 0, sy * s_scale + dy, s_width, 1);
                    for (int dx = 1; dx < s_scale; dx++) {
                        XCopyArea(s_display, s_window, s_window, s_gc,
                                 0, sy * s_scale + dy, s_width, 1,
                                 dx * s_width, sy * s_scale + dy);
                    }
                }
            }
        }
        
        XFlush(s_display);
    }
    
    if (fps_limit > 0) {
        struct timeval current_time, diff;
        gettimeofday(&current_time, NULL);
        
        if (last_time.tv_sec != 0) {
            timersub(&current_time, &last_time, &diff);
            long elapsed_us = diff.tv_sec * 1000000 + diff.tv_usec;
            long target_us = 1000000 / fps_limit;
            
            if (elapsed_us < target_us) {
                usleep(target_us - elapsed_us);
            }
        }
        
        gettimeofday(&last_time, NULL);
    }
    
    return 0;
}

void mfb_close() {
    if (s_ximage) {
        XDestroyImage(s_ximage);
        s_ximage = NULL;
        s_image_data = NULL;
    }
    
    if (s_display) {
        XFreeGC(s_display, s_gc);
        XDestroyWindow(s_display, s_window);
        XCloseDisplay(s_display);
        s_display = NULL;
    }
    
    s_buffer = NULL;
    s_close = 0;
}

char *mfb_keystatus() {
    return key_status;
}

#endif