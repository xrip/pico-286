/**
 * MIT License
 *
 * Copyright (c) 2022 Vincent Mistler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef PWM_PIN0
#define PWM_PIN0 (AUDIO_PWM_PIN & 0xfe)
#define PWM_PIN1 (PWM_PIN0+1)
#endif

#include <stdio.h>

#include "audio.h"

#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#ifdef AUDIO_PWM_PIN
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#endif

/**
 * return the default i2s context used to store information about the setup
 */
i2s_config_t i2s_get_default_config(void) {
    i2s_config_t i2s_config = {
            .sample_freq = 44100,
            .channel_count = 2,
            .data_pin = PCM_PIN,
            .clock_pin_base = CLOCK_PIN,
            .pio = pio0,
            // .sm = 0, // filled by claim_unused_sm
            // .dma_channel = 0, // filled by dma_claim_unused_channel 
            // .dma_buf = NULL, // filled by malloc
            // .dma_trans_count = 0, // filled in main()
            // .volume = 0, // filled by call to i2s_volume() in main
    };

    return i2s_config;
}

/**
 * Initialize the I2S driver. Must be called before calling i2s_write or i2s_dma_write
 * i2s_config: I2S context obtained by i2s_get_default_config()
 */


void i2s_init(i2s_config_t *i2s_config) {


#ifndef AUDIO_PWM_PIN

    uint8_t func = GPIO_FUNC_PIO1;    // TODO: GPIO_FUNC_PIO0 for pio0 or GPIO_FUNC_PIO1 for pio1

    pio_gpio_init(i2s_config->pio, i2s_config->data_pin);
    pio_gpio_init(i2s_config->pio, i2s_config->clock_pin_base);
    pio_gpio_init(i2s_config->pio, i2s_config->clock_pin_base + 1);

    i2s_config->sm = pio_claim_unused_sm(i2s_config->pio, true);

    /* Set PIO clock */
    uint32_t system_clock_frequency = clock_get_hz(clk_sys);
    uint32_t divider = system_clock_frequency * 4 / i2s_config->sample_freq; // avoid arithmetic overflow

#ifdef I2S_CS4334
    uint offset = pio_add_program(i2s_config->pio, &audio_i2s_cs4334_program);
    audio_i2s_cs4334_program_init(i2s_config->pio, i2s_config->sm , offset, i2s_config->data_pin , i2s_config->clock_pin_base);
    divider >>= 3;
#else
    uint offset = pio_add_program(i2s_config->pio, &audio_i2s_program);
    audio_i2s_program_init(i2s_config->pio, i2s_config->sm, offset, i2s_config->data_pin, i2s_config->clock_pin_base);

#endif

    pio_sm_set_clkdiv_int_frac(i2s_config->pio, i2s_config->sm, divider >> 8u, divider & 0xffu);

    pio_sm_set_enabled(i2s_config->pio, i2s_config->sm, false);
#endif
    /* Allocate memory for the DMA buffer */
    i2s_config->dma_buf = malloc(i2s_config->dma_trans_count * sizeof(uint32_t));

    /* Direct Memory Access setup */
    i2s_config->dma_channel = dma_claim_unused_channel(true);

    dma_channel_config dma_config = dma_channel_get_default_config(i2s_config->dma_channel);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);

    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);

    volatile uint32_t *addr_write_DMA = &(i2s_config->pio->txf[i2s_config->sm]);
#ifdef AUDIO_PWM_PIN
    gpio_set_function(PWM_PIN0, GPIO_FUNC_PWM);
    gpio_set_function(PWM_PIN1, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(PWM_PIN0);


   
    pwm_config c_pwm=pwm_get_default_config();
    pwm_config_set_clkdiv(&c_pwm,1.0);
    //pwm_config_set_wrap(&c_pwm,(1<<12)-1);//MAX PWM value
    pwm_config_set_wrap(&c_pwm,clock_get_hz(clk_sys)/(i2s_config->sample_freq));//MAX PWM value
    pwm_init(slice_num,&c_pwm,true);

    //Для синхронизации используем другой произвольный канал ШИМ


    channel_config_set_dreq(&dma_config, pwm_get_dreq(slice_num));     


    addr_write_DMA=(uint32_t*)&pwm_hw->slice[slice_num].cc;
#else
    channel_config_set_dreq(&dma_config, pio_get_dreq(i2s_config->pio, i2s_config->sm, true));
#endif

    dma_channel_configure(i2s_config->dma_channel,
                          &dma_config,
                          addr_write_DMA,    // Destination pointer
                          i2s_config->dma_buf,                        // Source pointer
                          i2s_config->dma_trans_count,                // Number of 32 bits words to transfer
                          false                                       // Start immediately
    );

    pio_sm_set_enabled(i2s_config->pio, i2s_config->sm, true);
}

/**
 * Write samples to I2S directly and wait for completion (blocking)
 * i2s_config: I2S context obtained by i2s_get_default_config()
 *     sample: pointer to an array of len x 32 bits samples
 *             Each 32 bits sample contains 2x16 bits samples, 
 *             one for the left channel and one for the right channel
 *        len: length of sample in 32 bits words
 */
void i2s_write(const i2s_config_t *i2s_config, const int16_t *samples, const size_t len) {
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(i2s_config->pio, i2s_config->sm, (uint32_t) samples[i]);
    }
}

/**
 * Write samples to DMA buffer and initiate DMA transfer (non blocking)
 * i2s_config: I2S context obtained by i2s_get_default_config()
 *     sample: pointer to an array of dma_trans_count x 32 bits samples
 */
void i2s_dma_write(i2s_config_t *i2s_config, const int16_t *samples) {
    /* Wait the completion of the previous DMA transfer */
    dma_channel_wait_for_finish_blocking(i2s_config->dma_channel);
    /* Copy samples into the DMA buffer */

#ifdef AUDIO_PWM_PIN
    for(uint16_t i=0;i<i2s_config->dma_trans_count*2;i++) {
           
            i2s_config->dma_buf[i] = (65536/2+(samples[i]))>>(3+i2s_config->volume);

        }
#else

    if (i2s_config->volume == 0) {
        memcpy(i2s_config->dma_buf, samples, i2s_config->dma_trans_count * sizeof(int32_t));
    } else {
        for (uint16_t i = 0; i < i2s_config->dma_trans_count * 2; i++) {
            i2s_config->dma_buf[i] = samples[i] >> i2s_config->volume;
        }
    }
#endif


    /* Initiate the DMA transfer */
    dma_channel_transfer_from_buffer_now(i2s_config->dma_channel,
                                         i2s_config->dma_buf,
                                         i2s_config->dma_trans_count);
}

/**
 * Adjust the output volume
 * i2s_config: I2S context obtained by i2s_get_default_config()
 *     volume: desired volume between 0 (highest. volume) and 16 (lowest volume)
 */
void i2s_volume(i2s_config_t *i2s_config, uint8_t volume) {
    if (volume > 16) volume = 16;
    i2s_config->volume = volume;
}

/**
 * Increases the output volume
 */
void i2s_increase_volume(i2s_config_t *i2s_config) {
    if (i2s_config->volume > 0) {
        i2s_config->volume--;
    }
}

/**
 * Decreases the output volume
 */
void i2s_decrease_volume(i2s_config_t *i2s_config) {
    if (i2s_config->volume < 16) {
        i2s_config->volume++;
    }
}

#define I2C_ADDR 0x18
#define DEBUG_I2C (0)

static void writeRegister(uint8_t reg, uint8_t value) {
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = value;
    int res = i2c_write_timeout_us(i2c0, I2C_ADDR, buf, sizeof(buf),
                                   /* nostop */ false, 1000);
    if (res != 2) {
        panic("i2c_write_timeout failed: res=%d\n", res);
    }
    if (DEBUG_I2C)
        printf("Write Reg: %d = 0x%x\n", reg, value);
}

static uint8_t readRegister(uint8_t reg) {
    uint8_t buf[1];
    buf[0] = reg;
    int res = i2c_write_timeout_us(i2c0, I2C_ADDR, buf, sizeof(buf),
                                   /* nostop */ true, 1000);
    if (res != 1) {
        if (DEBUG_I2C)
            printf("res=%d\n", res);
        panic("i2c_write_timeout failed: res=%d\n", res);
    }
    res = i2c_read_timeout_us(i2c0, I2C_ADDR, buf, sizeof(buf),
                              /* nostop */ false, 1000);
    if (res != 1) {
        if (DEBUG_I2C)
            printf("res=%d\n", res);
        panic("i2c_read_timeout failed: res=%d\n", res);
    }
    uint8_t value = buf[0];
    if (DEBUG_I2C)
        printf("Read Reg: %d = 0x%x\n", reg, value);
    return value;
}

static void modifyRegister(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t current = readRegister(reg);
    if (DEBUG_I2C)
        printf(
            "Modify Reg: %d = [Before: 0x%x] with mask 0x%x and value 0x%x\n",
            reg, current, mask, value);
    uint8_t new_value = (current & ~mask) | (value & mask);
    writeRegister(reg, new_value);
}

static void setPage(uint8_t page) {
    if (DEBUG_I2C)
        printf("Set page %d\n", page);
    writeRegister(0x00, page);
}

static void Wire_begin() {
    i2c_init(i2c0, 100000);
    gpio_set_function(20, GPIO_FUNC_I2C);
    gpio_set_function(21, GPIO_FUNC_I2C);
}


void audio_dac_init(void) {

    gpio_init(22);
    gpio_set_dir(22, true);
    gpio_put(22, true); // allow i2s to come out of reset

    Wire_begin();
    sleep_ms(1000);

    if (DEBUG_I2C)
        printf("initialize codec\n");

    // Reset codec
    writeRegister(0x01, 0x01);
    sleep_ms(10);

    // Interface Control
    modifyRegister(0x1B, 0xC0, 0x00);
    modifyRegister(0x1B, 0x30, 0x00);

    // Clock MUX and PLL settings
    modifyRegister(0x04, 0x03, 0x03);
    modifyRegister(0x04, 0x0C, 0x04);

    writeRegister(0x06, 0x20); // PLL J
    writeRegister(0x08, 0x00); // PLL D LSB
    writeRegister(0x07, 0x00); // PLL D MSB

    modifyRegister(0x05, 0x0F, 0x02); // PLL P/R
    modifyRegister(0x05, 0x70, 0x10);

    // DAC/ADC Config
    modifyRegister(0x0B, 0x7F, 0x08); // NDAC
    modifyRegister(0x0B, 0x80, 0x80);

    modifyRegister(0x0C, 0x7F, 0x02); // MDAC
    modifyRegister(0x0C, 0x80, 0x80);

    modifyRegister(0x12, 0x7F, 0x08); // NADC
    modifyRegister(0x12, 0x80, 0x80);

    modifyRegister(0x13, 0x7F, 0x02); // MADC
    modifyRegister(0x13, 0x80, 0x80);

    // PLL Power Up
    modifyRegister(0x05, 0x80, 0x80);

    // Headset and GPIO Config
    setPage(1);
    modifyRegister(0x2e, 0xFF, 0x0b);
    setPage(0);
    modifyRegister(0x43, 0x80, 0x80); // Headset Detect
    modifyRegister(0x30, 0x80, 0x80); // INT1 Control
    modifyRegister(0x33, 0x3C, 0x14); // GPIO1

    // DAC Setup
    modifyRegister(0x3F, 0xC0, 0xC0);

    // DAC Routing
    setPage(1);
    modifyRegister(0x23, 0xC0, 0x40);
    modifyRegister(0x23, 0x0C, 0x04);

    // DAC Volume Control
    setPage(0);
    modifyRegister(0x40, 0x0C, 0x00);
    writeRegister(0x41, 0x28); // Left DAC Vol
    writeRegister(0x42, 0x28); // Right DAC Vol

    // ADC Setup
    modifyRegister(0x51, 0x80, 0x80);
    modifyRegister(0x52, 0x80, 0x00);
    writeRegister(0x53, 0x68); // ADC Volume

    // Headphone and Speaker Setup
    setPage(1);
    modifyRegister(0x1F, 0xC0, 0xC0); // HP Driver
    modifyRegister(0x28, 0x04, 0x04); // HP Left Gain
    modifyRegister(0x29, 0x04, 0x04); // HP Right Gain
    writeRegister(0x24, 0x0A);        // Left Analog HP
    writeRegister(0x25, 0x0A);        // Right Analog HP

    modifyRegister(0x28, 0x78, 0x40); // HP Left Gain
    modifyRegister(0x29, 0x78, 0x40); // HP Right Gain

    // Speaker Amp
    modifyRegister(0x20, 0x80, 0x80);
    modifyRegister(0x2A, 0x04, 0x04);
    modifyRegister(0x2A, 0x18, 0x08);
    writeRegister(0x26, 0x0A);

    // Return to page 0
    setPage(0);

    if (DEBUG_I2C)
        printf("Initialization complete!\n");

    // Read all registers for verification
    if (DEBUG_I2C) {
        printf("Reading all registers for verification:\n");

        setPage(0);
        readRegister(0x00); // AIC31XX_PAGECTL
        readRegister(0x01); // AIC31XX_RESET
        readRegister(0x03); // AIC31XX_OT_FLAG
        readRegister(0x04); // AIC31XX_CLKMUX
        readRegister(0x05); // AIC31XX_PLLPR
        readRegister(0x06); // AIC31XX_PLLJ
        readRegister(0x07); // AIC31XX_PLLDMSB
        readRegister(0x08); // AIC31XX_PLLDLSB
        readRegister(0x0B); // AIC31XX_NDAC
        readRegister(0x0C); // AIC31XX_MDAC
        readRegister(0x0D); // AIC31XX_DOSRMSB
        readRegister(0x0E); // AIC31XX_DOSRLSB
        readRegister(0x10); // AIC31XX_MINI_DSP_INPOL
        readRegister(0x12); // AIC31XX_NADC
        readRegister(0x13); // AIC31XX_MADC
        readRegister(0x14); // AIC31XX_AOSR
        readRegister(0x19); // AIC31XX_CLKOUTMUX
        readRegister(0x1A); // AIC31XX_CLKOUTMVAL
        readRegister(0x1B); // AIC31XX_IFACE1
        readRegister(0x1C); // AIC31XX_DATA_OFFSET
        readRegister(0x1D); // AIC31XX_IFACE2
        readRegister(0x1E); // AIC31XX_BCLKN
        readRegister(0x1F); // AIC31XX_IFACESEC1
        readRegister(0x20); // AIC31XX_IFACESEC2
        readRegister(0x21); // AIC31XX_IFACESEC3
        readRegister(0x22); // AIC31XX_I2C
        readRegister(0x24); // AIC31XX_ADCFLAG
        readRegister(0x25); // AIC31XX_DACFLAG1
        readRegister(0x26); // AIC31XX_DACFLAG2
        readRegister(0x27); // AIC31XX_OFFLAG
        readRegister(0x2C); // AIC31XX_INTRDACFLAG
        readRegister(0x2D); // AIC31XX_INTRADCFLAG
        readRegister(0x2E); // AIC31XX_INTRDACFLAG2
        readRegister(0x2F); // AIC31XX_INTRADCFLAG2
        readRegister(0x30); // AIC31XX_INT1CTRL
        readRegister(0x31); // AIC31XX_INT2CTRL
        readRegister(0x33); // AIC31XX_GPIO1
        readRegister(0x3C); // AIC31XX_DACPRB
        readRegister(0x3D); // AIC31XX_ADCPRB
        readRegister(0x3F); // AIC31XX_DACSETUP
        readRegister(0x40); // AIC31XX_DACMUTE
        readRegister(0x41); // AIC31XX_LDACVOL
        readRegister(0x42); // AIC31XX_RDACVOL
        readRegister(0x43); // AIC31XX_HSDETECT
        readRegister(0x51); // AIC31XX_ADCSETUP
        readRegister(0x52); // AIC31XX_ADCFGA
        readRegister(0x53); // AIC31XX_ADCVOL

        setPage(1);
        readRegister(0x1F); // AIC31XX_HPDRIVER
        readRegister(0x20); // AIC31XX_SPKAMP
        readRegister(0x21); // AIC31XX_HPPOP
        readRegister(0x22); // AIC31XX_SPPGARAMP
        readRegister(0x23); // AIC31XX_DACMIXERROUTE
        readRegister(0x24); // AIC31XX_LANALOGHPL
        readRegister(0x25); // AIC31XX_RANALOGHPR
        readRegister(0x26); // AIC31XX_LANALOGSPL
        readRegister(0x27); // AIC31XX_RANALOGSPR
        readRegister(0x28); // AIC31XX_HPLGAIN
        readRegister(0x29); // AIC31XX_HPRGAIN
        readRegister(0x2A); // AIC31XX_SPLGAIN
        readRegister(0x2B); // AIC31XX_SPRGAIN
        readRegister(0x2C); // AIC31XX_HPCONTROL
        readRegister(0x2E); // AIC31XX_MICBIAS
        readRegister(0x2F); // AIC31XX_MICPGA
        readRegister(0x30); // AIC31XX_MICPGAPI
        readRegister(0x31); // AIC31XX_MICPGAMI
        readRegister(0x32); // AIC31XX_MICPGACM

        setPage(3);
        readRegister(0x10); // AIC31XX_TIMERDIVIDER
    }
}
