// Host speed check of the CPU core: runs a fixed real-mode loop and prints the best of 10 runs.
// Use it to compare two versions of cpu.c on the same machine; the number means nothing alone.
// Build and run from the project root (Windows, MinGW):
//   gcc -std=gnu17 -fms-extensions -O2 -Isrc -Isrc/emulator -Isrc/emu8950 -Isrc/printf tests/cpu_bench.c src/emulator/cpu.c src/emulator/fpu.c src/printf/printf.c -o tests/cpu_bench.exe && tests/cpu_bench.exe
#include <stdio.h>
#include <time.h>
#include "cpu_host.h"

// CS=1000h, DS=ES=2000h, SS=3000h. A mix of moves, ALU, memory, a string copy, a call, MUL/DIV and jumps.
static const uint8_t loop_code[] = {
    0xB9, 0x40, 0x00,       // 0000 mov cx,40h
    0x31, 0xF6,             // 0003 xor si,si
    0xBF, 0x00, 0x01,       // 0005 mov di,100h
    0xFC,                   // 0008 cld
    0xF3, 0xA5,             // 0009 rep movsw
    0xBB, 0x00, 0x02,       // 000B mov bx,200h
    0x8B, 0x07,             // 000E mov ax,[bx]
    0x03, 0x47, 0x02,       // 0010 add ax,[bx+2]
    0x89, 0x47, 0x04,       // 0013 mov [bx+4],ax
    0xFF, 0x47, 0x06,       // 0016 inc word [bx+6]
    0xE8, 0x14, 0x00,       // 0019 call 0030
    0xB8, 0x34, 0x12,       // 001C mov ax,1234h
    0xB9, 0x07, 0x00,       // 001F mov cx,7
    0xF7, 0xE1,             // 0022 mul cx
    0x31, 0xD2,             // 0024 xor dx,dx
    0xF7, 0xF1,             // 0026 div cx
    0x3D, 0x00, 0x00,       // 0028 cmp ax,0
    0x75, 0xD3,             // 002B jne 0000
    0xEB, 0xD1,             // 002D jmp 0000
    0x90,                   // 002F nop
    0x50,                   // 0030 push ax
    0xD1, 0xE0,             // 0031 shl ax,1
    0x8A, 0x1E, 0x10, 0x00, // 0033 mov bl,[10h]
    0x58,                   // 0037 pop ax
    0xC3,                   // 0038 ret
};

enum { STEPS_PER_CALL = 10000, CALLS = 10000 }; // 100 million exec86 steps per run

int main(void) {
    memcpy(memory + 0x10000, loop_code, sizeof loop_code);
    fast_memory = memory; // as in the emulator: RAM and the memory from 1 MB without read86/write86
    fast_memory_size = RAM_SIZE;
    extended_memory = memory + HMA_START;
    extended_memory_size = sizeof memory - HMA_START;
    a20_enabled = 1; // the flat memory has no A20 gate
    double best = 1e9;
    for (int run = 0; run < 10; run++) {
        load_segment(regcs, 0x1000);
        CPU_IP = 0;
        load_segment(regds, 0x2000);
        load_segment(reges, 0x2000);
        load_segment(regss, 0x3000);
        CPU_SP = 0xFFFE;
        x86_flags.value = 2;
        const clock_t start = clock();
        for (int i = 0; i < CALLS; i++) exec86(STEPS_PER_CALL);
        const double seconds = (double) (clock() - start) / CLOCKS_PER_SEC;
        if (seconds < best) best = seconds;
    }
    fprintf(stdout, "100M steps: best %.3f s = %.1f M steps/s\n", best, 100.0 / best);
    return 0;
}
