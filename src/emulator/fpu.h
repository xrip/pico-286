#pragma once

#include <stdint.h>

// 8087 uses 80-bit extended precision floating point numbers.
// We'll represent them as a struct.
typedef struct {
    uint64_t significand;
    uint16_t sign_exponent; // 1-bit sign, 15-bit exponent
} float80;

// FPU State
typedef struct {
    float80 regs[8];
    uint16_t control_word;
    uint16_t status_word;
    uint16_t tag_word;

    // Last instruction and data pointers
    uint32_t fip; // FPU Instruction Pointer
    uint16_t fcs; // FPU Code Segment
    uint32_t fdp; // FPU Data Pointer
    uint16_t fds; // FPU Data Segment
    uint16_t fop; // FPU Opcode

} fpu_t;

// The FPU state will be a global variable, similar to other hardware components.
extern fpu_t fpu;

// FPU Tag Word states
#define FPU_TAG_VALID   0
#define FPU_TAG_ZERO    1
#define FPU_TAG_SPECIAL 2
#define FPU_TAG_EMPTY   3

// Function prototypes
void fpu_reset(void);
void fpu_op(uint8_t opcode);
