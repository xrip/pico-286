/*
 * HSTX DVI scanout for the VGA/TV hybrid driver.
 *
 * Derived from DnCraptor/frank-quake2, drivers/hdmi/psx_video.c,
 * commit 5d865a098f7e927e826c2ae9879e422854495669, itself derived from
 * pico_hdmi by fliperama86 (public domain / Unlicense).
 * See LICENSE.pico_hdmi. This version keeps the BGR555 TMDS serializer and
 * line pipeline, replaces resource/lifecycle handling, and omits HDMI audio.
 */
#include "hstx_video.h"
#include "hstx_audio.h"

#include <assert.h>
#include <stddef.h>

#include <pico.h>
#include <pico/platform.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/resets.h>
#include <hardware/sync.h>
#include <hardware/structs/hstx_ctrl.h>
#include <hardware/structs/hstx_fifo.h>
#include <hardware/structs/dma_debug.h>

extern volatile uint8_t port3DA;

/* Blank and active packets end with payload words that last to the end of
 * the line. Their DMA completion points therefore agree, including active/blank edges.
 * Nine-word blank lists would reintroduce a sub-line rearm deadline there. */
/* Each line carries its own data island, so blank and black lines cannot
 * share a packet. Two are enough: when the IRQ writes the line after next,
 * the DMA has read all of the line before (it has completed) and is on the
 * next one, which has the other packet. */
static hstx_video_packet_t spare_packets[2];
static uint spare_slot;

static hstx_video_packet_t *__time_critical_func(spare_packet)(bool active, bool vsync) {
    spare_slot ^= 1u;
    hstx_packet_init(&spare_packets[spare_slot], active, vsync);
    return &spare_packets[spare_slot];
}
#define BLANK_PACKET(vsync) spare_packet(false, vsync)
#define BLACK_PACKET spare_packet(true, false)
#define START_PACKET (&spare_packets[0])

static int dma_channels[2] = {-1, -1};
static uint32_t dma_mask;
static uint32_t dma_ctrl32[2];
static uint owner_core;
static bool initialized;
static volatile bool running;
static bool irq_was_enabled;
static uint8_t previous_irq_priority;
static gpio_function_t previous_pin_functions[8];
static bool pins_owned;

static uint next_vline;
static uint expected_completed_slot;
static hstx_video_line_callback_t line_callback;
static hstx_video_vsync_callback_t vsync_callback;

volatile uint32_t hstx_video_frame_count;
volatile uint32_t hstx_video_restart_count;
volatile uint32_t hstx_video_recovery_phase;

/* All functions reached by the IRQ (including its recovery path) are in
 * SRAM. All register access SDK helpers used below are inline. */
static void __time_critical_func(hstx_halt_dma)(void) {
    hstx_video_recovery_phase = 1;
    dma_set_irq1_channel_mask_enabled(dma_mask, false);
    /* Disable both channels before aborting either: an in-flight completion
     * must not restart its partner while teardown is draining transfers. */
    for (uint slot = 0; slot < 2; ++slot) {
        hw_clear_bits(&dma_hw->ch[dma_channels[slot]].al1_ctrl,
                      DMA_CH0_CTRL_TRIG_EN_BITS);
    }
    dma_channel_abort((uint)dma_channels[0]);
    dma_channel_abort((uint)dma_channels[1]);
    hstx_ctrl_hw->csr = 0;
    dma_hw->ints1 = dma_mask;
}

static void __time_critical_func(hstx_configure_serializer)(void) {
    hstx_video_recovery_phase = 2;
    /* EN=0 resets the shifter, not the FIFO. Reset the whole HSTX peripheral
     * so stop/start and underrun recovery cannot replay stale FIFO words.
     * The clk_hstx generator and PLLs are outside this peripheral reset. */
    reset_block(RESETS_RESET_HSTX_BITS);
    unreset_block_wait(RESETS_RESET_HSTX_BITS);

    hstx_ctrl_hw->expand_tmds =
        4u << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
        29u << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        4u << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        2u << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        4u << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        7u << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
    hstx_ctrl_hw->expand_shift =
        2u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        16u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB;
    /* RAW_SHIFT=0 repeats each blank payload symbol twice (once with
     * HDMI_SOUND: see hstx_packet.h). RAW_REPEAT porch commands ignore
     * RAW_N_SHIFTS, so their physical pixel counts stay exact. */

    /* Five DDR shifts emit one 10-bit TMDS symbol per lane. */
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB;
    hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
        const uint32_t select =
            (lane * 10u) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10u + 1u) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[2u + lane * 2u] = select | HSTX_CTRL_BIT0_INV_BITS;
        hstx_ctrl_hw->bit[3u + lane * 2u] = select;
    }
}

static void __time_critical_func(hstx_arm_transfer)(
        uint slot, const hstx_video_packet_t *packet) {
    dma_channel_hw_t *channel = &dma_hw->ch[dma_channels[slot]];
    /* The channel is disabled while its source and count are published.
     * Enabling through AL1_CTRL does not trigger it; its partner chains to it.
     * In particular, avoid the triggering CTRL_TRIG / READ_ADDR aliases. */
    channel->read_addr = (uintptr_t)packet;
    channel->transfer_count = HSTX_PACKET_WORDS;
    __dmb();
    channel->al1_ctrl = dma_ctrl32[slot];
}

static bool __time_critical_func(hstx_start_pipeline)(void) {
    port3DA = 9;
    hstx_configure_serializer();
    next_vline = 2;
    expected_completed_slot = 0;
    /* Lines 0 and 1 both send START_PACKET; the next spare write goes to the other one. */
    hstx_packet_init(START_PACKET, false, false);
    hstx_packet_set_island(START_PACKET, hstx_audio_island(0));
    spare_slot = 0;
    for (uint slot = 0; slot < 2; ++slot) {
        dma_channel_hw_t *channel = &dma_hw->ch[dma_channels[slot]];
        channel->al1_ctrl = dma_ctrl32[slot] & ~DMA_CH0_CTRL_TRIG_EN_BITS;
        channel->write_addr = (uintptr_t)&hstx_fifo_hw->fifo;
        // A peripheral reset also resets its DREQ producer. Discard old DMA
        // credits and request a fresh handshake before restarting its FIFO.
        dma_debug_hw->ch[dma_channels[slot]].dbg_ctdreq = 0;
        hstx_arm_transfer(slot, START_PACKET);
    }
    dma_hw->ints1 = dma_mask;
    dma_set_irq1_channel_mask_enabled(dma_mask, true);
    dma_start_channel_mask(1u << dma_channels[0]);

    hstx_video_recovery_phase = 3;
    /* A packet has 329 words and the FIFO holds eight: this DMA cannot
     * complete and chain until HSTX is enabled below. Prime at least the first
     * command + operand so the first output symbol starts with valid data.
     * Bound the loop so a bad clock/setup fails instead of hanging boot/IRQ. */
    for (uint attempt = 0; attempt < 1024; ++attempt) {
        if ((hstx_fifo_hw->stat & HSTX_FIFO_STAT_LEVEL_BITS) >= 2u) {
            hw_set_bits(&hstx_ctrl_hw->csr, HSTX_CTRL_CSR_EN_BITS);
            hstx_video_recovery_phase = 0;
            return true;
        }
        __asm volatile ("nop");
    }
    hstx_halt_dma();
    hstx_video_recovery_phase = 4;
    return false;
}

static void __time_critical_func(hstx_queue_next_transfer)(uint slot) {
    hstx_video_packet_t *packet;
    if (next_vline >= V_ACTIVE_START) {
        packet = line_callback != NULL ? line_callback(next_vline - V_ACTIVE_START) : NULL;
        if (packet == NULL || ((uintptr_t)packet & 3u)) packet = BLACK_PACKET;
    } else {
        const bool vsync = next_vline >= V_FRONT && next_vline < V_FRONT + V_SYNC;
        port3DA = 9;
        packet = BLANK_PACKET(vsync);
        if (next_vline == V_FRONT) {
            ++hstx_video_frame_count;
            if (vsync_callback != NULL) vsync_callback();
        }
    }
    hstx_packet_set_island(packet, hstx_audio_island(next_vline));
    hstx_arm_transfer(slot, packet);
    if (++next_vline == V_TOTAL) next_vline = 0;
}

static void __time_critical_func(hstx_dma_irq_handler)(void) {
    uint32_t pending = dma_hw->ints1 & dma_mask;
    if (pending == 0 || !running) {
        return;
    }
    const uint slot = expected_completed_slot;
    const uint32_t expected = 1u << dma_channels[slot];
    if (pending != expected) {
        /* Both completions accumulated or the expected chain was lost.
         * Reset promptly; never interpret the stale callback pointer as the
         * next scanline, and never acknowledge another driver's IRQ source. */
        ++hstx_video_restart_count;
        hstx_halt_dma();
        running = hstx_start_pipeline();
        return;
    }

    dma_hw->ints1 = expected;
    hw_clear_bits(&dma_hw->ch[dma_channels[slot]].al1_ctrl,
                  DMA_CH0_CTRL_TRIG_EN_BITS);
    __dmb();
    /* The partner could have finished between the pending snapshot above
     * and this EN clear, retriggering the completed slot with its old source
     * and reload count. Clearing EN pauses an active channel: BUSY stays set.
     * Do not overwrite source/count on that paused transfer. A new pending
     * completion also means the pipeline has outrun this rearm operation. */
    if ((dma_hw->ch[dma_channels[slot]].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) ||
            (dma_hw->ints1 & dma_mask)) {
        ++hstx_video_restart_count;
        hstx_halt_dma();
        running = hstx_start_pipeline();
        return;
    }
    expected_completed_slot ^= 1u;
    hstx_queue_next_transfer(slot);

    /* If the partner completed while this channel was disabled, its chain
     * trigger was lost. Read the departing partner BEFORE the rearmed slot:
     * the reverse order can sample the slot before it starts, then sample
     * the partner after it stops, falsely observing both idle during a
     * perfectly normal chain handoff. */
    __dmb();
    dma_channel_hw_t *partner = &dma_hw->ch[dma_channels[slot ^ 1u]];
    dma_channel_hw_t *rearmed = &dma_hw->ch[dma_channels[slot]];
    uint32_t busy = partner->ctrl_trig;
    busy |= rearmed->ctrl_trig;
    if (!(busy & DMA_CH0_CTRL_TRIG_BUSY_BITS)) {
        /* Confirm once, in the same order, after the first two MMIO reads
         * and a barrier have allowed the chain trigger to propagate. This
         * is a bounded recheck, not a wait loop inside the video IRQ. */
        __dmb();
        busy = partner->ctrl_trig;
        busy |= rearmed->ctrl_trig;
        if (!(busy & DMA_CH0_CTRL_TRIG_BUSY_BITS)) {
            ++hstx_video_restart_count;
            hstx_halt_dma();
            running = hstx_start_pipeline();
        }
    }
}

void hstx_video_set_line_callback(hstx_video_line_callback_t callback) {
    assert(!running);
    line_callback = callback;
}

void hstx_video_set_vsync_callback(hstx_video_vsync_callback_t callback) {
    assert(!running);
    vsync_callback = callback;
}

bool hstx_video_init(void) {
    if (initialized) {
        return owner_core == get_core_num();
    }
    // This backend owns IRQ1. Reject both shared and exclusive handlers;
    // irq_get_exclusive_handler() alone would miss an existing shared chain.
    if (irq_has_handler(DMA_IRQ_1)) {
        return false;
    }
    dma_channels[0] = dma_claim_unused_channel(false);
    if (dma_channels[0] < 0) {
        return false;
    }
    dma_channels[1] = dma_claim_unused_channel(false);
    if (dma_channels[1] < 0) {
        dma_channel_unclaim((uint)dma_channels[0]);
        dma_channels[0] = -1;
        return false;
    }
    dma_mask = (1u << dma_channels[0]) | (1u << dma_channels[1]);
    for (uint slot = 0; slot < 2; ++slot) {
        const uint channel = (uint)dma_channels[slot];
        dma_channel_cleanup(channel);
        dma_channel_config config = dma_channel_get_default_config(channel);
        channel_config_set_read_increment(&config, true);
        channel_config_set_write_increment(&config, false);
        channel_config_set_chain_to(&config, (uint)dma_channels[slot ^ 1u]);
        channel_config_set_dreq(&config, DREQ_HSTX);
        channel_config_set_high_priority(&config, true);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
        dma_ctrl32[slot] = config.ctrl;
    }
    hstx_audio_init();
    owner_core = get_core_num();
    previous_irq_priority = irq_get_priority(DMA_IRQ_1);
    irq_was_enabled = irq_is_enabled(DMA_IRQ_1);
    irq_set_exclusive_handler(DMA_IRQ_1, hstx_dma_irq_handler);
    initialized = true;
    return true;
}

bool hstx_video_start(void) {
    if (!initialized || owner_core != get_core_num()) {
        return false;
    }
    if (running) {
        return true;
    }
    if (clock_get_hz(clk_hstx) != HSTX_VIDEO_CLOCK_HZ ||
            !(clocks_hw->clk[clk_hstx].ctrl & CLOCKS_CLK_HSTX_CTRL_ENABLE_BITS)) {
        return false;
    }
    const uint32_t interrupts = save_and_disable_interrupts();
    for (uint i = 0; i < 8; ++i) {
        previous_pin_functions[i] = gpio_get_function(HSTX_VIDEO_PIN_BASE + i);
        gpio_set_function(HSTX_VIDEO_PIN_BASE + i, GPIO_FUNC_HSTX);
    }
    pins_owned = true;
    irq_set_priority(DMA_IRQ_1, 0);
    irq_set_enabled(DMA_IRQ_1, true);
    running = hstx_start_pipeline();
    restore_interrupts(interrupts);
    if (!running) {
        hstx_video_stop();
    }
    return running;
}

void hstx_video_stop(void) {
    if (!initialized) {
        return;
    }
    assert(owner_core == get_core_num());
    const uint32_t interrupts = save_and_disable_interrupts();
    running = false;
    irq_set_enabled(DMA_IRQ_1, false);
    hstx_halt_dma();
    irq_clear(DMA_IRQ_1);
    reset_block(RESETS_RESET_HSTX_BITS);
    if (pins_owned) {
        for (uint i = 0; i < 8; ++i) {
            gpio_set_function(HSTX_VIDEO_PIN_BASE + i, previous_pin_functions[i]);
        }
        pins_owned = false;
    }
    irq_set_priority(DMA_IRQ_1, previous_irq_priority);
    irq_set_enabled(DMA_IRQ_1, irq_was_enabled);
    restore_interrupts(interrupts);
}

void hstx_video_deinit(void) {
    if (!initialized) {
        return;
    }
    assert(owner_core == get_core_num());
    hstx_video_stop();
    const uint32_t interrupts = save_and_disable_interrupts();
    irq_set_enabled(DMA_IRQ_1, false);
    irq_clear(DMA_IRQ_1);
    irq_remove_handler(DMA_IRQ_1, hstx_dma_irq_handler);
    for (uint slot = 0; slot < 2; ++slot) {
        dma_channel_cleanup((uint)dma_channels[slot]);
        dma_channel_unclaim((uint)dma_channels[slot]);
        dma_channels[slot] = -1;
    }
    dma_mask = 0;
    initialized = false;
    irq_set_enabled(DMA_IRQ_1, irq_was_enabled);
    restore_interrupts(interrupts);
}

bool hstx_video_is_running(void) {
    return running;
}
