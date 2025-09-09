// https://pdos.csail.mit.edu/6.828/2010/readings/hardware/8259A.pdf
/*
	Intel 8259 interrupt controller
*/

#include "emulator.h"

struct i8259_s i8259_controller = {
    .interrupt_mask_register = 0xFF,
    .interrupt_vector_offset = 8
};

uint8_t in8259(uint16_t port_number) {
#ifdef DEBUG_PIC
    debug_log(DEBUG_DETAIL, "[I8259] Read port 0x%X\n", port_number);
#endif
    switch (port_number & 1) {
        case 0:
            return i8259_controller.register_read_mode ? i8259_controller.in_service_register : i8259_controller.interrupt_request_register;
        case 1: //read mask register
            return i8259_controller.interrupt_mask_register;
    }
    return 0;
}

void out8259(uint16_t port_number, uint8_t register_value) {
#ifdef DEBUG_PIC
    debug_log(DEBUG_DETAIL, "[I8259] Write port 0x%X: %X\n", port_number, register_value);
#endif
    switch (port_number & 1) {
        case 0:
            if (register_value & 0x10) { //ICW1
#ifdef DEBUG_PIC
                debug_log(DEBUG_DETAIL, "[I8259] ICW1 = %02X\r\n", register_value);
#endif
                i8259_controller.interrupt_mask_register = 0x00;
                i8259_controller.initialization_command_words[1] = register_value;
                i8259_controller.initialization_command_word_step = 2;
                i8259_controller.register_read_mode = 0;
            } else if ((register_value & 0x08) == 0) { //OCW2
#ifdef DEBUG_PIC
                debug_log(DEBUG_DETAIL, "[I8259] OCW2 = %02X\r\n", register_value);
#endif
//                i8259_controller.ocw[2] = register_value;
                switch (register_value & 0xE0) {
                    case 0x60: //specific EOI
                        i8259_controller.interrupt_request_register &= ~(1 << (register_value & 0x03));
                        i8259_controller.in_service_register &= ~(1 << (register_value & 0x03));
                        break;
                    case 0x40: //no operation
                        break;
                    case 0x20: //non-specific EOI
                        i8259_controller.interrupt_request_register &= ~i8259_controller.in_service_register;
                        i8259_controller.in_service_register = 0x00;
                        break;
                    default: //other
#ifdef DEBUG_PIC
                        debug_log(DEBUG_DETAIL, "[I8259] Unhandled EOI type: %u\r\n", register_value & 0xE0);
#endif
                        break;
                }
            } else { //OCW3
#ifdef DEBUG_PIC
                debug_log(DEBUG_DETAIL, "[I8259] OCW3 = %02X\r\n", register_value);
#endif
//                i8259_controller.ocw[3] = register_value;
                if (register_value & 0x02) {
                    i8259_controller.register_read_mode = register_value & 1;
                }
            }
            break;
        case 1:
#ifdef DEBUG_PIC
            debug_log(DEBUG_DETAIL, "[I8259] ICW%u = %02X\r\n", i8259_controller.initialization_command_word_step, register_value);
#endif
            switch (i8259_controller.initialization_command_word_step) {
                case 2: //ICW2
                    i8259_controller.initialization_command_words[2] = register_value;
                    i8259_controller.interrupt_vector_offset = register_value & 0xF8;
                    if (i8259_controller.initialization_command_words[1] & 0x02) {
                        i8259_controller.initialization_command_word_step = 4;
                    } else {
                        i8259_controller.initialization_command_word_step = 3;
                    }
                    break;
                case 3: //ICW3
                    i8259_controller.initialization_command_words[3] = register_value;
                    if (i8259_controller.initialization_command_words[1] & 0x01) {
                        i8259_controller.initialization_command_word_step = 4;
                    } else {
                        i8259_controller.initialization_command_word_step = 5; //done with ICWs
                    }
                    break;
                case 4: //ICW4
                    i8259_controller.initialization_command_words[4] = register_value;
                    i8259_controller.initialization_command_word_step = 5; //done with ICWs
                    break;
                case 5: //just set IMR value now
                    i8259_controller.interrupt_mask_register = register_value;
                    break;
            }
            break;
    }
}