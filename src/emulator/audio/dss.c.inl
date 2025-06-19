// https://archive.org/details/dss-programmers-guide
#pragma GCC optimize("Ofast")
#include "emulator/emulator.h"

#define FIFO_BUFFER_SIZE 16

static uint8_t fifo_buffer[FIFO_BUFFER_SIZE] = { 0 };
static uint8_t fifo_head = 0;  // Write position
static uint8_t fifo_tail = 0;  // Read position
static uint8_t fifo_count = 0; // Number of elements
static uint8_t dss_data;

int16_t covox_sample = 0;

static const int16_t sample_lut[256] = {
    // Pre-computed (i-128) << 6 for i = 0..255
    -8192, -8128, -8064, -8000, -7936, -7872, -7808, -7744,
    -7680, -7616, -7552, -7488, -7424, -7360, -7296, -7232,
    -7168, -7104, -7040, -6976, -6912, -6848, -6784, -6720,
    -6656, -6592, -6528, -6464, -6400, -6336, -6272, -6208,
    -6144, -6080, -6016, -5952, -5888, -5824, -5760, -5696,
    -5632, -5568, -5504, -5440, -5376, -5312, -5248, -5184,
    -5120, -5056, -4992, -4928, -4864, -4800, -4736, -4672,
    -4608, -4544, -4480, -4416, -4352, -4288, -4224, -4160,
    -4096, -4032, -3968, -3904, -3840, -3776, -3712, -3648,
    -3584, -3520, -3456, -3392, -3328, -3264, -3200, -3136,
    -3072, -3008, -2944, -2880, -2816, -2752, -2688, -2624,
    -2560, -2496, -2432, -2368, -2304, -2240, -2176, -2112,
    -2048, -1984, -1920, -1856, -1792, -1728, -1664, -1600,
    -1536, -1472, -1408, -1344, -1280, -1216, -1152, -1088,
    -1024,  -960,  -896,  -832,  -768,  -704,  -640,  -576,
     -512,  -448,  -384,  -320,  -256,  -192,  -128,   -64,
        0,    64,   128,   192,   256,   320,   384,   448,
      512,   576,   640,   704,   768,   832,   896,   960,
     1024,  1088,  1152,  1216,  1280,  1344,  1408,  1472,
     1536,  1600,  1664,  1728,  1792,  1856,  1920,  1984,
     2048,  2112,  2176,  2240,  2304,  2368,  2432,  2496,
     2560,  2624,  2688,  2752,  2816,  2880,  2944,  3008,
     3072,  3136,  3200,  3264,  3328,  3392,  3456,  3520,
     3584,  3648,  3712,  3776,  3840,  3904,  3968,  4032,
     4096,  4160,  4224,  4288,  4352,  4416,  4480,  4544,
     4608,  4672,  4736,  4800,  4864,  4928,  4992,  5056,
     5120,  5184,  5248,  5312,  5376,  5440,  5504,  5568,
     5632,  5696,  5760,  5824,  5888,  5952,  6016,  6080,
     6144,  6208,  6272,  6336,  6400,  6464,  6528,  6592,
     6656,  6720,  6784,  6848,  6912,  6976,  7040,  7104,
     7168,  7232,  7296,  7360,  7424,  7488,  7552,  7616,
     7680,  7744,  7808,  7872,  7936,  8000,  8064,  8128
};

INLINE int16_t dss_sample() {
    if (__builtin_expect(fifo_count == 0, 0)) {
        return 0;
    }
    
    const uint8_t sample = fifo_buffer[fifo_tail];
    fifo_tail = fifo_tail + 1 & FIFO_BUFFER_SIZE - 1;  // Fast modulo for power-of-2
    --fifo_count;
    
    return sample_lut[sample];
}

static INLINE void fifo_push_byte(uint8_t value) { // core #0
    if (__builtin_expect(fifo_count >= FIFO_BUFFER_SIZE, 0))
        return;
        
    fifo_buffer[fifo_head] = value;
    fifo_head = fifo_head + 1 & FIFO_BUFFER_SIZE - 1;  // Fast modulo for power-of-2
    ++fifo_count;
}

static INLINE uint8_t fifo_is_full() {
    return fifo_count == FIFO_BUFFER_SIZE ? 0x40 : 0x00;
}

static INLINE uint8_t dss_in(const uint16_t portnum) {
    return portnum & 1 ? fifo_is_full() : dss_data;
}

static INLINE void dss_out(const uint16_t portnum, const uint8_t value) {
    static uint8_t control = 0;
    
    if (__builtin_expect(portnum == 0x378, 1)) {
        // Data port - store value and push to FIFO (most common case)
        dss_data = value;
        fifo_push_byte(value);
    } else if (portnum == 0x37A) {
        // Control port - check for strobe edge (bit 2: 0->1 transition)  
        if ((value & 4) && !(control & 4)) {
            fifo_push_byte(dss_data);
        }
        control = value;
    }
}