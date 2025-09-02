#include <graphics.h>

enum graphics_mode_t graphics_mode;
uint16_t ntsc_palette[4 * 256] __attribute__ ((aligned (4)));

/* ===========================================================================
 * Function: ntsc_init
 * Purpose: Initialize the complete NTSC video generation system
 * =========================================================================== */
void graphics_init() {
    /* Clock Configuration
     * 315 MHz is the PERFECT frequency for NTSC video generation!
     * NTSC color burst is exactly 315/88 MHz = 3.579545... MHz
     * 315 MHz / 22 = 315/22 MHz = 14.318181... MHz (exactly 4x color burst)
     * 14.318181 MHz / 4 = 3.579545 MHz (EXACT NTSC color burst frequency)
     * This configuration provides PERFECT NTSC timing with 0% error! */
    const uint32_t system_clock_khz = 315000;
    const uint32_t pwm_period_cycles = 11;

    // vreg_set_voltage(VREG_VOLTAGE_1_30);
    // set_sys_clock_khz(system_clock_khz, true);

    // Configure PWM output pin
    gpio_set_function(NTSC_PIN_OUTPUT, GPIO_FUNC_PWM);
    const uint pwm_slice = pwm_gpio_to_slice_num(NTSC_PIN_OUTPUT);

    // Configure PWM for video signal generation
    pwm_config pwm_cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&pwm_cfg, 2.0f); // 2x clock division

    pwm_init(pwm_slice, &pwm_cfg, true);
    pwm_set_wrap(pwm_slice, pwm_period_cycles - 1);

    // Get PWM compare register address for DMA writes
    volatile void *pwm_compare_addr = &pwm_hw->slice[pwm_slice].cc;
    // Offset by 2 bytes to write to the upper 16 bits (channel B)
    pwm_compare_addr = (volatile void *) ((uintptr_t) pwm_compare_addr + 2);

    // Allocate DMA channels for ping-pong operation
    ntsc_dma_chan_primary = dma_claim_unused_channel(true);
    ntsc_dma_chan_secondary = dma_claim_unused_channel(true);

    // Configure primary DMA channel
    dma_channel_config primary_config = dma_channel_get_default_config(ntsc_dma_chan_primary);
    channel_config_set_transfer_data_size(&primary_config, DMA_SIZE_16);
    channel_config_set_read_increment(&primary_config, true); // Increment source
    channel_config_set_write_increment(&primary_config, false); // Fixed destination
    channel_config_set_dreq(&primary_config, DREQ_PWM_WRAP0 + pwm_slice);
    channel_config_set_chain_to(&primary_config, ntsc_dma_chan_secondary); // Chain to secondary

    dma_channel_configure(
        ntsc_dma_chan_primary,
        &primary_config,
        pwm_compare_addr, // Destination: PWM register
        ntsc_scanline_buffers[0], // Source: Buffer 0
        NTSC_SAMPLES_PER_LINE, // Transfer count
        false // Don't start yet
    );

    // Configure a secondary DMA channel (mirrors primary, chains back)
    dma_channel_config secondary_config = dma_channel_get_default_config(ntsc_dma_chan_secondary);
    channel_config_set_transfer_data_size(&secondary_config, DMA_SIZE_16);
    channel_config_set_read_increment(&secondary_config, true);
    channel_config_set_write_increment(&secondary_config, false);
    channel_config_set_dreq(&secondary_config, DREQ_PWM_WRAP0 + pwm_slice);
    channel_config_set_chain_to(&secondary_config, ntsc_dma_chan_primary); // Chain back

    dma_channel_configure(
        ntsc_dma_chan_secondary,
        &secondary_config,
        pwm_compare_addr, // Destination: PWM register
        ntsc_scanline_buffers[1], // Source: Buffer 1
        NTSC_SAMPLES_PER_LINE, // Transfer count
        false // Don't start yet
    );

    // Pre-fill buffers with the first two scanlines
    ntsc_generate_scanline(ntsc_scanline_buffers[0], 0);
    ntsc_generate_scanline(ntsc_scanline_buffers[1], 1);

    // Enable DMA completion interrupts for both channels
    dma_set_irq0_channel_mask_enabled(1u << ntsc_dma_chan_primary | 1u << ntsc_dma_chan_secondary,true);

    // Install and enable interrupt handler
    irq_set_exclusive_handler(DMA_IRQ_0, ntsc_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Start video generation by triggering the first DMA transfer
    dma_start_channel_mask(1u << ntsc_dma_chan_primary);
}



void graphics_set_palette(const uint8_t index, const uint32_t rgb) {
    const uint8_t r = rgb >> 16 & 0xFF;
    const uint8_t g = rgb >> 8 & 0xFF;
    const uint8_t b = rgb & 0xFF;

    // ntsc_set_color expects parameters in order: (blue, red, green)
    ntsc_set_color(index, b, r, g);
}