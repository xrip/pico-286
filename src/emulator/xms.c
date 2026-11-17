#pragma GCC optimize("Ofast")
#include "emulator.h"
// https://www.phatcode.net/res/218/files/limems40.txt
// https://www.phatcode.net/res/219/files/xms20.txt
// http://www.techhelpmanual.com/944-xms_functions.html
// http://www.techhelpmanual.com/651-emm_functions.html
// http://www.techhelpmanual.com/650-expanded_memory_specification__ems_.html
// http://www.techhelpmanual.com/943-extended_memory_specification__xms_.html
// http://www.techhelpmanual.com/698-int_2fh_43xxh__himem_sys__extended_memory_manager__services.html

//#define DEBUG_XMS
#if defined(DEBUG_XMS)
#define debug_log(...) printf(__VA_ARGS__)
#else
#define debug_log(...) ((void)0)
#endif

#define XMS_VERSION 0x00
#define REQUEST_HMA 0x01
#define RELEASE_HMA 0x02
#define GLOBAL_ENABLE_A20 0x03
#define GLOBAL_DISABLE_A20 0x04
#define LOCAL_ENABLE_A20 0x05
#define LOCAL_DISABLE_A20 0x06
#define QUERY_A20 0x07

#define QUERY_EMB 0x08
#define ALLOCATE_EMB 0x09
#define RELEASE_EMB 0x0A
#define MOVE_EMB 0x0B

#define LOCK_EMB 0x0C
#define UNLOCK_EMB 0x0D
#define EMB_HANDLE_INFO 0x0E
#define REALLOCATE_EMB 0x0F

#define REQUEST_UMB 0x10
#define RELEASE_UMB 0x11

// Handle n is blocks[n - 1]. Sizes and starts are in KB inside the XMS area (XMS_START).
#define XMS_HANDLES 64
#define XMS_KB (XMS_MEMORY_SIZE >> 10)

typedef struct {
    uint16_t start_kb, size_kb;
    uint8_t lock_count;
    bool used;
} xms_block_t;

static xms_block_t blocks[XMS_HANDLES];

typedef struct umb {
    uint16_t segment;
    uint16_t size; // paragraphs
    int allocated_paragraphs; // -1 for a chain
} umb_t;

static umb_t umb_blocks[] = {
    // 0xD0000–0xDFFFF (64 KB)
    {0xD000, 0x0080, 0}, {0xD080, 0x0080, 0}, {0xD100, 0x0080, 0}, {0xD180, 0x0080, 0},
    {0xD200, 0x0080, 0}, {0xD280, 0x0080, 0}, {0xD300, 0x0080, 0}, {0xD380, 0x0080, 0},
    {0xD400, 0x0080, 0}, {0xD480, 0x0080, 0}, {0xD500, 0x0080, 0}, {0xD580, 0x0080, 0},
    {0xD600, 0x0080, 0}, {0xD680, 0x0080, 0}, {0xD700, 0x0080, 0}, {0xD780, 0x0080, 0},
    {0xD800, 0x0080, 0}, {0xD880, 0x0080, 0}, {0xD900, 0x0080, 0}, {0xD980, 0x0080, 0},
    {0xDA00, 0x0080, 0}, {0xDA80, 0x0080, 0}, {0xDB00, 0x0080, 0}, {0xDB80, 0x0080, 0},
    {0xDC00, 0x0080, 0}, {0xDC80, 0x0080, 0}, {0xDD00, 0x0080, 0}, {0xDD80, 0x0080, 0},
    {0xDE00, 0x0080, 0}, {0xDE80, 0x0080, 0}, {0xDF00, 0x0080, 0}, {0xDF80, 0x0080, 0},

    // 0xE0000–0xEFFFF (64 KB)
    {0xE000, 0x0080, 0}, {0xE080, 0x0080, 0}, {0xE100, 0x0080, 0}, {0xE180, 0x0080, 0},
    {0xE200, 0x0080, 0}, {0xE280, 0x0080, 0}, {0xE300, 0x0080, 0}, {0xE380, 0x0080, 0},
    {0xE400, 0x0080, 0}, {0xE480, 0x0080, 0}, {0xE500, 0x0080, 0}, {0xE580, 0x0080, 0},
    {0xE600, 0x0080, 0}, {0xE680, 0x0080, 0}, {0xE700, 0x0080, 0}, {0xE780, 0x0080, 0},
    {0xE800, 0x0080, 0}, {0xE880, 0x0080, 0}, {0xE900, 0x0080, 0}, {0xE980, 0x0080, 0},
    {0xEA00, 0x0080, 0}, {0xEA80, 0x0080, 0}, {0xEB00, 0x0080, 0}, {0xEB80, 0x0080, 0},
    {0xEC00, 0x0080, 0}, {0xEC80, 0x0080, 0}, {0xED00, 0x0080, 0}, {0xED80, 0x0080, 0},
    {0xEE00, 0x0080, 0}, {0xEE80, 0x0080, 0}, {0xEF00, 0x0080, 0}, {0xEF80, 0x0080, 0},

    // 0xF0000–0xF7FFF (32 KB)
    {0xF000, 0x0080, 0}, {0xF080, 0x0080, 0}, {0xF100, 0x0080, 0}, {0xF180, 0x0080, 0},
    {0xF200, 0x0080, 0}, {0xF280, 0x0080, 0}, {0xF300, 0x0080, 0}, {0xF380, 0x0080, 0},
    {0xF400, 0x0080, 0}, {0xF480, 0x0080, 0}, {0xF500, 0x0080, 0}, {0xF580, 0x0080, 0},
    {0xF600, 0x0080, 0}, {0xF680, 0x0080, 0}, {0xF700, 0x0080, 0}, {0xF780, 0x0080, 0},

    // 0xF8000–0xFBFFF (16 KB)
    {0xF800, 0x0080, 0}, {0xF880, 0x0080, 0}, {0xF900, 0x0080, 0}, {0xF980, 0x0080, 0},
    {0xFA00, 0x0080, 0}, {0xFA80, 0x0080, 0}, {0xFB00, 0x0080, 0}, {0xFB80, 0x0080, 0},
};
#define UMB_BLOCKS_COUNT (sizeof(umb_blocks) / sizeof(umb_t))

static int umb_blocks_allocated = 0;

int a20_enabled = 0;

void xms_reset() {
    for (int i = 0; i < UMB_BLOCKS_COUNT; ++i) {
        umb_blocks[i].allocated_paragraphs = 0;
    }
    memset(blocks, 0, sizeof(blocks));
}

const umb_t *get_largest_free_umb_block(uint16_t *psz) {
    const umb_t *best = NULL;
    int best_length = 0;
    int i = 0;
    while (i < UMB_BLOCKS_COUNT) {
        if (0 == umb_blocks[i].allocated_paragraphs) {
            int j = i;
            int length = 0;
            while (j < UMB_BLOCKS_COUNT && umb_blocks[j].allocated_paragraphs == 0) {
                if (j > i) {
                    const uint16_t expected_segment = umb_blocks[j - 1].segment + umb_blocks[j - 1].size;
                    if (umb_blocks[j].segment != expected_segment)
                        break;
                }
                length += umb_blocks[j].size;
                j++;
            }
            if (length > best_length) {
                best = &umb_blocks[i];
                best_length = length;
            }
            i = j;
        } else {
            i++;
        }
    }
    *psz = best_length;
    return best;
}

umb_t *get_free_umb_block(const uint16_t size) {
    umb_t *best = NULL;
    int best_size = 0;
    int i = 0;
    while (i < UMB_BLOCKS_COUNT) {
        if (umb_blocks[i].allocated_paragraphs != 0) {
            i++;
            continue;
        }
        uint16_t total_size = 0;
        int j = i;
        while (j < UMB_BLOCKS_COUNT && umb_blocks[j].allocated_paragraphs == 0) {
            if (j > i) {
                const uint16_t expected_segment = umb_blocks[j - 1].segment + umb_blocks[j - 1].size;
                if (umb_blocks[j].segment != expected_segment)
                    break;
            }
            total_size += umb_blocks[j].size;
            j++;
        }
        if (total_size >= size) {
            if (best == NULL || total_size < best_size) {
                best = &umb_blocks[i];
                best_size = total_size;
            }
        }
        i = j; // skip tested block
    }
    return best;
}
// Copies words between physical addresses (not through paging) with A20 open; INT 15h 87h and
// XMS 0Bh use it.
// With an overlap and the destination above the source it copies from the end.
void move_linear(const uint32_t destination, const uint32_t source, const uint32_t length) {
    const int saved_a20 = a20_enabled;
    a20_enabled = 1;
    if (destination > source && destination < source + length) {
        for (uint32_t i = length; i >= 2; i -= 2) writew86_ob(destination + i - 2, readw86_ob(source + i - 2));
    } else {
        for (uint32_t i = 0; i < length; i += 2) writew86_ob(destination + i, readw86_ob(source + i));
    }
    a20_enabled = saved_a20;
}

static xms_block_t *block_of(const uint16_t handle) {
    return handle && handle <= XMS_HANDLES && blocks[handle - 1].used ? &blocks[handle - 1] : NULL;
}

// Free KB from start to the next used block; 0 if start is inside a used block.
static uint16_t free_kb_at(const uint16_t start) {
    uint16_t end = XMS_KB;
    for (int i = 0; i < XMS_HANDLES; i++) {
        const xms_block_t *block = &blocks[i];
        if (!block->used || !block->size_kb) continue;
        if (start >= block->start_kb && start < block->start_kb + block->size_kb) return 0;
        if (block->start_kb > start && block->start_kb < end) end = block->start_kb;
    }
    return end - start;
}

// Free runs start at 0 or at the end of a used block. Returns the start of the first run
// of at least size KB, or -1; *largest gets the biggest run.
static int find_free(const uint16_t size, uint16_t *largest) {
    int found = -1;
    *largest = 0;
    for (int i = -1; i < XMS_HANDLES; i++) {
        if (i >= 0 && !blocks[i].used) continue;
        const uint16_t start = i < 0 ? 0 : blocks[i].start_kb + blocks[i].size_kb;
        const uint16_t free_kb = free_kb_at(start);
        if (free_kb > *largest) *largest = free_kb;
        if (found < 0 && free_kb >= size) found = start;
    }
    return found;
}

// Linear address of one side of a move. Handle 0 is a real-mode segment:offset.
// Returns 0, bad_handle, or bad_handle + 1 for an offset outside the block.
static uint8_t move_address(const uint16_t handle, const uint32_t offset, const uint32_t length,
                            uint32_t *address, const uint8_t bad_handle) {
    if (!handle) {
        *address = (offset >> 16 << 4) + (offset & 0xFFFF);
        return 0;
    }
    const xms_block_t *block = block_of(handle);
    if (!block) return bad_handle;
    const uint32_t size = block->size_kb * 1024u;
    if (offset > size || length > size - offset) return bad_handle + 1;
    *address = XMS_START + block->start_kb * 1024u + offset;
    return 0;
}

void xms_handler() { // in flash: XMS calls are rare
    switch (CPU_AH) {
        case XMS_VERSION: {
            // Get XMS Version
            CPU_AX = 0x0200; // We are himem 2.06
            CPU_BX = 0x0206; // driver version
            CPU_DX = 0x0001; // HMA Exist
            break;
        }
        case REQUEST_HMA: {
            // Request HMA
            // Stub: Implement HMA request functionality
            CPU_AX = 1; // Success
            break;
        }
        case RELEASE_HMA: {
            // Release HMA
            // Stub: Implement HMA release functionality
            CPU_AX = 1; // Success
            break;
        }
        case GLOBAL_ENABLE_A20:
        case LOCAL_ENABLE_A20: {
            // Local Enable A20
            CPU_AX = 1; // Success
            CPU_BL = 0;
            a20_enabled = 1;
            break;
        }
        case GLOBAL_DISABLE_A20:
        case LOCAL_DISABLE_A20: {
            // Local Disable A20
            CPU_AX = 1; // Success
            CPU_BL = 0;
            a20_enabled = 0;
            break;
        }
        case QUERY_A20: {
            // Query A20 (Function 07h):
            CPU_AX = a20_enabled; // Success
            break;
        }

        case QUERY_EMB: {
            uint16_t largest;
            find_free(0, &largest);
            uint16_t free_kb = XMS_KB;
            for (int i = 0; i < XMS_HANDLES; i++) {
                if (blocks[i].used) free_kb -= blocks[i].size_kb;
            }
            CPU_AX = largest;
            CPU_DX = free_kb;
            CPU_BL = largest ? 0 : 0xA0;
            break;
        }
        case ALLOCATE_EMB: {
            debug_log("[XMS] Allocate %dKb\n", CPU_DX);
            int handle = 0;
            while (handle < XMS_HANDLES && blocks[handle].used) handle++;
            uint16_t largest;
            const int start = find_free(CPU_DX, &largest);
            if (handle == XMS_HANDLES || start < 0) {
                CPU_AX = 0;
                CPU_DX = 0;
                CPU_BL = handle == XMS_HANDLES ? 0xA1 : 0xA0;
                break;
            }
            blocks[handle] = (xms_block_t) {(uint16_t) start, CPU_DX, 0, true};
            CPU_AX = 1;
            CPU_DX = handle + 1;
            CPU_BL = 0;
            break;
        }
        case RELEASE_EMB:
        case LOCK_EMB:
        case UNLOCK_EMB:
        case EMB_HANDLE_INFO:
        case REALLOCATE_EMB: {
            xms_block_t *block = block_of(CPU_DX);
            uint8_t error = 0;
            if (!block) {
                error = 0xA2;
            } else if (CPU_AH == RELEASE_EMB) {
                if (block->lock_count) error = 0xAB;
                else block->used = false;
            } else if (CPU_AH == LOCK_EMB) {
                if (block->lock_count == 0xFF) {
                    error = 0xAC;
                } else {
                    block->lock_count++;
                    const uint32_t address = XMS_START + block->start_kb * 1024u;
                    CPU_DX = address >> 16;
                    CPU_BX = address & 0xFFFF;
                }
            } else if (CPU_AH == UNLOCK_EMB) {
                if (block->lock_count) block->lock_count--;
                else error = 0xAA;
            } else if (CPU_AH == EMB_HANDLE_INFO) {
                uint8_t free_handles = 0;
                for (int i = 0; i < XMS_HANDLES; i++) free_handles += !blocks[i].used;
                CPU_BH = block->lock_count;
                CPU_BL = free_handles;
                CPU_DX = block->size_kb;
            } else if (block->lock_count) { // REALLOCATE_EMB
                error = 0xAB;
            } else {
                block->used = false; // its own space counts as free
                uint16_t largest;
                const int start = free_kb_at(block->start_kb) >= CPU_BX ? block->start_kb : find_free(CPU_BX, &largest);
                block->used = true;
                if (start < 0) {
                    error = 0xA0;
                } else {
                    const uint16_t kept_kb = block->size_kb < CPU_BX ? block->size_kb : CPU_BX;
                    move_linear(XMS_START + start * 1024u, XMS_START + block->start_kb * 1024u, kept_kb * 1024u);
                    block->start_kb = (uint16_t) start;
                    block->size_kb = CPU_BX;
                }
            }
            CPU_AX = !error;
            if (error) CPU_BL = error; // on success BL is a result of 0Ch and 0Eh
            break;
        }
        case MOVE_EMB: {
            const uint32_t table = DS_BASE + CPU_SI; // the move structure
            const uint32_t length = readdw86(table);
            uint32_t source, destination;
            uint8_t error = length & 1 ? 0xA7 : 0;
            if (!error) error = move_address(readw86(table + 4), readdw86(table + 6), length, &source, 0xA3);
            if (!error) error = move_address(readw86(table + 10), readdw86(table + 12), length, &destination, 0xA5);
            if (!error) move_linear(destination, source, length);
            debug_log("[XMS] Move %u bytes %06X -> %06X, error %02X\n", length, source, destination, error);
            CPU_AX = !error;
            CPU_BL = error;
            break;
        }
        case REQUEST_UMB: {
            // Request Upper Memory Block (Function 10h):
            if (CPU_DX == 0xFFFF) {
                // Query largest available block
                if (umb_blocks_allocated < UMB_BLOCKS_COUNT) {
                    uint16_t sz = 0;
                    const umb_t *umb_block = get_largest_free_umb_block(&sz);
                    if (umb_block != NULL) {
                        CPU_AX = 1;
                        CPU_BX = umb_block->segment;
                        CPU_DX = sz;
                        CPU_BL = 0;
                        break;
                    }
                }
            } else {
                const uint16_t requested_size = CPU_DX;
                umb_t *umb_block = get_free_umb_block(requested_size);
                if (umb_block != NULL) {
                    int unmarked_size = requested_size;
                    CPU_BX = umb_block->segment;
                    CPU_AX = 0x0001;
                    umb_t *ub = umb_block;
                    int total_allocated = 0;
                    while (unmarked_size > 0) {
                        total_allocated += umb_block->size;
                        umb_block->allocated_paragraphs = -1;
                        umb_blocks_allocated++;
                        unmarked_size -= umb_block->size;
                        umb_block++;
                    }
                    ub->allocated_paragraphs = total_allocated;
                    CPU_DX = total_allocated;
                    break;
                }
            }

            uint16_t sz = 0;
            get_largest_free_umb_block(&sz);
            CPU_AX = 0x0000;
            CPU_DX = sz;
            CPU_BL = umb_blocks_allocated >= UMB_BLOCKS_COUNT ? 0xB1 : 0xB0;
            break;
        }
        case RELEASE_UMB: {
            // Release Upper Memory Block (Function 11h)
            // Stub: Release Upper Memory Block
            for (int i = 0; i < UMB_BLOCKS_COUNT; ++i)
                if (umb_blocks[i].segment == CPU_BX && umb_blocks[i].allocated_paragraphs > 0) {
                    int par = umb_blocks[i].allocated_paragraphs;
                    while (par > 0 && i < UMB_BLOCKS_COUNT) {
                        umb_blocks[i].allocated_paragraphs = 0;
                        par -= umb_blocks[i++].size;
                        umb_blocks_allocated--;
                    }
                    CPU_AX = 0x0001; // Success
                    CPU_BL = 0;
                    return;
                }

            CPU_AX = 0x0000; // Failure
            CPU_DX = 0x0000;
            CPU_BL = 0xB2; // Error code
            break;
        }
        default: {
            // Unhandled function
            CPU_AX = 0x0000; // Function not supported
            CPU_BL = 0x80; // Function not implemented

            break;
        }
    }
}
