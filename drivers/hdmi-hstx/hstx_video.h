#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hstx_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HSTX_VIDEO_WIDTH = 640,
    HSTX_VIDEO_HEIGHT = 480,
    HSTX_VIDEO_CLOCK_HZ = 126000000,
    HSTX_VIDEO_PIN_BASE = 12
};

/* Called for output lines 0..479 from DMA_IRQ_1, on the start() core.
 * The callback and everything it reads must be in SRAM. It may render a
 * bounded scanline, but must not block, allocate, log, or disable interrupts
 * for an unbounded time. Returned storage must remain unchanged until the
 * fourth subsequent line callback: four buffers indexed by (line & 3) work.
 * Return a complete packet initialized with hstx_packet_init(packet,true,false).
 * Fill its pixel payload directly; NULL selects a complete black packet.
 * No whole-frame RGB framebuffer is allocated by this driver. */
typedef const hstx_video_packet_t *(*hstx_video_line_callback_t)(uint32_t output_line);
typedef void (*hstx_video_vsync_callback_t)(void);

/* Set callbacks only while stopped. init/start/stop/deinit must run on the
 * same core. Calling stop() drains DMA before returning, after which all
 * pixel buffers may immediately be reused or released. */
void hstx_video_set_line_callback(hstx_video_line_callback_t callback);
void hstx_video_set_vsync_callback(hstx_video_vsync_callback_t callback);

/* Claims two unused DMA channels and an exclusive DMA_IRQ_1 handler.
 * Rejects any existing shared or exclusive IRQ1 handler. stop/deinit restore
 * the prior IRQ enable state and priority. Does not change PLLs, clocks,
 * the system bus priority, or any NTSC resources. */
bool hstx_video_init(void);

/* Requires a previously configured, enabled 126 MHz clk_hstx. Uses GPIO12..19:
 * CLK-/+, D0-/+, D1-/+, D2-/+. Outputs 640x480 at exactly 60 Hz when the
 * supplied clock is exact (800 x 525 totals, 25.2 MHz pixel clock).
 * DVI-compatible TMDS video only: no HDMI audio or data islands. */
bool hstx_video_start(void);
void hstx_video_stop(void);
void hstx_video_deinit(void);
bool hstx_video_is_running(void);

/* Diagnostics: frames scheduled and restarts after a missed DMA deadline.
 * A nonzero restart count means the interrupt latency/render budget was
 * exceeded; recovery is not a substitute for meeting that budget. */
extern volatile uint32_t hstx_video_frame_count;
extern volatile uint32_t hstx_video_restart_count;
/* Cold-path recovery progress: 0=running, 1=disable/abort, 2=serializer reset,
 * 3=prime FIFO, 4=prime failed. Useful if a failed output stops making IRQs. */
extern volatile uint32_t hstx_video_recovery_phase;

typedef struct {
    int32_t channel[2];
    uint32_t ctrl[2];
    uint32_t count[2];
    uint32_t read_addr[2];
    uint32_t write_addr[2];
    uint32_t dreq_credits[2];
    uint32_t intr;
    uint32_t ints1;
    uint32_t inte1;
    uint32_t abort;
    uint32_t fifo_stat;
    uint32_t hstx_csr;
    uint32_t expected_slot;
    uint32_t next_line;
    uint32_t owner_core;
    uint32_t running;
    uint32_t frames;
    uint32_t restarts;
    uint32_t recovery_phase;
} hstx_video_debug_t;

/* On-demand reads only: no IRQ masking, output changes, or scanout logging.
 * Values may span a DMA handoff while output runs; a frozen engine is stable.
 * CPU-local NVIC state is deliberately excluded when called from the other core. */
void hstx_video_debug_snapshot(hstx_video_debug_t *snapshot);

#ifdef __cplusplus
}
#endif
