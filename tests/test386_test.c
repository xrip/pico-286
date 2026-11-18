// Runs the test386.asm CPU test ROM (github.com/barotto/test386.asm; not in git, see docs/README.md)
// and prints its POST codes. POST FFh: all tests passed. On an error the ROM stops in a short
// loop (CLI, HLT, JMP), and the last POST code names the test that failed.
// Build and run from the project root (Windows, MinGW):
//   gcc -std=gnu17 -fms-extensions -O2 -Isrc -Isrc/emulator -Isrc/emu8950 -Isrc/printf tests/test386_test.c src/emulator/cpu.c src/emulator/fpu.c src/printf/printf.c -o tests/test386_test.exe
//   tests/test386_test.exe <test386.bin>
#include <stdio.h>
#include "cpu_host.h"

int main(int argc, char **argv) {
    // The 64 KB ROM goes at F0000, the 128 KB one (ROM128: more tests) at E0000
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    const size_t size = rom ? fread(memory + 0xE0000, 1, 0x20000, rom) : 0;
    if (size == 0x10000) {
        memmove(memory + 0xF0000, memory + 0xE0000, size);
    } else if (size != 0x20000) {
        fprintf(stderr, "usage: %s <test386.bin> (a 64 or 128 KB ROM)\n", argv[0]);
        return 2;
    }
    fast_memory = memory; // the low 640 KB and the memory from 1 MB without read86/write86, as in the emulator
    fast_memory_size = RAM_SIZE;
    extended_memory = memory + HMA_START;
    extended_memory_size = sizeof memory - HMA_START;
    a20_enabled = 1; // the flat memory has no A20 gate
    load_segment(regcs, 0xF000);
    CPU_IP = 0xFFF0;

    uint8_t post = 0;
    uint32_t last_eip = 0, same_eip = 0;
    uint32_t trail[32] = {0}; // the last instructions, to find the failing one in test386.lst
    uint32_t trail_count = 0;
    uint32_t step;
    for (step = 0; step < 100000000; step++) {
        exec86(1);
        if (last_out_port == 0x190 && (uint8_t) last_out_value != post) {
            post = (uint8_t) last_out_value;
            printf("POST %02X\n", post);
            if (post == 0xFF) return 0;
        }
        // The ROM stops with HLT at CPL 0 (its code is at linear = physical), or loops at CPL 3.
        const uint32_t eip = CS_BASE + CPU_IP;
        if (memory[PHYSICAL(eip)] == 0xF4 && !(CPU_CS & 3) && !x86_flags.bits.VM) break;
        same_eip = eip == last_eip ? same_eip + 1 : 0;
        if (same_eip > 1000000) break; // longer than any REP count in the ROM
        if (eip != last_eip) trail[trail_count++ % 32] = CPU_IP;
        last_eip = eip;
    }
    printf("after %u instructions: ", step);
    printf("stopped in test %02X at %04X:%08X (linear %08X); the EIPs before it:\n", post, CPU_CS, CPU_IP, CS_BASE + CPU_IP);
    for (int i = 0; i < 32; i++) printf(" %X", trail[(trail_count + i) % 32]);
    printf("\nEAX %08X ECX %08X EDX %08X EBX %08X ESP %08X EFLAGS %08X\n", dwordregs[regax], dwordregs[regcx],
           dwordregs[regdx], dwordregs[regbx], dwordregs[regsp], x86_flags.value);
    return 1;
}
