#include <string.h>

#include "graphics.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

#define SCREEN_WIDTH (320)
#define SCREEN_HEIGHT (240)

//текстовый буфер
uint8_t* text_buffer = NULL;

//программы PIO

//программа конвертации адреса для TV_OUT
uint16_t pio_program_instructions_conv_TV[] = {
    //	 .wrap_target
    0x80a0, //  0: pull   block
    0x40e8, //  1: in	 osr, 8
    0x4037, //  2: in	 x, 23
    0x8020, //  3: push   block
    //	 .wrap
};

const struct pio_program pio_program_conv_addr_TV = {
    .instructions = pio_program_instructions_conv_TV,
    .length = 4,
    .origin = -1,
};

//программа видеовывода VGA
static uint16_t pio_program_TV_instructions[] = {
    //	 .wrap_target
    0x6008, //  0: out	pins, 8
    //	 .wrap
};

static const struct pio_program program_pio_TV = {
    .instructions = pio_program_TV_instructions,
    .length = 1,
    .origin = -1,
};

typedef struct {
    int H_len;
    int begin_img_shx;
    int img_size_x;

    int N_lines;

    int sync_size;
    uint8_t SYNC_TMPL;
    uint8_t NO_SYNC_TMPL;

    double CLK_SPD;
} TV_MODE;

typedef struct {
    uint width;
    uint height;
    int shift_x;
    int shift_y;
    uint8_t* data;
} graphics_buffer_t;


//режим видеовыхода
static TV_MODE v_mode = {
    .H_len = 512,
    .N_lines = 525,
    .SYNC_TMPL = 241,
    .NO_SYNC_TMPL = 240,
    .CLK_SPD = 31500000.0
};

static graphics_buffer_t graphics_buffer = {
    .data = NULL,
    .shift_x = 0,
    .shift_y = 0,
    .width = SCREEN_WIDTH,
    .height = SCREEN_HEIGHT,
};

//буферы строк
//количество буферов задавать кратно степени двойки
//
#define N_LINE_BUF_log2 (2)
#define N_LINE_BUF_DMA (1<<N_LINE_BUF_log2)
#define N_LINE_BUF (N_LINE_BUF_DMA)

//максимальный размер строки
#define LINE_SIZE_MAX (512)

//указатели на буферы строк
//выравнивание нужно для кольцевого буфера
static uint32_t rd_addr_DMA_CTRL[N_LINE_BUF * 2]__attribute__ ((aligned (4*N_LINE_BUF_DMA)));
//непосредственно буферы строк

static uint32_t lines_buf[N_LINE_BUF][LINE_SIZE_MAX / 4];


static int SM_video = -1;
static int SM_conv = -1;


//DMA каналы
//каналы работы с первичным графическим буфером
static int dma_chan_ctrl = -1;
static int dma_chan = -1;
//каналы работы с конвертацией палитры
static int dma_chan_pal_conv_ctrl = -1;
static int dma_chan_pal_conv = -1;

//ДМА палитра для конвертации
#if PICO_RP2350
static __aligned(512) uint32_t conv_color[128];
#else
static __aligned(512) __scratch_x("palette_conv") uint32_t conv_color[128];
#endif

static enum graphics_mode_t graphics_mode;
static output_format_e active_output_format;
static repeating_timer_t video_timer;


//программа установки начального адреса массива-конвертора
static void pio_set_x(PIO pio, const int sm, const uint32_t v) {
    const uint instr_shift = pio_encode_in(pio_x, 4);
    const uint instr_mov = pio_encode_mov(pio_x, pio_isr);
    for (int i = 0; i < 8; i++) {
        const uint32_t nibble = v >> i * 4 & 0xf;
        pio_sm_exec(pio, sm, pio_encode_set(pio_x, nibble));
        pio_sm_exec(pio, sm, instr_shift);
    }
    pio_sm_exec(pio, sm, instr_mov);
}


//определение палитры
void graphics_set_palette(uint8_t i, uint32_t color888) {
    if (i >= 240) return;
    uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
    uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };

    uint8_t B = (color888 & 0xff) / 42;
    uint8_t G = (color888 >> 8 & 0xff) / 42;
    uint8_t R = (color888 >> 16 & 0xff) / 42;

    uint8_t c_hi = conv0[R] << 4 | conv0[G] << 2 | conv0[B];
    uint8_t c_lo = conv1[R] << 4 | conv1[G] << 2 | conv1[B];

    uint16_t palette16_mask = 0xc0 << 8 | 0xc0;

    uint16_t* conv_color16 = (uint16_t *)conv_color;
    conv_color16[i] = (c_hi << 8 | c_lo) & 0x3f3f | palette16_mask;
}


//основная функция заполнения буферов видеоданных
#if PICO_RP2350
static void __not_in_flash("tv_main_loop") main_video_loopTV() {
#else
static void __scratch_x("tv_main_loop") main_video_loopTV() {
#endif
    static uint dma_inx_out = 0;
    static uint lines_buf_inx = 0;

    if (dma_chan_ctrl == -1) return; //не определен дма канал

    //получаем индекс выводимой строки
    uint dma_inx = (N_LINE_BUF_DMA - 2 + (dma_channel_hw_addr(dma_chan_ctrl)->read_addr - (uint32_t)rd_addr_DMA_CTRL) /
                    4) % (N_LINE_BUF_DMA);

    //uint n_loop=(N_LINE_BUF_DMA+dma_inx-dma_inx_out)%N_LINE_BUF_DMA;

    static uint32_t line_active = 0;
    static uint8_t* input_buffer = NULL;
    static uint32_t frame_i = 0;

    //while(n_loop--)
    while (dma_inx_out != dma_inx) {
        //режим VGA
        line_active++;
        if (line_active == v_mode.N_lines) {
            line_active = 0;
            frame_i++;
            input_buffer = graphics_buffer.data;
        }

        lines_buf_inx = (lines_buf_inx + 1) % N_LINE_BUF;
        uint8_t* output_buffer = (uint8_t *)lines_buf[lines_buf_inx];

        bool is_line_visible = true;

        // if (false)
        switch (active_output_format) {
            case TV_OUT_PAL:
                switch (line_active) {
                    case 0:
                    case 1:
                        //|___|--|___|--| type=1
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        is_line_visible = false;
                        break;

                    case 2:
                        // ____|--|_|----type=2
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 3:
                    case 4: //|_|----|_|---- type=0
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;

                    case 5: break; //шаблон как у видимой строки, но без изображения


                    case 310:
                    case 311:
                        //|_|----|_|---- type=0
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 312:
                        //|_|---|____|--| type=3
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 313:
                    case 314:
                        //|___|--|___|--| type=1
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 315:
                    case 316:
                        //|_|----|_|---- type=0
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 317:
                        //|_|---------type=4
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 622:
                        //|__|---|_|----type=5
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                }
                break;
            case TV_OUT_NTSC:
                switch (line_active) {
                    case 0:
                    case 1:
                    case 2:
                        //|_|----|_|---- type=0
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 3:
                    case 4:
                    case 5:
                        //|___|--|___|--| type=1
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 6:
                    case 7:
                    case 8:
                        //|_|----|_|---- type=0
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;

                    case 262:
                        //|__|---|_|----type=5
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 263:
                    case 264:
                        //|_|----|_|---- type=0
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 265:

                        //|_|---|____|--| type=3
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 266:
                    case 267:
                        //|___|--|___|--| type=1
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 268:

                        // ____|--|_|----type=2
                        memset(output_buffer, v_mode.SYNC_TMPL, (v_mode.H_len / 2) - v_mode.sync_size);
                        output_buffer += (v_mode.H_len / 2) - v_mode.sync_size;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.sync_size);
                        output_buffer += v_mode.sync_size;
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 269:
                    case 270:
                        //|_|----|_|---- type=0
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        output_buffer += (v_mode.H_len / 2) - (v_mode.sync_size / 2);
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len / 2) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 271:

                        //|_|---------type=4
                        memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size / 2);
                        output_buffer += v_mode.sync_size / 2;
                        memset(output_buffer, v_mode.NO_SYNC_TMPL, (v_mode.H_len) - (v_mode.sync_size / 2));
                        is_line_visible = false;
                        break;


                    default:
                        break;
                }


                break;
        }

        //ТВ строка с изображением
        if (is_line_visible) {
            memset(output_buffer, v_mode.SYNC_TMPL, v_mode.sync_size);
            memset(output_buffer + v_mode.sync_size, v_mode.NO_SYNC_TMPL, v_mode.begin_img_shx - v_mode.sync_size);
            int post_img_clear = 32;
            memset(output_buffer + (v_mode.H_len - post_img_clear), v_mode.NO_SYNC_TMPL, post_img_clear);
            output_buffer += v_mode.begin_img_shx;

            int y = -1;
            switch (active_output_format) {
                case TV_OUT_PAL:
                    if ((line_active > 4) && (line_active < 310)) { y = line_active - 23; };
                    if ((line_active > 317) && (line_active < 622)) { y = line_active - 335; };
                    y -= 24;
                    break;
                case TV_OUT_NTSC:
                    if ((line_active > 8) && (line_active < 262)) { y = line_active - 20; };
                    if ((line_active > 271)) { y = line_active - 282; };
                    break;
            }

            if (y >= 240 || y < 0 || input_buffer == NULL) {
                //вне изображения
                memset(output_buffer, v_mode.NO_SYNC_TMPL, v_mode.H_len - v_mode.begin_img_shx);
            }
            else {
                //зона изображения
                if (active_output_format == TV_OUT_PAL) output_buffer += 33;

                /*if (active_mode == g_mode_320x240x4bpp) {
                    uint8_t* vbuf8 = vbuf + (line_inx) * g_buf.width / 2;
                    if (vbuf != NULL)
                        for (int i = g_buf.width / 16; i--;) {
                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;
                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;
                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;
                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;

                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;
                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;
                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;
                            *out_buf8++ = *vbuf8 & 0x0f;
                            *out_buf8++ = *vbuf8++ >> 4;
                        }
                }
                else*/
                if (graphics_buffer.data)
                switch (graphics_mode) {
                    default:
                    case GRAPHICSMODE_DEFAULT: {
                        //для 8-битного буфера
                        uint8_t* input_buffer8 = input_buffer + y * graphics_buffer.width;
                        if (input_buffer != NULL) {
                            // TODO: shift_y, background_color
                            for (uint x = graphics_buffer.shift_x; x--;) {
                                *output_buffer++ = 200;
                            }

                            for (uint x = graphics_buffer.width; x--;) {
                                *output_buffer++ = *input_buffer8 < 240 ? *input_buffer8 : 0;
                                input_buffer8++;
                            }

                            for (uint x = graphics_buffer.shift_x; x--;) {
                                *output_buffer++ = 200;
                            }
                        }
                        break;
                    }
                    case TEXTMODE_DEFAULT: {
                        *output_buffer++ = 200;

                        for (int x = 0; x < TEXTMODE_COLS; x++) {
                            const uint16_t offset = y / 8 * (TEXTMODE_COLS * 2) + x * 2;
                            const uint8_t c = text_buffer[offset];
                            const uint8_t colorIndex = text_buffer[offset + 1];
                            uint8_t glyph_row = font_6x8[c * 8 + y % 8];

                            for (int bit = 6; bit--;) {
                                *output_buffer++ = glyph_row & 1
                                                       ? textmode_palette[colorIndex & 0xf] //цвет шрифта
                                                       : textmode_palette[colorIndex >> 4]; //цвет фона

                                glyph_row >>= 1;
                            }
                        }
                        *output_buffer = 200;
                        break;
                    }
                }
            }
            // //test
            // memset(out_buf8,0,v_mode.H_len-v_mode.begin_img_shx);
            // for(int i=0;i<240;i++) *out_buf8++=i;
        }

        // if (line_active<hw_mode.V_visible_lines){
        // 	//зона изображения
        // 	//if (false)
        // 	switch (active_mode){
        // 		case g_mode_320x240x4bpp:
        // 		case g_mode_320x240x8bpp:
        // 			//320x240 графика
        // 			if (line_active&1){
        // 				//повтор шаблона строки
        // 			} else {
        // 				//новая строка
        // 				lines_buf_inx=(lines_buf_inx+1)%N_LINE_BUF;
        // 				uint8_t* out_buf8=(uint8_t*)lines_buf[lines_buf_inx];
        // 				//зона синхры, затираем все шаблоны
        // 				if(line_active<(N_LINE_BUF_DMA*2)){
        // 					memset(out_buf8,hw_mode.HS_TMPL,hw_mode.HS_len);
        // 					memset(out_buf8+hw_mode.HS_len,hw_mode.NO_SYNC_TMPL,hw_mode.H_visible_begin-hw_mode.HS_len);
        // 					memset(out_buf8+hw_mode.H_visible_begin+hw_mode.H_visible_len,hw_mode.NO_SYNC_TMPL,hw_mode.H_len-(hw_mode.H_visible_begin+hw_mode.H_visible_len));
        // 				}
        // 				//формирование картинки
        // 				int line=line_active/2;
        // 				out_buf8+=hw_mode.H_visible_begin;
        // 				uint8_t* vbuf8;
        // 				//для 4-битного буфера
        // 				if (active_mode==g_mode_320x240x4bpp){
        // 					vbuf8=vbuf+(line)*g_buf.width/2;
        // 					if (vbuf!=NULL)
        // 						for(int i=hw_mode.H_visible_len/16;i--;){
        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;
        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;
        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;
        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;

        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;
        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;
        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;
        // 							*out_buf8++=*vbuf8&0x0f;
        // 							*out_buf8++=*vbuf8++>>4;
        // 						}
        // 				} else {
        // 				//для 8-битного буфера
        // 					vbuf8=vbuf+(line)*g_buf.width;
        // 					if (vbuf!=NULL)
        // 						for(int i=hw_mode.H_visible_len/16;i--;){
        // 							// *out_buf8++=100;
        // 							//	*out_buf8++=(i<240)?i:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;

        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 							*out_buf8++=(*vbuf8<240)?*vbuf8:0;vbuf8++;
        // 						}
        // 				}
        // 			}
        // 			break;

        // 		default:
        // 			break;
        // 	}
        // } else {
        // 	if((line_active>=hw_mode.VS_begin)&&(line_active<=hw_mode.VS_end)){//кадровый синхроимпульс
        // 		if (line_active==hw_mode.VS_begin){
        // 			//новый шаблон
        // 			lines_buf_inx=(lines_buf_inx+1)%N_LINE_BUF;
        // 			uint8_t* out_buf8=(uint8_t*)lines_buf[lines_buf_inx];
        // 			memset(out_buf8,hw_mode.VHS_TMPL,hw_mode.HS_len);
        // 			memset(out_buf8+hw_mode.HS_len,hw_mode.VS_TMPL,hw_mode.H_len-hw_mode.HS_len);

        // 		} else {
        // 			//повтор шаблона
        // 		}
        // 	} else {
        // 		//строчный синхроимпульс
        // 		if((line_active==(hw_mode.VS_end+1))||(line_active==(hw_mode.V_visible_lines))){
        // 			//новый шаблон
        // 			lines_buf_inx=(lines_buf_inx+1)%N_LINE_BUF;
        // 			uint8_t* out_buf8=(uint8_t*)lines_buf[lines_buf_inx];
        // 			memset(out_buf8,hw_mode.HS_TMPL,hw_mode.HS_len);
        // 			memset(out_buf8+hw_mode.HS_len,hw_mode.NO_SYNC_TMPL,hw_mode.H_len-hw_mode.HS_len);
        // 		} else {
        // 			//повтор шаблона
        // 		}
        // 	}
        // }

        rd_addr_DMA_CTRL[dma_inx_out] = (uint32_t)&lines_buf[lines_buf_inx];
        //включаем заполненный буфер в данные для вывода
        dma_inx_out = (dma_inx_out + 1) % (N_LINE_BUF_DMA);
        dma_inx = (N_LINE_BUF_DMA - 2 + (dma_channel_hw_addr(dma_chan_ctrl)->read_addr - (uint32_t)rd_addr_DMA_CTRL) /
                   4) % (N_LINE_BUF_DMA);
    }
}

void graphics_set_buffer(uint8_t* buffer, const uint16_t width, const uint16_t height) {
    graphics_buffer.data = buffer;
    graphics_buffer.height = height;
    graphics_buffer.width = width;
}

void graphics_set_textbuffer(uint8_t* buffer) {
    text_buffer = buffer;
};

void graphics_set_offset(const int x, const int y) {
    graphics_buffer.shift_x = x;
    graphics_buffer.shift_y = y;
};

static bool __not_in_flash_func(video_timer_callbackTV(repeating_timer_t *rt)) {
    main_video_loopTV();
    return true;
}


//выделение и настройка общих ресурсов - 4 DMA канала, PIO программ и 2 SM
void tv_init(const output_format_e output_format) {
    active_output_format = output_format;

    switch (active_output_format) {
        case TV_OUT_NTSC:
            v_mode.CLK_SPD = 2 * 3.1 * 1e6;
            v_mode.N_lines = 525;


            v_mode.H_len = v_mode.CLK_SPD / 1e6 * 63.9;
            v_mode.H_len &= 0xfffffffc;

            v_mode.sync_size = 4.7 * v_mode.H_len / 64;
            v_mode.begin_img_shx = 10.5 * v_mode.H_len / 64;
            v_mode.img_size_x = v_mode.H_len - 12 * v_mode.H_len / 64;
            v_mode.img_size_x &= 0xfffffffc;

            break;

        case TV_OUT_PAL:
            v_mode.CLK_SPD = 2 * 3.733333333 * 1e6;

            v_mode.N_lines = 625;


            v_mode.H_len = v_mode.CLK_SPD / 1e6 * 63.9;
            v_mode.H_len &= 0xfffffffc;

            v_mode.sync_size = 4.7 * v_mode.H_len / 64;
            v_mode.begin_img_shx = 10.5 * v_mode.H_len / 64;
            v_mode.img_size_x = v_mode.H_len - 12 * v_mode.H_len / 64;
            v_mode.img_size_x &= 0xfffffffc;

            break;
    }

    //настройка PIO
    SM_video = pio_claim_unused_sm(PIO_VIDEO, true);
    SM_conv = pio_claim_unused_sm(PIO_VIDEO_ADDR, true);
    //выделение  DMA каналов
    dma_chan_ctrl = dma_claim_unused_channel(true);
    dma_chan = dma_claim_unused_channel(true);
    dma_chan_pal_conv_ctrl = dma_claim_unused_channel(true);
    dma_chan_pal_conv = dma_claim_unused_channel(true);

    //заполнение палитры по умолчанию(ч.б.)
    for (int ci = 0; ci < 240; ci++) graphics_set_palette(ci, (ci << 16) | (ci << 8) | ci); //

    //---------------

    uint offs_prg0 = 0;
    uint offs_prg1 = 0;
    const int base_inx = 240;

    offs_prg1 = pio_add_program(PIO_VIDEO_ADDR, &pio_program_conv_addr_TV);
    offs_prg0 = pio_add_program(PIO_VIDEO, &program_pio_TV);
    pio_set_x(PIO_VIDEO_ADDR, SM_conv, (uint32_t)conv_color >> 9);
    uint16_t* conv_color16 = (uint16_t *)conv_color;

    conv_color16[base_inx] = 0b1100000011000000; //нет синхры
    conv_color16[base_inx + 1] = 0b1000000010000000; //есть синхра


    //настройка PIO SM для конвертации

    pio_sm_config c_c = pio_get_default_sm_config();

    sm_config_set_wrap(&c_c, offs_prg1, offs_prg1 + (pio_program_conv_addr_TV.length - 1));
    sm_config_set_in_shift(&c_c, true, false, 32);

    pio_sm_init(PIO_VIDEO_ADDR, SM_conv, offs_prg1, &c_c);
    pio_sm_set_enabled(PIO_VIDEO_ADDR, SM_conv, true);

    //настройка PIO SM для вывода данных
    c_c = pio_get_default_sm_config();


    //настройка рабочей SM TV
    sm_config_set_wrap(&c_c, offs_prg0, offs_prg0 + (program_pio_TV.length - 1));
    for (int i = 0; i < 8; i++) {
        gpio_set_slew_rate(TV_BASE_PIN + i, GPIO_SLEW_RATE_FAST);
        pio_gpio_init(PIO_VIDEO, TV_BASE_PIN + i);
        gpio_set_drive_strength(TV_BASE_PIN + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(TV_BASE_PIN + i, GPIO_SLEW_RATE_FAST);
    }
    pio_sm_set_consecutive_pindirs(PIO_VIDEO, SM_video, TV_BASE_PIN, 8, true); //конфигурация пинов на выход
    sm_config_set_out_pins(&c_c, TV_BASE_PIN, 8);

    sm_config_set_out_shift(&c_c, true, true, 16);
    sm_config_set_fifo_join(&c_c, PIO_FIFO_JOIN_TX);

    sm_config_set_clkdiv(&c_c, clock_get_hz(clk_sys) / (2 * v_mode.CLK_SPD));
    pio_sm_init(PIO_VIDEO, SM_video, offs_prg0, &c_c);
    pio_sm_set_enabled(PIO_VIDEO, SM_video, true);

    //настройки DMA

    //основной рабочий канал
    dma_channel_config cfg_dma = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg_dma, DMA_SIZE_8);
    channel_config_set_chain_to(&cfg_dma, dma_chan_ctrl); // chain to other channel

    channel_config_set_read_increment(&cfg_dma, true);
    channel_config_set_write_increment(&cfg_dma, false);


    uint dreq = DREQ_PIO1_TX0 + SM_conv;
    if (PIO_VIDEO_ADDR == pio0) dreq = DREQ_PIO0_TX0 + SM_conv;
    channel_config_set_dreq(&cfg_dma, dreq);

    dma_channel_configure(
        dma_chan,
        &cfg_dma,
        &PIO_VIDEO_ADDR->txf[SM_conv], // Write address
        lines_buf[0], // read address
        v_mode.H_len / 1, //
        false // Don't start yet
    );

    //контрольный канал для основного
    cfg_dma = dma_channel_get_default_config(dma_chan_ctrl);
    channel_config_set_transfer_data_size(&cfg_dma, DMA_SIZE_32);
    channel_config_set_chain_to(&cfg_dma, dma_chan); // chain to other channel

    channel_config_set_read_increment(&cfg_dma, true);
    channel_config_set_write_increment(&cfg_dma, false);
    channel_config_set_ring(&cfg_dma,false, 2 + N_LINE_BUF_log2);


    dma_channel_configure(
        dma_chan_ctrl,
        &cfg_dma,
        &dma_hw->ch[dma_chan].read_addr, // Write address
        rd_addr_DMA_CTRL, // read address
        1, //
        false // Don't start yet
    );

    //канал - конвертер палитры
    cfg_dma = dma_channel_get_default_config(dma_chan_pal_conv);

    const int n_trans_data = 1;

    channel_config_set_transfer_data_size(&cfg_dma, DMA_SIZE_16);


    channel_config_set_chain_to(&cfg_dma, dma_chan_pal_conv_ctrl); // chain to other channel

    channel_config_set_read_increment(&cfg_dma, false);
    channel_config_set_write_increment(&cfg_dma, false);

    dreq = DREQ_PIO1_TX0 + SM_video;
    if (PIO_VIDEO == pio0) dreq = DREQ_PIO0_TX0 + SM_video;
    channel_config_set_dreq(&cfg_dma, dreq);

    dma_channel_configure(
        dma_chan_pal_conv,
        &cfg_dma,
        &PIO_VIDEO->txf[SM_video], // Write address
        &conv_color[0], // read address
        n_trans_data, //
        false // Don't start yet
    );

    //канал управления конвертером палитры

    cfg_dma = dma_channel_get_default_config(dma_chan_pal_conv_ctrl);
    channel_config_set_transfer_data_size(&cfg_dma, DMA_SIZE_32);
    channel_config_set_chain_to(&cfg_dma, dma_chan_pal_conv); // chain to other channel

    channel_config_set_read_increment(&cfg_dma, false);
    channel_config_set_write_increment(&cfg_dma, false);

    dreq = DREQ_PIO1_RX0 + SM_conv;
    if (PIO_VIDEO_ADDR == pio0) dreq = DREQ_PIO0_RX0 + SM_conv;

    channel_config_set_dreq(&cfg_dma, dreq);

    dma_channel_configure(
        dma_chan_pal_conv_ctrl,
        &cfg_dma,
        &dma_hw->ch[dma_chan_pal_conv].read_addr, // Write address
        &PIO_VIDEO_ADDR->rxf[SM_conv], // read address
        1, //
        true // start yet
    );


    dma_start_channel_mask(1u << dma_chan_ctrl);

    int hz = 50000;
    if (!add_repeating_timer_us(1000000 / hz, video_timer_callbackTV, NULL, &video_timer)) {
        return;
    }
};


void graphics_init() {
    tv_init(TV_OUT_PAL);

    // FIXME сделать конфигурацию пользователем
    graphics_set_palette(200, RGB888(0x00, 0x00, 0x00)); //black
    graphics_set_palette(201, RGB888(0x00, 0x00, 0xC4)); //blue
    graphics_set_palette(202, RGB888(0x00, 0xC4, 0x00)); //green
    graphics_set_palette(203, RGB888(0x00, 0xC4, 0xC4)); //cyan
    graphics_set_palette(204, RGB888(0xC4, 0x00, 0x00)); //red
    graphics_set_palette(205, RGB888(0xC4, 0x00, 0xC4)); //magenta
    graphics_set_palette(206, RGB888(0xC4, 0x7E, 0x00)); //brown
    graphics_set_palette(207, RGB888(0xC4, 0xC4, 0xC4)); //light gray
    graphics_set_palette(208, RGB888(0x4E, 0x4E, 0x4E)); //dark gray
    graphics_set_palette(209, RGB888(0x4E, 0x4E, 0xDC)); //light blue
    graphics_set_palette(210, RGB888(0x4E, 0xDC, 0x4E)); //light green
    graphics_set_palette(211, RGB888(0x4E, 0xF3, 0xF3)); //light cyan
    graphics_set_palette(212, RGB888(0xDC, 0x4E, 0x4E)); //light red
    graphics_set_palette(213, RGB888(0xF3, 0x4E, 0xF3)); //light magenta
    graphics_set_palette(214, RGB888(0xF3, 0xF3, 0x4E)); //yellow
    graphics_set_palette(215, RGB888(0xFF, 0xFF, 0xFF)); //white
}

void clrScr(const uint8_t color) {
    if (text_buffer)
        memset(text_buffer, color, TEXTMODE_COLS * TEXTMODE_ROWS * 2);
}


void graphics_set_mode(const enum graphics_mode_t mode) {
    graphics_mode = mode;
    clrScr(0);
}
