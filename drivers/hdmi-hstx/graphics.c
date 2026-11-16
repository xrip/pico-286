#include "hdmi_hstx.h"
#include "hstx_video.h"
#include "hstx_render.h"

#include <graphics.h>
#include <graphics_modes.h>
#include <graphics_pc_raster.h>
#include <hardware/dma.h>
#include <hardware/sync.h>
#include <pico.h>

#if defined(NTSC_TV)
#include <ntsc-composite.h>
#endif

#if defined(GRAPHICS_NO_BUILTIN_FRAMEBUFFER)
#define HSTX_INITIAL_FRAMEBUFFER NULL
#else
static uint8_t framebuffer[GRAPHICS_FRAME_WIDTH * GRAPHICS_FRAME_HEIGHT]
        __attribute__((aligned(4)));
#define HSTX_INITIAL_FRAMEBUFFER framebuffer
#endif

static const uint8_t *active_framebuffer = HSTX_INITIAL_FRAMEBUFFER;
static const uint8_t *volatile pending_framebuffer;
static volatile uint32_t frame_count;
static bool started;
#if defined(GRAPHICS_PC_EMULATOR)
extern volatile uint8_t port3DA;
#endif

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

// HSTX may retain a callback's pixels while another line is being prepared.
// Four slots satisfy that lifetime without a second full framebuffer.
static hstx_video_packet_t packets[4];
static unsigned packet_slot = 3;
static uint8_t staging[GRAPHICS_FRAME_WIDTH] __attribute__((aligned(4)));
static enum graphics_mode_t repeated_mode;
static hstx_video_packet_t *repeated_packet;
#if defined(HDMI_SOUND)
static uint copy_channel;
#endif

static hstx_video_packet_t *__time_critical_func(hstx_graphics_line)(
        uint32_t output_line) {
    const enum graphics_mode_t mode = graphics_source_mode();
#if defined(GRAPHICS_PC_EMULATOR)
    port3DA = graphics_pc_raster_status(mode, output_line, false);
#endif
    if (graphics_source_is_pc_mode()) {
        const unsigned width = graphics_source_native_width();
        if (width == GRAPHICS_FRAME_WIDTH && (output_line & 1u) &&
                repeated_packet != NULL && repeated_mode == mode) {
#if defined(HDMI_SOUND)
            // The line carries its own data island, so it needs a packet of its own.
            // DMA copies the pixels: the HSTX reads them only after the current
            // line and this line's header, long after the copy ends.
            packet_slot = hstx_packet_next_slot(packet_slot);
            hstx_video_packet_t *packet = &packets[packet_slot];
            dma_channel_set_write_addr(copy_channel, packet->payload.pairs, false);
            dma_channel_transfer_from_buffer_now(copy_channel, repeated_packet->payload.pairs,
                                                 HSTX_PACKET_PIXEL_WORDS);
            return packet;
#else
            return repeated_packet;
#endif
        }
        packet_slot = hstx_packet_next_slot(packet_slot);
        hstx_video_packet_t *packet = &packets[packet_slot];
        if (width == GRAPHICS_FRAME_WIDTH) {
            graphics_source_render_line32(output_line, width, 480u,
                                           packet->payload.pairs, doubled_palette);
        } else {
            graphics_source_render_line16(output_line, width, 480u,
                                           packet->payload.pixels, graphics_palette);
        }
        repeated_mode = mode;
        repeated_packet = packet;
#if defined(GRAPHICS_PC_EMULATOR)
        port3DA = graphics_pc_raster_status(mode, output_line, true);
#endif
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
#if defined(GRAPHICS_PC_EMULATOR)
    port3DA = graphics_pc_raster_status(mode, output_line, true);
#endif
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
    ++frame_count;
}

void hdmi_hstx_graphics_init(void) {
    if (started) {
        return;
    }
    graphics_source_init();
    for (unsigned i = 0; i < 4; ++i) {
        hstx_packet_init(&packets[i], true, false);
    }
#if defined(HDMI_SOUND)
    copy_channel = (uint)dma_claim_unused_channel(true);
    dma_channel_config copy = dma_channel_get_default_config(copy_channel);
    channel_config_set_write_increment(&copy, true);
    channel_config_set_irq_quiet(&copy, true);
    dma_channel_set_config(copy_channel, &copy, false);
#endif
    hstx_video_set_line_callback(hstx_graphics_line);
    hstx_video_set_vsync_callback(hstx_graphics_vsync);
    if (!hstx_video_init()) {
        panic("HSTX initialization failed");
    }
#if defined(NTSC_TV)
    ntsc_composite_init(active_framebuffer);
    dma_start_channel_mask(ntsc_composite_start_mask());
#endif
    if (!hstx_video_start()) {
        panic("HSTX start failed");
    }
    started = true;
}

void graphics_set_mode(enum graphics_mode_t mode) {
    graphics_source_set_mode(mode);
}

void graphics_wait_vblank(void) {
    if (!started) {
        return;
    }
    const uint32_t previous = frame_count;
    while (frame_count == previous) {
        tight_loop_contents();
    }
    __mem_fence_acquire();
}

void graphics_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height) {
    graphics_source_set_buffer(buffer, width, height);
    if (width == GRAPHICS_FRAME_WIDTH && height == GRAPHICS_FRAME_HEIGHT) {
        // Immediate binding, as in the VGA/NTSC API. Use present for a safe
        // double-buffer swap while scanout is running.
        active_framebuffer = buffer;
#if defined(NTSC_TV)
        ntsc_composite_set_framebuffer(buffer);
#endif
    }
}

void graphics_set_offset(int x, int y) {
    graphics_source_set_offset(x, y);
}

void graphics_set_palette(uint8_t index, uint32_t color) {
#if defined(NTSC_TV)
    ntsc_composite_set_palette(index, color);
#endif
#if defined(GRAPHICS_PC_EMULATOR)
    color = graphics_pc_palette_color(index, color, false);
#endif
    const uint32_t pixel = hstx_rgb888_to_bgr555(color);
    graphics_palette[index] = (uint16_t)pixel;
    doubled_palette[index] = pixel | (pixel << 16);
}

void graphics_set_bgcolor(uint32_t color888) {
    (void)color888;
}

uint8_t *graphics_get_framebuffer(void) {
    return HSTX_INITIAL_FRAMEBUFFER;
}

void graphics_present_framebuffer(const uint8_t *buffer) {
    if (buffer == NULL) {
        return;
    }
    if (!started) {
        active_framebuffer = buffer;
        return;
    }

    // Each output latches independently at its own frame boundary. Do not
    // release the old application buffer until BOTH outputs have taken this
    // one: their frame rates are deliberately not locked to each other.
    __mem_fence_release();
    pending_framebuffer = buffer;
#if defined(NTSC_TV)
    ntsc_composite_request_framebuffer(buffer);
    ntsc_composite_wait_framebuffer();
#endif
    while (pending_framebuffer != NULL) {
        tight_loop_contents();
    }
    __mem_fence_acquire();
}
