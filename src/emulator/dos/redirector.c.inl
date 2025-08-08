#include <stdio.h>
#include <string.h>
#include <windows.h>

// Forward declaration for the network redirector handler
void network_redirector_handler();

// Define a structure to hold file information
typedef struct {
    FILE* fp;
    char path[256];
} DosFile;

// Maximum number of open files
#define MAX_FILES 32
DosFile open_files[MAX_FILES];

// Current working directory for the remote drive
char current_remote_dir[256] = "C:\\";

// Helper function to get a free file handle
int get_free_handle() {
    for (int i = 0; i < MAX_FILES; i++) {
        if (open_files[i].fp == NULL) {
            return i;
        }
    }
    return -1; // No free handles
}

// Helper to get full path from guest path
void get_full_path(char* dest, const char* guest_path) {
    if (guest_path[1] == ':') {
        sprintf(dest, "host/%s", guest_path + 2);
    } else {
        sprintf(dest, "host/%s/%s", current_remote_dir, guest_path);
    }
}

void mem_read_bytes(uint16_t seg, uint16_t off, void* dest, uint16_t len) {
    uint8_t* d = (uint8_t*)dest;
    uint32_t addr = ((uint32_t)seg << 4) + off;
    for (int i = 0; i < len; i++) {
        d[i] = read86(addr + i);
    }
}

void mem_write_bytes(uint16_t seg, uint16_t off, const void* src, uint16_t len) {
    const uint8_t* s = (const uint8_t*)src;
    uint32_t addr = ((uint32_t)seg << 4) + off;
    for (int i = 0; i < len; i++) {
        write86(addr + i, s[i]);
    }
}

// Network Redirector Handler
void network_redirector_handler() {
    char path[256];
    char dos_path[128];

    switch (CPU_AX) {
        case 0x1100: // Installation Check
            CPU_AL = 0xFF; // Indicate that the redirector is installed
            break;

        case 0x1101: // Remove Remote Directory
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            get_full_path(path, dos_path);
            if (rmdir(path) == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 3; // Path not found
                CPU_FL_CF = 1;
            }
            break;

        case 0x1103: // Create Remote Directory
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            get_full_path(path, dos_path);
            if (mkdir(path) == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 5; // Access denied
                CPU_FL_CF = 1;
            }
            break;

        case 0x1105: // Change Directory
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            strcpy(current_remote_dir, dos_path);
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        case 0x1106: // Close Remote File
            {
                int handle = CPU_BX;
                if (handle < MAX_FILES && open_files[handle].fp) {
                    fclose(open_files[handle].fp);
                    open_files[handle].fp = NULL;
                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1107: // Commit Remote File
            {
                int handle = CPU_BX;
                if (handle < MAX_FILES && open_files[handle].fp) {
                    fflush(open_files[handle].fp);
                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1108: // Read Remote File
            {
                int handle = CPU_BX;
                uint16_t count = CPU_CX;
                if (handle < MAX_FILES && open_files[handle].fp) {
                    char* buffer = (char*)malloc(count);
                    size_t bytes_read = fread(buffer, 1, count, open_files[handle].fp);
                    mem_write_bytes(CPU_DS, CPU_DX, buffer, bytes_read);
                    free(buffer);
                    CPU_AX = bytes_read;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1109: // Write Remote File
            {
                int handle = CPU_BX;
                uint16_t count = CPU_CX;
                if (handle < MAX_FILES && open_files[handle].fp) {
                    char* buffer = (char*)malloc(count);
                    mem_read_bytes(CPU_DS, CPU_DX, buffer, count);
                    size_t bytes_written = fwrite(buffer, 1, count, open_files[handle].fp);
                    free(buffer);
                    CPU_AX = bytes_written;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1110: // Rename Remote File
            {
                char old_path_dos[128], new_path_dos[128];
                char old_path_host[256], new_path_host[256];
                mem_read_bytes(CPU_DS, CPU_DX, old_path_dos, sizeof(old_path_dos));
                mem_read_bytes(CPU_ES, CPU_DI, new_path_dos, sizeof(new_path_dos));
                get_full_path(old_path_host, old_path_dos);
                get_full_path(new_path_host, new_path_dos);
                if (rename(old_path_host, new_path_host) == 0) {
                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 2; // File not found
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1112: // Delete Remote File
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            get_full_path(path, dos_path);
            if (remove(path) == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 2; // File not found
                CPU_FL_CF = 1;
            }
            break;

        case 0x1115: // Open Existing File
            {
                mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
                get_full_path(path, dos_path);
                int handle = get_free_handle();
                if (handle != -1) {
                    open_files[handle].fp = fopen(path, "rb+");
                    if (open_files[handle].fp) {
                        strcpy(open_files[handle].path, path);
                        CPU_AX = handle;
                        CPU_FL_CF = 0;
                    } else {
                        CPU_AX = 2; // File not found
                        CPU_FL_CF = 1;
                    }
                } else {
                    CPU_AX = 4; // Too many open files
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1117: // Create/Truncate File
            {
                mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
                get_full_path(path, dos_path);
                int handle = get_free_handle();
                if (handle != -1) {
                    open_files[handle].fp = fopen(path, "wb+");
                    if (open_files[handle].fp) {
                        strcpy(open_files[handle].path, path);
                        CPU_AX = handle;
                        CPU_FL_CF = 0;
                    } else {
                        CPU_AX = 3; // Path not found
                        CPU_FL_CF = 1;
                    }
                } else {
                    CPU_AX = 4; // Too many open files
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x110A: // Lock/Unlock Region
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        case 0x110C: // Get Disk Information
            {
                ULARGE_INTEGER free_bytes_available, total_number_of_bytes, total_number_of_free_bytes;
                if (GetDiskFreeSpaceEx("C:\\", &free_bytes_available, &total_number_of_bytes, &total_number_of_free_bytes)) {
                    CPU_AX = 512; // bytes per sector
                    CPU_BX = total_number_of_free_bytes.QuadPart / 512; // free sectors
                    CPU_CX = 1; // sectors per cluster
                    CPU_DX = total_number_of_bytes.QuadPart / 512; // total sectors
                } else {
                    CPU_AX = 1;
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x110D: // Set File Attributes
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        case 0x1118: // Find First File (using 111A alias)
        case 0x111A: // Find First File
            {
                static HANDLE find_handle = INVALID_HANDLE_VALUE;
                WIN32_FIND_DATA find_data;
                char search_path[256];

                mem_read_bytes(CPU_DS, CPU_DX, dos_path, 128);
                get_full_path(search_path, dos_path);

                if (find_handle != INVALID_HANDLE_VALUE) {
                    FindClose(find_handle);
                }

                find_handle = FindFirstFile(search_path, &find_data);

                if (find_handle != INVALID_HANDLE_VALUE) {
                    mem_write_bytes(CPU_ES, CPU_BX + 0x15, find_data.cFileName, strlen(find_data.cFileName) + 1);
                    // Other attributes can be set here if needed
                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 18; // No more files
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x111B: // Find Next File
            {
                static HANDLE find_handle = INVALID_HANDLE_VALUE;
                WIN32_FIND_DATA find_data;

                if (find_handle != INVALID_HANDLE_VALUE && FindNextFile(find_handle, &find_data)) {
                    mem_write_bytes(CPU_ES, CPU_BX + 0x15, find_data.cFileName, strlen(find_data.cFileName) + 1);
                    // Other attributes can be set here if needed
                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 18; // No more files
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1120: // Flush All Disk Buffers
            for (int i = 0; i < MAX_FILES; i++) {
                if (open_files[i].fp) {
                    fflush(open_files[i].fp);
                }
            }
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        default:
            // Unhandled subfunction
            CPU_AX = 1; // Set error code
            CPU_FL_CF = 1; // Set carry flag to indicate error
            break;
    }
}
