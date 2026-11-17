// Runs the SingleStepTests real-mode CPU tests (MOO files) against our CPU and counts passes.
// The tests are not in git; see docs/README.md. Unpack the .MOO.gz files first (gzip -d).
// Build and run from the project root (Windows, MinGW):
//   gcc -std=gnu17 -fms-extensions -O2 -Isrc -Isrc/emulator -Isrc/emu8950 -Isrc/printf tests/sst_test.c src/emulator/cpu.c src/printf/printf.c -o tests/sst_test.exe
//   tests/sst_test.exe <folder with .MOO files> [-v]
// -v prints the first failing test of each file.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_host.h"

// MOO register order: ax bx cx dx cs ss ds es sp bp si di ip flags
enum { REGISTER_COUNT = 14, FLAGS = 13 };
// The flags (index 13) are not a plain word in our CPU: see cpu_flags().
static uint16_t *register_pointer(int index) {
    switch (index) {
        case 0: return &CPU_AX;
        case 1: return &CPU_BX;
        case 2: return &CPU_CX;
        case 3: return &CPU_DX;
        case 4: return &CPU_CS;
        case 5: return &CPU_SS;
        case 6: return &CPU_DS;
        case 7: return &CPU_ES;
        case 8: return &CPU_SP;
        case 9: return &CPU_BP;
        case 10: return &CPU_SI;
        case 11: return &CPU_DI;
        default: return &CPU_IP;
    }
}
static const char *const register_names[REGISTER_COUNT] = {
    "ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags"
};
// The flags as PUSHF shows them in real mode on a 286.
static uint16_t cpu_flags(void) { return (x86_flags.value & 0x0FD5) | 2; }

typedef struct {
    const uint8_t *data;
    uint32_t length;
} chunk_t;

static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }
static uint16_t u16(const uint8_t *p) { return p[0] | p[1] << 8; }

// Finds the first chunk of this type in [data, data + length).
static int find_chunk(const uint8_t *data, uint32_t length, const char *type, chunk_t *chunk) {
    for (uint32_t at = 0; at + 8 <= length; at += 8 + u32(data + at + 4)) {
        if (memcmp(data + at, type, 4) == 0) {
            chunk->data = data + at + 8;
            chunk->length = u32(data + at + 4);
            return 1;
        }
    }
    return 0;
}

// Reads a REGS or RMSK chunk: the bitmask, then one word per set bit.
static uint16_t read_registers(chunk_t chunk, uint16_t values[REGISTER_COUNT]) {
    const uint16_t present = u16(chunk.data);
    const uint8_t *p = chunk.data + 2;
    for (int i = 0; i < REGISTER_COUNT; i++) {
        if (present & 1 << i) {
            values[i] = u16(p);
            p += 2;
        }
    }
    return present;
}

static int is_prefix(uint8_t byte) {
    return byte == 0x26 || byte == 0x2E || byte == 0x36 || byte == 0x3E || byte == 0xF0 || byte == 0xF2 || byte == 0xF3;
}

// Runs until the HLT that ends every test has run. Returns 0 if it never comes.
static int run_to_halt(void) {
    for (int step = 0; step < 200000; step++) {
        uint32_t address = ((uint32_t) CPU_CS << 4) + CPU_IP;
        while (is_prefix(memory[PHYSICAL(address)])) address++;
        const int halt = memory[PHYSICAL(address)] == 0xF4;
        exec86(1);
        if (halt) return 1;
    }
    return 0;
}

static char failure[256];

// Runs one TEST chunk. Returns 1 if it passes, 0 if it fails, -1 if it is skipped.
static int run_test(chunk_t test, const uint16_t file_masks[REGISTER_COUNT]) {
    const uint8_t *subchunks = test.data + 4;
    const uint32_t length = test.length - 4;
    chunk_t init, final, regs, ram, bytes, excp;
    if (!find_chunk(subchunks, length, "INIT", &init) || !find_chunk(subchunks, length, "FINA", &final) ||
        !find_chunk(subchunks, length, "BYTS", &bytes)) {
        snprintf(failure, sizeof failure, "broken test chunk");
        return 0;
    }
    // INT n into one of our C BIOS hooks runs the hook, not the vector: not a CPU test.
    const uint8_t *code = bytes.data + 4;
    while (is_prefix(*code)) code++;
    if (code[0] == 0xCD && (code[1] == 0x10 || code[1] == 0x13 || code[1] == 0x15 || code[1] == 0x19 ||
                            code[1] == 0x1A || code[1] == 0x2F)) {
        return -1;
    }

    clear_memory(); // so a test never sees what the one before it wrote
    uint16_t values[REGISTER_COUNT] = {0};
    find_chunk(init.data, init.length, "REGS", &regs);
    read_registers(regs, values);
    for (int i = 0; i < REGISTER_COUNT - 1; i++) *register_pointer(i) = values[i];
    x86_flags.value = values[FLAGS];
    find_chunk(init.data, init.length, "RAM ", &ram);
    for (uint32_t i = 0, count = u32(ram.data); i < count; i++) {
        flat_write(u32(ram.data + 4 + i * 5), ram.data[4 + i * 5 + 4]);
    }

    if (!run_to_halt()) {
        snprintf(failure, sizeof failure, "no HLT reached");
        return 0;
    }

    uint16_t masks[REGISTER_COUNT];
    memcpy(masks, file_masks, sizeof masks);
    chunk_t mask_chunk;
    if (find_chunk(final.data, final.length, "RMSK", &mask_chunk)) read_registers(mask_chunk, masks);

    uint16_t expected[REGISTER_COUNT];
    find_chunk(final.data, final.length, "REGS", &regs);
    const uint16_t present = read_registers(regs, expected);
    for (int i = 0; i < REGISTER_COUNT; i++) {
        if (!(present & 1 << i)) continue;
        const uint16_t actual = i == FLAGS ? cpu_flags() : *register_pointer(i);
        if ((actual & masks[i]) != (expected[i] & masks[i])) {
            snprintf(failure, sizeof failure, "%s = %04X, expected %04X (mask %04X)",
                     register_names[i], actual, expected[i], masks[i]);
            return 0;
        }
    }

    // The flags an exception pushes may hold undefined bits: mask them like the flags register.
    uint32_t flags_address = 0xFFFFFFFF;
    if (find_chunk(subchunks, length, "EXCP", &excp)) flags_address = PHYSICAL(u32(excp.data + 1));
    find_chunk(final.data, final.length, "RAM ", &ram);
    for (uint32_t i = 0, count = u32(ram.data); i < count; i++) {
        const uint32_t address = PHYSICAL(u32(ram.data + 4 + i * 5));
        uint8_t mask = 0xFF;
        if (address == flags_address) mask = masks[FLAGS];
        if (address == PHYSICAL(flags_address + 1)) mask = masks[FLAGS] >> 8;
        const uint8_t value = ram.data[4 + i * 5 + 4];
        if ((memory[address] & mask) != (value & mask)) {
            snprintf(failure, sizeof failure, "memory[%06X] = %02X, expected %02X", address, memory[address], value);
            return 0;
        }
    }
    return 1;
}

static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *) a, *(char *const *) b); }

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <folder with .MOO files> [-v]\n", argv[0]);
        return 2;
    }
    const int verbose = argc > 2 && strcmp(argv[2], "-v") == 0;
    DIR *dir = opendir(argv[1]);
    if (!dir) {
        perror(argv[1]);
        return 2;
    }
    char *names[2048];
    int name_count = 0;
    for (struct dirent *entry; (entry = readdir(dir)) && name_count < 2048;) {
        const size_t n = strlen(entry->d_name);
        if (n > 4 && strcmp(entry->d_name + n - 4, ".MOO") == 0) names[name_count++] = strdup(entry->d_name);
    }
    closedir(dir);
    qsort(names, name_count, sizeof *names, compare_names);

    long total_passed = 0, total_failed = 0, total_skipped = 0;
    int clean_files = 0;
    for (int f = 0; f < name_count; f++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", argv[1], names[f]);
        FILE *file = fopen(path, "rb");
        if (!file) continue;
        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        rewind(file);
        uint8_t *data = malloc(size);
        fread(data, 1, size, file);
        fclose(file);

        uint16_t file_masks[REGISTER_COUNT];
        for (int i = 0; i < REGISTER_COUNT; i++) file_masks[i] = 0xFFFF;
        long passed = 0, failed = 0, skipped = 0;
        int shown = 0;
        for (uint32_t at = 0; at + 8 <= (uint32_t) size; at += 8 + u32(data + at + 4)) {
            const chunk_t chunk = {data + at + 8, u32(data + at + 4)};
            if (memcmp(data + at, "RMSK", 4) == 0) read_registers(chunk, file_masks);
            if (memcmp(data + at, "TEST", 4) != 0) continue;
            const int result = run_test(chunk, file_masks);
            if (result > 0) passed++;
            else if (result < 0) skipped++;
            else {
                failed++;
                if (verbose && !shown++) {
                    chunk_t name;
                    find_chunk(chunk.data + 4, chunk.length - 4, "NAME", &name);
                    printf("  %s #%u \"%.*s\": %s\n", names[f], u32(chunk.data), (int) u32(name.data),
                           (const char *) name.data + 4, failure);
                }
            }
        }
        free(data);
        printf("%-10s %6ld passed %6ld failed %6ld skipped\n", names[f], passed, failed, skipped);
        total_passed += passed;
        total_failed += failed;
        total_skipped += skipped;
        clean_files += failed == 0;
    }
    printf("TOTAL: %ld passed, %ld failed, %ld skipped (%.2f%% of run tests); %d of %d files with no failures\n",
           total_passed, total_failed, total_skipped,
           100.0 * total_passed / (total_passed + total_failed ? total_passed + total_failed : 1), clean_files, name_count);
    return 0;
}
