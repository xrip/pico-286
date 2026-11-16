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

// Output lines: the blank ones first, then the active ones.
enum {
    V_FRONT = 10,
    V_SYNC = 2,
    V_BACK = 33,
    V_ACTIVE_START = V_FRONT + V_SYNC + V_BACK,
    V_TOTAL = V_ACTIVE_START + HSTX_VIDEO_HEIGHT
};

/* Called for output lines 0..479 from DMA_IRQ_1, on the start() core.
 * The callback and everything it reads must be in SRAM. It may render a
 * bounded scanline, but must not block, allocate, log, or disable interrupts
 * for an unbounded time. Returned storage must remain unchanged until the
 * fourth subsequent line callback: four buffers indexed by (line & 3) work.
 * Return a complete packet initialized with hstx_packet_init(packet,true,false).
 * Fill its pixel payload directly; NULL selects a complete black packet.
 * With HDMI_SOUND the driver writes the packet's data island, so every line
 * needs a packet of its own, even when two lines show the same pixels.
 * No whole-frame RGB framebuffer is allocated by this driver. */
typedef hstx_video_packet_t *(*hstx_video_line_callback_t)(uint32_t output_line);
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
 * Without HDMI_SOUND: DVI-compatible TMDS video only, no data islands.
 * With HDMI_SOUND: HDMI, every line carries a data island (hstx_audio.c). */
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

#ifdef __cplusplus
}
#endif
