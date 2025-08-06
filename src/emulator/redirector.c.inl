#pragma once
#include "emulator.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <dirent.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif

// =============================================================================
// Configuration
// =============================================================================

#define REDIRECTOR_DRIVE_LETTER 'H'
#define REDIRECTOR_HOST_PATH "C:\\host"
#define MAX_OPEN_FILES 32

// =============================================================================
// Data Structures
// =============================================================================

// Represents an open file on the host system
typedef struct {
    FILE* fp;
    char host_path[256];
    bool in_use;
} RedirectedFile;

// Represents the state of the redirected drive
typedef struct {
    char host_path[256];
    char current_dos_path[256];
    RedirectedFile open_files[MAX_OPEN_FILES];
    // State for FindFirst/FindNext
    DIR* find_handle;
    char find_pattern[256];
    char find_path[512];
} RedirectorState;

static RedirectorState redirector_state;

// =============================================================================
// Function Prototypes (to be implemented)
// =============================================================================

static void redirector_init();
void redirector_handler();

// Subfunction handlers
static void handle_install_check();
static void handle_remove_dir();
static void handle_make_dir();
static void handle_change_dir();
static void handle_close_file();
static void handle_commit_file();
static void handle_read_file();
static void handle_write_file();
static void handle_get_attributes();
static void handle_set_attributes();
static void handle_rename_file();
static void handle_delete_file();
static void handle_open_file();
static void handle_create_file();
static void handle_find_first();
static void handle_find_next();

// =============================================================================
// Implementation
// =============================================================================

// Helper to read a null-terminated string from guest memory
static void read_guest_string(uint16_t seg, uint16_t off, char* out_buffer, int max_len) {
    uint32_t addr = (seg << 4) + off;
    int i = 0;
    for (; i < max_len - 1; ++i) {
        char c = read86(addr + i);
        out_buffer[i] = c;
        if (c == '\0') {
            break;
        }
    }
    out_buffer[i] = '\0';
}

// Helper to create a full host path from a DOS path
static void get_full_host_path(const char* dos_path, char* host_path_out) {
    strcpy(host_path_out, redirector_state.host_path);
    if (dos_path[0] != '\\') {
        strcat(host_path_out, "\\");
    }
    strcat(host_path_out, dos_path);

    // Normalize path separators for non-Windows hosts
    #ifndef _WIN32
    for (char* p = host_path_out; *p; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    #endif
}

static void redirector_init() {
    strncpy(redirector_state.host_path, REDIRECTOR_HOST_PATH, sizeof(redirector_state.host_path) - 1);
    redirector_state.host_path[sizeof(redirector_state.host_path) - 1] = '\0';

    strcpy(redirector_state.current_dos_path, "\\");

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        redirector_state.open_files[i].in_use = false;
        redirector_state.open_files[i].fp = NULL;
    }

    // Create the host directory if it doesn't exist
    #ifdef _WIN32
        CreateDirectory(redirector_state.host_path, NULL);
    #else
        mkdir(redirector_state.host_path, 0755);
    #endif
}

static void handle_install_check() {
    CPU_AL = 0xFF; // Redirector is installed and active
    CPU_FL_CF = 0;
}

static void handle_open_file() {
    char dos_path[256];
    char host_path[512];
    char f_mode[4] = {0};

    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));

    int handle = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!redirector_state.open_files[i].in_use) {
            handle = i;
            break;
        }
    }

    if (handle == -1) {
        CPU_FL_CF = 1;
        CPU_AX = 4; // Too many open files
        return;
    }

    get_full_host_path(dos_path, host_path);

    uint8_t access_mode = CPU_CL & 0x07;
    switch(access_mode) {
        case 0: strcpy(f_mode, "rb"); break;
        case 1: strcpy(f_mode, "r+b"); break; // Open for write implies r+w
        case 2: strcpy(f_mode, "r+b"); break;
        default:
            CPU_FL_CF = 1;
            CPU_AX = 12; // Invalid access mode
            return;
    }

    FILE* fp = fopen(host_path, f_mode);

    if (fp) {
        redirector_state.open_files[handle].fp = fp;
        redirector_state.open_files[handle].in_use = true;
        strncpy(redirector_state.open_files[handle].host_path, host_path, sizeof(redirector_state.open_files[handle].host_path));

        CPU_FL_CF = 0;
        CPU_AX = handle;
    } else {
        CPU_FL_CF = 1;
        CPU_AX = 2; // File not found
    }
}

static void handle_create_file() {
    char dos_path[256];
    char host_path[512];

    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));

    int handle = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!redirector_state.open_files[i].in_use) {
            handle = i;
            break;
        }
    }

    if (handle == -1) {
        CPU_FL_CF = 1;
        CPU_AX = 4; // Too many open files
        return;
    }

    get_full_host_path(dos_path, host_path);

    FILE* fp = fopen(host_path, "w+b");

    if (fp) {
        redirector_state.open_files[handle].fp = fp;
        redirector_state.open_files[handle].in_use = true;
        strncpy(redirector_state.open_files[handle].host_path, host_path, sizeof(redirector_state.open_files[handle].host_path));

        CPU_FL_CF = 0;
        CPU_AX = handle;
    } else {
        CPU_FL_CF = 1;
        CPU_AX = 5; // Access denied
    }
}

static void handle_close_file() {
    uint16_t handle = CPU_BX;

    if (handle >= MAX_OPEN_FILES || !redirector_state.open_files[handle].in_use) {
        CPU_FL_CF = 1;
        CPU_AX = 6; // Invalid handle
        return;
    }

    if (fclose(redirector_state.open_files[handle].fp) == 0) {
        redirector_state.open_files[handle].in_use = false;
        redirector_state.open_files[handle].fp = NULL;
        CPU_FL_CF = 0;
        CPU_AX = 0;
    } else {
        CPU_FL_CF = 1;
        CPU_AX = 6; // Invalid handle
    }
}

static void handle_commit_file() {
    // For our purposes, commit is the same as close.
    handle_close_file();
}

static void handle_read_file() {
    uint16_t handle = CPU_BX;
    uint16_t bytes_to_read = CPU_CX;
    uint32_t buffer_addr = (CPU_DS << 4) + CPU_DX;

    if (handle >= MAX_OPEN_FILES || !redirector_state.open_files[handle].in_use) {
        CPU_FL_CF = 1;
        CPU_AX = 6; // Invalid handle
        return;
    }

    char* temp_buffer = (char*)malloc(bytes_to_read);
    if (!temp_buffer) {
        CPU_FL_CF = 1;
        CPU_AX = 8; // Not enough memory
        return;
    }

    size_t bytes_read = fread(temp_buffer, 1, bytes_to_read, redirector_state.open_files[handle].fp);

    if (ferror(redirector_state.open_files[handle].fp)) {
        free(temp_buffer);
        CPU_FL_CF = 1;
        CPU_AX = 29; // Generic read error
        return;
    }

    for(size_t i = 0; i < bytes_read; i++) {
        write86(buffer_addr + i, temp_buffer[i]);
    }

    free(temp_buffer);

    CPU_FL_CF = 0;
    CPU_CX = bytes_read;
    CPU_AX = 0;
}

static void handle_write_file() {
    uint16_t handle = CPU_BX;
    uint16_t bytes_to_write = CPU_CX;
    uint32_t buffer_addr = (CPU_DS << 4) + CPU_DX;

    if (handle >= MAX_OPEN_FILES || !redirector_state.open_files[handle].in_use) {
        CPU_FL_CF = 1;
        CPU_AX = 6; // Invalid handle
        return;
    }

    char* temp_buffer = (char*)malloc(bytes_to_write);
    if (!temp_buffer) {
        CPU_FL_CF = 1;
        CPU_AX = 8; // Not enough memory
        return;
    }

    for(size_t i = 0; i < bytes_to_write; i++) {
        temp_buffer[i] = read86(buffer_addr + i);
    }

    size_t bytes_written = fwrite(temp_buffer, 1, bytes_to_write, redirector_state.open_files[handle].fp);

    free(temp_buffer);

    if (bytes_written < bytes_to_write) {
        clearerr(redirector_state.open_files[handle].fp); // Clear error for future operations
        CPU_FL_CF = 1;
        CPU_AX = 29; // Generic write error (e.g., disk full)
        return;
    }

    CPU_FL_CF = 0;
    CPU_CX = bytes_written;
    CPU_AX = 0;
}

#include <errno.h>
#include <sys/stat.h>
#include <time.h>

// DOS attribute constants
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_VOLID  0x08
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

// Helper to convert time_t to DOS date/time format
static void time_t_to_dos_datetime(const time_t* t, uint16_t* dos_date, uint16_t* dos_time) {
    struct tm* tm_info = localtime(t);
    if (!tm_info) {
        *dos_date = 0;
        *dos_time = 0;
        return;
    }
    *dos_time = (tm_info->tm_hour << 11) | (tm_info->tm_min << 5) | (tm_info->tm_sec / 2);
    *dos_date = ((tm_info->tm_year + 1900 - 1980) << 9) | ((tm_info->tm_mon + 1) << 5) | tm_info->tm_mday;
}

static void handle_get_attributes() {
    char dos_path[256];
    char host_path[512];
    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));
    get_full_host_path(dos_path, host_path);

    struct stat statbuf;
    if (stat(host_path, &statbuf) != 0) {
        CPU_FL_CF = 1;
        CPU_AX = 2; // File not found
        return;
    }

    uint16_t dos_attr = 0;
    if (S_ISDIR(statbuf.st_mode)) {
        dos_attr |= _A_SUBDIR;
    }
    // Note: S_IWUSR is write permission for user. Lack of it means read-only.
    if (!(statbuf.st_mode & S_IWRITE)) {
        dos_attr |= _A_RDONLY;
    }

    uint16_t dos_date, dos_time;
    time_t_to_dos_datetime(&statbuf.st_mtime, &dos_date, &dos_time);

    uint32_t file_size = statbuf.st_size;

    CPU_FL_CF = 0;
    CPU_AX = dos_attr;
    CPU_CX = dos_time;
    CPU_DX = dos_date;
    CPU_BX = (file_size >> 16) & 0xFFFF;
    CPU_DI = file_size & 0xFFFF;
}

static void handle_set_attributes() {
    char dos_path[256];
    char host_path[512];
    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));
    get_full_host_path(dos_path, host_path);

    uint16_t dos_attr = CPU_CX;

    struct stat statbuf;
    if (stat(host_path, &statbuf) != 0) {
        CPU_FL_CF = 1;
        CPU_AX = 2; // File not found
        return;
    }

    // For now, only handle read-only attribute
    #ifdef _WIN32
        DWORD win_attr = GetFileAttributesA(host_path);
        if (dos_attr & _A_RDONLY) {
            win_attr |= FILE_ATTRIBUTE_READONLY;
        } else {
            win_attr &= ~FILE_ATTRIBUTE_READONLY;
        }
        if (SetFileAttributesA(host_path, win_attr)) {
            CPU_FL_CF = 0;
            CPU_AX = 0;
        } else {
            CPU_FL_CF = 1;
            CPU_AX = 5; // Access denied
        }
    #else
        mode_t new_mode = statbuf.st_mode;
        if (dos_attr & _A_RDONLY) {
            new_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
        } else {
            new_mode |= S_IWUSR;
        }
        if (chmod(host_path, new_mode) == 0) {
            CPU_FL_CF = 0;
            CPU_AX = 0;
        } else {
            CPU_FL_CF = 1;
            CPU_AX = 5; // Access denied
        }
    #endif
}

static void handle_make_dir() {
    char dos_path[256];
    char host_path[512];
    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));
    get_full_host_path(dos_path, host_path);

    #ifdef _WIN32
        if (CreateDirectoryA(host_path, NULL)) {
    #else
        if (mkdir(host_path, 0755) == 0) {
    #endif
        CPU_FL_CF = 0;
        CPU_AX = 0;
    } else {
        CPU_FL_CF = 1;
        CPU_AX = 5; // Access denied
    }
}

static void handle_remove_dir() {
    char dos_path[256];
    char host_path[512];
    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));
    get_full_host_path(dos_path, host_path);

    #ifdef _WIN32
        if (RemoveDirectoryA(host_path)) {
    #else
        if (rmdir(host_path) == 0) {
    #endif
        CPU_FL_CF = 0;
        CPU_AX = 0;
    } else {
        CPU_FL_CF = 1;
        if (errno == ENOTEMPTY) CPU_AX = 16;
        else CPU_AX = 5; // Access denied
    }
}

static void handle_change_dir() {
    char dos_path[256];
    char host_path[512];
    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));

    // Simple path normalization for now.
    // A real implementation would handle '..' etc.
    if (strcmp(dos_path, "\\") == 0) {
        strcpy(redirector_state.current_dos_path, "\\");
    } else {
        snprintf(redirector_state.current_dos_path, sizeof(redirector_state.current_dos_path), "\\%s", dos_path);
    }

    get_full_host_path(redirector_state.current_dos_path, host_path);

    struct stat statbuf;
    if (stat(host_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
        CPU_FL_CF = 0;
        CPU_AX = 0;
    } else {
        CPU_FL_CF = 1;
        CPU_AX = 3; // Path not found
        strcpy(redirector_state.current_dos_path, "\\"); // Revert on failure
    }
}

static void handle_lseek() {
    uint16_t handle = CPU_BX;
    uint32_t offset = ((uint32_t)CPU_CX << 16) | CPU_DX;
    uint8_t whence_code = CPU_AL;
    int whence;

    if (handle >= MAX_OPEN_FILES || !redirector_state.open_files[handle].in_use) {
        CPU_FL_CF = 1;
        CPU_AX = 6; // Invalid handle
        return;
    }

    switch (whence_code) {
        case 0: whence = SEEK_SET; break;
        case 1: whence = SEEK_CUR; break;
        case 2: whence = SEEK_END; break;
        default:
            CPU_FL_CF = 1;
            CPU_AX = 1; // Invalid function (for whence)
            return;
    }

    if (fseek(redirector_state.open_files[handle].fp, offset, whence) == 0) {
        long new_pos = ftell(redirector_state.open_files[handle].fp);
        CPU_FL_CF = 0;
        CPU_DX = (new_pos >> 16) & 0xFFFF;
        CPU_AX = new_pos & 0xFFFF;
    } else {
        CPU_FL_CF = 1;
        CPU_AX = 25; // Seek error
    }
}

static void handle_delete_file() {
    char dos_path[256];
    char host_path[512];
    read_guest_string(CPU_DS, CPU_SI, dos_path, sizeof(dos_path));
    get_full_host_path(dos_path, host_path);

    if (remove(host_path) == 0) {
        CPU_FL_CF = 0;
        CPU_AX = 0;
    } else {
        CPU_FL_CF = 1;
        if (errno == ENOENT) CPU_AX = 2;   // File not found
        else if (errno == EACCES) CPU_AX = 5;   // Access denied
        else CPU_AX = 3; // Path not found (generic error)
    }
}

static void handle_rename_file() {
    char old_dos_path[256];
    char new_dos_path[256];
    char old_host_path[512];
    char new_host_path[512];

    read_guest_string(CPU_DS, CPU_SI, old_dos_path, sizeof(old_dos_path));
    read_guest_string(CPU_ES, CPU_DI, new_dos_path, sizeof(new_dos_path));

    get_full_host_path(old_dos_path, old_host_path);
    get_full_host_path(new_dos_path, new_host_path);

    if (rename(old_host_path, new_host_path) == 0) {
        CPU_FL_CF = 0;
        CPU_AX = 0;
    } else {
        CPU_FL_CF = 1;
        if (errno == EACCES) CPU_AX = 5;       // Access denied
        else if (errno == ENOENT) CPU_AX = 2;  // File not found
        else CPU_AX = 17; // Not same device
    }
}


// Helper for wildcard matching.
static bool wildcard_match(const char* pattern, const char* text) {
    while (*pattern) {
        if (*pattern == '?') {
            if (!*text) return false; // '?' must match a character
            pattern++;
            text++;
        } else if (*pattern == '*') {
            while (*pattern == '*') pattern++; // Skip multiple '*'
            if (!*pattern) return true; // '*' at end matches everything
            while (*text) {
                if (wildcard_match(pattern, text)) return true;
                text++;
            }
            return false;
        } else {
            if (toupper((unsigned char)*pattern) != toupper((unsigned char)*text)) return false;
            pattern++;
            text++;
        }
    }
    return !*text;
}

// Helper to fill the DTA with file information.
static void fill_dta(uint32_t dta_addr, const struct stat* statbuf, const char* filename) {
    // DTA structure for FindFirst/FindNext result:
    // Offset +21 (0x15): Attributes (1 byte)
    // Offset +22 (0x16): File Time (2 bytes)
    // Offset +24 (0x18): File Date (2 bytes)
    // Offset +26 (0x1A): File Size (4 bytes)
    // Offset +30 (0x1E): Filename (13 bytes, ASCIIZ)

    uint16_t dos_attr = 0;
    if (S_ISDIR(statbuf->st_mode)) dos_attr |= _A_SUBDIR;
    if (!(statbuf->st_mode & S_IWRITE)) dos_attr |= _A_RDONLY;
    write86(dta_addr + 0x15, dos_attr);

    uint16_t dos_date, dos_time;
    time_t_to_dos_datetime(&statbuf->st_mtime, &dos_date, &dos_time);
    writew86(dta_addr + 0x16, dos_time);
    writew86(dta_addr + 0x18, dos_date);

    writedw86(dta_addr + 0x1A, statbuf->st_size);

    char dos_name[13];
    strncpy(dos_name, filename, 12);
    dos_name[12] = '\0';
    for(int i=0; dos_name[i]; i++) dos_name[i] = toupper((unsigned char)dos_name[i]);
    for(int i=0; i<13; i++) {
        write86(dta_addr + 0x1E + i, dos_name[i]);
        if (dos_name[i] == '\0') break;
    }
}

static void handle_find_next(); // Forward declaration

static void handle_find_first() {
    char filespec[256];
    read_guest_string(CPU_DS, CPU_SI, filespec, sizeof(filespec));
    uint32_t dta_addr = (CPU_ES << 4) + CPU_BX;

    if (redirector_state.find_handle) {
        closedir(redirector_state.find_handle);
        redirector_state.find_handle = NULL;
    }

    char* pattern_part = strrchr(filespec, '\\');
    if (pattern_part) {
        *pattern_part = '\0';
        pattern_part++;
        get_full_host_path(filespec, redirector_state.find_path);
    } else {
        pattern_part = filespec;
        get_full_host_path(redirector_state.current_dos_path, redirector_state.find_path);
    }
    strcpy(redirector_state.find_pattern, pattern_part);

    redirector_state.find_handle = opendir(redirector_state.find_path);
    if (!redirector_state.find_handle) {
        CPU_FL_CF = 1;
        CPU_AX = 18; // No more files
        return;
    }

    // Write search info to DTA reserved area (for FindNext)
    // We don't really need this for our implementation, but DOS might expect it.
    // For now, we'll just call FindNext to get the first file.
    handle_find_next();
}

static void handle_find_next() {
    uint32_t dta_addr = (CPU_ES << 4) + CPU_BX;

    if (!redirector_state.find_handle) {
        CPU_FL_CF = 1;
        CPU_AX = 18; // No more files
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(redirector_state.find_handle)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        if (wildcard_match(redirector_state.find_pattern, ent->d_name)) {
            char full_entry_path[1024];
            snprintf(full_entry_path, sizeof(full_entry_path), "%s/%s", redirector_state.find_path, ent->d_name);

            struct stat statbuf;
            if (stat(full_entry_path, &statbuf) == 0) {
                fill_dta(dta_addr, &statbuf, ent->d_name);
                CPU_FL_CF = 0;
                CPU_AX = 0;
                return;
            }
        }
    }

    closedir(redirector_state.find_handle);
    redirector_state.find_handle = NULL;
    CPU_FL_CF = 1;
    CPU_AX = 18; // No more files
}

void redirector_handler() {
    // RBIL documents AX for redirector calls, but some sources say AH=11h is fixed
    // and AL is the subfunction. The spec says "AX=1116h", so we switch on AX.
    switch (CPU_AX) {
        case 0x1100: handle_install_check(); break;
        // Directory operations
        case 0x1101: handle_remove_dir(); break;
        case 0x1103: handle_make_dir(); break;
        case 0x1105: handle_change_dir(); break;
        // File operations
        case 0x1106: handle_close_file(); break;
        case 0x1107: handle_commit_file(); break;
        case 0x1108: handle_read_file(); break;
        case 0x1109: handle_write_file(); break;
        case 0x110E: handle_set_attributes(); break;
        case 0x110F: handle_get_attributes(); break;
        case 0x1111: handle_rename_file(); break;
        case 0x1113: handle_delete_file(); break;
        case 0x1116: handle_open_file(); break;
        case 0x1117: handle_create_file(); break;
        case 0x1118: handle_create_file(); break; // Create w/o CDS, treat as normal create
        case 0x111A: handle_lseek(); break;
        // Find operations
        case 0x111B: handle_find_first(); break;
        case 0x111C: handle_find_next(); break;

        default:
            printf("REDIRECTOR: Unhandled INT 2Fh function AX=%04Xh\n", CPU_AX);
            CPU_FL_CF = 1;
            CPU_AX = 1; // ERROR_INVALID_FUNCTION
            break;
    }
}
