#pragma GCC optimize("Ofast")
#include "graphics.h"
#include "hardware/clocks.h"
#include "stdbool.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/systick.h"

#include "hardware/dma.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdio.h>
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "stdlib.h"
#include "emulator/emulator.h"

uint16_t pio_program_VGA_instructions[] = {
        //     .wrap_target
        0x6008, //  0: out    pins, 8
        //     .wrap
};

const struct pio_program pio_program_VGA = {
        .instructions = pio_program_VGA_instructions,
        .length = 1,
        .origin = -1,
};

extern int cursor_blink_state;
static uint32_t *lines_pattern[4];
static uint32_t *lines_pattern_data = NULL;
static int _SM_VGA = -1;


static int N_lines_total = 525;
static int N_lines_visible = 480;
static int line_VS_begin = 490;
static int line_VS_end = 491;
static int shift_picture = 0;

static int visible_line_size = 320;


static int dma_channel_control;
static int dma_channel_data;

static uint8_t *graphics_framebuffer;
uint8_t *text_buffer = NULL;
static uint graphics_buffer_width = 0;
static uint graphics_buffer_height = 0;
static int framebuffer_offset_x = 0;
static int graphics_buffer_shift_y = 0;

static bool is_flash_line = false;
static bool is_flash_frame = false;

//буфер 1к графической палитры
static uint16_t __aligned(4) palette[2][256];
//static uint16_t palette[2][256];

static uint32_t bg_color[2];
static uint16_t palette16_mask = 0;

static uint8_t text_buffer_width = 80;
static uint8_t text_buffer_height = 0;

static uint16_t __aligned(4) txt_palette[16];

//буфер 2К текстовой палитры для быстрой работы
//static uint16_t *txt_palette_fast = NULL;
static uint16_t __aligned(4) txt_palette_fast[256 * 4];
static int txt_palette_init = 0;

enum graphics_mode_t graphics_mode;

extern uint8_t __aligned(4) DEBUG_VRAM[80*10];

void __time_critical_func() dma_handler_VGA() {
    dma_hw->ints0 = 1u << dma_channel_control;
    static uint32_t frame_number = 0;
    static uint32_t screen_line = 0;
    screen_line++;

    if (screen_line == N_lines_total) {
        screen_line = 0;
        frame_number++;
    }

    if (screen_line >= N_lines_visible) {
        //заполнение цветом фона
        if (screen_line == N_lines_visible | screen_line == N_lines_visible + 3) {
            uint32_t * output_buffer_32bit = lines_pattern[2 + (screen_line & 1)];
            output_buffer_32bit += shift_picture / 4;
            uint32_t p_i = (screen_line & is_flash_line) + (frame_number & is_flash_frame) & 1;
            uint32_t color32 = bg_color[p_i];
            for (int i = visible_line_size / 2; i--;) {
                *output_buffer_32bit++ = color32;
            }
        }

        //синхросигналы
        if (screen_line >= line_VS_begin && screen_line <= line_VS_end)
            dma_channel_set_read_addr(dma_channel_control, &lines_pattern[1], false); //VS SYNC
        else
            dma_channel_set_read_addr(dma_channel_control, &lines_pattern[0], false);
        return;
    }

    if (!graphics_framebuffer) {
        dma_channel_set_read_addr(dma_channel_control, &lines_pattern[0], false);
        return;
    } //если нет видеобуфера - рисуем пустую строку

    if (screen_line >= 399)
        port3DA = 8;
    else
        port3DA = 0;

    if (screen_line & 1)
        port3DA |= 1;

    uint32_t * *output_buffer = &lines_pattern[2 + (screen_line & 1)];
    uint16_t *output_buffer_16bit = (uint16_t *) (*output_buffer) + shift_picture / 2;
    if (1 && screen_line >= 400) {
        uint8_t y = screen_line - 400;
        uint8_t y_div_8 = y / 8;
        uint8_t glyph_line = y % 8;

        const uint8_t colors[4] = { 0x0f, 0xf0, 10, 12 };
        //указатель откуда начать считывать символы
        uint8_t* text_buffer_line = &DEBUG_VRAM[__fast_mul(y_div_8, 80)];
        for (uint8_t column = 80; column--;) {
            const uint8_t character = *text_buffer_line++ ;
            const uint8_t color = character >> 6;
            uint8_t glyph_pixels = font_8x8[(32 + (character & 63)) * 8 + glyph_line];
            //считываем из быстрой палитры начало таблицы быстрого преобразования 2-битных комбинаций цветов пикселей
            uint16_t* palette_color = &txt_palette_fast[4 * colors[color]] ;

            *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
            glyph_pixels >>= 2;
            *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
            glyph_pixels >>= 2;
            *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
            glyph_pixels >>= 2;
            *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
        }
        dma_channel_set_read_addr(dma_channel_control, output_buffer, false);
        return;
    }
    switch (graphics_mode) {
        case TEXTMODE_40x25_COLOR:
        case TEXTMODE_40x25_BW: {
            // "слой" символа
            uint8_t y_div_16 = screen_line / 16;
            uint8_t glyph_line = (screen_line / 2) % 8;

            //указатель откуда начать считывать символы
            uint8_t* text_buffer_line = graphics_framebuffer + 0x8000 + (vram_offset << 1) + __fast_mul(y_div_16, 80);

            for (uint8_t column = 0; column < 40; column++) {
                //из таблицы символов получаем "срез" текущего символа
                uint8_t glyph_pixels = font_8x8[*text_buffer_line++ * 8 + glyph_line];
                //считываем из быстрой палитры начало таблицы быстрого преобразования 2-битных комбинаций цветов пикселей
                uint8_t color = *text_buffer_line++;
                uint16_t* palette_color = &txt_palette_fast[4 * (color & cga_blinking)] ;

                // Cursor blinking check
                uint8_t cursor_active = cursor_blink_state &&
                                        y_div_16 == CURSOR_Y && column == CURSOR_X &&
                                        glyph_line >= cursor_start && glyph_line <= cursor_end;

                if (cga_blinking == 0x7F && (color & 0x80) && cursor_blink_state) {
                    glyph_pixels = 0;
                }

                if (cursor_active) {
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                } else {
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                }
            }
            dma_channel_set_read_addr(dma_channel_control, output_buffer, false);
            return;
        }
        case TEXTMODE_80x25_COLOR:
        case TEXTMODE_80x25_BW:  {
            // "слой" символа
            uint8_t y_div_16 = screen_line / 16;
            uint8_t glyph_line = screen_line % 16;

            //указатель откуда начать считывать символы
            uint8_t* text_buffer_line = graphics_framebuffer + 0x8000 + (vram_offset << 1) + __fast_mul(y_div_16, 160);

            for (uint8_t column = 0; column < 80; column++) {
                uint8_t glyph_pixels = font_8x16[*text_buffer_line++ * 16 + glyph_line];
                uint8_t color = *text_buffer_line++;
                uint16_t* palette_color = &txt_palette_fast[4 * (color & cga_blinking)] ;

                uint8_t cursor_active =
                        cursor_blink_state && y_div_16 == CURSOR_Y && column == CURSOR_X &&
                        (cursor_start > cursor_end
                         ? !(glyph_line >= cursor_end << 1 &&
                             glyph_line <= cursor_start << 1)
                         : glyph_line >= cursor_start << 1 && glyph_line <= cursor_end << 1);

                if (cga_blinking == 0x7F && (color & 0x80) && cursor_blink_state) {
                    glyph_pixels = 0;
                }

                if (cursor_active) {
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                    *output_buffer_16bit++ = palette_color[3];
                } else {
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                    glyph_pixels >>= 2;
                    *output_buffer_16bit++ = palette_color[glyph_pixels & 3];
                }

            }
            dma_channel_set_read_addr(dma_channel_control, output_buffer, false);
            return;
        }


    }

    if (screen_line % 2 && (graphics_mode != HERC_640x480x2_90 && graphics_mode != HERC_640x480x2)) return;
    uint32_t y = screen_line >> 1;

    if (screen_line >= 400) {
        dma_channel_set_read_addr(dma_channel_control, &lines_pattern[0], false); // TODO: ensue it is required
        return;
    }

    // Зона прорисовки изображения. Начальные точки буферов
    uint8_t *input_buffer_8bit = graphics_framebuffer + 0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
    // Индекс палитры в зависимости от настроек чередования строк и кадров
    uint16_t *current_palette = palette[(y & is_flash_line) + (frame_number & is_flash_frame) & 1];

    uint8_t *output_buffer_8bit;
    switch (graphics_mode) {
        case CGA_320x200x4:
        case CGA_320x200x4_BW: {
            //2bit buf
            for (int x = 320 / 4; x--;) {
                uint8_t cga_byte = *input_buffer_8bit++;

                uint8_t color = (cga_byte >> 6) & 3;
                *output_buffer_16bit++ = current_palette[color ? color : cga_foreground_color];
                color = (cga_byte >> 4) & 3;
                *output_buffer_16bit++ = current_palette[color ? color : cga_foreground_color];
                color = (cga_byte >> 2) & 3;
                *output_buffer_16bit++ = current_palette[color ? color : cga_foreground_color];
                color = (cga_byte >> 0) & 3;
                *output_buffer_16bit++ = current_palette[color ? color : cga_foreground_color];
            }
            break;
        }
        case CGA_640x200x2:
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;
            //1bit buf
            for (int x = 640 / 8; x--;) {
                uint8_t cga_byte = *input_buffer_8bit++;

                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 7) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 6) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 5) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 4) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 3) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 2) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 1) & 1), cga_foreground_color)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 0) & 1), cga_foreground_color)];
            }
            break;
        case HERC_640x480x2_90:
            if (screen_line >= 348) break;
        case HERC_640x480x2:
            //4bit buf
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;
            if (graphics_mode == HERC_640x480x2_90) {
                input_buffer_8bit = 5 + graphics_framebuffer + (screen_line & 3) * 8192 + __fast_mul((screen_line >> 2), 90);
            } else {
                input_buffer_8bit = graphics_framebuffer + (screen_line & 3) * 8192 + __fast_mul((screen_line >> 2), 80);
            }
            // Each byte containing 8 pixels
            for (int x = 640 / 8; x--;) {
                uint8_t cga_byte = *input_buffer_8bit++;

                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 7) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 6) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 5) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 4) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 3) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 2) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 1) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 0) & 1), 15)];
            }
            break;
        case COMPOSITE_160x200x16_force:
        case COMPOSITE_160x200x16:
        case TGA_160x200x16:
            input_buffer_8bit = tga_offset + graphics_framebuffer + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
            for (int x = 320 / 4; x--;) {
                uint8_t cga_byte = *input_buffer_8bit++; // Fetch 8 pixels from TGA memory
                uint8_t color1 = ((cga_byte >> 4) & 15);
                uint8_t color2 = (cga_byte & 15);

                if (!color1 && videomode == 0x8) color1 = cga_foreground_color;
                if (!color2 && videomode == 0x8) color2 = cga_foreground_color;

                *output_buffer_16bit++ = current_palette[color1];
                *output_buffer_16bit++ = current_palette[color1];
                *output_buffer_16bit++ = current_palette[color2];
                *output_buffer_16bit++ = current_palette[color2];
            }
            break;
        case TGA_320x200x16: {
            //4bit buf
            input_buffer_8bit = tga_offset + graphics_framebuffer + (y & 3) * 8192 + __fast_mul(y >> 2, 160);
            for (int x = 320 / 2; x--;) {
                *output_buffer_16bit++ = current_palette[*input_buffer_8bit >> 4 & 15];
                *output_buffer_16bit++ = current_palette[*input_buffer_8bit & 15];
                input_buffer_8bit++;
            }
            break;
        }
        case TGA_640x200x16: {
            //4bit buf
            input_buffer_8bit = graphics_framebuffer + __fast_mul(y, 320);
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;

            for (int x = 640 / 2; x--;) {
                *output_buffer_8bit++ = current_palette[*input_buffer_8bit >> 4 & 15];
                *output_buffer_8bit++ = current_palette[*input_buffer_8bit & 15];
                input_buffer_8bit++;
            }
            break;
        }
        case VGA_640x480x2:
            input_buffer_8bit = graphics_framebuffer + __fast_mul(y, 80);
            output_buffer_8bit = (uint8_t *) output_buffer_16bit;
            for (int x = 640 / 8; x--;) {
                //*output_buffer_16bit++=current_palette[*input_buffer_8bit++];
                uint8_t cga_byte = *input_buffer_8bit++;

                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 7) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 6) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 5) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 4) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 3) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 2) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 1) & 1), 15)];
                *output_buffer_8bit++ = current_palette[__fast_mul(((cga_byte >> 0) & 1), 15)];
            }
            break;
        case VGA_320x200x256:
            input_buffer_8bit = graphics_framebuffer + __fast_mul(y, 320);
            for (int x = 640 / 2; x--;) {
                *output_buffer_16bit++ = current_palette[*input_buffer_8bit++];
            }
            break;
        case VGA_320x200x256x4:
            input_buffer_8bit = graphics_framebuffer + vga_plane_offset + __fast_mul(y, 80);
            for (int x = 640 / 4; x--;) {
                *output_buffer_16bit++ = current_palette[input_buffer_8bit[0]];
                *output_buffer_16bit++ = current_palette[input_buffer_8bit[16000]];
                *output_buffer_16bit++ = current_palette[input_buffer_8bit[32000]];
                *output_buffer_16bit++ = current_palette[input_buffer_8bit[48000]];
                input_buffer_8bit++;
            }
            break;
            #if 0
        case EGA_320x200x16x4: {
            input_buffer_8bit = graphics_framebuffer + vga_plane_offset + __fast_mul(y, 40);
            for (int x = 0; x < 40; x++) {
                for (int bit = 7; bit >= 0; bit--) {
                    uint8_t color = (input_buffer_8bit[0] >> bit) & 1;
                    color |= ((input_buffer_8bit[8000] >> bit) & 1) << 1;
                    color |= ((input_buffer_8bit[16000] >> bit) & 1) << 2;
                    color |= ((input_buffer_8bit[24000] >> bit) & 1) << 3;
                    *output_buffer_16bit++ = current_palette[vga_graphics_control[7] & (1 << color) ? vga_graphics_control[0] : color];
                }
                input_buffer_8bit++;
            }
            break;
        }
            #endif
        default:
            input_buffer_8bit = graphics_framebuffer + __fast_mul(y, 320);
            for (int x = 640 / 2; x--;) {
                *output_buffer_16bit++ = current_palette[*input_buffer_8bit++];
            }
            break;
    }
    dma_channel_set_read_addr(dma_channel_control, output_buffer, false);
}

void graphics_set_mode(enum graphics_mode_t mode) {
    switch (mode) {
        case TEXTMODE_40x25_BW:
        case TEXTMODE_40x25_COLOR:
            text_buffer_width = 40;
            text_buffer_height = 30;
            break;
        case TEXTMODE_80x25_BW:
        case TEXTMODE_80x25_COLOR:
        default:
            text_buffer_width = 80;
            text_buffer_height = 30;
    }

    //memset(graphics_buffer, 0, graphics_buffer_height * graphics_buffer_width);
    if (_SM_VGA < 0) return; // если  VGA не инициализирована -

    graphics_mode = mode;

    // Если мы уже проиницилизированы - выходим
    if (txt_palette_init && lines_pattern_data) {
        return;
    };
    uint8_t TMPL_VHS8 = 0;
    uint8_t TMPL_VS8 = 0;
    uint8_t TMPL_HS8 = 0;
    uint8_t TMPL_LINE8 = 0;

    int line_size;
    double fdiv = 100;
    int HS_SIZE = 4;
    int HS_SHIFT = 100;

    switch (graphics_mode) {
        case TEXTMODE_40x25_BW:
        case TEXTMODE_40x25_COLOR:
        case TEXTMODE_80x25_BW:
        case TEXTMODE_80x25_COLOR:
            //текстовая палитра
            for (int i = 0; i < 16; i++) {
                txt_palette[i] = txt_palette[i] & 0x3f | palette16_mask >> 8;
            }

            if (!txt_palette_init) {
                //txt_palette_fast = (uint16_t *) calloc(256 * 4, sizeof(uint16_t));
                for (int i = 0; i < 256; i++) {
                    const uint8_t c1 = txt_palette[i & 0xf];
                    const uint8_t c0 = txt_palette[i >> 4];

                    txt_palette_fast[i * 4 + 0] = c0 | c0 << 8;
                    txt_palette_fast[i * 4 + 1] = c1 | c0 << 8;
                    txt_palette_fast[i * 4 + 2] = c0 | c1 << 8;
                    txt_palette_fast[i * 4 + 3] = c1 | c1 << 8;
                }
                txt_palette_init = true;
            }
        case CGA_640x200x2:
        case CGA_320x200x4:
        case CGA_320x200x4_BW:
        case HERC_640x480x2:
        case TGA_160x200x16:
        case TGA_640x200x16:
        case VGA_320x200x256:
        case VGA_640x480x2:
        case VGA_320x200x256x4:
        case EGA_320x200x16x4:
        case TGA_320x200x16:
        case COMPOSITE_160x200x16:
        case COMPOSITE_160x200x16_force:
            TMPL_LINE8 = 0b11000000;
            HS_SHIFT = 328 * 2;
            HS_SIZE = 48 * 2;

            line_size = 400 * 2;

            shift_picture = line_size - HS_SHIFT;

            palette16_mask = 0xc0c0;

            visible_line_size = 320;

            N_lines_total = 525;
            N_lines_visible = 480;
            line_VS_begin = 490;
            line_VS_end = 491;

            fdiv = clock_get_hz(clk_sys) / 25175000.0; //частота пиксельклока
            break;
        default:
            return;
    }

    //корректировка  палитры по маске бит синхры
    bg_color[0] = bg_color[0] & 0x3f3f3f3f | palette16_mask | palette16_mask << 16;
    bg_color[1] = bg_color[1] & 0x3f3f3f3f | palette16_mask | palette16_mask << 16;
    for (int i = 0; i < 256; i++) {
        palette[0][i] = palette[0][i] & 0x3f3f | palette16_mask;
        palette[1][i] = palette[1][i] & 0x3f3f | palette16_mask;
    }

    //инициализация шаблонов строк и синхросигнала
    if (!lines_pattern_data) //выделение памяти, если не выделено
    {
        const uint32_t div32 = (uint32_t) (fdiv * (1 << 16) + 0.0);
        PIO_VGA->sm[_SM_VGA].clkdiv = div32 & 0xfffff000; //делитель для конкретной sm
        dma_channel_set_trans_count(dma_channel_data, line_size / 4, false);

        lines_pattern_data = (uint32_t *) calloc(line_size * 4 / 4, sizeof(uint32_t));

        for (int i = 0; i < 4; i++) {
            lines_pattern[i] = &lines_pattern_data[i * (line_size / 4)];
        }
        // memset(lines_pattern_data,N_TMPLS*1200,0);
        TMPL_VHS8 = TMPL_LINE8 ^ 0b11000000;
        TMPL_VS8 = TMPL_LINE8 ^ 0b10000000;
        TMPL_HS8 = TMPL_LINE8 ^ 0b01000000;

        uint8_t *base_ptr = (uint8_t *) lines_pattern[0];
        //пустая строка
        memset(base_ptr, TMPL_LINE8, line_size);
        //memset(base_ptr+HS_SHIFT,TMPL_HS8,HS_SIZE);
        //выровненная синхра вначале
        memset(base_ptr, TMPL_HS8, HS_SIZE);

        // кадровая синхра
        base_ptr = (uint8_t *) lines_pattern[1];
        memset(base_ptr, TMPL_VS8, line_size);
        //memset(base_ptr+HS_SHIFT,TMPL_VHS8,HS_SIZE);
        //выровненная синхра вначале
        memset(base_ptr, TMPL_VHS8, HS_SIZE);

        //заготовки для строк с изображением
        base_ptr = (uint8_t *) lines_pattern[2];
        memcpy(base_ptr, lines_pattern[0], line_size);
        base_ptr = (uint8_t *) lines_pattern[3];
        memcpy(base_ptr, lines_pattern[0], line_size);
    }
}

void graphics_set_buffer(uint8_t *buffer, const uint16_t width, const uint16_t height) {
    graphics_framebuffer = buffer;
    graphics_buffer_width = width;
    graphics_buffer_height = height;
}


void graphics_set_offset(const int x, const int y) {
    framebuffer_offset_x = x;
    graphics_buffer_shift_y = y;
}

void graphics_set_flashmode(const bool flash_line, const bool flash_frame) {
    is_flash_frame = flash_frame;
    is_flash_line = flash_line;
}

void graphics_set_textbuffer(uint8_t *buffer) {
    text_buffer = buffer;
}

void graphics_set_bgcolor(const uint32_t color888) {
    const uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
    const uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };

    const uint8_t b = (color888 & 0xff) / 42;

    const uint8_t r = (color888 >> 16 & 0xff) / 42;
    const uint8_t g = (color888 >> 8 & 0xff) / 42;

    const uint8_t c_hi = conv0[r] << 4 | conv0[g] << 2 | conv0[b];
    const uint8_t c_lo = conv1[r] << 4 | conv1[g] << 2 | conv1[b];
    bg_color[0] = ((c_hi << 8 | c_lo) & 0x3f3f | palette16_mask) << 16 |
                  ((c_hi << 8 | c_lo) & 0x3f3f | palette16_mask);
    bg_color[1] = ((c_lo << 8 | c_hi) & 0x3f3f | palette16_mask) << 16 |
                  ((c_lo << 8 | c_hi) & 0x3f3f | palette16_mask);
}

void graphics_set_palette(const uint8_t i, const uint32_t color888) {
    const uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
    const uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };

    const uint8_t b = (color888 & 0xff) / 42;

    const uint8_t r = (color888 >> 16 & 0xff) / 42;
    const uint8_t g = (color888 >> 8 & 0xff) / 42;

    const uint8_t c_hi = conv0[r] << 4 | conv0[g] << 2 | conv0[b];
    const uint8_t c_lo = conv1[r] << 4 | conv1[g] << 2 | conv1[b];

    palette[0][i] = (c_hi << 8 | c_lo) & 0x3f3f | palette16_mask;
    palette[1][i] = (c_lo << 8 | c_hi) & 0x3f3f | palette16_mask;
}

void graphics_init() {
    //инициализация палитры по умолчанию
#if 1
    const uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
    const uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };
    for (int i = 0; i < 256; i++) {
        const uint8_t b = i & 0b11;
        const uint8_t r = i >> 5 & 0b111;
        const uint8_t g = i >> 2 & 0b111;

        const uint8_t c_hi = 0xc0 | conv0[r] << 4 | conv0[g] << 2 | b;
        const uint8_t c_lo = 0xc0 | conv1[r] << 4 | conv1[g] << 2 | b;

        palette[0][i] = c_hi << 8 | c_lo;
        palette[1][i] = c_lo << 8 | c_hi;
    }
#endif
    //текстовая палитра
    for (int i = 0; i < 16; i++) {
        const uint8_t b = i & 1 ? (i >> 3 ? 3 : 2) : 0;
        const uint8_t r = i & 4 ? (i >> 3 ? 3 : 2) : 0;
        const uint8_t g = i & 2 ? (i >> 3 ? 3 : 2) : 0;

        const uint8_t c = r << 4 | g << 2 | b;

        txt_palette[i] = c & 0x3f | 0xc0;
    }
    //инициализация PIO
    //загрузка программы в один из PIO
    const uint offset = pio_add_program(PIO_VGA, &pio_program_VGA);
    _SM_VGA = pio_claim_unused_sm(PIO_VGA, true);
    const uint sm = _SM_VGA;

    for (int i = 0; i < 8; i++) {
        gpio_init(VGA_BASE_PIN + i);
        gpio_set_dir(VGA_BASE_PIN + i, GPIO_OUT);
        pio_gpio_init(PIO_VGA, VGA_BASE_PIN + i);
    }; //резервируем под выход PIO

    //pio_sm_config c = pio_vga_program_get_default_config(offset);

    pio_sm_set_consecutive_pindirs(PIO_VGA, sm, VGA_BASE_PIN, 8, true); //конфигурация пинов на выход

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + 0, offset + (pio_program_VGA.length - 1));

    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX); //увеличение буфера TX за счёт RX до 8-ми
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_out_pins(&c, VGA_BASE_PIN, 8);
    pio_sm_init(PIO_VGA, sm, offset, &c);

    pio_sm_set_enabled(PIO_VGA, sm, true);

    //инициализация DMA
    dma_channel_control = dma_claim_unused_channel(true);
    dma_channel_data = dma_claim_unused_channel(true);
    //основной ДМА канал для данных
    dma_channel_config c0 = dma_channel_get_default_config(dma_channel_data);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);

    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);

    uint dreq = DREQ_PIO1_TX0 + sm;
    if (PIO_VGA == pio0) dreq = DREQ_PIO0_TX0 + sm;

    channel_config_set_dreq(&c0, dreq);
    channel_config_set_chain_to(&c0, dma_channel_control); // chain to other channel

    dma_channel_configure(
            dma_channel_data,
            &c0,
            &PIO_VGA->txf[sm], // Write address
            lines_pattern[0], // read address
            600 / 4, //
            false // Don't start yet
    );
    //канал DMA для контроля основного канала
    dma_channel_config c1 = dma_channel_get_default_config(dma_channel_control);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);

    channel_config_set_read_increment(&c1, false);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_chain_to(&c1, dma_channel_data); // chain to other channel
    //channel_config_set_dreq(&c1, DREQ_PIO0_TX0);

    dma_channel_configure(
            dma_channel_control,
            &c1,
            &dma_hw->ch[dma_channel_data].read_addr, // Write address
            &lines_pattern[0], // read address
            1, //
            false // Don't start yet
    );
    //dma_channel_set_read_addr(dma_chan, &DMA_BUF_ADDR[0], false);

    graphics_set_mode(TGA_320x200x16);

    irq_set_exclusive_handler(VGA_DMA_IRQ, dma_handler_VGA);

    dma_channel_set_irq0_enabled(dma_channel_control, true);

    irq_set_enabled(VGA_DMA_IRQ, true);
    dma_start_channel_mask(1u << dma_channel_data);
}


void clrScr(const uint8_t color) {
    uint16_t *t_buf = (uint16_t *) text_buffer;
    int size = TEXTMODE_COLS * TEXTMODE_ROWS;

    while (size--) *t_buf++ = color << 4 | ' ';
}
