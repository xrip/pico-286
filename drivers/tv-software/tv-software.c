#
//программный композит
#include <stdio.h>
#include "graphics.h"
#include "hardware/clocks.h"
#include <stdalign.h>
#include "emulator/emulator.h"
#include "graphics.h"

#include "hardware/structs/pll.h"
#include "hardware/structs/systick.h"

#include "hardware/dma.h"
#include "hardware/irq.h"
#include <string.h>
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "stdlib.h"
#pragma GCC optimize("Ofast")
uint8_t* text_buffer = NULL;


typedef struct tv_out_mode_t {
    // double color_freq;
    float color_index;
    COLOR_FREQ_t c_freq;
    enum graphics_mode_t mode_bpp;
    g_out_TV_t tv_system;
    NUM_TV_LINES_t N_lines;
    bool cb_sync_PI_shift_lines;
    bool cb_sync_PI_shift_half_frame;
} tv_out_mode_t;

//параметры по умолчанию
static tv_out_mode_t tv_out_mode;


//программы PIO
//программа видеовывода
static uint16_t pio_program_TV_instructions[] = {
    //	 .wrap_target

    //	 .wrap_target
    0x6008, //  0: out	pins, 8
    //	 .wrap
    //	 .wrap
};

static const struct pio_program program_pio_TV = {
    .instructions = pio_program_TV_instructions,
    .length = 1,
    .origin = -1,
};

typedef struct TV_MODE {
    int H_len;
    int begin_img_shx;
    int img_W;

    int N_lines;

    int sync_size;
    uint8_t SYNC_TMPL;
    uint8_t NO_SYNC_TMPL;
    uint8_t LVL_C_MAX;
    uint8_t LVL_BLACK;
    uint8_t LVL_BLACK_TMPL;
    uint8_t LVL_Y_MAX;
} TV_MODE;

typedef struct G_BUFFER {
    uint width;
    uint height;
    int shift_x;
    int shift_y;
    uint8_t* data;
} G_BUFFER;


//режим видеовыхода
static TV_MODE video_mode = {
    .H_len = 512,
    .N_lines = 525,
    .SYNC_TMPL = 0,
    .NO_SYNC_TMPL = 0,
    .LVL_C_MAX = 0,
    .LVL_BLACK = 0,
    .LVL_Y_MAX = 0,
};


static G_BUFFER graphics_buffer = {
    .data = NULL,
    .shift_x = 0,
    .shift_y = 0,
    .height = 240,
    .width = 320
};


//пины
//пин синхросигнала(для совместимости с RGB по ч.б.) 0-7
#define SYNC_PIN (6)
//максимальное значение DAC
#define MAX_DAC (63)
//перераспределение порядка пинов(для совместимости с RGB по ч.б.)
#define CONV_DAC(x) ((((x)<<2)&0x30)|(((x)>>2)&0x0c)|((x)&0x03))

//буферы строк
//количество буферов задавать кратно степени двойки
//например 2^2=4 буфера
#define N_LINE_BUF_log2 (1)


#define N_LINE_BUF_DMA (1<<N_LINE_BUF_log2)
#define N_LINE_BUF (N_LINE_BUF_DMA)

//максимальный размер строки(кратно 4)
#define LINE_SIZE_MAX (1024)
//указатели на буферы строк
//выравнивание нужно для кольцевого буфера
static uint32_t rd_addr_DMA_CTRL[N_LINE_BUF * 2]__attribute__ ((aligned (4*N_LINE_BUF_DMA)));
static uint32_t transfer_count_DMA_CTRL[N_LINE_BUF * 2]__attribute__ ((aligned (4*N_LINE_BUF_DMA)));
//непосредственно буферы строк

static uint32_t lines_buf[N_LINE_BUF][LINE_SIZE_MAX / 4];


static int SM_video = -1;


//DMA каналы
//каналы работы с первичным графическим буфером
static int dma_chan_ctrl = -1;
static int dma_chan_ctrl2 = -1;
static int dma_chan = -1;


//ДМА палитра для конвертации(256 знач)
static uint32_t conv_colorNORM[2][256]; //2к
static uint32_t conv_colorINV[2][256]; //2к
static uint32_t* conv_color[2];

//палитра сохранённая
static uint8_t __scratch_x("buff4") paletteRGB[3][256]; //768 байт

static repeating_timer_t video_timer;

extern int cursor_blink_state;


void graphics_set_modeTV(tv_out_mode_t mode) {
    if (SM_video == -1) return;
    //можно добавить проверку на валидность данных, но пока так
    tv_out_mode = mode;
    for (int i = 0; i < 256; i++) {
        graphics_set_palette(i, (paletteRGB[2][i] << 16) | (paletteRGB[1][i] << 8) | (paletteRGB[0][i] << 0));
    };

    switch (tv_out_mode.N_lines) {
        case _624_lines:
            video_mode.N_lines = 312;
            break;
        case _625_lines:
            video_mode.N_lines = 625;
            break;
        case _524_lines:
            video_mode.N_lines = 262;
            break;
        case _525_lines:
            video_mode.N_lines = 525;
            break;
    }


    double color_freq;
    switch (tv_out_mode.c_freq) {
        case _3579545: color_freq = 3.579545 * 1e6;
            break;
        case _4433619: color_freq = 4.43361875 * 1e6;
            break;
    }

    video_mode.H_len = ((color_freq * 4) / 1e6) * 63.9;
    video_mode.H_len &= 0xfffffff8;

    video_mode.sync_size = 4.7 * video_mode.H_len / 64;
    video_mode.sync_size &= 0xfffffff8;

    video_mode.begin_img_shx = 10.5 * video_mode.H_len / 64;
    video_mode.img_W = video_mode.H_len - ((12 * video_mode.H_len) / 64);
    video_mode.img_W &= 0xfffffffc;

    video_mode.LVL_C_MAX = 15;
    video_mode.SYNC_TMPL = 0;
    video_mode.NO_SYNC_TMPL = CONV_DAC(video_mode.LVL_C_MAX) | (1 << SYNC_PIN);
    video_mode.LVL_BLACK = 0 + video_mode.LVL_C_MAX;

    video_mode.LVL_Y_MAX = 40;

    if (tv_out_mode.tv_system == g_TV_OUT_NTSC) {
        video_mode.LVL_BLACK += 2;
        video_mode.LVL_Y_MAX += 3;
    };
    video_mode.LVL_BLACK_TMPL = CONV_DAC(video_mode.LVL_BLACK) | (1 << SYNC_PIN);
};


static uint32_t cbNORM[2][10]; //цветовая вспышка 80байт
static uint32_t cbINV[2][10]; //цветовая вспышка	инвертированная 80 байт

static uint32_t* cb[2]; //цветовая вспышка
//определение палитры(переделать)
void graphics_set_palette(uint8_t i, uint32_t color888) {
    conv_color[0] = conv_colorNORM[0];
    conv_color[1] = conv_colorNORM[1];
    cb[0] = cbNORM[0];
    cb[1] = cbNORM[1];

    uint8_t R8 = (color888 >> 16) & 0xff;
    uint8_t G8 = (color888 >> 8) & 0xff;
    uint8_t B8 = (color888 >> 0) & 0xff;
    paletteRGB[2][i] = R8;
    paletteRGB[1][i] = G8;
    paletteRGB[0][i] = B8;
    float R = R8 / 255.0;
    float G = G8 / 255.0;
    float B = B8 / 255.0;

    float Y = 0.299 * R + 0.587 * G + 0.114 * B;
    // if (active_out==g_TV_OUT_NTSC) Y=0.299*R+0.587*G+0.114*B;
    uint8_t base8 = video_mode.LVL_BLACK;

    const int cycle_size = 4;
    int8_t Y8 = ((int)(Y * video_mode.LVL_Y_MAX)) + base8;

    uint32_t cd0_32, cd1_32;
    int8_t* cd0 = (int8_t*)&cd0_32;
    int8_t* cd1 = (int8_t*)&cd1_32;

    float sin[] = { 0, 1, 0, -1 };
    // float sin[]={-1,1,1,-1,-1};//test

    float cos[] = { 1, 0, -1, 0 };
    switch (tv_out_mode.tv_system) {
        case g_TV_OUT_PAL: {
            float U = 0.493 * (B - Y);
            float V = 0.877 * (R - Y);

            int ph = 2;
            int dph = 0;
            if (tv_out_mode.cb_sync_PI_shift_lines) {
                dph = -1;
            }
            for (int i = 0; i < cycle_size; i++) {
                float k = 1.3 * tv_out_mode.color_index;
                //подобрать , чтобы не было перегруза 1.25 или увеличить для более ярких цветов
                int max_v = video_mode.LVL_C_MAX;
                int P = k * max_v * (U * sin[(i + ph + 1 + dph) % 4] + V * cos[(i + ph + 1 + dph) % 4]) + 0.0; //+1
                int M = k * max_v * (U * sin[(i + ph) % 4] - V * cos[(i + ph) % 4]) + 0.0;


                P = P < -max_v ? -max_v : P;
                P = P > max_v ? max_v : P;


                M = M < -max_v ? -max_v : M;
                M = M > max_v ? max_v : M;


                cd0[i] = (M);
                cd1[i] = (P);
            }
            // ph=0;

            //заполнение цветовой вспышки
            uint8_t* cb8_0 = (uint8_t *)cb[0];
            uint8_t* cb8_1 = (uint8_t *)cb[1];
            uint8_t* cb8_0_i = (uint8_t *)cbINV[0];
            uint8_t* cb8_1_i = (uint8_t *)cbINV[1];

            uint8_t ampl = 0;
            uint8_t max_ampl = video_mode.LVL_C_MAX;

            // изменение функций синуса и косинуса(доворот на пи/4)
            float Q = 0.7;
            float I = 0.7;
            for (int i = 0; i < cycle_size; i++) {
                cos[i] = cos[i] * Q - sin[i] * I;
                sin[i] = cos[i] * I + sin[i] * Q;
            }

            ph = 3; //3
            Q = 1;
            I = 0;
            //  Q=0.8;
            //  I=-0.1;
            if (tv_out_mode.cb_sync_PI_shift_lines) dph = 3;

            for (int i = 0; i < 40; i++) {
                ampl = max_ampl * 1;
                if (i < cycle_size * 1) ampl = i * max_ampl / cycle_size;
                if (i > (cycle_size * 9)) ampl = (cycle_size * 10 - i) * (max_ampl) / cycle_size;

                if (tv_out_mode.color_index == 0) ampl = 0; //полное отклюение цвета

                int bb = ampl * (Q * sin[(i + ph) % 4] + I * cos[(i + ph) % 4]) + 0.0;

                bb = (bb > max_ampl) ? max_ampl : bb;
                bb = (bb < -max_ampl) ? -max_ampl : bb;
                cb8_0[i] = max_ampl + bb;

                bb = ampl * (Q * sin[(i + ph + dph) % 4] + I * cos[(i + ph + dph) % 4]) + 0.0;

                bb = (bb > max_ampl) ? max_ampl : bb;
                bb = (bb < -max_ampl) ? -max_ampl : bb;
                cb8_1[i] = max_ampl + bb;


                cb8_0[i] = CONV_DAC(cb8_0[i]) | (1 << SYNC_PIN);
                cb8_1[i] = CONV_DAC(cb8_1[i]) | (1 << SYNC_PIN);

                //инверсная вспышка
                bb = -ampl * (Q * sin[(i + ph) % 4] + I * cos[(i + ph) % 4]) + 0.0;

                bb = (bb > max_ampl) ? max_ampl : bb;
                bb = (bb < -max_ampl) ? -max_ampl : bb;
                cb8_0_i[i] = max_ampl + bb;

                bb = -ampl * (Q * sin[(i + ph + dph) % 4] + I * cos[(i + ph + dph) % 4]) + 0.0;

                bb = (bb > max_ampl) ? max_ampl : bb;
                bb = (bb < -max_ampl) ? -max_ampl : bb;
                cb8_1_i[i] = max_ampl + bb;


                cb8_0_i[i] = CONV_DAC(cb8_0_i[i]) | (1 << SYNC_PIN);
                cb8_1_i[i] = CONV_DAC(cb8_1_i[i]) | (1 << SYNC_PIN);
            }
        }
        break;
        case g_TV_OUT_NTSC: {
            float Q = 0.4127 * (B - Y) + 0.4778 * (R - Y);
            float I = -0.268 * (B - Y) + 0.7358 * (R - Y);
            // Q*=0.7;
            // I*=0.8;
            // I=0;
            // I=-I;
            // int ph=3;

            int ph = 3;
            for (int i = 0; i < cycle_size; i++) {
                float k = 1.5 * tv_out_mode.color_index; //127;
                int max_v = video_mode.LVL_C_MAX;
                int C = ((int)(k * max_v * (Q * sin[(i + ph) % 4] + I * cos[(i + ph) % 4])));
                C = C < -max_v ? -max_v : C;
                C = C > max_v ? max_v : C;
                cd0[i] = (C);
                cd1[i] = (-C);
            }


            uint8_t* cb8_0 = (uint8_t *)cb[0];
            uint8_t* cb8_1 = (uint8_t *)cb[1];
            uint8_t ampl = 127;
            uint8_t max_ampl = video_mode.LVL_C_MAX;


            //  Q=0.75;
            //  I=0.75;
            Q = 1;
            I = -1;
            for (int i = 0; i < 40; i++) {
                ampl = max_ampl * 0.5; //уменьшение амплитуды - ярче цвета, но и больше размазывание цвета
                if (i < cycle_size * 1) ampl = i * max_ampl / cycle_size;
                if (i > (cycle_size * 9)) ampl = (cycle_size * 10 - i) * (max_ampl) / cycle_size;

                if (tv_out_mode.color_index == 0) ampl = 0; //полное отклюение цвета

                // cb8_0[i]=ampl+ampl*(sin(ph))+0.5;
                // cb8_1[i]=ampl+ampl*(sin(ph+3*M_PI/2))+0.5;

                // cb8_0[i]=127+ampl*(sin[i%4])+0.5;
                // cb8_1[i]=127+ampl*(sin[(i+2)%4])+0.5;   //+3

                int dd = ampl * (Q * sin[i % 4] + I * cos[i % 4]);
                dd = dd > max_ampl ? max_ampl : dd;
                dd = dd < -max_ampl ? -max_ampl : dd;


                cb8_0[i] = max_ampl + dd;
                cb8_1[i] = max_ampl - dd; //+3


                cb8_0[i] = CONV_DAC(cb8_0[i]) | (1 << SYNC_PIN);
                cb8_1[i] = CONV_DAC(cb8_1[i]) | (1 << SYNC_PIN);
            }
        }
        break;
        default:
            break;
    }


    uint32_t Y32 = (Y8 << 24) | (Y8 << 16) | (Y8 << 8) | (Y8 << 0);
    int8_t* yi = (int8_t*)&Y32;
    int8_t* ci = (int8_t*)&cd0_32;

    for (int i = 0; i < 4; i++) { yi[i] = CONV_DAC(yi[i]+ci[i]) | (1 << SYNC_PIN); };
    conv_color[0][i] = Y32;

    Y32 = (Y8 << 24) | (Y8 << 16) | (Y8 << 8) | (Y8 << 0);
    ci = (int8_t*)&cd1_32;
    for (int i = 0; i < 4; i++) { yi[i] = CONV_DAC(yi[i]+ci[i]) | (1 << SYNC_PIN); };
    conv_color[1][i] = Y32;

    //цвет со сдвигом фазы
    uint32_t c32 = conv_color[0][i];
    conv_colorINV[0][i] = (c32 >> 16) | ((c32 & 0xffff) << 16);
    c32 = conv_color[1][i];
    conv_colorINV[1][i] = (c32 >> 16) | ((c32 & 0xffff) << 16);
}


//основная функция заполнения буферов видеоданных
static bool __time_critical_func(video_timer_callbackTV)(repeating_timer_t* rt) {
    static uint dma_inx_out = 0;
    static uint lines_buf_inx = 0;

    if (dma_chan_ctrl == -1) return 1; //не определен дма канал

    //получаем индекс выводимой строки
    uint dma_inx = (N_LINE_BUF_DMA - 2 + ((dma_channel_hw_addr(dma_chan_ctrl)->read_addr - (uint32_t)rd_addr_DMA_CTRL) /
                                          4)) % (N_LINE_BUF_DMA);

    //uint n_loop=(N_LINE_BUF_DMA+dma_inx-dma_inx_out)%N_LINE_BUF_DMA;

    static uint32_t line_active = 0;
    static uint8_t* input_buffer = NULL;
    static uint32_t frame_i = 0;
    static uint32_t g_str_index = 1;
    //while(n_loop--)
    while (dma_inx_out != dma_inx) {
        //режим VGA
        line_active++;
        g_str_index++;

        int dec_str = 0;


        if (line_active == video_mode.N_lines) {
            line_active = 0;
            frame_i++;
            input_buffer = graphics_buffer.data;
        }

        lines_buf_inx = (lines_buf_inx + 1) % N_LINE_BUF;
        uint8_t* output_buffer8 = (uint8_t *)lines_buf[lines_buf_inx];

        bool is_line_visible = true;
        // if (false)
        switch (tv_out_mode.N_lines) {
            case _624_lines:
            case _625_lines:

                switch (line_active) {
                    case 0:
                    case 1:
                        //|___|--|___|--| type=1
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        is_line_visible = false;
                        break;

                    case 2:
                        // ____|--|_|----type=2
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 3:
                    case 4: //|_|----|_|---- type=0
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;

                    case 5: break; //шаблон как у видимой строки, но без изображения


                    case 310:
                    case 311:
                        //|_|----|_|---- type=0
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 312:
                        //|_|---|____|--| type=3
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 313:
                    case 314:
                        //|___|--|___|--| type=1
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 315:
                    case 316:
                        //|_|----|_|---- type=0
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 317:
                        //|_|---------type=4
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 622:
                        //|__|---|_|----type=5
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                }
                break;
            case _524_lines:
            case _525_lines:

                switch (line_active) {
                    case 0:
                    case 1:
                    case 2:
                        //|_|----|_|---- type=0
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 3:
                    case 4:
                    case 5:
                        //|___|--|___|--| type=1
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 6:
                    case 7:
                    case 8:

                        //|_|----|_|---- type=0
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;

                    case 262:
                        //|__|---|_|----type=5
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 263:
                    case 264:
                        //|_|----|_|---- type=0
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 265:
                        //
                        //|_|---|____|--| type=3
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 266:
                    case 267:

                        // PIO_VIDEO->txf[SM_video]=v_mode.NO_SYNC_TMPL|(v_mode.NO_SYNC_TMPL<<8);
                        //|___|--|___|--| type=1
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        is_line_visible = false;
                        break;
                    case 268:

                        // ____|--|_|----type=2
                        memset(output_buffer8, video_mode.SYNC_TMPL, (video_mode.H_len / 2) - video_mode.sync_size);
                        output_buffer8 += (video_mode.H_len / 2) - video_mode.sync_size;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL, video_mode.sync_size);
                        output_buffer8 += video_mode.sync_size;
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 269:
                    case 270:
                        //|_|----|_|---- type=0
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        output_buffer8 += (video_mode.H_len / 2) - (video_mode.sync_size / 2);
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len / 2) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;
                    case 271:

                        //|_|---------type=4
                        memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size / 2);
                        output_buffer8 += video_mode.sync_size / 2;
                        memset(output_buffer8, video_mode.NO_SYNC_TMPL,
                               (video_mode.H_len) - (video_mode.sync_size / 2));
                        is_line_visible = false;
                        break;


                    default:
                        break;
                }
                break;
        }

        int li = 0;

        if (tv_out_mode.tv_system == g_TV_OUT_PAL) {
            li = g_str_index & 1;


            // static bool is_even_frame;

            //   dec_str+=2;
            //625 строк
            //  if (false)
            if (tv_out_mode.cb_sync_PI_shift_lines) {
                if (tv_out_mode.cb_sync_PI_shift_half_frame) {
                    if ((line_active == 0) ||
                        ((line_active == video_mode.N_lines / 2) && (
                             (tv_out_mode.N_lines == _625_lines) || (tv_out_mode.N_lines == _525_lines)))) {
                        dec_str += 2;
                        static bool is_inv;
                        if (is_inv) {
                            cb[0] = cbINV[0];
                            conv_color[0] = conv_colorINV[0];

                            cb[1] = cbINV[1];
                            conv_color[1] = conv_colorINV[1];
                        }
                        else {
                            cb[0] = cbNORM[0];
                            conv_color[0] = conv_colorNORM[0];
                            cb[1] = cbNORM[1];
                            conv_color[1] = conv_colorNORM[1];
                        }
                        is_inv = !is_inv;
                    } //нейтрализация сдвига фазы "лишней строки"(не кратной 4)
                    // if ((tv_out_mode.N_lines==_625_lines)||(tv_out_mode.N_lines==_525_lines))
                    // 	if (line_active==v_mode.N_lines/2)  {g_str_index+=1;dec_str+=2;}
                }
            }
            else {
                if (tv_out_mode.cb_sync_PI_shift_half_frame) {
                    if ((line_active == 0)) {
                        g_str_index += 2;
                        dec_str += 2;
                    } //нейтрализация сдвига фазы "лишней строки"(не кратной 4)
                    if ((tv_out_mode.N_lines == _625_lines) || (tv_out_mode.N_lines == _525_lines))
                        if (line_active == video_mode.N_lines / 2) {
                            g_str_index += 2;
                            dec_str += 2;
                        }
                }

                dec_str += 1;

                switch (g_str_index & 3) {
                    case 0:
                        cb[0] = cbNORM[0];
                        conv_color[0] = conv_colorNORM[0];
                        break;
                    case 3:
                        cb[1] = cbNORM[1];
                        conv_color[1] = conv_colorNORM[1];
                        break;
                    case 2:
                        cb[0] = cbINV[0];
                        conv_color[0] = conv_colorINV[0];
                        break;
                    case 1:
                        cb[1] = cbINV[1];
                        conv_color[1] = conv_colorINV[1];


                    default:
                        break;
                }
            }
        }

        else {
            if (tv_out_mode.cb_sync_PI_shift_lines) //сдвиг фазы поднесущей соседних строк относительно ССИ на пи
            {
                dec_str = 2;
            }
            else {
                g_str_index += 1;
            }

            if (tv_out_mode.cb_sync_PI_shift_half_frame) {
                if ((line_active == 0)) {
                    dec_str ^= 2;
                    g_str_index -= 1;
                } //нейтрализация сдвига фазы "лишней строки"(не кратной 4)
                if ((tv_out_mode.N_lines == _625_lines) || (tv_out_mode.N_lines == _525_lines))
                    if (line_active == video_mode.N_lines / 2) {
                        dec_str ^= 2;
                        g_str_index -= 1;
                    }
            }
            li = (g_str_index) & 1;
        };


        //ТВ строка с изображением
        if (is_line_visible) {
            memset(output_buffer8, video_mode.SYNC_TMPL, video_mode.sync_size);
            memset(output_buffer8 + video_mode.sync_size, video_mode.NO_SYNC_TMPL,
                   video_mode.begin_img_shx - video_mode.sync_size);
            int post_img_clear = 60;
            memset(output_buffer8 + (video_mode.H_len - post_img_clear), video_mode.NO_SYNC_TMPL, post_img_clear);


            // // //цветовая вспышка
            int mul_sh = 19;
            if (tv_out_mode.c_freq == _4433619) mul_sh = 23; //сдвиг вспышки для более высокой частоты
            if (li) memcpy(output_buffer8 + 0 + mul_sh * 4, cb[1], 40);
            else memcpy(output_buffer8 + 0 + mul_sh * 4, cb[0], 40);

            //цветовая вспышка V2

            // uint32_t* out_buf32_cb=(uint32_t*)lines_buf[lines_buf_inx];
            // out_buf32_cb+=19;//сдвиг начала цветовой вспышки

            // uint32_t* cb32=cb[li];
            // for(int i=10;i--;)*out_buf32_cb++=*cb32++;


            output_buffer8 += video_mode.begin_img_shx;
            //di коэффициент сжатия с учётом количества строк и частоты поднесущей
            uint16_t di = 0x100;
            int d_end = 0;
            int buffer_shift = 0;
            int y = -1;

            switch (tv_out_mode.N_lines) {
                case _624_lines:
                case _625_lines:
                    di = (tv_out_mode.c_freq == _4433619) ? 0xD7 / 2 : 0x10B / 2;
                    d_end = (tv_out_mode.c_freq == _4433619) ? 152 : 118;
                    buffer_shift = (tv_out_mode.c_freq == _4433619) ? 72 : 60;
                    if ((line_active > 4) && (line_active < 310)) { y = line_active - 23; }; //-23
                    if ((line_active > 317) && (line_active < 622)) { y = line_active - 335; }; //-335
                    y -= 24;
                    break;
                case _524_lines:
                case _525_lines:
                    di = (tv_out_mode.c_freq == _4433619) ? 0xB6 / 2 : 0xDE / 2;


                    if ((line_active > 8) && (line_active < 262)) { y = line_active - 20; };
                    if ((line_active > 271)) { y = line_active - 282; };
                    break;
            }

            if ((y >= 240) || (y < 0) || (input_buffer == NULL)) {
                //вне изображения
                memset(output_buffer8, video_mode.LVL_BLACK_TMPL, video_mode.img_W);
            }
            else {
                //зона изображения
                //цветовая вспышка(тест в зоне изображения)
                // if (li)	memcpy(out_buf8-v_mode.begin_img_shx+22*4,cb[1],40);
                // else memcpy(out_buf8-v_mode.begin_img_shx+22*4,cb[0],40);


                // memset(out_buf8,v_mode.LVL_BLACK_TMPL,v_mode.img_W);	//test


                //if (active_out==g_TV_OUT_PAL) out_buf8+=33;

                uint ibuf = 0;
                // int next_ibuf=0;
                int next_ibuf = 0x100;

                // uint16_t* out_buf16=(uint16_t*)lines_buf[lines_buf_inx];//(((uint32_t)out_buf8)&0xfffffffe);
                // out_buf16+=v_mode.begin_img_shx/2;

                uint32_t* out_buf32 = (uint32_t *)lines_buf[lines_buf_inx];
                out_buf32 += video_mode.begin_img_shx / 4;

                /*
                if (tv_out_mode.mode_bpp == g_mode_320x240x4bpp) {
                    uint8_t* vbuf8 = vbuf + (line_inx) * g_buf.width / 2;
                    // uint16_t c16=(v_mode.LVL_BLACK_TMPL)|(v_mode.LVL_BLACK_TMPL<<8);

                    uint8_t c8 = *vbuf8;
                    uint32_t cout32;
                    cout32 = conv_color[li][c8 & 0xf];
                    // uint8_t* c_4=&conv_color[0][c8&0xf];
                    uint8_t* c_4 = &cout32;


                    out_buf8 += buf_sh;

                    bool is_read_buf = false;

                    if (vbuf != NULL)
                        for (int i = 0; i < (v_mode.img_W - d_end) / 4; i++)
                        // for(int i=0;i<(g_buf.width*2);i++)
                        {
                            *out_buf8++ = c_4[0];
                            next_ibuf -= di;
                            if (next_ibuf <= 0) {
                                if (is_read_buf) {
                                    c8 = *(++vbuf8);
                                    cout32 = conv_color[li][c8 & 0xf];
                                }
                                else cout32 = conv_color[li][c8 >> 4];
                                next_ibuf += 0x100;
                                is_read_buf = !is_read_buf;
                            }

                            *out_buf8++ = c_4[1];
                            next_ibuf -= di;
                            if (next_ibuf <= 0) {
                                if (is_read_buf) {
                                    c8 = *(++vbuf8);
                                    cout32 = conv_color[li][c8 & 0xf];
                                }
                                else cout32 = conv_color[li][c8 >> 4];
                                next_ibuf += 0x100;
                                is_read_buf = !is_read_buf;
                            }

                            *out_buf8++ = c_4[2];
                            next_ibuf -= di;
                            if (next_ibuf <= 0) {
                                if (is_read_buf) {
                                    c8 = *(++vbuf8);
                                    cout32 = conv_color[li][c8 & 0xf];
                                }
                                else cout32 = conv_color[li][c8 >> 4];
                                next_ibuf += 0x100;
                                is_read_buf = !is_read_buf;
                            }

                            *out_buf8++ = c_4[3];
                            next_ibuf -= di;
                            if (next_ibuf <= 0) {
                                if (is_read_buf) {
                                    c8 = *(++vbuf8);
                                    cout32 = conv_color[li][c8 & 0xf];
                                }
                                else cout32 = conv_color[li][c8 >> 4];
                                next_ibuf += 0x100;
                                is_read_buf = !is_read_buf;
                            }
                        }


                    // }
                }
                else
                */
                // graphics_buffer.shift_y = graphics_buffer.shift_y > 8 ? graphics_buffer.shift_y - 8 : 0;
                // graphics_buffer.shift_y =0;
                if (graphics_buffer.data != NULL || text_buffer != NULL ) {
                    uint8_t* input_buffer_8bit;

                    switch (tv_out_mode.mode_bpp) {
                        case TEXTMODE_80x25_COLOR: {
                            const uint8_t y_div_6 = y / 6;
                            const uint8_t glyph_line = y - y_div_6 * 6; // Optimized modulo
                            const uint8_t* text_buffer_line = text_buffer + __fast_mul(y_div_6, 160);

                            for (unsigned int column = 0; column < TEXTMODE_COLS; column++) {
                                uint8_t glyph_pixels = font_4x6[__fast_mul(*text_buffer_line++, 6) + glyph_line];
                                const uint8_t color = *text_buffer_line++; // TODO: cga_blinking

                                if (color & 0x80 && cursor_blink_state) {
                                    glyph_pixels = 0;
                                }

                                // TODO: Actual cursor size
                                const uint8_t cursor_active = cursor_blink_state &&
                                                              y_div_6 == CURSOR_Y && column == CURSOR_X &&
                                                              glyph_line >= 4;
                                uint32_t cout32;
                                uint8_t* c_4;

                                if (cursor_active) {
                                    cout32 = conv_color[li][textmode_palette[color & 0xf]];
                                    c_4 = (uint8_t*)&cout32;
                                    *output_buffer8++ = c_4[0];
                                    *output_buffer8++ = c_4[1];
                                    *output_buffer8++ = c_4[2];
                                    *output_buffer8++ = c_4[3];
                                } else if (cga_blinking && color >> 7 & 1) {
                                    for (int bit = 4; bit--;) {
                                        cout32 = conv_color[li][cursor_blink_state
                                                                    ? color >> 4 & 0x7
                                                                    : glyph_pixels & 1
                                                                          ? textmode_palette[color & 0xf] //цвет шрифта
                                                                          : textmode_palette[color >> 4 & 0x7]];
                                        c_4 = (uint8_t*)&cout32;
                                        *output_buffer8++ = c_4[bit % 4];
                                        glyph_pixels >>= 1;
                                    }
                                } else {
                                    for (int bit = 4; bit--;) {
                                        cout32 = conv_color[li][glyph_pixels & 1
                                                                    ? textmode_palette[color & 0xf] //цвет шрифта
                                                                    : textmode_palette[color >> 4]];
                                        c_4 = (uint8_t*)&cout32;
                                        *output_buffer8++ = c_4[bit % 4];
                                        glyph_pixels >>= 1;
                                    }
                                }
                            }
                            break;
                        }
                        case CGA_320x200x4:
                        case CGA_320x200x4_BW: {
                            input_buffer_8bit = graphics_buffer.data + 0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
                            //2bit buf
                            for (int x = 320 / 4; x--;) {
                                const uint8_t cga_byte = *input_buffer_8bit++;
                                uint32_t cout32;
                                uint8_t* c_4;
                                uint8_t color;

                                color = cga_byte >> 6 & 3;
                                cout32 = conv_color[li][color ? color : cga_foreground_color];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[0];

                                color = cga_byte >> 4 & 3;
                                cout32 = conv_color[li][color ? color : cga_foreground_color];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[1];

                                color = cga_byte >> 2 & 3;
                                cout32 = conv_color[li][color ? color : cga_foreground_color];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[2];

                                color = cga_byte >> 0 & 3;
                                cout32 = conv_color[li][color ? color : cga_foreground_color];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[3];
                            }
                            break;
                        }
                        case COMPOSITE_160x200x16_force:
                        case COMPOSITE_160x200x16:
                        case TGA_160x200x16:
                            input_buffer_8bit = tga_offset + graphics_buffer.data + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
                            for (int x = 320 / 4; x--;) {
                                const uint8_t cga_byte = *input_buffer_8bit++; // Fetch 8 pixels from TGA memory
                                uint8_t color1 = cga_byte >> 4;
                                uint8_t color2 = cga_byte & 15;
                                uint32_t cout32;
                                uint8_t* c_4;

                                if (videomode == 0x8) {
                                    if (!color1) color1 = cga_foreground_color;
                                    if (!color2) color2 = cga_foreground_color;
                                }

                                cout32 = conv_color[li][color1];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[0];
                                *output_buffer8++ = c_4[1];

                                cout32 = conv_color[li][color2];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[2];
                                *output_buffer8++ = c_4[3];
                            }
                            break;
                        case TGA_320x200x16: {
                            //4bit buf
                            input_buffer_8bit = tga_offset + graphics_buffer.data + (y & 3) * 8192 + __fast_mul(y >> 2, 160);
                            for (int x = 320 / 2; x--;) {
                                uint8_t val = *input_buffer_8bit++;
                                uint32_t cout32;
                                uint8_t* c_4;

                                cout32 = conv_color[li][val >> 4];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[0];

                                cout32 = conv_color[li][val & 15];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[1];
                            }
                            break;
                        }
                        case VGA_320x200x256x4:
                            input_buffer_8bit = graphics_buffer.data + __fast_mul(y, 80);
                            for (int x = 640 / 4; x--;) {
                                uint32_t cout32;
                                uint8_t* c_4;

                                cout32 = conv_color[li][*input_buffer_8bit];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[0];

                                cout32 = conv_color[li][*(input_buffer_8bit + 16000)];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[1];

                                cout32 = conv_color[li][*(input_buffer_8bit + 32000)];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[2];

                                cout32 = conv_color[li][*(input_buffer_8bit + 48000)];
                                c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[3];

                                input_buffer_8bit++;
                            }
                            break;
                        case EGA_320x200x16x4: {
                            input_buffer_8bit = graphics_buffer.data + __fast_mul(y, 40);
                            for (int x = 0; x < 40; x++) {
                                for (int bit = 7; bit--;) {
                                    uint32_t cout32 = conv_color[li][input_buffer_8bit[0] >> bit & 1 |
                                                                    (input_buffer_8bit[16000] >> bit & 1) << 1 |
                                                                    (input_buffer_8bit[32000] >> bit & 1) << 2 |
                                                                    (input_buffer_8bit[48000] >> bit & 1) << 3];
                                    uint8_t* c_4 = (uint8_t*)&cout32;
                                    *output_buffer8++ = c_4[bit % 4];
                                }
                                input_buffer_8bit++;
                            }
                            break;
                        }
                        default:
                        case VGA_320x200x256: {
                            input_buffer_8bit = graphics_buffer.data + __fast_mul(y, 320);
                            for (unsigned int x = 320; x--;) {
                                const uint8_t color = *input_buffer_8bit++;
                                uint32_t cout32 = conv_color[li][color];
                                uint8_t* c_4 = (uint8_t*)&cout32;
                                *output_buffer8++ = c_4[x%4];
                            }
                            break;
                        }
                    }
                }
            }
        }

        //управление длиной строки
        transfer_count_DMA_CTRL[dma_inx_out] = video_mode.H_len - dec_str;
        rd_addr_DMA_CTRL[dma_inx_out] = (uint32_t)&lines_buf[lines_buf_inx];
        //включаем заполненный буфер в данные для вывода
        dma_inx_out = (dma_inx_out + 1) % (N_LINE_BUF_DMA);
        dma_inx = (N_LINE_BUF_DMA - 2 + ((dma_channel_hw_addr(dma_chan_ctrl)->read_addr - (uint32_t)rd_addr_DMA_CTRL) /
                                         4)) % (N_LINE_BUF_DMA);
        // dma_inx=(N_LINE_BUF_DMA-2+((dma_channel_hw_addr(dma_chan_ctrl)->read_addr-(uint32_t)rd_addr_DMA_CTRL)/4))%(N_LINE_BUF_DMA);
    }
    return true;
}

void graphics_set_buffer(uint8_t* buffer, const uint16_t width, const uint16_t height) {
    graphics_buffer.data = buffer;
    graphics_buffer.width = width;
    graphics_buffer.height = height;
}

//выделение и настройка общих ресурсов - 4 DMA канала, PIO программ и 2 SM
void graphics_init() {
    //настройка PIO
    SM_video = pio_claim_unused_sm(PIO_VIDEO, true);

    tv_out_mode.tv_system = g_TV_OUT_NTSC;
    tv_out_mode.N_lines = _525_lines;
    tv_out_mode.mode_bpp = VGA_320x200x256;
    tv_out_mode.c_freq = _3579545;
    tv_out_mode.color_index = 1.0; //0-1
    tv_out_mode.cb_sync_PI_shift_lines = true;
    tv_out_mode.cb_sync_PI_shift_half_frame = true;
    //выделение  DMA каналов
    dma_chan_ctrl = dma_claim_unused_channel(true);
    dma_chan_ctrl2 = dma_claim_unused_channel(true);
    dma_chan = dma_claim_unused_channel(true);


    //---------------

    //заполнение палитры по умолчанию(ч.б.)
    for (int ci = 0; ci < 256; ci++) graphics_set_palette(ci, (ci << 16) | (ci << 8) | ci); //


    //настройка рабочей SM TV

    uint offs_prg0 = 0;
    offs_prg0 = pio_add_program(PIO_VIDEO, &program_pio_TV);
    uint16_t* conv_color16 = (uint16_t *)conv_color;

    pio_sm_config c_c = pio_get_default_sm_config();
    double color_freq;
    switch (tv_out_mode.c_freq) {
        case _3579545: color_freq = 3.579545 * 1e6;
            break;
        case _4433619: color_freq = 4.43361875 * 1e6;
            break;
    }
    sm_config_set_clkdiv(&c_c, clock_get_hz(clk_sys) / (color_freq * 4));

    sm_config_set_wrap(&c_c, offs_prg0, offs_prg0 + (program_pio_TV.length - 1));
    for (int i = 0; i < 8; i++) {
        gpio_set_slew_rate(TV_BASE_PIN + i, GPIO_SLEW_RATE_FAST);
        pio_gpio_init(PIO_VIDEO, TV_BASE_PIN + i);
        gpio_set_drive_strength(TV_BASE_PIN + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(TV_BASE_PIN + i, GPIO_SLEW_RATE_FAST);
    }
    pio_sm_set_consecutive_pindirs(PIO_VIDEO, SM_video, TV_BASE_PIN, 8, true); //конфигурация пинов на выход
    sm_config_set_out_pins(&c_c, TV_BASE_PIN, 8);

    sm_config_set_out_shift(&c_c, true, true, 8); //16,32
    sm_config_set_fifo_join(&c_c, PIO_FIFO_JOIN_TX);

    pio_sm_init(PIO_VIDEO, SM_video, offs_prg0, &c_c);
    pio_sm_set_enabled(PIO_VIDEO, SM_video, true);

    //установка параметров по умолчанию
    graphics_set_modeTV(tv_out_mode);

    //настройки DMA

    //основной рабочий канал
    dma_channel_config cfg_dma = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg_dma, DMA_SIZE_8);
    channel_config_set_chain_to(&cfg_dma, dma_chan_ctrl2); // chain to other channel

    channel_config_set_read_increment(&cfg_dma, true);
    channel_config_set_write_increment(&cfg_dma, false);


    uint dreq = DREQ_PIO1_TX0 + SM_video;
    if (PIO_VIDEO == pio0) dreq = DREQ_PIO0_TX0 + SM_video;
    channel_config_set_dreq(&cfg_dma, dreq);

    dma_channel_configure(
        dma_chan,
        &cfg_dma,
        &PIO_VIDEO->txf[SM_video], // Write address
        lines_buf[0], // read address
        video_mode.H_len / 1, //
        false // Don't start yet
    );

    //контрольный канал для основного(адрес чтения)
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
        // &dma_hw->ch[dma_chan].al2_transfer_count,
        rd_addr_DMA_CTRL, // read address
        1, //
        false // Don't start yet
    );


    //контрольный канал для основного(количество транзакций)
    cfg_dma = dma_channel_get_default_config(dma_chan_ctrl2);
    channel_config_set_transfer_data_size(&cfg_dma, DMA_SIZE_32);
    channel_config_set_chain_to(&cfg_dma, dma_chan_ctrl); // chain to other channel

    channel_config_set_read_increment(&cfg_dma, true);
    channel_config_set_write_increment(&cfg_dma, false);
    channel_config_set_ring(&cfg_dma,false, 2 + N_LINE_BUF_log2);

    for (int i = 0; i < N_LINE_BUF * 2; i++) {
        transfer_count_DMA_CTRL[i] = video_mode.H_len / 1;
    }

    dma_channel_configure(
        dma_chan_ctrl2,
        &cfg_dma,
        &dma_hw->ch[dma_chan].transfer_count, // Write address
        // &dma_hw->ch[dma_chan].al2_transfer_count,
        transfer_count_DMA_CTRL, // read address
        1, //
        false // Don't start yet
    );


    dma_start_channel_mask((1u << dma_chan_ctrl2));

    int hz = 30000;
    if (!alarm_pool_add_repeating_timer_us(alarm_pool_create(2, 16), 1000000 / hz, video_timer_callbackTV, NULL,
                                           &video_timer)) {
        return;
    }
    // graphics_get_default_modeTV();
    graphics_set_modeTV(tv_out_mode);
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
};

void graphics_set_textbuffer(uint8_t* buffer) {
    text_buffer = buffer;
};

void graphics_set_offset(const int x, const int y) {
    graphics_buffer.shift_x = x;
    graphics_buffer.shift_y = y;
};

void clrScr(const uint8_t color) {
    if (text_buffer)
        memset(text_buffer, 0, TEXTMODE_COLS * TEXTMODE_ROWS * 2);
}

void graphics_set_mode(const enum graphics_mode_t mode) {
    tv_out_mode.mode_bpp = mode;
    tv_out_mode.color_index = TEXTMODE_80x25_COLOR == mode ? 0.0 : 1.0;
    tv_out_mode.cb_sync_PI_shift_lines = TEXTMODE_80x25_COLOR == mode ? true : false;
    graphics_set_modeTV(tv_out_mode);
    clrScr(0);
}
