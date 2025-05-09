// https://wiki.osdev.org/ISA_DMA
// https://pdos.csail.mit.edu/6.828/2004/readings/hardware/8237A.pdf
/* 	Intel 8237 DMA controller */
// #define DEBUG_DMA 1
#pragma once

#define DMA_COMMAND_REGISTER 0x08
#define DMA_REQUEST_REGISTER 0x09
#define DMA_CHANNEL_MASK_REGISTER 0x0A
#define DMA_MODE_REGISTER 0x0B
#define DMA_CLEAR_FF 0x0C          // Clear flip-flop port
#define DMA_STATUS_REGISTER 0x0D
#define DMA_TEMP_REGISTER 0x0E
#define DMA_MASK_REGISTER 0x0F


#define DMA_CHANNELS 4

typedef struct dma_channel_t {
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

dma_channel_s dma_channels[DMA_CHANNELS] = {0};

static uint8_t flipflop, memtomem;

static INLINE void i8237_writeport(const uint16_t portnum, const uint8_t value) {
#ifdef DEBUG_DMA
    printf("[DMA] Write port 0x%X: %X\n", portnum, value);
#endif
    switch (portnum & 0xF) {
        case 0 ... 7: {
            const uint8_t channel = (portnum >> 1) & 3;

            if (portnum & 0x01) {
                //write terminal count
                if (flipflop) {
                    dma_channels[channel].count = (dma_channels[channel].count & 0x00FF) | (uint16_t) value << 8;
#ifdef DEBUG_DMA
                    printf("[DMA] Channel %u count set to %08X\r\n", channel, dma_channels[channel].count);
#endif
                } else {
                    dma_channels[channel].count = (dma_channels[channel].count & 0xFF00) | (uint16_t) value;
                }
                dma_channels[channel].reload_count = dma_channels[channel].count;
            } else {
                if (flipflop) {
                    dma_channels[channel].address = (dma_channels[channel].address & 0x00FF) | (uint16_t) value << 8;
#ifdef DEBUG_DMA
                    printf("[DMA] Channel %u addr set to %08X\r\n", channel, dma_channels[channel].page + dma_channels[channel].address);
#endif
                } else {
                    dma_channels[channel].address = (dma_channels[channel].address & 0xFF00) | (uint16_t) value;
                }

                dma_channels[channel].reload_address = dma_channels[channel].address;
            }


            flipflop ^= 1;
            break;
        }
        case DMA_COMMAND_REGISTER: //DMA channel 0-3 command register
            memtomem = value & 1;
            break;
        case DMA_REQUEST_REGISTER: //DMA request register
            dma_channels[value & 3].dreq = (value >> 2) & 1;
            break;
        case DMA_CHANNEL_MASK_REGISTER: //DMA channel 0-3 mask register
            dma_channels[value & 3].masked = (value >> 2) & 1;
        // printf("channel %i masked %i\n", value & 3, (value >> 2) & 1);
            break;
        case DMA_MODE_REGISTER: //DMA channel 0-3 mode register
            dma_channels[value & 3].transfer_type = (value >> 2) & 3;
            dma_channels[value & 3].auto_init = (value >> 4) & 1;
            dma_channels[value & 3].address_increase = (value & 0x20) ? 0xFFFFFFFF : 0x1; // bit 5
            dma_channels[value & 3].mode = (value >> 6) & 3;
            break;
        case DMA_TEMP_REGISTER: // Mask Reset
            i8237_reset();
            break;
        case DMA_STATUS_REGISTER: // DMA master clear
            i8237_reset();
        case DMA_CLEAR_FF: //clear byte pointer flipflop
            flipflop = 0;
            break;
        case DMA_MASK_REGISTER: //DMA write mask register
            dma_channels[0].masked = (value >> 0) & 1;
            dma_channels[1].masked = (value >> 1) & 1;
            dma_channels[2].masked = (value >> 2) & 1;
            dma_channels[3].masked = (value >> 3) & 1;
            break;
    }
}

inline void i8237_reset() {
    memset(dma_channels, 0x00, sizeof(dma_channel_s) * DMA_CHANNELS);

    dma_channels[0].masked = 1;
    dma_channels[1].masked = 1;
    dma_channels[2].masked = 1;
    dma_channels[3].masked = 1;
}

static INLINE void i8237_writepage(const uint16_t portnum, const uint8_t value) {
    uint8_t channel;
#ifdef DEBUG_DMA
    //printf("[DMA] Write port 0x%X: %X\n", portnum, value);
#endif
    switch (portnum & 0xF) {
        case 0x07:
            channel = 0;
            break;
        case 0x03:
            channel = 1;
            break;
        case 0x01:
            channel = 2;
            break;
        case 0x02:
            channel = 3;
            break;
        default:
            return;
    }
    dma_channels[channel].page = (uint32_t) value << 16;
#ifdef DEBUG_DMA
    printf("[DMA] Channel %u page set to %x address now %08X\r\n", channel, dma_channels[channel].page, dma_channels[channel].page | dma_channels[channel].address);
#endif
}

static INLINE uint8_t i8237_readport(const uint16_t portnum) {
    uint8_t result = 0xFF;
#ifdef DEBUG_DMA
    printf("[DMA] Read port 0x%X\n", portnum);
#endif

    switch (portnum & 0xf) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            const uint8_t channel = (portnum >> 1) & 3;

            if (portnum & 1) {
                //count
                if (flipflop) {
                    result = (uint8_t) (dma_channels[channel].count >> 8); //TODO: or give back the reload??
                } else {
                    result = (uint8_t) dma_channels[channel].count; //TODO: or give back the reload??
                }
            } else {
                //address
                if (flipflop) {
                    result = (uint8_t) (dma_channels[channel].address >> 8);
                } else {
                    result = (uint8_t) dma_channels[channel].address;
                }
            }
            flipflop ^= 1;
            break;
        case 0x08: //status register
            result = 0x0F;
    }
    return result;
}

static INLINE uint8_t i8237_readpage(const uint16_t portnum) {
    uint8_t channel;
#ifdef DEBUG_DMA
    printf("[DMA] Read port 0x%X\n", portnum);
#endif
    switch (portnum & 0xF) {
        case 0x07:
            channel = 0;
            break;
        case 0x03:
            channel = 1;
            break;
        case 0x01:
            channel = 2;
            break;
        case 0x02:
            channel = 3;
            break;
        default:
            return 0xFF;
    }
    return (uint8_t) (dma_channels[channel].page >> 16);
}

static INLINE void update_count(const uint8_t channel) {
    dma_channels[channel].address += dma_channels[channel].address_increase;
    dma_channels[channel].count--;

    if (dma_channels[channel].count == 0xFFFF) {
        if (dma_channels[channel].auto_init) {
            dma_channels[channel].count = dma_channels[channel].reload_count;
            dma_channels[channel].address = dma_channels[channel].reload_address & 0xFFFF;
        } else {
            dma_channels[channel].masked = 1;
        }
    }
}

INLINE uint8_t i8237_read(const uint8_t channel) {
    if (dma_channels[channel].masked) return 0;

    const uint32_t address = dma_channels[channel].page + dma_channels[channel].address;
    const uint8_t result = read86(address);
    update_count(channel);

    return result;
}

INLINE void i8237_write(const uint8_t channel, const uint8_t value) {
    printf("Write to %06X %x value %x\r\n", dma_channels[channel].page + dma_channels[channel].address,
           dma_channels[channel].count, value);
    register uint32_t address = dma_channels[channel].page + dma_channels[channel].address;
    update_count(channel);
    write86(address, value);
}
