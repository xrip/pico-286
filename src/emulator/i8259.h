#pragma once

#include <stdint.h>

typedef struct {
    uint8_t interrupt_mask_register;
    uint8_t interrupt_request_register;
    uint8_t in_service_register;
    uint8_t initialization_command_word_step;
    uint8_t initialization_command_words_1;
    uint8_t interrupt_vector_offset;
    uint8_t register_read_mode;
} i8259_s;

extern i8259_s i8259;

static inline uint8_t i8259_read(const uint16_t port_number) {
    switch (port_number) {
        case 0x20:
            return i8259.register_read_mode
                       ? i8259.in_service_register
                       : i8259.interrupt_request_register;
        case 0x21:
            return i8259.interrupt_mask_register;
        default:
            return 0xFF;
    }
}

static inline void i8259_write(const uint16_t port_number, const uint8_t data) {
    switch (port_number) {
        case 0x20:
            if (data & 0x10) { // ICW1: initialization clears pending and in-service IRQs
                i8259.interrupt_mask_register = 0x00;
                __atomic_store_n(&i8259.interrupt_request_register, 0, __ATOMIC_RELAXED);
                i8259.in_service_register = 0;
                i8259.initialization_command_words_1 = data;
                i8259.initialization_command_word_step = 2;
                i8259.register_read_mode = 0;
            } else if ((data & 0x08) == 0) {
                switch (data & 0xE0) {
                    case 0x20: // non-specific EOI: clear the highest-priority in-service IRQ
                        i8259.in_service_register &= i8259.in_service_register - 1;
                        break;
                    case 0x60:
                        i8259.in_service_register &= (uint8_t)~(1u << (data & 0x07));
                        break;
                    default:
                        break;
                }
            } else if ((data & 0x0A) == 0x0A) {
                i8259.register_read_mode = data & 1;
            }
            break;

        case 0x21:
            switch (i8259.initialization_command_word_step) {
                case 2:
                    i8259.interrupt_vector_offset = data & 0xF8;
                    i8259.initialization_command_word_step =
                        (i8259.initialization_command_words_1 & 0x02) ? 4 : 3;
                    break;
                case 3:
                    i8259.initialization_command_word_step =
                        (i8259.initialization_command_words_1 & 0x01) ? 4 : 5;
                    break;
                case 4:
                    i8259.initialization_command_word_step = 5;
                    break;
                case 5:
                    i8259.interrupt_mask_register = data;
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

// IRR is set from other threads/cores (timer, sound), so its read-modify-writes are atomic.
// Called for every instruction: plain reads and no __builtin_ctz (a library call on Cortex-M0+).
static inline uint8_t i8259_get_pending_irqs(void) {
    const uint8_t isr = i8259.in_service_register;
    // fully nested mode: only IRQs with higher priority than the highest one in service
    const uint8_t higher_priority = (uint8_t)((isr & -isr) - 1);
    return i8259.interrupt_request_register & (uint8_t)~i8259.interrupt_mask_register & higher_priority;
}

static inline void i8259_interrupt(const uint8_t irq) {
    __atomic_fetch_or(&i8259.interrupt_request_register, (uint8_t)(1u << irq), __ATOMIC_RELAXED);
}

static inline uint8_t i8259_nextirq(void) {
    const uint8_t pending = i8259_get_pending_irqs();
    if (!pending) {
        return 0;
    }

    const uint8_t irq = (uint8_t)__builtin_ctz(pending);
    __atomic_fetch_and(&i8259.interrupt_request_register, (uint8_t)~(1u << irq), __ATOMIC_RELAXED);
    i8259.in_service_register |= (uint8_t)(1u << irq);
    return i8259.interrupt_vector_offset + irq;
}
