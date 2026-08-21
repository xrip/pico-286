#include "emulator.h"

i8259_s i8259 = {
    .interrupt_mask_register = 0xFF,
    .interrupt_vector_offset = 0x08,
};
