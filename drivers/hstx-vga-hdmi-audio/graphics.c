#include "hdmi_hstx.h"
#include "hstx_video.h"
#include "hstx_render.h"

#include <graphics.h>
#include <graphics_modes.h>
#include <graphics_pc_raster.h>
#include <hardware/dma.h>
#include <hardware/sync.h>
#include <pico.h>


#define HSTX_INITIAL_FRAMEBUFFER NULL

static const uint8_t *active_framebuffer = HSTX_INITIAL_FRAMEBUFFER;
static const uint8_t *volatile pending_framebuffer;
static volatile uint32_t frame_count;
static bool started;
extern volatile uint8_t port3DA;
extern void crtc_latch_start_address(void);

// All 256 graphics indices are colors; HSTX commands carry synchronization
// separately. A halfword store makes an individual palette update atomic.
static uint16_t graphics_palette[GRAPHICS_COLOR_COUNT];
static uint32_t doubled_palette[GRAPHICS_COLOR_COUNT];

#define HSTX_TEXT_COLOR(i) \
    (uint16_t)(((GRAPHICS_CGA_RGB(i) >> 19) & 0x001fu) | \
               ((GRAPHICS_CGA_RGB(i) >> 6) & 0x03e0u) | \
               ((GRAPHICS_CGA_RGB(i) << 7) & 0x7c00u)),
// Explicit placement prevents LTO moving a read-only lookup back into XIP.
static const uint16_t __not_in_flash("hstx_text_palette") text_palette[16] = {
    GRAPHICS_TEXT_COLORS(HSTX_TEXT_COLOR)
};
#undef HSTX_TEXT_COLOR

// VGA: the same render calls with PWM words in place of BGR555 pairs.
static bool vga, vga_frame_wide;
static uint32_t pwm_palette[GRAPHICS_COLOR_COUNT];

// HSTX may retain a callback's pixels while another line is being prepared.
// Four slots satisfy that lifetime without a second full framebuffer.
static hstx_video_packet_t packets[4];
static unsigned packet_slot = 3;
static uint8_t staging[GRAPHICS_FRAME_WIDTH] __attribute__((aligned(4)));
static enum graphics_mode_t repeated_mode;
static hstx_video_packet_t *repeated_packet;
static uint copy_channel;

static hstx_video_packet_t *__time_critical_func(hstx_graphics_line)(
        uint32_t output_line) {
    const enum graphics_mode_t mode = graphics_source_mode();
    port3DA = graphics_pc_raster_status(mode, output_line, false);
    if (graphics_source_is_pc_mode()) {
        const unsigned width = graphics_source_native_width();
        if (vga && (width != GRAPHICS_FRAME_WIDTH) != vga_frame_wide) return NULL; // black till vsync
        if (width == GRAPHICS_FRAME_WIDTH && (output_line & 1u) &&
                repeated_packet != NULL && repeated_mode == mode) {
            // The line carries its own data island, so it needs a packet of its own.
            // DMA copies the pixels: the HSTX reads them only after the current
            // line and this line's header, long after the copy ends.
            packet_slot = hstx_packet_next_slot(packet_slot);
            hstx_video_packet_t *packet = &packets[packet_slot];
            dma_channel_set_write_addr(copy_channel, packet->payload.pairs, false);
            dma_channel_transfer_from_buffer_now(copy_channel, repeated_packet->payload.pairs,
                                                 HSTX_PACKET_PIXEL_WORDS);
            return packet;
        }
        packet_slot = hstx_packet_next_slot(packet_slot);
        hstx_video_packet_t *packet = &packets[packet_slot];
        if (width == GRAPHICS_FRAME_WIDTH) {
            graphics_source_render_line32(output_line, width, 480u,
                                           packet->payload.pairs, vga ? pwm_palette : doubled_palette);
        } else if (vga) {
            graphics_source_render_line32(output_line, width, 480u,
                                           packet->payload.vga, pwm_palette);
        } else {
            graphics_source_render_line16(output_line, width, 480u,
                                           packet->payload.pixels, graphics_palette);
        }
        repeated_mode = mode;
        repeated_packet = packet;
        port3DA = graphics_pc_raster_status(mode, output_line, true);
        return packet;
    }

    packet_slot = hstx_packet_next_slot(packet_slot);
    hstx_video_packet_t *packet = &packets[packet_slot];
    if (graphics_source_is_text()) {
        struct graphics_text_line text;
        if (!graphics_text_line(output_line, 480u, &text)) {
            return NULL;
        }
        hstx_render_text(packet->payload.pixels, &text, text_palette);
    } else {
        const uint8_t *source = graphics_source_line(active_framebuffer,
                                                     output_line / 2u, staging);
        for (unsigned x = 0; x < GRAPHICS_FRAME_WIDTH; ++x) {
            packet->payload.pairs[x] = doubled_palette[source[x]];
        }
    }
    port3DA = graphics_pc_raster_status(mode, output_line, true);
    return packet;
}

static void __time_critical_func(hstx_graphics_vsync)(void) {
    const uint8_t *pending = pending_framebuffer;
    if (pending != NULL) {
        __mem_fence_acquire();
        active_framebuffer = pending;
        __mem_fence_release();
        pending_framebuffer = NULL;
    }
    graphics_text_frame_tick();
    crtc_latch_start_address();
    if (vga) { // 640 words a line for text and the other 640-wide modes
        vga_frame_wide = graphics_source_native_width() != GRAPHICS_FRAME_WIDTH;
        hstx_video_set_vga_wide(vga_frame_wide);
    }
    ++frame_count;
}

void hdmi_hstx_graphics_init(void) {
    if (started) {
        return;
    }
    graphics_source_init();
    copy_channel = (uint)dma_claim_unused_channel(true);
    dma_channel_config copy = dma_channel_get_default_config(copy_channel);
    channel_config_set_write_increment(&copy, true);
    channel_config_set_irq_quiet(&copy, true);
    dma_channel_set_config(copy_channel, &copy, false);
    hstx_video_set_line_callback(hstx_graphics_line);
    hstx_video_set_vsync_callback(hstx_graphics_vsync);
    if (!hstx_video_init()) {
        panic("HSTX initialization failed");
    }
    vga = hstx_video_is_vga();
    for (unsigned i = 0; i < 4; ++i) {
        if (vga) {
            hstx_vga_packet_init(&packets[i]);
        } else {
            hstx_packet_init(&packets[i], true, false);
        }
    }
    if (!hstx_video_start()) {
        panic("HSTX start failed");
    }
    started = true;
}

void graphics_set_mode(enum graphics_mode_t mode) {
    graphics_source_set_mode(mode);
}

void graphics_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height) {
    graphics_source_set_buffer(buffer, width, height);
    if (width == GRAPHICS_FRAME_WIDTH && height == GRAPHICS_FRAME_HEIGHT) {
        // Immediate binding, as in the VGA/NTSC API. Use present for a safe
        // double-buffer swap while scanout is running.
        active_framebuffer = buffer;
    }
}

void graphics_set_offset(int x, int y) {
    graphics_source_set_offset(x, y);
}

void graphics_set_palette(uint8_t index, uint32_t color) {
    color = graphics_pc_palette_color(index, color);
    if (vga) {
        pwm_palette[index] = hstx_rgb888_to_vga(color);
        return;
    }
    const uint32_t pixel = hstx_rgb888_to_bgr555(color);
    graphics_palette[index] = (uint16_t)pixel;
    doubled_palette[index] = pixel | (pixel << 16);
}

void graphics_set_bgcolor(uint32_t color888) {
    (void)color888;
}

