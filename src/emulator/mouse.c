#include "emulator.h"

// COM1 (3F8h-3FFh): 16450-style UART with a Microsoft serial mouse on it.
// The UART model is ported from RP8086 (src/hardware/uart16550.h).
// The mouse runs on the same thread as the CPU, so the receive FIFO needs no locking.

#define IER_RX_DATA 0x01
#define IIR_NO_INTERRUPT 0x01
#define IIR_RX_DATA 0x04
#define LCR_DLAB 0x80
#define MCR_DTR 0x01
#define MCR_RTS 0x02
#define LSR_DATA_READY 0x01
#define LSR_THR_EMPTY 0x20
#define LSR_TRANSMITTER_EMPTY 0x40
#define MSR_CTS_DSR_DCD 0xB0

static struct {
    uint8_t rx_fifo[16];
    uint8_t rx_head; // next free slot
    uint8_t rx_tail; // next byte to read
    uint8_t rbr;     // last byte read
    uint16_t divisor;
    uint8_t ier, lcr, mcr, scratch;
} uart;

static inline bool rx_ready(void) {
    return uart.rx_head != uart.rx_tail;
}

// The driver reads one byte per interrupt, so IRQ4 is raised again while bytes are waiting.
static inline void raise_rx_irq(void) {
    if ((uart.ier & IER_RX_DATA) && rx_ready()) {
        doirq(4);
    }
}

static void receive(const uint8_t data) {
    const uint8_t next_head = (uart.rx_head + 1) & 15;
    if (next_head == uart.rx_tail) {
        return; // FIFO full: overrun, the byte is lost
    }
    uart.rx_fifo[uart.rx_head] = data;
    uart.rx_head = next_head;
    raise_rx_irq();
}

void mouse_portout(uint16_t port_number, const uint8_t value) {
    switch (port_number & 7) {
        case 0: // THR (no device listens) / divisor low
            if (uart.lcr & LCR_DLAB) {
                uart.divisor = (uart.divisor & 0xFF00) | value;
            }
            break;
        case 1: // IER / divisor high
            if (uart.lcr & LCR_DLAB) {
                uart.divisor = (uart.divisor & 0x00FF) | (uint16_t) (value << 8);
            } else {
                uart.ier = value;
                raise_rx_irq(); // enabling the RX interrupt also reports data already waiting
            }
            break;
        case 3:
            uart.lcr = value;
            break;
        case 4: { // MCR: the mouse resets when DTR or RTS comes up and sends its ID
            const uint8_t rising = value & (uint8_t) ~uart.mcr;
            uart.mcr = value;
            if (rising & (MCR_DTR | MCR_RTS)) {
                uart.rx_tail = uart.rx_head; // drop bytes from before the reset
                receive('M');
            }
            break;
        }
        case 7:
            uart.scratch = value;
            break;
        default: // FCR, LSR, MSR: ignored
            break;
    }
}

uint8_t mouse_portin(uint16_t port_number) {
    switch (port_number & 7) {
        case 0: // RBR / divisor low
            if (uart.lcr & LCR_DLAB) {
                return uart.divisor & 0xFF;
            }
            if (rx_ready()) {
                uart.rbr = uart.rx_fifo[uart.rx_tail];
                uart.rx_tail = (uart.rx_tail + 1) & 15;
                raise_rx_irq();
            }
            return uart.rbr;
        case 1: // IER / divisor high
            return (uart.lcr & LCR_DLAB) ? uart.divisor >> 8 : uart.ier;
        case 2: // IIR
            return (uart.ier & IER_RX_DATA) && rx_ready() ? IIR_RX_DATA : IIR_NO_INTERRUPT;
        case 3:
            return uart.lcr;
        case 4:
            return uart.mcr;
        case 5: // LSR: the transmitter is always free
            return LSR_THR_EMPTY | LSR_TRANSMITTER_EMPTY | (rx_ready() ? LSR_DATA_READY : 0);
        case 6:
            return MSR_CTS_DSR_DCD;
        default:
            return uart.scratch;
    }
}

void sermouseevent(const uint8_t button_state, const int8_t x_movement, const int8_t y_movement) {
    // Microsoft mouse packet: sync bit, buttons and the top bits of X/Y, then the low 6 bits of X and Y
    uint8_t high_bits = (x_movement < 0) ? 3 : 0;
    if (y_movement < 0) {
        high_bits |= 12;
    }
    receive(0x40 | (button_state << 4) | high_bits);
    receive(x_movement & 63);
    receive(y_movement & 63);
}
