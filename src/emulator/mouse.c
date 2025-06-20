#include "emulator.h"

static struct sermouse_s {
    uint8_t registers[8];
    uint8_t data_buffer[16];
    int8_t buffer_write_position;
    int base_port_address;
} serial_mouse;


static inline void bufsermousedata(uint8_t data_byte) {
    // Prevent buffer overflow
    if (serial_mouse.buffer_write_position == 16)
        return;
    
    // Trigger interrupt when starting to fill empty buffer
    if (serial_mouse.buffer_write_position == 0)
        doirq(4);
    
    serial_mouse.data_buffer[serial_mouse.buffer_write_position++] = data_byte;
}

void mouse_portout(uint16_t port_number, uint8_t output_value) {
    // Mask to get only the relevant port bits
    port_number &= 7;
    const uint8_t previous_register_value = serial_mouse.registers[port_number];
    serial_mouse.registers[port_number] = output_value;
    
    switch (port_number) {
        case 4: // Modem control register
            // Check if DTR (Data Terminal Ready) bit has changed
            if ((output_value & 1) != (previous_register_value & 1)) {
                // Software toggling of DTR causes mouse reset
                serial_mouse.buffer_write_position = 0;
                
                // Fill buffer with identification sequence
                // Multiple 'M' characters help drivers verify mouse presence
                bufsermousedata('M');
                bufsermousedata('M');
                bufsermousedata('M');
                bufsermousedata('M');
                bufsermousedata('M');
                bufsermousedata('M');
            }
            break;
    }
}

uint8_t mouse_portin(uint16_t port_number) {
    uint8_t return_value;
    
    // Mask to get only the relevant port bits
    port_number &= 7;
    
    switch (port_number) {
        case 0: // Data receive register
            // Get the oldest byte from the buffer
            return_value = serial_mouse.data_buffer[0];
            
            // Shift remaining data forward in the buffer
            memmove(serial_mouse.data_buffer, &serial_mouse.data_buffer[1], 15);
            serial_mouse.buffer_write_position--;
            
            // Ensure buffer position doesn't go negative
            if (serial_mouse.buffer_write_position < 0)
                serial_mouse.buffer_write_position = 0;
            
            // Trigger interrupt if more data is available
            if (serial_mouse.buffer_write_position > 0)
                doirq(4);
            
            // Toggle some control bit (possibly RTS)
            serial_mouse.registers[4] = ~serial_mouse.registers[4] & 1;
            return return_value;
            
        case 5: // Line status register (read-only)
            // Return data ready status
            if (serial_mouse.buffer_write_position > 0)
                return_value = 1;  // Data available
            else
                return_value = 0;  // No data
            
            // Always return 0x1 regardless of actual status calculation
            return 0x1;
    }
    
    // Return the register value for other port numbers
    return serial_mouse.registers[port_number & 7];
}

void sermouseevent(const uint8_t button_state, const int8_t x_movement, const int8_t y_movement) {
    // Build high bits for movement direction flags
    uint8_t direction_flags = (x_movement < 0) ? 3 : 0;  // X direction bits
    if (y_movement < 0)
        direction_flags |= 12;  // Y direction bits
    
    // Send mouse packet: sync bit + buttons + direction flags
    bufsermousedata(0x40 | (button_state << 4) | direction_flags);
    
    // Send X movement (6-bit value)
    bufsermousedata(x_movement & 63);
    
    // Send Y movement (6-bit value)
    bufsermousedata(y_movement & 63);
}