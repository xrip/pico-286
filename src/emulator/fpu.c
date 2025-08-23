#include "fpu.h"
#include "emulator.h"
#include <string.h>

// The global FPU state
fpu_t fpu;

// --- Helper macros and functions ---

// Status Word stack top pointer (ST)
#define FPU_GET_ST() ((fpu.status_word >> 11) & 7)
#define FPU_SET_ST(st) (fpu.status_word = (fpu.status_word & 0xC7FF) | (((st) & 7) << 11))

// Tag Word access
static inline int fpu_get_tag(int reg_idx) {
    return (fpu.tag_word >> (reg_idx * 2)) & 3;
}

static inline void fpu_set_tag(int reg_idx, int tag) {
    fpu.tag_word &= ~(3 << (reg_idx * 2));
    fpu.tag_word |= (tag & 3) << (reg_idx * 2);
}

// FPU stack operations
static inline void fpu_stack_push() {
    uint8_t st = FPU_GET_ST();
    st = (st - 1) & 7;
    FPU_SET_ST(st);
}

static inline void fpu_stack_pop() {
    fpu_set_tag(FPU_GET_ST(), FPU_TAG_EMPTY);
    uint8_t st = FPU_GET_ST();
    st = (st + 1) & 7;
    FPU_SET_ST(st);
}

// --- Data Conversion ---

static float80 float32_to_float80(uint32_t f32) {
    float80 f80;
    int sign = (f32 >> 31) & 1;
    int exponent = (f32 >> 23) & 0xFF;
    uint32_t fraction = f32 & 0x7FFFFF;

    if (exponent == 0 && fraction == 0) { // Zero
        f80.significand = 0;
        f80.sign_exponent = 0;
        if (sign) f80.sign_exponent |= 0x8000;
        return f80;
    }

    if (exponent == 255) { // NaN or Infinity
        f80.sign_exponent = 0x7FFF | (sign << 15);
        f80.significand = (fraction == 0) ? 0x8000000000000000 : (uint64_t)(fraction | 0x400000) << 40;
        return f80;
    }

    exponent = exponent - 127 + 16383;
    uint64_t significand = ((uint64_t)fraction << 40) | 0x8000000000000000;

    f80.sign_exponent = exponent | (sign << 15);
    f80.significand = significand;
    return f80;
}

static uint32_t float80_to_float32(float80 f80) {
    int sign = (f80.sign_exponent >> 15) & 1;
    int exponent = f80.sign_exponent & 0x7FFF;
    uint64_t significand = f80.significand;

    if (exponent == 0 && significand == 0) { // Zero
        return sign << 31;
    }

    exponent = exponent - 16383 + 127;
    uint32_t fraction = (significand >> 40) & 0x7FFFFF;

    if (exponent <= 0) { // Underflow to zero
        return sign << 31;
    }
    if (exponent >= 255) { // Overflow to infinity
        return (sign << 31) | (0xFF << 23);
    }

    return (sign << 31) | (exponent << 23) | fraction;
}

// --- FPU Arithmetic ---

// Stub for 80-bit float addition
static float80 fpu_add(float80 a, float80 b) {
    // This is a complex operation. For now, just return one of the operands
    // to have a placeholder.
    return a;
}


// --- FPU Instructions ---

static void fpu_load_m32real() {
    extern uint32_t ea;
    uint32_t mem_val = readdw86(ea);

    fpu_stack_push();
    uint8_t st = FPU_GET_ST();
    fpu.regs[st] = float32_to_float80(mem_val);

    if (fpu.regs[st].significand == 0 && (fpu.regs[st].sign_exponent & 0x7FFF) == 0) {
        fpu_set_tag(st, FPU_TAG_ZERO);
    } else {
        fpu_set_tag(st, FPU_TAG_VALID);
    }
}

static void fpu_store_m32real_and_pop() {
    extern uint32_t ea;
    uint8_t st = FPU_GET_ST();
    uint32_t f32 = float80_to_float32(fpu.regs[st]);
    writedw86(ea, f32);
    fpu_stack_pop();
}


// --- FPU Implementation ---

void fpu_reset(void) {
    fpu.control_word = 0x037F;
    fpu.status_word = 0x0000;
    fpu.tag_word = 0xFFFF;
    fpu.fip = 0;
    fpu.fcs = 0;
    fpu.fdp = 0;
    fpu.fds = 0;
    fpu.fop = 0;
}

void fpu_op(uint8_t opcode) {
    extern uint8_t mode, reg, rm;
    uint8_t st = FPU_GET_ST();

    switch (opcode) {
        case 0xD8:
            if (mode == 3) { // Register operand
                uint8_t i = rm;
                float80* st0 = &fpu.regs[st];
                float80* sti = &fpu.regs[(st + i) & 7];
                switch (reg) {
                    case 0: // FADD ST(0), ST(i)
                        *st0 = fpu_add(*st0, *sti);
                        break;
                }
            }
            break;

        case 0xD9:
            if (mode == 3) {
                uint8_t op = rm | (reg << 3);
                if (op == 0xE8) { // FLD1
                    fpu_stack_push();
                    fpu.regs[FPU_GET_ST()].significand = 0x8000000000000000;
                    fpu.regs[FPU_GET_ST()].sign_exponent = 0x3FFF;
                    fpu_set_tag(FPU_GET_ST(), FPU_TAG_VALID);
                } else if (op == 0xEE) { // FLDZ
                    fpu_stack_push();
                    fpu.regs[FPU_GET_ST()].significand = 0;
                    fpu.regs[FPU_GET_ST()].sign_exponent = 0;
                    fpu_set_tag(FPU_GET_ST(), FPU_TAG_ZERO);
                }
            } else { // Memory operand
                switch (reg) {
                    case 0: // FLD m32real
                        fpu_load_m32real();
                        break;
                    case 3: // FSTP m32real
                        fpu_store_m32real_and_pop();
                        break;
                }
            }
            break;

        case 0xDB:
            if (mode == 3 && rm == 0xE3) {
                fpu_reset();
            }
            break;

        case 0xDD:
             if (mode == 3) {
                fpu.regs[rm] = fpu.regs[st];
                fpu_set_tag(rm, fpu_get_tag(st));
                fpu_stack_pop();
            }
            break;
    }
}
