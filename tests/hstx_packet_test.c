// gcc -std=c17 -O2 -Wall -Wextra -Werror -Idrivers/hdmi-hstx tests/hstx_packet_test.c -o hstx_packet_test
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

int main(void) {
    test_packets();
    test_packet_lifetimes();
    puts("HSTX packets: pixels, blanking, FIFO accept positions and buffer lifetime checks passed");
}
