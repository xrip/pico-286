#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DMA_COMMAND_REGISTER 0x08
#define DMA_REQUEST_REGISTER 0x09
#define DMA_CHANNEL_MASK_REGISTER 0x0A
#define DMA_MODE_REGISTER 0x0B
#define DMA_CLEAR_FF 0x0C
#define DMA_STATUS_REGISTER 0x0D
#define DMA_MASTER_CLEAR 0x0D
#define DMA_CLEAR_MASK_REGISTER 0x0E
#define DMA_MASK_REGISTER 0x0F
#define DMA_CHANNELS 4

typedef struct {
    uint32_t page;
    uint32_t address;
    uint32_t reload_address;
    uint32_t address_increase;
    uint16_t count;
    uint16_t reload_count;
    uint8_t auto_init;
    uint8_t mode;
    uint8_t enable;
    uint8_t masked;
    uint8_t dreq;
    uint8_t finished;
    uint8_t transfer_type;
} dma_channel_s;

extern dma_channel_s dma_channels[DMA_CHANNELS];
extern uint8_t i8237_byte_flipflop;

#ifndef I8237_MEMORY_READ
#define I8237_MEMORY_READ(address) read86(address)
#endif
#ifndef I8237_MEMORY_WRITE
#define I8237_MEMORY_WRITE(address, value) write86((address), (value))
#endif

static inline void i8237_reset(void) {
    memset(dma_channels, 0, sizeof(dma_channels));
    i8237_byte_flipflop = 0;
    for (uint8_t channel = 0; channel < DMA_CHANNELS; ++channel) {
        dma_channels[channel].masked = 1;
    }
}

static inline void i8237_writeport(const uint16_t port_number, const uint8_t data) {
    const uint8_t port = port_number & 0x0F;
    if (port <= 7) {
        const uint8_t channel = (port >> 1) & 3;
        if (port & 1) {
            if (i8237_byte_flipflop) {
                dma_channels[channel].count =
                    (dma_channels[channel].count & 0x00FFu) | ((uint16_t)data << 8);
            } else {
                dma_channels[channel].count =
                    (dma_channels[channel].count & 0xFF00u) | data;
            }
            dma_channels[channel].reload_count = dma_channels[channel].count;
        } else {
            if (i8237_byte_flipflop) {
                dma_channels[channel].reload_address =
                    (dma_channels[channel].reload_address & 0x00FFu) | ((uint16_t)data << 8);
                dma_channels[channel].address = dma_channels[channel].reload_address;
            } else {
                dma_channels[channel].reload_address =
                    (dma_channels[channel].reload_address & 0xFF00u) | data;
            }
        }
        i8237_byte_flipflop ^= 1;
        return;
    }

    switch (port) {
        case DMA_COMMAND_REGISTER:
            for (uint8_t channel = 0; channel < DMA_CHANNELS; ++channel) {
                dma_channels[channel].auto_init = (data >> 5) & 1;
            }
            break;
        case DMA_REQUEST_REGISTER:
            dma_channels[data & 3].dreq = (data >> 2) & 1;
            break;
        case DMA_CHANNEL_MASK_REGISTER:
            dma_channels[data & 3].masked = (data >> 2) & 1;
            break;
        case DMA_MODE_REGISTER: {
            const uint8_t channel = data & 3;
            dma_channels[channel].transfer_type = (data >> 2) & 3;
            dma_channels[channel].auto_init = (data >> 4) & 1;
            dma_channels[channel].address_increase = (data & 0x20) ? UINT32_MAX : 1;
            dma_channels[channel].mode = (data >> 6) & 3;
            break;
        }
        case DMA_MASTER_CLEAR:
            i8237_reset();
            break;
        case DMA_CLEAR_MASK_REGISTER:
            for (uint8_t channel = 0; channel < DMA_CHANNELS; ++channel) {
                dma_channels[channel].masked = 0;
            }
            break;
        case DMA_CLEAR_FF:
            i8237_byte_flipflop = 0;
            break;
        case DMA_MASK_REGISTER:
            for (uint8_t channel = 0; channel < DMA_CHANNELS; ++channel) {
                dma_channels[channel].masked = (data >> channel) & 1;
            }
            break;
        default:
            break;
    }
}

static inline void i8237_writepage(const uint16_t port_number, const uint8_t data) {
    uint8_t channel;
    switch (port_number) {
        case 0x81: channel = 2; break;
        case 0x82: channel = 3; break;
        case 0x83: channel = 0; break;
        case 0x87: channel = 1; break;
        default: return;
    }
    dma_channels[channel].page = (uint32_t)data << 16;
}

static inline uint8_t i8237_readport(const uint16_t port_number) {
    const uint8_t port = port_number & 0x0F;
    if (port <= 7) {
        const uint8_t channel = (port >> 1) & 3;
        uint8_t value;
        if (port & 1) {
            value = i8237_byte_flipflop
                        ? (uint8_t)(dma_channels[channel].count >> 8)
                        : (uint8_t)dma_channels[channel].count;
        } else {
            value = i8237_byte_flipflop
                        ? (uint8_t)(dma_channels[channel].address >> 8)
                        : (uint8_t)dma_channels[channel].address;
        }
        i8237_byte_flipflop ^= 1;
        return value;
    }

    if (port == DMA_STATUS_REGISTER) {
        uint8_t value = 0;
        for (uint8_t channel = 0; channel < DMA_CHANNELS; ++channel) {
            if (dma_channels[channel].dreq) {
                value |= (uint8_t)(1u << channel);
            }
            if (dma_channels[channel].finished) {
                value |= (uint8_t)(1u << (channel + 4));
                dma_channels[channel].finished = 0;
            }
        }
        return value;
    }
    return 0xFF;
}

static inline uint8_t i8237_readpage(const uint16_t port_number) {
    uint8_t channel;
    switch (port_number) {
        case 0x81: channel = 2; break;
        case 0x82: channel = 3; break;
        case 0x83: channel = 0; break;
        case 0x87: channel = 1; break;
        default: return 0xFF;
    }
    return (uint8_t)(dma_channels[channel].page >> 16);
}

static inline void i8237_update_count(dma_channel_s *channel, const uint16_t count) {
    const uint16_t old_count = channel->count;
    channel->address = (channel->address + channel->address_increase * count) & 0xFFFFu;
    channel->count -= count;

    if (channel->count > old_count) {
        channel->finished = 1;
        if (channel->auto_init) {
            channel->count = channel->reload_count;
            channel->address = channel->reload_address;
        } else {
            channel->masked = 1;
        }
    }
}

static inline uint8_t i8237_read(const uint8_t channel) {
    if (dma_channels[channel].masked) {
        return 0;
    }
    const uint32_t memory_address = dma_channels[channel].page + dma_channels[channel].address;
    const uint8_t data = I8237_MEMORY_READ(memory_address);
    i8237_update_count(&dma_channels[channel], 1);
    return data;
}

static inline void i8237_write(const uint8_t channel, const uint8_t value) {
    if (dma_channels[channel].masked) {
        return;
    }
    const uint32_t memory_address = dma_channels[channel].page + dma_channels[channel].address;
    I8237_MEMORY_WRITE(memory_address, value);
    i8237_update_count(&dma_channels[channel], 1);
}
