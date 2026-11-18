// gcc -std=c17 -O2 -Wall -Wextra -Werror -Idrivers/hstx-vga-hdmi-audio tests/hstx_packet_test.c -o hstx_packet_test
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hstx_packet.h"

typedef struct {
    unsigned pixels, active;
    unsigned read_at[HSTX_PACKET_WORDS];
    uint32_t values[800];
    bool encoded[800];
} trace_t;

static uint32_t take_word(const uint32_t *words, unsigned *read, trace_t *trace) {
    assert(*read < HSTX_PACKET_WORDS);
    trace->read_at[*read] = trace->pixels;
    return words[(*read)++];
}

// Model the documented command count as OUTPUT symbols. RAW_REPEAT never
// reloads its operand. RAW reloads every symbol (RAW_N_SHIFTS=1: each data
// island word is one symbol). TMDS reloads once per two symbols; ENC_SHIFT=16
// selects the next BGR555 halfword.
static trace_t decode(const hstx_video_packet_t *packet) {
    uint32_t words[HSTX_PACKET_WORDS];
    memcpy(words, packet, sizeof words);
    trace_t trace = {0};
    unsigned read = 0;
    while (read < HSTX_PACKET_WORDS) {
        const uint32_t command = take_word(words, &read, &trace);
        const unsigned op = command & 0xf000u;
        const unsigned count = command & 0xfffu;
        if (op == HSTX_CMD_NOP) {
            assert(count == 0);
            continue;
        }
        assert(op == HSTX_CMD_RAW_REPEAT || op == HSTX_CMD_RAW || op == HSTX_CMD_TMDS);
        uint32_t data = 0;
        if (op == HSTX_CMD_RAW_REPEAT) data = take_word(words, &read, &trace);
        for (unsigned i = 0; i < count; ++i) {
            if (op == HSTX_CMD_RAW || (op == HSTX_CMD_TMDS && !(i & 1u)))
                data = take_word(words, &read, &trace);
            assert(trace.pixels < 800);
            trace.encoded[trace.pixels] = op == HSTX_CMD_TMDS;
            trace.values[trace.pixels++] = op == HSTX_CMD_TMDS ?
                    ((data >> ((i & 1u) * 16u)) & 0xffffu) : data;
            trace.active += op == HSTX_CMD_TMDS;
        }
    }
    assert(trace.pixels == 800);
    return trace;
}

static void test_packets(void) {
    hstx_video_packet_t packets[3];
    trace_t traces[3];
    for (unsigned kind = 0; kind < 3; ++kind) {
        const bool active = kind == 0;
        const bool vsync = kind == 2;
        hstx_packet_init(&packets[kind], active, vsync);
        if (active) {
            for (unsigned x = 0; x < 640; ++x)
                packets[kind].payload.pixels[x] = (uint16_t)(x & 0x7fffu);
        }
        traces[kind] = decode(&packets[kind]);
        assert(traces[kind].active == (active ? 640u : 0u));
        for (unsigned x = 0; x < 800; ++x) {
            if (active && x >= 160) {
                assert(traces[kind].encoded[x]);
                assert(traces[kind].values[x] == x - 160);
            } else {
                // Blanking is raw symbols: control words, the data island and the guard bands.
                assert(!traces[kind].encoded[x]);
            }
        }
    }
    // With eight FIFO slots the DMA has its last word accepted at pop
    // HSTX_PACKET_WORDS - 9. Blank and vsync lines pop at the same positions.
    // An active line reads its pixels two per word, a blank line one per symbol,
    // so with the HDMI data island the last accept is 9 symbols apart
    // (782 against 791): completions are 791 or 809 pixels apart, not 800.
    const unsigned last_accept_pop = HSTX_PACKET_WORDS - 8u - 1u;
    assert(memcmp(traces[1].read_at, traces[2].read_at, sizeof traces[1].read_at) == 0);
    assert(traces[0].read_at[last_accept_pop] == 782u);
    assert(traces[1].read_at[last_accept_pop] == 791u);
    hstx_packet_init(&packets[0], true, false);
    const trace_t black = decode(&packets[0]);
    assert(black.active == 640);
    for (unsigned x = 160; x < 800; ++x) assert(black.values[x] == 0);
}

static void test_packet_lifetimes(void) {
    unsigned slot = 3;
    unsigned last_return[4] = {0};
    bool used[4] = {false};
    unsigned previous_mode = 0;
    unsigned repeated_slot = 0;
    bool have_repeated = false;
    for (unsigned call = 1; call < 480u * 10u; ++call) {
        const unsigned line = (call - 1) % 480u;
        // Switch on both odd and even physical lines, including frame wrap.
        const unsigned mode = (call / 37u) & 1u;
        const bool low = mode == 0;
        const bool repeat = low && (line & 1u) && have_repeated && previous_mode == mode;
        if (!repeat) {
            slot = hstx_packet_next_slot(slot);
            if (used[slot]) assert(call - last_return[slot] >= 4u);
            repeated_slot = slot;
            previous_mode = mode;
            have_repeated = true;
        }
        const unsigned returned = repeat ? repeated_slot : slot;
        used[returned] = true;
        last_return[returned] = call;
    }
}

// VGA: RAW_REPEAT gives `count` pixels of one word; RAW gives `count` pixels and
// reads a new word every `raw_shifts` pixels. Returns the pixel at which the DMA's
// last word goes into the eight-word FIFO.
static unsigned decode_vga(const uint32_t *words, unsigned length, unsigned raw_shifts, uint32_t line[800]) {
    unsigned read = 0, pixels = 0, last_accept = 0;
#define TAKE() (read == length - 9 ? last_accept = pixels : 0, words[read++])
    while (read < length) {
        const uint32_t command = TAKE();
        const unsigned op = command & 0xf000u, count = command & 0xfffu;
        assert(op == HSTX_CMD_RAW_REPEAT || op == HSTX_CMD_RAW);
        uint32_t data = op == HSTX_CMD_RAW_REPEAT ? TAKE() : 0;
        for (unsigned i = 0; i < count; ++i) {
            if (op == HSTX_CMD_RAW && i % raw_shifts == 0) data = TAKE();
            assert(pixels < 800);
            line[pixels++] = data;
        }
    }
    assert(pixels == 800 && read == length);
    return last_accept;
#undef TAKE
}

static void test_vga_packets(void) {
    static hstx_video_packet_t packet;
    uint32_t line[800];
    unsigned accepts[4];
    for (unsigned shifts = 1; shifts <= 2; ++shifts) {
        const uint32_t *words = hstx_vga_packet_init(&packet);
        for (unsigned x = 0; x < 640; ++x) packet.payload.vga[x] = x + 1;
        const unsigned length = HSTX_VGA_HEADER_WORDS + 640 / shifts;
        accepts[shifts - 1] = decode_vga(words, length, shifts, line);
        for (unsigned x = 0; x < 800; ++x)
            assert(line[x] == (x < 16 || (x >= 112 && x < 160) ? 0 : x < 112 ? HSTX_VGA_HSYNC : (x - 160) / shifts + 1));
    }
    for (unsigned vsync = 0; vsync < 2; ++vsync) {
        uint32_t blank[HSTX_VGA_BLANK_WORDS];
        hstx_vga_blank_init(blank, vsync);
        const uint32_t level = vsync ? HSTX_VGA_VSYNC : 0;
        accepts[2 + vsync] = decode_vga(blank, HSTX_VGA_BLANK_WORDS, 1, line);
        for (unsigned x = 0; x < 800; ++x)
            assert(line[x] == (x >= 16 && x < 112 ? (level | HSTX_VGA_HSYNC) : level));
    }
    // The DMA completions are near the line end on every kind of line, as with HDMI (782 and 791).
    assert(accepts[0] == 791 && accepts[1] == 782 && accepts[2] == 780 && accepts[3] == 780);
}

int main(void) {
    test_packets();
    test_packet_lifetimes();
    test_vga_packets();
    puts("HSTX packets: pixels, blanking, FIFO accept positions and buffer lifetime checks passed");
}
