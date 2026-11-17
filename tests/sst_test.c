// Runs the SingleStepTests real-mode CPU tests (MOO files) against our CPU and counts passes.
// Works with the 80286 suite (16-bit REGS chunks) and the 80386 suite (32-bit RG32 chunks).
// The tests are not in git; see docs/README.md. Unpack the .MOO.gz files first (gzip -d).
// Build and run from the project root (Windows, MinGW):
//   gcc -std=gnu17 -fms-extensions -O2 -Isrc -Isrc/emulator -Isrc/emu8950 -Isrc/printf tests/sst_test.c src/emulator/cpu.c src/emulator/fpu.c src/printf/printf.c -o tests/sst_test.exe
//   tests/sst_test.exe <folder with .MOO files> [-v]
// -v prints the first failing test of each file.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_host.h"
uint16_t makeflagsword(void); // FLAGS as PUSHF shows them (cpu.c)

// Registers in the RG32 order. A 16-bit REGS chunk is mapped onto the same numbers.
enum {
    R_CR0, R_CR3, R_EAX, R_EBX, R_ECX, R_EDX, R_ESI, R_EDI, R_EBP, R_ESP,
    R_CS, R_DS, R_ES, R_FS, R_GS, R_SS, R_EIP, R_EFLAGS, R_DR6, R_DR7, REGISTER_COUNT
};
static const char *const register_names[REGISTER_COUNT] = {
    "cr0", "cr3", "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
    "cs", "ds", "es", "fs", "gs", "ss", "eip", "eflags", "dr6", "dr7"
};
// REGS order: ax bx cx dx cs ss ds es sp bp si di ip flags
static const uint8_t regs16_order[14] = {
    R_EAX, R_EBX, R_ECX, R_EDX, R_CS, R_SS, R_DS, R_ES, R_ESP, R_EBP, R_ESI, R_EDI, R_EIP, R_EFLAGS
};
static const uint8_t general_index[] = {
    [R_EAX] = regax, [R_EBX] = regbx, [R_ECX] = regcx, [R_EDX] = regdx,
    [R_ESI] = regsi, [R_EDI] = regdi, [R_EBP] = regbp, [R_ESP] = regsp
};
static int8_t segment_index(int id) {
    switch (id) {
        case R_CS: return regcs;
        case R_DS: return regds;
        case R_ES: return reges;
        case R_SS: return regss;
        case R_FS: return regfs;
        case R_GS: return reggs;
        default: return -1;
    }
}

// Registers the CPU does not have (control and debug registers) are skipped.
static bool cpu_has(int id) {
    return (id >= R_EAX && id <= R_ESP) || segment_index(id) >= 0 || id == R_EIP || id == R_EFLAGS;
}

static void set_register(int id, uint32_t value) {
    if (id >= R_EAX && id <= R_ESP) dwordregs[general_index[id]] = value;
    else if (segment_index(id) >= 0) load_segment((uint8_t) segment_index(id), (uint16_t) value);
    else if (id == R_EIP) ip32 = value;
    else if (id == R_EFLAGS) x86_flags.value = value;
}

static uint32_t get_register(int id) {
    if (id >= R_EAX && id <= R_ESP) return dwordregs[general_index[id]];
    if (segment_index(id) >= 0) return segregs[segment_index(id)];
    if (id == R_EIP) return ip32;
    return makeflagsword(); // R_EFLAGS
}

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

// Reads a REGS/RMSK (16-bit) or RG32/RM32 (32-bit) chunk: a bitmask, then one value per set bit.
// Returns the bitmask in RG32 register numbers.
static uint32_t read_registers(chunk_t chunk, bool wide, uint32_t values[REGISTER_COUNT]) {
    uint32_t present = 0;
    if (wide) {
        const uint32_t bits = u32(chunk.data);
        const uint8_t *p = chunk.data + 4;
        for (int i = 0; i < REGISTER_COUNT; i++) {
            if (bits & 1u << i) {
                values[i] = u32(p);
                p += 4;
                present |= 1u << i;
            }
        }
    } else {
        const uint16_t bits = u16(chunk.data);
        const uint8_t *p = chunk.data + 2;
        for (int i = 0; i < 14; i++) {
            if (bits & 1u << i) {
                values[regs16_order[i]] = u16(p);
                p += 2;
                present |= 1u << regs16_order[i];
            }
        }
    }
    return present;
}

// Finds a register chunk in a state (INIT/FINA): RG32 if there is one, else REGS.
static bool find_registers(chunk_t state, const char *wide_type, const char *narrow_type, chunk_t *chunk, bool *wide) {
    *wide = find_chunk(state.data, state.length, wide_type, chunk);
    return *wide || find_chunk(state.data, state.length, narrow_type, chunk);
}

static int is_prefix(uint8_t byte) {
    return byte == 0x26 || byte == 0x2E || byte == 0x36 || byte == 0x3E || byte == 0x64 || byte == 0x65 ||
           byte == 0x66 || byte == 0x67 || byte == 0xF0 || byte == 0xF2 || byte == 0xF3;
}

// Runs until the HLT that ends every test has run. Returns 0 if it never comes.
static int run_to_halt(void) {
    for (int step = 0; step < 1000; step++) { // REP counts in the tests are at most 127
        uint32_t address = CS_BASE + CPU_IP;
        while (is_prefix(memory[PHYSICAL(address)])) address++;
        const int halt = memory[PHYSICAL(address)] == 0xF4;
        exec86(1);
        if (halt) return 1;
    }
    return 0;
}

static char failure[256];

// Runs one TEST chunk. Returns 1 if it passes, 0 if it fails, -1 if it is skipped.
static int run_test(chunk_t test, const uint32_t file_masks[REGISTER_COUNT]) {
    const uint8_t *subchunks = test.data + 4;
    const uint32_t length = test.length - 4;
    chunk_t init, final, regs, ram, bytes, excp;
    bool wide;
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
    uint32_t values[REGISTER_COUNT] = {0};
    find_registers(init, "RG32", "REGS", &regs, &wide);
    const uint32_t initial = read_registers(regs, wide, values);
    for (int i = 0; i < REGISTER_COUNT; i++) {
        if (initial & 1u << i) set_register(i, values[i]);
    }
    find_chunk(init.data, init.length, "RAM ", &ram);
    for (uint32_t i = 0, count = u32(ram.data); i < count; i++) {
        flat_write(u32(ram.data + 4 + i * 5), ram.data[4 + i * 5 + 4]);
    }

    if (!run_to_halt()) {
        snprintf(failure, sizeof failure, "no HLT reached");
        return 0;
    }

    uint32_t masks[REGISTER_COUNT];
    memcpy(masks, file_masks, sizeof masks);
    chunk_t mask_chunk;
    bool mask_wide;
    if (find_registers(final, "RM32", "RMSK", &mask_chunk, &mask_wide)) read_registers(mask_chunk, mask_wide, masks);

    uint32_t expected[REGISTER_COUNT];
    find_registers(final, "RG32", "REGS", &regs, &wide);
    const uint32_t present = read_registers(regs, wide, expected);
    const uint32_t width_mask = wide ? 0xFFFFFFFFu : 0xFFFFu;
    for (int i = 0; i < REGISTER_COUNT; i++) {
        if (!(present & 1u << i) || !cpu_has(i)) continue;
        uint32_t mask = masks[i] & width_mask;
        if (i == R_EFLAGS) mask &= 0x3FFFF; // bits 18-31 are reserved on the 386 (the 386EX tests have them set)
        const uint32_t actual = get_register(i);
        if ((actual & mask) != (expected[i] & mask)) {
            snprintf(failure, sizeof failure, "%s = %08X, expected %08X (mask %08X)",
                     register_names[i], actual, expected[i], mask);
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
        if (address == flags_address) mask = (uint8_t) masks[R_EFLAGS];
        if (address == PHYSICAL(flags_address + 1)) mask = (uint8_t) (masks[R_EFLAGS] >> 8);
        const uint8_t value = ram.data[4 + i * 5 + 4];
        if ((memory[address] & mask) != (value & mask)) {
            snprintf(failure, sizeof failure, "memory[%06X] = %02X, expected %02X", address, memory[address], value);
            return 0;
        }
    }
    return 1;
}

// Flags the manual leaves undefined, per opcode: the f_umask column of 80386.csv in the
// test folder (op = opcode hex, ex = group extension). Only the 386 suite has this file.
typedef struct {
    char op[5];
    int extension; // -1: no group
    uint16_t defined_flags;
} flag_mask_t;
static flag_mask_t flag_masks[1024];
static int flag_mask_count;

static void load_flag_masks(const char *folder) {
    char path[1024], line[1024];
    snprintf(path, sizeof path, "%s/80386.csv", folder);
    FILE *file = fopen(path, "r");
    if (!file) return;
    while (fgets(line, sizeof line, file) && flag_mask_count < 1024) {
        char *fields[48] = {0};
        int count = 0;
        for (char *p = line; count < 48; count++) {
            fields[count] = p;
            p = strchr(p, ',');
            if (!p) {
                count++;
                break;
            }
            *p++ = 0;
        }
        // columns: 1 op, 5 ex, 40 f_umask
        if (count < 40 || !fields[39][0] || strncmp(fields[39], "0x", 2) != 0) continue;
        flag_mask_t *mask = &flag_masks[flag_mask_count++];
        snprintf(mask->op, sizeof mask->op, "%s", fields[0]);
        mask->extension = fields[4][0] ? atoi(fields[4]) : -1;
        mask->defined_flags = (uint16_t) strtol(fields[39], NULL, 16);
    }
    fclose(file);
}

// Defined-flag mask for a test file such as "6766D3.5.MOO", or 0xFFFF.
static uint16_t defined_flags_for(const char *file_name) {
    while (strncmp(file_name, "66", 2) == 0 || strncmp(file_name, "67", 2) == 0) file_name += 2;
    char op[5] = {0};
    int length = 0;
    while (length < 4 && file_name[length] && file_name[length] != '.') length++;
    memcpy(op, file_name, length);
    const int extension = file_name[length] == '.' && file_name[length + 1] != 'M' ? atoi(file_name + length + 1) : -1;
    for (int i = 0; i < flag_mask_count; i++) {
        if (strcmp(flag_masks[i].op, op) == 0 && flag_masks[i].extension == extension) return flag_masks[i].defined_flags;
    }
    return 0xFFFF;
}

static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *) a, *(char *const *) b); }

// Hashes of revoked tests, from revocation_list.txt in the test folder.
static char revoked[256][41];
static int revoked_count;

static void load_revocation_list(const char *folder) {
    char path[1024];
    snprintf(path, sizeof path, "%s/revocation_list.txt", folder);
    FILE *file = fopen(path, "r");
    if (!file) return;
    char line[128];
    while (revoked_count < 256 && fgets(line, sizeof line, file)) {
        if (strlen(line) >= 40) memcpy(revoked[revoked_count++], line, 40);
    }
    fclose(file);
}

static bool is_revoked(chunk_t test) {
    chunk_t hash;
    if (!revoked_count || !find_chunk(test.data + 4, test.length - 4, "HASH", &hash)) return false;
    char hex[41];
    for (int i = 0; i < 20; i++) sprintf(hex + i * 2, "%02x", hash.data[i]);
    for (int i = 0; i < revoked_count; i++) {
        if (strncasecmp(revoked[i], hex, 40) == 0) return true;
    }
    return false;
}

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
    load_revocation_list(argv[1]);
    load_flag_masks(argv[1]);
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

        uint32_t file_masks[REGISTER_COUNT];
        for (int i = 0; i < REGISTER_COUNT; i++) file_masks[i] = 0xFFFFFFFFu;
        file_masks[R_EFLAGS] = 0xFFFF0000u | defined_flags_for(names[f]);
        long passed = 0, failed = 0, skipped = 0;
        int shown = 0;
        for (uint32_t at = 0; at + 8 <= (uint32_t) size; at += 8 + u32(data + at + 4)) {
            const chunk_t chunk = {data + at + 8, u32(data + at + 4)};
            if (memcmp(data + at, "RMSK", 4) == 0 || memcmp(data + at, "RM32", 4) == 0) {
                const uint32_t undefined = file_masks[R_EFLAGS];
                read_registers(chunk, memcmp(data + at, "RM32", 4) == 0, file_masks);
                file_masks[R_EFLAGS] &= undefined;
            }
            if (memcmp(data + at, "TEST", 4) != 0) continue;
            const int result = is_revoked(chunk) ? -1 : run_test(chunk, file_masks);
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
        printf("%-12s %6ld passed %6ld failed %6ld skipped\n", names[f], passed, failed, skipped);
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
