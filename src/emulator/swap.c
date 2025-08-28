#ifdef TOTAL_VIRTUAL_MEMORY_KBS
#include "emulator.h"
#include "swap.h"
#include "f_util.h"
#include "ff.h"
#include <pico.h>
#include <hardware/gpio.h>

#undef printf_
#define printf_(...)

#define PAGE_CHANGE_FLAG 0x8000
#define PAGE_ID_MASK 0x7FFF

uint16_t ALIGN(4, SWAP_PAGES[SWAP_BLOCKS]) = {0};
uint8_t ALIGN(4, SWAP_PAGES_CACHE[RAM_SIZE]) = {0};

static INLINE uint32_t get_swap_page_for(uint32_t address);

uint8_t swap_read(uint32_t address) {
    const register uint32_t swap_page = get_swap_page_for(address);
    const register uint32_t address_in_page = address & RAM_IN_PAGE_ADDR_MASK;
    return SWAP_PAGES_CACHE[(swap_page * SWAP_PAGE_SIZE) + address_in_page];
}

static INLINE uint16_t read16arr(const uint8_t *arr, uint32_t addr32) {
    return (uint16_t)arr[addr32] | ((uint16_t)arr[addr32 + 1] << 8);
}

static INLINE uint32_t read32arr(const uint8_t *arr, uint32_t addr32) {
    return (uint32_t)arr[addr32] | ((uint32_t)arr[addr32 + 1] << 8) | ((uint32_t)arr[addr32 + 2] << 16) | ((uint32_t)arr[addr32 + 3] << 24);
}

uint16_t swap_read16(uint32_t addr32) {
    const register uint32_t ram_page = get_swap_page_for(addr32);
    const register uint32_t addr_in_page = addr32 & RAM_IN_PAGE_ADDR_MASK;
    return read16arr(SWAP_PAGES_CACHE, ram_page * SWAP_PAGE_SIZE + addr_in_page);
}

uint32_t swap_read32(uint32_t addr32) {
    const register uint32_t ram_page = get_swap_page_for(addr32);
    const register uint32_t addr_in_page = addr32 & RAM_IN_PAGE_ADDR_MASK;
    return read32arr(SWAP_PAGES_CACHE, ram_page * SWAP_PAGE_SIZE + addr_in_page);
}

void swap_write(uint32_t addr32, uint8_t value) {
    register uint32_t ram_page = get_swap_page_for(addr32);
    register uint32_t addr_in_page = addr32 & RAM_IN_PAGE_ADDR_MASK;
    SWAP_PAGES_CACHE[ram_page * SWAP_PAGE_SIZE + addr_in_page] = value;
    register uint16_t ram_page_desc = SWAP_PAGES[ram_page];
    if (!(ram_page_desc & PAGE_CHANGE_FLAG)) {
        // if higest (15) bit is set, it means - the page has changes
        SWAP_PAGES[ram_page] = ram_page_desc | PAGE_CHANGE_FLAG; // mark it as changed - bit 15
    }
}

void swap_write16(uint32_t addr32, uint16_t value) {
    register uint32_t ram_page = get_swap_page_for(addr32);
    register uint32_t addr_in_page = addr32 & RAM_IN_PAGE_ADDR_MASK;
    register uint8_t *addr_in_ram = SWAP_PAGES_CACHE + ram_page * SWAP_PAGE_SIZE + addr_in_page;
    addr_in_ram[0] = (uint8_t) value;
    addr_in_ram[1] = (uint8_t) (value >> 8);
    register uint16_t ram_page_desc = SWAP_PAGES[ram_page];
    if (!(ram_page_desc & PAGE_CHANGE_FLAG)) {
        // if higest (15) bit is set, it means - the page has changes
        SWAP_PAGES[ram_page] = ram_page_desc | PAGE_CHANGE_FLAG; // mark it as changed - bit 15
    }
}

void swap_write32(uint32_t addr32, uint32_t value) {
    register uint32_t ram_page = get_swap_page_for(addr32);
    register uint32_t addr_in_page = addr32 & RAM_IN_PAGE_ADDR_MASK;
    register uint8_t *addr_in_ram = SWAP_PAGES_CACHE + ram_page * SWAP_PAGE_SIZE + addr_in_page;
    addr_in_ram[0] = (uint8_t) value;
    addr_in_ram[1] = (uint8_t) (value >> 8);
    addr_in_ram[2] = (uint8_t) (value >> 16);
    addr_in_ram[3] = (uint8_t) (value >> 24);
    register uint16_t ram_page_desc = SWAP_PAGES[ram_page];
    if (!(ram_page_desc & PAGE_CHANGE_FLAG)) {
        // if higest (15) bit is set, it means - the page has changes
        SWAP_PAGES[ram_page] = ram_page_desc | PAGE_CHANGE_FLAG; // mark it as changed - bit 15
    }
}

static uint16_t oldest_ram_page = 1, last_ram_page = 0;
static uint32_t last_lba_page = 0;

#include "pico/sync.h"
static spin_lock_t *swap_spinlock;

uint32_t get_swap_page_for(uint32_t address) {
    uint32_t lba_page = address >> SHIFT_AS_DIV;
    if (last_lba_page == lba_page) return last_ram_page;

    last_lba_page = lba_page;
    for (uint32_t page = 1; page < SWAP_BLOCKS; ++page) {
        if ((SWAP_PAGES[page] & PAGE_ID_MASK) == lba_page) {
            last_ram_page = page;
            return page;
        }
    }

    uint16_t ram_page = oldest_ram_page++;
    if (oldest_ram_page >= SWAP_BLOCKS - 1) oldest_ram_page = 1;

    uint32_t save = spin_lock_blocking(swap_spinlock);
    { // critical section
        if (!(SWAP_PAGES[ram_page] & PAGE_CHANGE_FLAG)) {
            swap_file_read_block(SWAP_PAGES_CACHE + (ram_page * SWAP_PAGE_SIZE), lba_page * SWAP_PAGE_SIZE, SWAP_PAGE_SIZE);
        } else {
            swap_file_flush_block(SWAP_PAGES_CACHE + (ram_page * SWAP_PAGE_SIZE), (SWAP_PAGES[ram_page] & PAGE_ID_MASK) * SWAP_PAGE_SIZE, SWAP_PAGE_SIZE);
            swap_file_read_block(SWAP_PAGES_CACHE + (ram_page * SWAP_PAGE_SIZE), lba_page * SWAP_PAGE_SIZE, SWAP_PAGE_SIZE);
        }
        SWAP_PAGES[ram_page] = lba_page;
        last_ram_page = ram_page;
        spin_unlock(swap_spinlock, save);
    }
    return ram_page;
}

static const char *path = "\\XT\\pagefile.sys";
static FIL swap_file;

bool init_swap() {
    printf("Initializing pagefile...\n");
    swap_spinlock = spin_lock_instance(0);
    f_unlink(path);
    FRESULT result = f_open(&swap_file, path, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
    if (result == FR_OK) {
        UINT bytes_written;
        for (size_t i = 0; i < (TOTAL_VIRTUAL_MEMORY_KBS << 10); i += SWAP_PAGE_SIZE) {
            result = f_write(&swap_file, SWAP_PAGES_CACHE, SWAP_PAGE_SIZE, &bytes_written);
            if(i)printf_("%d         \r", (TOTAL_VIRTUAL_MEMORY_KBS << 10) / i);
            if (result != FR_OK) return /*printf("Error initializing pagefile\n"),*/ false;
        }
    } else return /*printf("Error creating pagefile\n"),*/ false;
    printf("Done!\n");
    f_close(&swap_file);
    return f_open(&swap_file, path, FA_READ | FA_WRITE) == FR_OK;
}

static FRESULT swap_file_seek(uint32_t offset) {
    FRESULT result = f_lseek(&swap_file, offset);
    if (result != FR_OK) {
        f_open(&swap_file, path, FA_READ | FA_WRITE);
        printf("Seek error: %d\n", result);
    }
    return result;
}

void swap_file_read_block(uint8_t *dst, uint32_t offset, uint32_t size) {
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    if (swap_file_seek(offset) == FR_OK) {
        UINT bytes_read;
        if (f_read(&swap_file, dst, size, &bytes_read) != FR_OK) printf("Read error\n");
    }
    gpio_put(PICO_DEFAULT_LED_PIN, false);
}

void swap_file_flush_block(const uint8_t *src, uint32_t offset, uint32_t size) {
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    if (swap_file_seek(offset) == FR_OK) {
        UINT bytes_written;
        if (f_write(&swap_file, src, size, &bytes_written) != FR_OK) printf("Write error\n");
    }
    gpio_put(PICO_DEFAULT_LED_PIN, false);
}
#endif