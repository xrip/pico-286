#pragma GCC optimize("Ofast")
#include "graphics.h"
#include <stdio.h>
#include <string.h>
#include "malloc.h"
#include <stdalign.h>
#include <pico.h>
#include <emulator/emulator.h>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

//PIO параметры
static uint pio_program_offset_video = 0;
static uint pio_program_offset_converter = 0;

//SM
static int sm_video_output = -1;
static int sm_address_converter = -1;

//активный видеорежим
static enum graphics_mode_t graphics_mode = TEXTMODE_80x25_COLOR;

//буфер  палитры 256 цветов в формате R8G8B8
static uint32_t color_palette[256];


#define SCREEN_WIDTH (320)
#define SCREEN_HEIGHT (240)
//графический буфер
static uint8_t *graphics_framebuffer = NULL;
static int framebuffer_width = 0;
static int framebuffer_height = 0;
static int framebuffer_offset_x = 0;
static int framebuffer_offset_y = 0;

//текстовый буфер
uint8_t *text_buffer = NULL;


//DMA каналы
//каналы работы с первичным графическим буфером
static int dma_channel_control;
static int dma_channel_data;
//каналы работы с конвертацией палитры
static int dma_channel_palette_control;
static int dma_channel_palette_data;

//DMA буферы
//основные строчные данные
static uint32_t *scanline_buffers[2] = {NULL,NULL};
static uint32_t *dma_buffer_addresses[2];

//ДМА палитра для конвертации
//в хвосте этой памяти выделяется dma_data
static alignas(4096) uint32_t tmds_palette_buffer[1224];


//индекс, проверяющий зависание
static uint32_t interrupt_counter = 0;

//функции и константы HDMI
#define HDMI_CTRL_BASE_INDEX (250)

extern int cursor_blink_state;

/**
 * PIO program for address conversion in palette lookup
 * This program converts 8-bit palette indices to TMDS-encoded RGB data
 */
uint16_t pio_instructions_address_converter[] = {
     0x80a0, //  0: pull   block           ; Get palette index from DMA
     0x40e8, //  1: in     osr, 8          ; Shift 8 bits into ISR
     0x4034, //  2: in     x, 20           ; Shift 20 bits from X (base address)
     0x8020, //  3: push   block           ; Push converted address to output
 };


const struct pio_program pio_program_address_converter = {
    .instructions = pio_instructions_address_converter,
    .length = 4,
    .origin = -1,
};

/**
 * PIO program for HDMI video output
 * Outputs 6 bits per clock cycle with proper side-set clock generation
 */
static const uint16_t pio_instructions_hdmi_output[] = {
    0x7006, //  0: out    pins, 6         side 2  ; Output 6 data bits, clock high
    0x7006, //  1: out    pins, 6         side 2  ; Output 6 data bits, clock high
    0x7006, //  2: out    pins, 6         side 2  ; Output 6 data bits, clock high
    0x7006, //  3: out    pins, 6         side 2  ; Output 6 data bits, clock high
    0x7006, //  4: out    pins, 6         side 2  ; Output 6 data bits, clock high
    0x6806, //  5: out    pins, 6         side 1  ; Output 6 data bits, clock low
    0x6806, //  6: out    pins, 6         side 1  ; Output 6 data bits, clock low
    0x6806, //  7: out    pins, 6         side 1  ; Output 6 data bits, clock low
    0x6806, //  8: out    pins, 6         side 1  ; Output 6 data bits, clock low
    0x6806, //  9: out    pins, 6         side 1  ; Output 6 data bits, clock low
};

static const struct pio_program pio_program_hdmi_output = {
    .instructions = pio_instructions_hdmi_output,
    .length = 10,
    .origin = -1,
};

/**
 * Generate TMDS differential pair data for RGB channels
 * @param red_data 10-bit TMDS encoded red channel data
 * @param green_data 10-bit TMDS encoded green channel data
 * @param blue_data 10-bit TMDS encoded blue channel data
 * @return 64-bit serialized differential pair data
 */
static uint64_t generate_hdmi_differential_data(const uint16_t red_data,
                                               const uint16_t green_data,
                                               const uint16_t blue_data) {
    uint64_t serialized_output = 0;

    // Process each of the 10 bits in the TMDS data
    for (int bit_index = 0; bit_index < 10; bit_index++) {
        serialized_output <<= 6;
        if (bit_index == 5) serialized_output <<= 2;  // Extra shift for timing

        // Extract current bit from each channel
        uint8_t red_bit = (red_data >> (9 - bit_index)) & 1;
        uint8_t green_bit = (green_data >> (9 - bit_index)) & 1;
        uint8_t blue_bit = (blue_data >> (9 - bit_index)) & 1;

        // Create differential pairs (bit and its inverse)
        red_bit |= (red_bit ^ 1) << 1;
        green_bit |= (green_bit ^ 1) << 1;
        blue_bit |= (blue_bit ^ 1) << 1;

        // Apply differential pair inversion if configured
#if (HDMI_PIN_invert_diffpairs)
            red_bit ^= 0b11;
            green_bit ^= 0b11;
            blue_bit ^= 0b11;
#endif

        // Pack into a 6-bit output word
#if HDMI_PIN_RGB_notBGR
        serialized_output |= (red_bit << 4) | (green_bit << 2) | (blue_bit << 0);
#else
        serialized_output |= (blue_bit << 4) | (green_bit << 2) | (red_bit << 0);
#endif
    }
    return serialized_output;
}

/**
 * TMDS 8b/10b encoder for single color channel
 * Implements the TMDS encoding algorithm to convert 8-bit color to 10-bit TMDS
 * @param input_byte 8-bit input color value
 * @return 10-bit TMDS encoded value
 */
static inline uint tmds_encode_8b10b(const uint8_t input_byte) {
    // Count number of 1s in input byte using builtin
    const int ones_count = __builtin_popcount(input_byte);

    // Determine encoding method: XOR or XNOR
    bool use_xnor = ones_count > 4 || ones_count == 4 && (input_byte & 1) == 0;

    // Generate 8-bit encoded data
    uint16_t encoded_data = input_byte & 1;  // Start with LSB
    uint16_t previous_bit = encoded_data;

    for (int i = 1; i < 8; i++) {
        const uint16_t current_bit = (input_byte >> i) & 1;
        const uint16_t encoded_bit = use_xnor ? !(previous_bit ^ current_bit) : (previous_bit ^ current_bit);
        encoded_data |= encoded_bit << i;
        previous_bit = encoded_bit;
    }

    // Add control bits (bits 8 and 9)
    encoded_data |= use_xnor ? 1 << 9 : 1 << 8;

    return encoded_data;
}

/**
 * Set PIO state machine X register to 32-bit value
 * Used to set base address for palette lookup
 */
static inline void pio_set_x_register(PIO pio, const int state_machine, const uint32_t value) {
    const uint instr_shift = pio_encode_in(pio_x, 4);
    const uint instr_mov = pio_encode_mov(pio_x, pio_isr);

    // Load 32-bit value as eight 4-bit nibbles
    for (int i = 0; i < 8; i++) {
        const uint32_t nibble = (value >> (i * 4)) & 0xf;
        pio_sm_exec(pio, state_machine, pio_encode_set(pio_x, nibble));
        pio_sm_exec(pio, state_machine, instr_shift);
    }

    pio_sm_exec(pio, state_machine, instr_mov);
}



static void __time_critical_func() hdmi_scanline_interrupt_handler() {
    static uint32_t buffer_index;
    static uint current_scanline  = 0;

    interrupt_counter++;

    // Acknowledge DMA interrupt
    dma_hw->ints0 = 1u << dma_channel_control;

    // Set up the next scanline buffer
    dma_channel_set_read_addr(dma_channel_control, &dma_buffer_addresses[buffer_index & 1], false);

    current_scanline  = current_scanline  >= 524 ? 0 : current_scanline  + 1;

    // VSync
    port3DA = ((current_scanline >= 399) ? 8 : 0) | (current_scanline & 1);

    // Skip processing on even scanlines (simple line doubling for 240p output)
    if ((current_scanline & 1) == 0) return;

    buffer_index++;


    uint8_t *current_scanline_buffer  = (uint8_t *) scanline_buffers[buffer_index & 1];

    if (graphics_framebuffer && current_scanline  < 400) {
        //область изображения
        uint8_t *output_buffer = current_scanline_buffer  + 72; //для выравнивания синхры;
        const uint8_t y = current_scanline  / 2;
        const uint8_t *input_buffer_8bit;

        switch (graphics_mode) {
            case TEXTMODE_80x25_COLOR: {
                const uint8_t y_div_6 = y / 6;
                const uint8_t glyph_line = y - y_div_6 * 6; // Optimized modulo
                const uint8_t * text_buffer_line = text_buffer + __fast_mul(y_div_6, 160);

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

                    if (cursor_active) {
                        *output_buffer++ = textmode_palette[color & 0xf];
                        *output_buffer++ = textmode_palette[color & 0xf];
                        *output_buffer++ = textmode_palette[color & 0xf];
                        *output_buffer++ = textmode_palette[color & 0xf];
                    } else if (cga_blinking && color >> 7 & 1) {
                        #pragma GCC unroll(4)
                        for (int bit = 4; bit--;) {
                            *output_buffer++ = cursor_blink_state ? color >> 4 & 0x7 : glyph_pixels & 1
                                                   ? textmode_palette[color & 0xf] //цвет шрифта
                                                   : textmode_palette[color >> 4 & 0x7]; //цвет фона

                            glyph_pixels >>= 1;
                        }
                    } else {
                        #pragma GCC unroll(4)
                        for (int bit = 4; bit--;) {
                            *output_buffer++ = glyph_pixels & 1
                                                   ? textmode_palette[color & 0xf] //цвет шрифта
                                                   : textmode_palette[color >> 4]; //цвет фона

                            glyph_pixels >>= 1;
                        }
                    }
                }
                break;
            }
            case CGA_320x200x4:
            case CGA_320x200x4_BW: {
                input_buffer_8bit = graphics_framebuffer + 0x8000 + (vram_offset << 1) + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
                //2bit buf
                for (int x = 320 / 4; x--;) {
                    const uint8_t cga_byte = *input_buffer_8bit++;

                    uint8_t color = cga_byte >> 6 & 3;
                    *output_buffer++ = color ? color : cga_foreground_color;
                    color = cga_byte >> 4 & 3;
                    *output_buffer++ = color ? color : cga_foreground_color;
                    color = cga_byte >> 2 & 3;
                    *output_buffer++ = color ? color : cga_foreground_color;
                    color = cga_byte >> 0 & 3;
                    *output_buffer++ = color ? color : cga_foreground_color;
                }
                break;
            }
            case COMPOSITE_160x200x16_force:
            case COMPOSITE_160x200x16:
            case TGA_160x200x16:
                input_buffer_8bit = tga_offset + graphics_framebuffer + __fast_mul(y >> 1, 80) + ((y & 1) << 13);
                for (int x = 320 / 4; x--;) {
                    const uint8_t cga_byte = *input_buffer_8bit++; // Fetch 8 pixels from TGA memory
                    uint8_t color1 = cga_byte >> 4 & 15;
                    uint8_t color2 = cga_byte & 15;

                    if (videomode == 0x8) {
                        if (!color1) color1 = cga_foreground_color;
                        if (!color2) color2 = cga_foreground_color;
                    }

                    *output_buffer++ = color1;
                    *output_buffer++ = color1;
                    *output_buffer++ = color2;
                    *output_buffer++ = color2;
                }
                break;
            case TGA_320x200x16: {
                //4bit buf
                input_buffer_8bit = tga_offset + graphics_framebuffer + (y & 3) * 8192 + __fast_mul(y >> 2, 160);
                for (int x = SCREEN_WIDTH / 2; x--;) {
                    *output_buffer++ = *input_buffer_8bit >> 4 & 15;
                    *output_buffer++ = *input_buffer_8bit & 15;
                    input_buffer_8bit++;
                }
                break;
            }
            case VGA_320x200x256x4:
                input_buffer_8bit = graphics_framebuffer + __fast_mul(y, 80);
                for (int x = 640 / 4; x--;) {
                    //*output_buffer_16bit++=current_palette[*input_buffer_8bit++];
                    *output_buffer++ = *input_buffer_8bit;
                    *output_buffer++ = *(input_buffer_8bit + 16000);
                    *output_buffer++ = *(input_buffer_8bit + 32000);
                    *output_buffer++ = *(input_buffer_8bit + 48000);
                    input_buffer_8bit++;
                }
                break;
            case EGA_320x200x16x4: {
                input_buffer_8bit = graphics_framebuffer + __fast_mul(y, 40);
                for (int x = 0; x < 40; x++) {
                    for (int bit = 7; bit--;) {
                        *output_buffer++ = input_buffer_8bit[0] >> bit & 1 |
                                           (input_buffer_8bit[16000] >> bit & 1) << 1 |
                                           (input_buffer_8bit[32000] >> bit & 1) << 2 |
                                           (input_buffer_8bit[48000] >> bit & 1) << 3;
                    }
                    input_buffer_8bit++;
                }
                break;
            }
            default:
            case VGA_320x200x256: {
                input_buffer_8bit = graphics_framebuffer +__fast_mul(y, 320);
                for (unsigned int x = 320; x--;) {
                    const uint8_t color = *input_buffer_8bit++;
                    *output_buffer++ = (color & HDMI_CTRL_BASE_INDEX) == HDMI_CTRL_BASE_INDEX ? 0 : color;
                }
                break;
            }
        }


        // memset(activ_buf,2,320);//test

        //ССИ
        //для выравнивания синхры

        // --|_|---|_|---|_|----
        //---|___________|-----
        memset(current_scanline_buffer  + 48,HDMI_CTRL_BASE_INDEX, 24);
        memset(current_scanline_buffer ,HDMI_CTRL_BASE_INDEX + 1, 48);
        memset(current_scanline_buffer  + 392,HDMI_CTRL_BASE_INDEX, 8);

        //без выравнивания
        // --|_|---|_|---|_|----
        //------|___________|----
        //   memset(activ_buf+320,BASE_HDMI_CTRL_INX,8);
        //   memset(activ_buf+328,BASE_HDMI_CTRL_INX+1,48);
        //   memset(activ_buf+376,BASE_HDMI_CTRL_INX,24);
    } else {
        if ((current_scanline  >= 490) && (current_scanline  < 492)) {
            //кадровый синхроимпульс
            //для выравнивания синхры
            // --|_|---|_|---|_|----
            //---|___________|-----
            memset(current_scanline_buffer  + 48,HDMI_CTRL_BASE_INDEX + 2, 352);
            memset(current_scanline_buffer ,HDMI_CTRL_BASE_INDEX + 3, 48);
            //без выравнивания
            // --|_|---|_|---|_|----
            //-------|___________|----

            // memset(activ_buf,BASE_HDMI_CTRL_INX+2,328);
            // memset(activ_buf+328,BASE_HDMI_CTRL_INX+3,48);
            // memset(activ_buf+376,BASE_HDMI_CTRL_INX+2,24);
        } else {
            //ССИ без изображения
            //для выравнивания синхры
            memset(current_scanline_buffer  + 48,HDMI_CTRL_BASE_INDEX, 352);
            memset(current_scanline_buffer ,HDMI_CTRL_BASE_INDEX + 1, 48);

            // memset(activ_buf,BASE_HDMI_CTRL_INX,328);
            // memset(activ_buf+328,BASE_HDMI_CTRL_INX+1,48);
            // memset(activ_buf+376,BASE_HDMI_CTRL_INX,24);
        };
    }


    // y=(y==524)?0:(y+1);
    // inx_buf_dma++;
}


static inline void remove_dma_interrupt_handler() {
    irq_set_enabled(VIDEO_DMA_IRQ, false);
    irq_remove_handler(VIDEO_DMA_IRQ, irq_get_exclusive_handler(VIDEO_DMA_IRQ));
}

static inline void install_dma_interrupt_handler() {
    irq_set_exclusive_handler(VIDEO_DMA_IRQ, hdmi_scanline_interrupt_handler);
    irq_set_priority(VIDEO_DMA_IRQ, 0);
    irq_set_enabled(VIDEO_DMA_IRQ, true);
}

//деинициализация - инициализация ресурсов
static inline bool initialize_hdmi_output() {
    //выключение прерывания DMA
    if (VIDEO_DMA_IRQ == DMA_IRQ_0) {
        dma_channel_set_irq0_enabled(dma_channel_control, false);
    } else {
        dma_channel_set_irq1_enabled(dma_channel_control, false);
    }

    remove_dma_interrupt_handler();


    // Abort all DMA channels and wait for completion
    dma_hw->abort = 1 << dma_channel_control | 1 << dma_channel_data | 1 << dma_channel_palette_data | 1 << dma_channel_palette_control;

    while (dma_hw->abort) tight_loop_contents();

    //выключение SM основной и конвертора

    //pio_sm_restart(PIO_VIDEO, SM_video);
    pio_sm_set_enabled(PIO_VIDEO, sm_video_output, false);

    //pio_sm_restart(PIO_VIDEO_ADDR, SM_conv);
    pio_sm_set_enabled(PIO_VIDEO_ADDR, sm_address_converter, false);


    //удаление программ из соответствующих PIO
    pio_remove_program(PIO_VIDEO_ADDR, &pio_program_address_converter, pio_program_offset_converter);
    pio_remove_program(PIO_VIDEO, &pio_program_hdmi_output, pio_program_offset_video);


    pio_program_offset_converter = pio_add_program(PIO_VIDEO_ADDR, &pio_program_address_converter);
    pio_program_offset_video = pio_add_program(PIO_VIDEO, &pio_program_hdmi_output);

    pio_set_x_register(PIO_VIDEO_ADDR, sm_address_converter, (uint32_t) tmds_palette_buffer >> 12);

    //240-243 служебные данные(синхра) напрямую вносим в массив -конвертер
    uint64_t *tmds_buffer_64  = (uint64_t *) tmds_palette_buffer;
    const uint16_t ctrl_symbol_0 = 0b1101010100;
    const uint16_t ctrl_symbol_1 = 0b0010101011;
    const uint16_t ctrl_symbol_2 = 0b0101010100;
    const uint16_t ctrl_symbol_3 = 0b1010101011;

    const int base_index  = HDMI_CTRL_BASE_INDEX;

    // H-sync low, V-sync low
    tmds_buffer_64 [2 * base_index  + 0] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_3);
    tmds_buffer_64 [2 * base_index  + 1] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_3);

    // H-sync high, V-sync low
    tmds_buffer_64 [2 * (base_index  + 1) + 0] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_2);
    tmds_buffer_64 [2 * (base_index  + 1) + 1] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_2);

    // H-sync low, V-sync high
    tmds_buffer_64 [2 * (base_index  + 2) + 0] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_1);
    tmds_buffer_64 [2 * (base_index  + 2) + 1] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_1);

    // H-sync high, V-sync high
    tmds_buffer_64 [2 * (base_index  + 3) + 0] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_0 );
    tmds_buffer_64 [2 * (base_index  + 3) + 1] = generate_hdmi_differential_data(ctrl_symbol_0 , ctrl_symbol_0 , ctrl_symbol_0 );

    //настройка PIO SM для конвертации

    pio_sm_config config  = pio_get_default_sm_config();
    sm_config_set_wrap(&config , pio_program_offset_converter, pio_program_offset_converter + (pio_program_address_converter.length - 1));
    sm_config_set_in_shift(&config , true, false, 32);

    pio_sm_init(PIO_VIDEO_ADDR, sm_address_converter, pio_program_offset_converter, &config );
    pio_sm_set_enabled(PIO_VIDEO_ADDR, sm_address_converter, true);

    //настройка PIO SM для вывода данных
    config  = pio_get_default_sm_config();
    sm_config_set_wrap(&config , pio_program_offset_video, pio_program_offset_video + (pio_program_hdmi_output.length - 1));

    //настройка side set
    sm_config_set_sideset_pins(&config ,beginHDMI_PIN_clk);
    sm_config_set_sideset(&config , 2,false,false);

    for (int i = 0; i < 2; i++) {
        pio_gpio_init(PIO_VIDEO, beginHDMI_PIN_clk + i);
        gpio_set_drive_strength(beginHDMI_PIN_clk + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(beginHDMI_PIN_clk + i, GPIO_SLEW_RATE_FAST);
    }

    pio_sm_set_pins_with_mask(PIO_VIDEO, sm_video_output, 3u << beginHDMI_PIN_clk, 3u << beginHDMI_PIN_clk);
    pio_sm_set_pindirs_with_mask(PIO_VIDEO, sm_video_output, 3u << beginHDMI_PIN_clk, 3u << beginHDMI_PIN_clk);
    //пины

    for (int i = 0; i < 6; i++) {
        pio_gpio_init(PIO_VIDEO, beginHDMI_PIN_data + i);
        gpio_set_drive_strength(beginHDMI_PIN_data + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(beginHDMI_PIN_data + i, GPIO_SLEW_RATE_FAST);
    }
    pio_sm_set_consecutive_pindirs(PIO_VIDEO, sm_video_output, beginHDMI_PIN_data, 6, true);
    //конфигурация пинов на выход
    sm_config_set_out_pins(&config , beginHDMI_PIN_data, 6);

    //
    sm_config_set_out_shift(&config , true, true, 30);
    sm_config_set_fifo_join(&config , PIO_FIFO_JOIN_TX);

    sm_config_set_clkdiv(&config , clock_get_hz(clk_sys) / 252000000.0f);
    pio_sm_init(PIO_VIDEO, sm_video_output, pio_program_offset_video, &config );
    pio_sm_set_enabled(PIO_VIDEO, sm_video_output, true);

    //настройки DMA
    scanline_buffers[0] = &tmds_palette_buffer[1024];
    scanline_buffers[1] = &tmds_palette_buffer[1124];

    //основной рабочий канал
    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel_data);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
    channel_config_set_chain_to(&dma_config, dma_channel_control); // chain to other channel

    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);

    uint dreq = (PIO_VIDEO_ADDR == pio0) ? DREQ_PIO0_TX0 + sm_address_converter :
                                           DREQ_PIO1_TX0 + sm_address_converter;

    channel_config_set_dreq(&dma_config, dreq);

    dma_channel_configure(
        dma_channel_data,
        &dma_config,
        &PIO_VIDEO_ADDR->txf[sm_address_converter], // Write address
        &scanline_buffers[0][0], // read address
        400, //
        false // Don't start yet
    );

    //контрольный канал для основного
    dma_config = dma_channel_get_default_config(dma_channel_control);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_chain_to(&dma_config, dma_channel_data); // chain to other channel

    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, false);

    dma_buffer_addresses[0] = &scanline_buffers[0][0];
    dma_buffer_addresses[1] = &scanline_buffers[1][0];

    dma_channel_configure(
        dma_channel_control,
        &dma_config,
        &dma_hw->ch[dma_channel_data].read_addr, // Write address
        &dma_buffer_addresses[0], // read address
        1, //
        false // Don't start yet
    );

    //канал - конвертер палитры

    dma_config = dma_channel_get_default_config(dma_channel_palette_data);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_chain_to(&dma_config, dma_channel_palette_control); // chain to other channel

    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);

    dreq = DREQ_PIO1_TX0 + sm_video_output;
    if (PIO_VIDEO == pio0) dreq = DREQ_PIO0_TX0 + sm_video_output;

    channel_config_set_dreq(&dma_config, dreq);

    dma_channel_configure(
        dma_channel_palette_data,
        &dma_config,
        &PIO_VIDEO->txf[sm_video_output], // Write address
        &tmds_palette_buffer[0], // read address
        4, //
        false // Don't start yet
    );

    //канал управления конвертером палитры

    dma_config = dma_channel_get_default_config(dma_channel_palette_control);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_chain_to(&dma_config, dma_channel_palette_data); // chain to other channel

    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, false);

    dreq = DREQ_PIO1_RX0 + sm_address_converter;
    if (PIO_VIDEO_ADDR == pio0) dreq = DREQ_PIO0_RX0 + sm_address_converter;

    channel_config_set_dreq(&dma_config, dreq);

    dma_channel_configure(
        dma_channel_palette_control,
        &dma_config,
        &dma_hw->ch[dma_channel_palette_data].read_addr, // Write address
        &PIO_VIDEO_ADDR->rxf[sm_address_converter], // read address
        1, //
        true // start yet
    );

    //стартуем прерывание и канал
    if (VIDEO_DMA_IRQ == DMA_IRQ_0) {
        dma_channel_acknowledge_irq0(dma_channel_control);
        dma_channel_set_irq0_enabled(dma_channel_control, true);
    } else {
        dma_channel_acknowledge_irq1(dma_channel_control);
        dma_channel_set_irq1_enabled(dma_channel_control, true);
    }

    install_dma_interrupt_handler();

    dma_start_channel_mask((1u << dma_channel_control));

    return true;
};
//выбор видеорежима
void graphics_set_mode(enum graphics_mode_t mode) {
    graphics_mode = mode;
};

void graphics_set_palette(uint8_t i, uint32_t color888) {
    color_palette[i] = color888 & 0x00ffffff;


    if ((i >= HDMI_CTRL_BASE_INDEX) && (i != 255)) return; //не записываем "служебные" цвета

    uint64_t *conv_color64 = (uint64_t *) tmds_palette_buffer;
    const uint8_t R = (color888 >> 16) & 0xff;
    const uint8_t G = (color888 >> 8) & 0xff;
    const uint8_t B = (color888 >> 0) & 0xff;
    conv_color64[i * 2] = generate_hdmi_differential_data(tmds_encode_8b10b(R), tmds_encode_8b10b(G), tmds_encode_8b10b(B));
    conv_color64[i * 2 + 1] = conv_color64[i * 2] ^ 0x0003ffffffffffffl;
};

void graphics_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height) {
    graphics_framebuffer = buffer;
    framebuffer_width = width;
    framebuffer_height = height;
};


//выделение и настройка общих ресурсов - 4 DMA канала, PIO программ и 2 SM
void graphics_init() {
    //настройка PIO
    sm_video_output = pio_claim_unused_sm(PIO_VIDEO, true);
    sm_address_converter = pio_claim_unused_sm(PIO_VIDEO_ADDR, true);
    //выделение и преднастройка DMA каналов
    dma_channel_control = dma_claim_unused_channel(true);
    dma_channel_data = dma_claim_unused_channel(true);
    dma_channel_palette_control = dma_claim_unused_channel(true);
    dma_channel_palette_data = dma_claim_unused_channel(true);

    initialize_hdmi_output();
}

void graphics_set_bgcolor(uint32_t color888) //определяем зарезервированный цвет в палитре
{
    graphics_set_palette(255, color888);
};

void graphics_set_offset(int x, int y) {
    framebuffer_offset_x = x;
    framebuffer_offset_y = y;
};

void graphics_set_textbuffer(uint8_t *buffer) {
    text_buffer = buffer;
};
