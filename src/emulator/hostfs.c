#if !PICO_ON_DEVICE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#include "hostfs.h"
#include "emulator.h"

// The drive letter and number for the host filesystem passthrough.
// E: is drive 4 (0=A, 1=B, 2=C, 3=D, 4=E)
static const char host_drive_letter = 'E';
static const int host_drive_number = 4;
static const char* host_root_dir = "hostfs";

// Max number of open files we can manage. DOS uses small integers for handles.
// Handles 0-4 are reserved for standard devices. We'll use handles 5-19.
#define MAX_HOST_FILES 15
#define FIRST_HOST_HANDLE 5

// Max path length
#define MAX_HOST_PATH 260

// Structure to manage an open file
typedef struct {
    bool in_use;
    FILE *fp;
} HostFile;

// Array to hold the state of open files
static HostFile open_files[MAX_HOST_FILES];

// The current working directory for our host drive
static char current_host_dir[MAX_HOST_PATH];

// Helper to read a null-terminated string from emulated memory
static void read_dos_string(uint32_t address, char* buffer, int max_len) {
    for (int i = 0; i < max_len; i++) {
        char c = read86(address + i);
        buffer[i] = c;
        if (c == 0) {
            return;
        }
    }
    buffer[max_len - 1] = 0;
}

// Helper to write a null-terminated string to emulated memory
static void write_dos_string(uint32_t address, const char* str) {
    int i = 0;
    while (str[i]) {
        write86(address + i, str[i]);
        i++;
    }
    write86(address + i, 0);
}

// Build a full, clean host path from a DOS path.
static void get_full_host_path(const char* dos_path, char* host_path) {
    char temp_path[MAX_HOST_PATH];

    // If path starts with \ or /, it's from the root of our hostfs drive.
    if (dos_path[0] == '\\' || dos_path[0] == '/') {
        sprintf(temp_path, "%s%s", host_root_dir, dos_path);
    } else {
        // It's a relative path, combine with current_host_dir
        sprintf(temp_path, "%s/%s", current_host_dir, dos_path);
    }

    // A real implementation would need to resolve ".." and "." here.
    // For now, we'll keep it simple.
    strcpy(host_path, temp_path);

    // Convert backslashes to forward slashes for consistency
    for (char *p = host_path; *p; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }
}

void hostfs_init() {
    printf("HostFS: Passthrough initialized for drive %c: on path '%s'\n", host_drive_letter, host_root_dir);
    for (int i = 0; i < MAX_HOST_FILES; i++) {
        open_files[i].in_use = false;
    }
    // Set initial CWD to the root of our hostfs drive.
    strcpy(current_host_dir, host_root_dir);

    // Create the directory if it doesn't exist.
    #ifdef _WIN32
        _mkdir(host_root_dir);
    #else
        mkdir(host_root_dir, 0755);
    #endif

    // Initialize the drive state in the BDA
    RAM[BIOS_CURRENT_DRIVE] = 2; // Default to C:
    RAM[BIOS_LAST_DRIVE] = host_drive_number; // Last drive is E:
}

bool hostfs_int21h() {
    // Some functions don't use a drive letter, they assume the current drive.
    // We get the current drive from the emulated BIOS data area.
    int current_drive = RAM[BIOS_CURRENT_DRIVE];

    switch (CPU_AH) {
        case 0x19: // Get Current Drive
            // This is a system-wide call, but we should handle it to be safe.
            CPU_AL = current_drive;
            CPU_FL_CF = 0;
            return true;

        case 0x0E: // Select Current Drive
            RAM[BIOS_CURRENT_DRIVE] = CPU_DL;
            CPU_AL = RAM[BIOS_LAST_DRIVE]; // Return last drive
            CPU_FL_CF = 0;
            return true;

        case 0x36: // Get Disk Free Space
        {
            int drive = (CPU_DL == 0) ? current_drive : (CPU_DL - 1);
            if (drive == host_drive_number) {
                // Return large, dummy values for the host drive.
                CPU_AX = 0xFFFF; // Error code for functions that don't support it, but many programs ignore it and check flags
                CPU_FL_CF = 0;   // No error
                CPU_BX = 4096;   // Available clusters
                CPU_CX = 512;    // Bytes per sector
                CPU_DX = 8192;   // Total clusters
                return true;
            }
            break; // Not for us, let original handler run.
        }

        case 0x3D: // Open File
        {
            char dos_path[MAX_HOST_PATH];
            read_dos_string((uint32_t)CPU_DS << 4 | CPU_DX, dos_path, sizeof(dos_path));

            int drive = current_drive;
            const char* path_part = dos_path;
            if (dos_path[1] == ':') {
                drive = toupper(dos_path[0]) - 'A';
                path_part = dos_path + 2;
            }

            if (drive == host_drive_number) {
                int handle = -1;
                for (int i = 0; i < MAX_HOST_FILES; i++) {
                    if (!open_files[i].in_use) {
                        handle = i;
                        break;
                    }
                }

                if (handle == -1) {
                    CPU_AX = 0x04; // Too many open files
                    CPU_FL_CF = 1;
                    return true;
                }

                char host_path[MAX_HOST_PATH];
                get_full_host_path(path_part, host_path);

                const char* mode_str;
                switch (CPU_AL & 0x07) {
                    case 0: mode_str = "rb"; break;
                    case 1: mode_str = "wb"; break;
                    case 2: mode_str = "r+b"; break;
                    default: CPU_AX = 0x01; CPU_FL_CF = 1; return true; // Invalid function
                }

                FILE* fp = fopen(host_path, mode_str);

                if (!fp) {
                    CPU_AX = 0x02; // File not found
                    CPU_FL_CF = 1;
                    return true;
                }

                open_files[handle].in_use = true;
                open_files[handle].fp = fp;

                CPU_AX = handle + FIRST_HOST_HANDLE; // Return the handle
                CPU_FL_CF = 0;
                return true;
            }
            break;
        }

        case 0x3E: // Close File
        {
            int handle = CPU_BX - FIRST_HOST_HANDLE;
            if (handle >= 0 && handle < MAX_HOST_FILES && open_files[handle].in_use) {
                fclose(open_files[handle].fp);
                open_files[handle].in_use = false;
                CPU_AX = 0;
                CPU_FL_CF = 0;
                return true;
            }
            break;
        }

        case 0x3F: // Read File
        {
            int handle = CPU_BX - FIRST_HOST_HANDLE;
            uint16_t bytes_to_read = CPU_CX;
            uint32_t buffer_addr = (uint32_t)CPU_DS << 4 | CPU_DX;

            if (handle >= 0 && handle < MAX_HOST_FILES && open_files[handle].in_use) {
                char* temp_buffer = malloc(bytes_to_read);
                if (!temp_buffer) { CPU_AX = 0x08; CPU_FL_CF = 1; return true; }

                size_t bytes_read = fread(temp_buffer, 1, bytes_to_read, open_files[handle].fp);

                for (size_t i = 0; i < bytes_read; i++) {
                    write86(buffer_addr + i, temp_buffer[i]);
                }

                free(temp_buffer);

                CPU_AX = bytes_read;
                CPU_FL_CF = 0;
                return true;
            }
            break;
        }

        case 0x40: // Write File
        {
            int handle = CPU_BX - FIRST_HOST_HANDLE;
            uint16_t bytes_to_write = CPU_CX;
            uint32_t buffer_addr = (uint32_t)CPU_DS << 4 | CPU_DX;

            if (handle >= 0 && handle < MAX_HOST_FILES && open_files[handle].in_use) {
                if (bytes_to_write == 0) { // Write zero bytes means truncate
                    // ftruncate is not standard, but this is a common request
                    CPU_AX = 0; CPU_FL_CF = 0; return true;
                }

                char* temp_buffer = malloc(bytes_to_write);
                if (!temp_buffer) { CPU_AX = 0x08; CPU_FL_CF = 1; return true; }

                for (size_t i = 0; i < bytes_to_write; i++) {
                    temp_buffer[i] = read86(buffer_addr + i);
                }

                size_t bytes_written = fwrite(temp_buffer, 1, bytes_to_write, open_files[handle].fp);
                free(temp_buffer);

                CPU_AX = bytes_written;
                CPU_FL_CF = 0;
                return true;
            }
            break;
        }

        case 0x42: // Lseek
        {
            int handle = CPU_BX - FIRST_HOST_HANDLE;
            long offset = ((long)CPU_CX << 16) | CPU_DX;
            int whence = CPU_AL;

            if (handle >= 0 && handle < MAX_HOST_FILES && open_files[handle].in_use) {
                int fseek_whence;
                switch(whence) {
                    case 0: fseek_whence = SEEK_SET; break;
                    case 1: fseek_whence = SEEK_CUR; break;
                    case 2: fseek_whence = SEEK_END; break;
                    default: CPU_AX = 0x01; CPU_FL_CF = 1; return true; // Invalid function
                }

                if (fseek(open_files[handle].fp, offset, fseek_whence) != 0) {
                    CPU_AX = 0x01; CPU_FL_CF = 1; return true; // seek error
                }

                long new_pos = ftell(open_files[handle].fp);
                CPU_DX = (new_pos >> 16) & 0xFFFF;
                CPU_AX = new_pos & 0xFFFF;
                CPU_FL_CF = 0;
                return true;
            }
            break;
        }

        case 0x47: // Get Current Directory
        {
            int drive = (CPU_DL == 0) ? current_drive : (CPU_DL - 1);
            if (drive == host_drive_number) {
                char relative_path[MAX_HOST_PATH];
                // Remove the "hostfs" prefix to get the path relative to the virtual drive root
                if (strncmp(current_host_dir, host_root_dir, strlen(host_root_dir)) == 0) {
                    strcpy(relative_path, current_host_dir + strlen(host_root_dir));
                    if (relative_path[0] == '/') {
                        // It's a subdirectory, write it to DS:SI
                        write_dos_string((uint32_t)CPU_DS << 4 | CPU_SI, relative_path + 1);
                    } else {
                        // It's the root, write "\"
                        write_dos_string((uint32_t)CPU_DS << 4 | CPU_SI, "\\");
                    }
                }
                CPU_AX = 0;
                CPU_FL_CF = 0;
                return true;
            }
            break;
        }
    }

    return false; // Not our interrupt, pass it on.
}

bool hostfs_int2fh() {
    // This handler manages the network redirector interface.
    if (CPU_AH == 0x11) { // Redirector functions
        switch (CPU_AX) {
            case 0x1100: // Installation Check
                CPU_AL = 0xFF; // Tell DOS a redirector is installed.
                return true;

            case 0x1122: // Get Assigned Drive Letter
                // DOS calls this in a loop to find out which drive letters are taken.
                // We are given the last assigned letter in BL.
                // If BL is 0, we are the first. We return our letter.
                // If BL is not 0, we do nothing and let the call pass to the next handler.
                // This is a simplified chain protocol.
                if (CPU_BL == 0) {
                    CPU_BL = host_drive_number; // Assign our drive number (0-based)
                    CPU_AL = 0x01; // Tell DOS this is a valid drive
                }
                // In a real scenario, we'd also set ES:DI to a device header here.
                // But for many simple programs, just assigning the letter is enough.
                return true;
        }
    }

    return false;
}

#endif // !PICO_ON_DEVICE
