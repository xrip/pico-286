// Host test for extended memory and the XMS driver (docs/specs/xms30.txt). Build and run from the project root:
//   gcc -std=gnu17 -fms-extensions -O2 -Isrc -Isrc/emulator -Isrc/printf tests/xms_test.c -o tests/xms_test.exe && tests/xms_test.exe
#include <assert.h>
#include <stdio.h>
#include "../src/emulator/memory.c"
#include "../src/emulator/xms.c"
cpu_state_t cpu_state; // a20_enabled is in it (cpu.h)

uint32_t dwordregs[8];
uint16_t segregs[6];
uint32_t segment_base[6];
uint8_t protected_mode; // load_segment() needs it; the test stays in real mode
void load_segment_protected(uint8_t reg, uint16_t selector) {}
uint8_t vga_mem_read(uint32_t address) { return 0; }
uint16_t vga_mem_read16(uint32_t address) { return 0; }
void vga_mem_write(uint32_t address, uint8_t value) {}
void vga_mem_write16(uint32_t address, uint16_t value) {}

static void xms(uint8_t function, uint16_t dx) {
    CPU_AH = function;
    CPU_DX = dx;
    xms_handler();
}

#define MOVE_TABLE 0x10000 // DS:SI = 1000:0000

static void xms_move(uint32_t length, uint16_t source_handle, uint32_t source_offset,
                     uint16_t destination_handle, uint32_t destination_offset) {
    writedw86(MOVE_TABLE, length);
    writew86(MOVE_TABLE + 4, source_handle);
    writedw86(MOVE_TABLE + 6, source_offset);
    writew86(MOVE_TABLE + 10, destination_handle);
    writedw86(MOVE_TABLE + 12, destination_offset);
    load_segment(regds, MOVE_TABLE >> 4);
    CPU_SI = 0;
    xms(0x0B, 0);
}

static uint32_t lock(uint16_t handle) {
    xms(0x0C, handle);
    assert(CPU_AX == 1);
    return (uint32_t) CPU_DX << 16 | CPU_BX;
}

int main(void) {
    read86 = read86_ob;
    readw86 = readw86_ob;
    readdw86 = readdw86_ob;
    write86 = write86_ob;
    writew86 = writew86_ob;
    writedw86 = writedw86_ob;
    xms_reset();

    // Extended memory is in the address space; with A20 shut, bit 20 is 0
    a20_enabled = 1;
    write86(0x110000, 0x5A);
    writedw86(0x200000, 0x12345678);
    assert(EXTENDED[0x10000] == 0x5A && read86(0x110000) == 0x5A);
    assert(readdw86(0x200000) == 0x12345678);
    assert(read86(EXTENDED_END) == 0xFF);
    a20_enabled = 0;
    RAM[0x10] = 0x77;
    assert(read86(0x100010) == 0x77);
    write86(0x100011, 0x66);
    assert(RAM[0x11] == 0x66 && EXTENDED[0x11] == 0);

    // Query, allocate, lock: each handle has its own block
    xms(0x08, 0);
    assert(CPU_AX == XMS_KB && CPU_DX == XMS_KB);
    xms(0x09, 100);
    assert(CPU_AX == 1);
    const uint16_t first = CPU_DX;
    xms(0x09, 200);
    assert(CPU_AX == 1);
    const uint16_t second = CPU_DX;
    const uint32_t first_address = lock(first), second_address = lock(second);
    assert(first_address >= XMS_START && second_address >= first_address + 100 * 1024);
    xms(0x08, 0);
    assert(CPU_AX == XMS_KB - 300 && CPU_DX == XMS_KB - 300);

    // Move: conventional -> first, first -> second, second -> conventional (A20 stays shut)
    writedw86(0x20000, 0xCAFEF00D);
    xms_move(4, 0, 0x20000000, first, 8); // 2000:0000
    assert(a20_enabled == 0);
    a20_enabled = 1;
    assert(readdw86(first_address + 8) == 0xCAFEF00D && readdw86(second_address) == 0);
    a20_enabled = 0;
    xms_move(4, first, 8, second, 16);
    xms_move(4, second, 16, 0, 0x30000004); // 3000:0004
    assert(CPU_AX == 1 && readdw86(0x30004) == 0xCAFEF00D);

    // Move errors
    xms_move(3, 0, 0, first, 0);
    assert(CPU_AX == 0 && CPU_BL == 0xA7);
    xms_move(4, 99, 0, first, 0);
    assert(CPU_AX == 0 && CPU_BL == 0xA3);
    xms_move(4, first, 100 * 1024 - 2, 0, 0);
    assert(CPU_AX == 0 && CPU_BL == 0xA4);
    xms_move(4, 0, 0, 99, 0);
    assert(CPU_AX == 0 && CPU_BL == 0xA5);
    xms_move(4, 0, 0, second, 200 * 1024);
    assert(CPU_AX == 0 && CPU_BL == 0xA6);

    // Lock counts, info, free
    xms(0x0E, second);
    assert(CPU_AX == 1 && CPU_BH == 1 && CPU_BL == XMS_HANDLES - 2 && CPU_DX == 200);
    xms(0x0A, first);
    assert(CPU_AX == 0 && CPU_BL == 0xAB);
    xms(0x0D, first);
    assert(CPU_AX == 1);
    xms(0x0D, first);
    assert(CPU_AX == 0 && CPU_BL == 0xAA);
    xms(0x0A, first);
    assert(CPU_AX == 1);
    xms(0x0E, first);
    assert(CPU_AX == 0 && CPU_BL == 0xA2);

    // Reallocate: not while locked; a bigger block keeps its data
    CPU_BX = 1000;
    xms(0x0F, second);
    assert(CPU_AX == 0 && CPU_BL == 0xAB);
    xms(0x0D, second);
    xms_move(4, 0, 0x20000000, second, 200 * 1024 - 4);
    CPU_BX = 1000;
    xms(0x0F, second);
    assert(CPU_AX == 1);
    xms(0x0E, second);
    assert(CPU_DX == 1000);
    xms_move(4, second, 200 * 1024 - 4, 0, 0x30000000);
    assert(readdw86(0x30000) == 0xCAFEF00D);
    xms(0x08, 0);
    assert(CPU_DX == XMS_KB - 1000);
    CPU_BX = XMS_KB + 1;
    xms(0x0F, second);
    assert(CPU_AX == 0 && CPU_BL == 0xA0);

    // Too big, and out of handles
    xms(0x09, XMS_KB);
    assert(CPU_AX == 0 && CPU_BL == 0xA0);
    for (int i = 1; i < XMS_HANDLES; i++) {
        xms(0x09, 0);
        assert(CPU_AX == 1);
    }
    xms(0x09, 0);
    assert(CPU_AX == 0 && CPU_BL == 0xA1);

    // INT 15h 87h uses the same copy: conventional -> extended with A20 shut
    move_linear(XMS_START + 64, 0x20000, 4);
    a20_enabled = 1;
    assert(readdw86(XMS_START + 64) == 0xCAFEF00D);

    puts("xms_test: all passed"); // printf here is the embedded printf, with no output
    return 0;
}
