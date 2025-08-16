// Forward declaration for the network redirector handler
#pragma once
// #define DEBUG_2F
#include <ctype.h>
#include "ff.h"
#include <stdlib.h>


#if defined(DEBUG_2F)
#define debug_log(...) printf(__VA_ARGS__)
#else
#define debug_log(...)
#endif

// Convert FatFS FRESULT to DOS error codes according to RBIL6, sets CPU_AX and CPU_FL_CF
static void fresult_to_dos_error(const FRESULT fr) {
    CPU_FL_CF = fr == FR_OK ? 0 : 1;
    switch (fr) {
        case FR_OK:                   CPU_AX = 0;   break; // Success
        case FR_NO_FILE:              CPU_AX = 2;   break; // File not found
        case FR_NO_PATH:              CPU_AX = 3;   break; // Path not found
        case FR_TOO_MANY_OPEN_FILES:  CPU_AX = 4;   break; // Too many open files
        case FR_DENIED:               // Access denied
        case FR_EXIST:                CPU_AX = 5;   break; // Access denied (file exists)
        case FR_INVALID_OBJECT:       CPU_AX = 6;   break; // Invalid handle
        case FR_WRITE_PROTECTED:      CPU_AX = 19;  break; // Write protect error
        case FR_INVALID_DRIVE:        CPU_AX = 15;  break; // Invalid drive
        case FR_NOT_READY:            CPU_AX = 21;  break; // Drive not ready
        case FR_DISK_ERR:              // General failure
        case FR_INT_ERR:              CPU_AX = 29;  break; // General failure
        case FR_INVALID_NAME:         CPU_AX = 3;   break; // Path not found (invalid name)
        case FR_NOT_ENABLED:          // Invalid drive (not enabled)
        case FR_NO_FILESYSTEM:        CPU_AX = 15;  break; // Invalid drive (no filesystem)
        case FR_MKFS_ABORTED:         CPU_AX = 29;  break; // General failure
        case FR_TIMEOUT:              CPU_AX = 32;  break; // Sharing violation
        case FR_LOCKED:               CPU_AX = 33;  break; // Lock violation
        case FR_NOT_ENOUGH_CORE:      CPU_AX = 8;   break; // Insufficient memory
        case FR_INVALID_PARAMETER:    CPU_AX = 87;  break; // Invalid parameter
        default:                      CPU_AX = 29;  break; // General failure
    }
}


// Host filesystem passthrough base directory
#define HOST_BASE_DIR "\\XT\\"

// Maximum number of open files
#define MAX_FILES 32
FIL* open_files[MAX_FILES] = {0};

// Current working directory for the remote drive (relative to HOST_BASE_DIR)
char current_remote_dir[256] = "";

// Helper function to get a free file handle
static inline int8_t get_free_handle() {
    for (int i = 0; i < MAX_FILES; i++) {
        if (open_files[i] == NULL) {
            return i;
        }
    }
    return -1; // No free handles
}
// TODO: Use pathbuffer as transfer buffer to economy memory
static uint8_t transfer_buffer[512];

static void read_string_from_ram(uint32_t address, char* buffer, int max_len) {
    for (int i = 0; i < max_len; i++) {
        buffer[i] = read86(address + i);
        if (buffer[i] == '\0') {
            break;
        }
    }
    buffer[max_len] = '\0'; // ensure null termination
}

static inline void read_block_from_ram(uint32_t address, uint8_t* buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buffer[i] = read86(address + i);
    }
}

static inline void write_block_to_ram(uint32_t address, const uint8_t* buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        write86(address + i, buffer[i]);
    }
}

// Helper to get full path from guest path
static void get_full_path(char *dest, const char *guest_path) {
    if (guest_path[1] == ':') {
        // Absolute path with drive letter (e.g., "H:\file.txt" or "H:\TOOLS\file.txt")
        const char *path_part = guest_path + 2; // Skip "H:"
        if (path_part[0] == '\\') {
            path_part++; // Skip leading backslash, so "\TOOLS\file.txt" becomes "TOOLS\file.txt"
        }
        if (strlen(path_part) > 0) {
            sprintf(dest, HOST_BASE_DIR "/%s", path_part);
        } else {
            sprintf(dest, HOST_BASE_DIR);
        }
    } else if (guest_path[0] == '\\') {
        // Root-relative path (e.g., "\subdir\file.txt")
        sprintf(dest, HOST_BASE_DIR "%s", guest_path);
    } else {
        // Relative path (e.g., "file.txt" or "subdir\file.txt")
        if (strlen(current_remote_dir) > 0) {
            sprintf(dest, HOST_BASE_DIR "/%s/%s", current_remote_dir, guest_path);
        } else {
            sprintf(dest, HOST_BASE_DIR "/%s", guest_path);
        }
    }
    // printf("Path conversion: guest='%s', current_dir='%s' -> host='%s'\n", guest_path, current_remote_dir, dest);
    for (char *p = dest; *p; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }
}

// Convert filename to DOS 8.3 format
static void to_dos_name(const char *input, char *output) {
    int i, j;
    memset(output, ' ', 11); // Fill with spaces

    // Handle special directory entries
    if (strcmp(input, ".") == 0) {
        output[0] = '.';
        return;
    }
    if (strcmp(input, "..") == 0) {
        output[0] = output[1] = '.';
        return;
    }

    // Copy name (up to 8 chars)
    for (i = 0, j = 0; input[i] && input[i] != '.' && j < 8; i++) {
        if (input[i] != ' ') output[j++] = toupper(input[i]);
    }

    // Find extension
    while (input[i] && input[i] != '.') i++;
    if (input[i] == '.') {
        i++;
        // Copy extension (up to 3 chars)
        for (j = 8; input[i] && j < 11; i++) {
            if (input[i] != ' ') output[j++] = toupper(input[i]);
        }
    }
}

typedef struct {
    unsigned char fname[11];
    unsigned char fattr; /* (1=RO 2=HID 4=SYS 8=VOL 16=DIR 32=ARCH 64=DEVICE) */
    unsigned char f1[10];
    unsigned short time_lstupd; /* 16 bits: hhhhhmmm mmmsssss */
    unsigned short date_lstupd; /* 16 bits: YYYYYYYM MMMDDDDD */
    unsigned short start_clstr; /* (optional) */
    unsigned long fsize;
} foundfilestruct;

/* called 'srchrec' in phantom.c */
typedef struct __attribute__((packed, aligned)) {
    unsigned char drive_letter;
    unsigned char srch_tmpl[11];
    unsigned char srch_attr;
    unsigned short dir_entry;
    unsigned short par_clstr;
    unsigned char f1[4];
    foundfilestruct foundfile;
} sdbstruct;

/* DOS System File Table entry - ALL DOS VERSIONS
 * Some of the fields below are defined by the redirector, and differ
 * from the SFT normally found under DOS */
typedef struct __attribute__((packed)) {
    // DOS 4.0+ System File Table and FCB Table
    uint16_t total_handles;
    uint16_t open_mode;
    uint8_t attribute;
    uint16_t device_info;
    uint32_t unk0;
    uint16_t file_handle; // We store our handle here. Originally it was cluster_no
    uint16_t file_time;
    uint16_t file_date;
    uint32_t file_size;
    uint32_t file_position;
    uint16_t unk1;
    uint16_t unk2;
    uint16_t unk3;
    uint8_t unk4;
    char file_name[11];
} sftstruct;

#define FIRST_FILENAME_OFFSET 0x9e

static inline bool redirector_handler() {
    char path[256];
    char guest_path[256];
    static char new_path[256];
    /*
 * Pointers to SDA fields. Layout:
 *                             DOS4+   DOS 3, DR-DOS
 * DTA ptr                      0Ch     0Ch
 * First filename buffer        9Eh     92h
 * Search data block (SDB)     19Eh    192h
 * Dir entry for found file    1B3h    1A7h
 * Search attributes           24Dh    23Ah
 * File access/sharing mode    24Eh    23Bh
 * Ptr to current CDS          282h    26Ch
 * Extended open mode          2E1h    Not supported
 */

    static uint32_t sda_addr = 0;

    static DIR find_handle;
    static FILINFO find_fileinfo;


    switch (CPU_AX) {
        case 0x1100: // Installation Check
            if (!sda_addr) {
                // Set swappable data address, cause in emulator we don't have it
                sda_addr = ((uint32_t) CPU_BX << 4) + CPU_DX;
            }
            CPU_AL = 0xFF; // Indicate that the redirector is installed
            break;

        case 0x1101: {
            // Remove Remote Directory
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            get_full_path(path, guest_path);
            debug_log("Removing directory %s\n", path);

            fresult_to_dos_error(f_unlink(path));
        }
        break;

        case 0x1103: {
            // Create Remote Directory
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            get_full_path(path, guest_path);
            debug_log("Creating directory %s\n", path);
            fresult_to_dos_error(f_mkdir(path));
        }
        break;

        case 0x1105: {
            // Change Directory
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            debug_log("Change directory to: '%s'\n", guest_path);

            // Handle different path formats
            if (guest_path[0] == '\\' && guest_path[1] == '\0') {
                // Root directory "\"
                strcpy(current_remote_dir, "");
            } else if (guest_path[0] == '\\') {
                // Absolute path from root, remove leading backslash
                strcpy(current_remote_dir, guest_path + 1);
            } else {
                // Relative path
                strcpy(current_remote_dir, guest_path);
            }

            debug_log("Current remote dir set to: '%s'\n", current_remote_dir);
            CPU_AX = 0;
            CPU_FL_CF = 0;
        }
        break;
        case 0x1107: // Commit Remote File
        case 0x1106: // Close Remote File
        {
            uint32_t sft_addr = ((uint32_t) CPU_ES << 4) + CPU_DI;
            uint16_t file_handle = readw86(sft_addr + offsetof(sftstruct, file_handle));
            if (file_handle < MAX_FILES && open_files[file_handle]) {
                f_close(open_files[file_handle]);
                free(open_files[file_handle]);
                writew86(sft_addr + offsetof(sftstruct, total_handles), 0xffff);
                open_files[file_handle] = NULL;
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
            uint32_t sft_addr = ((uint32_t) CPU_ES << 4) + CPU_DI;
            uint16_t file_handle = readw86(sft_addr + offsetof(sftstruct, file_handle));

            if (file_handle < MAX_FILES && open_files[file_handle]) {
                uint32_t file_pos = readdw86(sft_addr + offsetof(sftstruct, file_position));
                uint16_t bytes_to_read = CPU_CX;
                debug_log("HANDLE COUNT %X %i (file_pos: %ld)\n", file_handle, bytes_to_read, file_pos);

                FRESULT seek_result = f_lseek(open_files[file_handle], file_pos);
                if (seek_result != FR_OK) {
                    debug_log("Seek error to position %ld\n", file_pos);
                    fresult_to_dos_error(seek_result);
                    break;
                }

                const uint32_t dta_addr = ((uint32_t) readw86(sda_addr + 14) << 4) + readw86(sda_addr + 12);
                UINT total_bytes_read = 0;
                while(total_bytes_read < bytes_to_read) {
                    UINT bytes_read_now = 0;
                    UINT chunk_size = bytes_to_read - total_bytes_read > sizeof(transfer_buffer) ? sizeof(transfer_buffer) : bytes_to_read - total_bytes_read;
                    FRESULT res = f_read(open_files[file_handle], transfer_buffer, chunk_size, &bytes_read_now);
                    if(res != FR_OK || bytes_read_now == 0) break;
                    write_block_to_ram(dta_addr + total_bytes_read, transfer_buffer, bytes_read_now);
                    total_bytes_read += bytes_read_now;
                }

                debug_log("bytes read %i at offset %ld -> %x\n", (int) total_bytes_read, file_pos, dta_addr);

                writedw86(sft_addr + offsetof(sftstruct, file_position), file_pos + total_bytes_read);
                CPU_AX = 0;
                CPU_CX = total_bytes_read;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 6; // Invalid handle
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x1109: // Write Remote File
        {
            uint32_t sft_addr = ((uint32_t) CPU_ES << 4) + CPU_DI;
            uint16_t file_handle = readw86(sft_addr + offsetof(sftstruct, file_handle));

            if (file_handle < MAX_FILES && open_files[file_handle]) {
                uint32_t file_pos = readdw86(sft_addr + offsetof(sftstruct, file_position));
                uint16_t bytes_to_write = CPU_CX;
                debug_log("WRITE HANDLE %X %i (file_pos: %ld)\n", file_handle, bytes_to_write, file_pos);

                FRESULT seek_result = f_lseek(open_files[file_handle], file_pos);
                if (seek_result != FR_OK) {
                    debug_log("Write seek error to position %ld\n", file_pos);
                    fresult_to_dos_error(seek_result);
                    break;
                }

                const uint32_t dta_addr = (readw86(sda_addr + 14) << 4) + readw86(sda_addr + 12);
                UINT total_bytes_written = 0;
                while(total_bytes_written < bytes_to_write) {
                    UINT bytes_written_now = 0;
                    UINT chunk_size = bytes_to_write - total_bytes_written > sizeof(transfer_buffer) ? sizeof(transfer_buffer) : bytes_to_write - total_bytes_written;
                    read_block_from_ram(dta_addr + total_bytes_written, transfer_buffer, chunk_size);
                    FRESULT res = f_write(open_files[file_handle], transfer_buffer, chunk_size, &bytes_written_now);
                    if(res != FR_OK || bytes_written_now == 0) break;
                    total_bytes_written += bytes_written_now;
                }

                debug_log("bytes written %i at offset %ld\n", (int) total_bytes_written, file_pos);

                writedw86(sft_addr + offsetof(sftstruct, file_position), file_pos + total_bytes_written);
                f_sync(open_files[file_handle]);
                CPU_CX = total_bytes_written;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 6; // Invalid handle
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x1111: // Rename Remote File
        {
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            get_full_path(path, guest_path);

            read_string_from_ram(sda_addr + 0x16A, guest_path, 255);
            get_full_path(new_path, guest_path);

            debug_log("Renaming '%s' to '%s'\n", path, new_path);

            fresult_to_dos_error(f_rename(path, new_path));

        }
        break;

        case 0x1113: {
            // Delete Remote File
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            get_full_path(path, guest_path);
            fresult_to_dos_error(f_unlink(path));
        }
        break;

        case 0x1116: // Open Existing File
        {
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            get_full_path(path, guest_path);
            debug_log("Opening %s %s\n", guest_path, path);

            const int8_t file_handle = get_free_handle();
            if (file_handle != -1) {
                open_files[file_handle] = malloc(sizeof(FIL));
                if (!open_files[file_handle]) {
                    CPU_AX = 4; // Too many open files (or out of memory)
                    CPU_FL_CF = 1;
                    break;
                }
                FRESULT res = f_open(open_files[file_handle], path, FA_READ | FA_WRITE);
                if (res != FR_OK) {
                    res = f_open(open_files[file_handle], path, FA_READ);
                }
                if (res == FR_OK) {
                    sftstruct sft;
                    const char *filename = strrchr(guest_path, '\\');
                    if (filename) filename++; else filename = guest_path;

                    to_dos_name(filename, sft.file_name);
                    sft.open_mode = (readw86(((uint32_t) CPU_ES << 4) + CPU_DI + offsetof(sftstruct, open_mode)) & 0xff00) | 0xff02;
                    sft.attribute = 0x8;
                    sft.device_info = 0x8040 | 'H';
                    sft.file_handle = file_handle;
                    sft.file_size = f_size(open_files[file_handle]);
                    sft.file_time = 0x1000;
                    sft.file_date = 0x1000;
                    sft.file_position = 0;
                    sft.unk0 = 0;
                    sft.unk1 = 0xFFFF;
                    sft.unk2 = 0xFFFF;
                    sft.unk3 = 0;
                    sft.unk4 = 0xFF;

                    uint32_t sft_addr = ((uint32_t) CPU_ES << 4) + CPU_DI;
                    for(int i=0; i<sizeof(sft); i++) write86(sft_addr + i, ((uint8_t*)&sft)[i]);

                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    free(open_files[file_handle]);
                    open_files[file_handle] = NULL;
                    fresult_to_dos_error(res);
                }
            } else {
                CPU_AX = 4; // Too many open files
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x1117: // Create/Truncate File
        {
            const int8_t file_handle = get_free_handle();
            if (file_handle != -1) {
                read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
                get_full_path(path, guest_path);

                open_files[file_handle] = malloc(sizeof(FIL));
                if (!open_files[file_handle]) {
                    CPU_AX = 4; // Too many open files (or out of memory)
                    CPU_FL_CF = 1;
                    break;
                }

                FRESULT create_result = f_open(open_files[file_handle], path, FA_CREATE_ALWAYS | FA_WRITE);
                if (create_result == FR_OK) {
                    sftstruct sft;
                    const char *filename = strrchr(guest_path, '\\');
                    if (filename) filename++; else filename = guest_path;

                    to_dos_name(filename, sft.file_name);
                    sft.open_mode = (readw86(((uint32_t) CPU_ES << 4) + CPU_DI + offsetof(sftstruct, open_mode)) & 0xff00) | 0x0002;
                    sft.attribute = 0x08;
                    sft.device_info = 0x8040 | 'H';
                    sft.file_handle = file_handle;
                    sft.file_size = 0;
                    sft.file_time = 0x1000;
                    sft.file_date = 0x1000;
                    sft.file_position = 0;
                    sft.unk0 = 0;
                    sft.unk1 = 0xFFFF;
                    sft.unk2 = 0xFFFF;
                    sft.unk3 = 0;
                    sft.unk4 = 0xFF;

                    uint32_t sft_addr = ((uint32_t) CPU_ES << 4) + CPU_DI;
                    for(int i=0; i<sizeof(sft); i++) write86(sft_addr + i, ((uint8_t*)&sft)[i]);

                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    free(open_files[file_handle]);
                    open_files[file_handle] = NULL;
                    fresult_to_dos_error(create_result);
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

        case 0x110C: // TODO: Get Disk Information
        {
            CPU_AX = 512;
            CPU_BX = 512;
            CPU_CX = 512;
            CPU_DX = 512;
            CPU_FL_CF = 0;
        }
        break;

        case 0x110e: // TODO: Set File Attributes
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        case 0x110F: {
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            get_full_path(path, guest_path);

            FILINFO file_info;
            FRESULT result = f_stat(path, &file_info);
            if (result != FR_OK) {
                fresult_to_dos_error(result);
            } else {
                uint16_t dos_attributes = 0;
                if (file_info.fattrib & AM_RDO) dos_attributes |= 0x01;
                if (file_info.fattrib & AM_HID) dos_attributes |= 0x02;
                if (file_info.fattrib & AM_SYS) dos_attributes |= 0x04;
                if (file_info.fattrib & AM_DIR) dos_attributes |= 0x10;
                if (file_info.fattrib & AM_ARC) dos_attributes |= 0x20;

                CPU_AX = dos_attributes;
                CPU_BX = (file_info.fsize >> 16) & 0xFFFF; // High word
                CPU_DI = file_info.fsize & 0xFFFF; // Low word
                CPU_CX = file_info.ftime;
                CPU_DX = file_info.fdate;
                CPU_FL_CF = 0;
            }
        }
        break;

        // https://fd.lod.bz/rbil/interrup/network/2f111b.html#4376
        case 0x111B: // Find First File
        {
            read_string_from_ram(sda_addr + FIRST_FILENAME_OFFSET, guest_path, 255);
            get_full_path(path, guest_path);
            debug_log("find first file: '%s'\n", path);

            char* last_slash = strrchr(path, '/');

            if (last_slash) {
                strcpy(new_path, last_slash + 1);
                if (last_slash == path) { // root directory
                    *(last_slash + 1) = '\0';
                } else {
                    *last_slash = '\0';
                }
            } else {
                strcpy(new_path, path);
                strcpy(path, ".");
            }

            if (new_path[0] == '\0' || strcmp(new_path, "????????.???") == 0) {
                strcpy(new_path, "*");
            }

            FRESULT find_result = f_findfirst(&find_handle, &find_fileinfo, path, new_path);
            if (find_result == FR_OK && find_fileinfo.fname[0]) {
                uint32_t dta_addr = ((uint32_t) readw86(sda_addr + 14) << 4) + readw86(sda_addr + 12);
                sdbstruct sdb;
                read_block_from_ram(dta_addr, (uint8_t*)&sdb, sizeof(sdb));

                sdb.drive_letter = 'H' | 128; // bit 7 should be set
                to_dos_name(find_fileinfo.fname, sdb.foundfile.fname);
                sdb.foundfile.fsize = find_fileinfo.fsize;
                sdb.foundfile.fattr = find_fileinfo.fattrib;

                for(int i=0; i<sizeof(sdb); i++) write86(dta_addr + i, ((uint8_t*)&sdb)[i]);

                CPU_FL_CF = 0;
            } else {
                debug_log("no files found for '%s' in '%s': %i\n", new_path, path, find_result);

                if (FR_OK == find_result) {
                    CPU_AX = 18; // No more files
                    CPU_FL_CF = 1;
                }  else {
                    fresult_to_dos_error(find_result);
                }
            }
        }
        break;

        case 0x111C: // Find Next File
        {
            FRESULT find_result = f_findnext(&find_handle, &find_fileinfo);
            if (find_result == FR_OK && find_fileinfo.fname[0]) {
                uint32_t dta_addr = (readw86(sda_addr + 14) << 4) + readw86(sda_addr + 12);
                sdbstruct sdb;
                read_block_from_ram(dta_addr, (uint8_t*)&sdb, sizeof(sdb));

                to_dos_name(find_fileinfo.fname, sdb.foundfile.fname);
                sdb.foundfile.fattr = find_fileinfo.fattrib;
                sdb.foundfile.fsize = find_fileinfo.fsize;
                sdb.foundfile.start_clstr = 0;

                for(int i=0; i<sizeof(sdb); i++) write86(dta_addr + i, ((uint8_t*)&sdb)[i]);

                CPU_FL_CF = 0;
            } else {
                debug_log("no more files found for '%s' in '%s': %i\n", path, find_result);

                if (FR_OK == find_result) {
                    CPU_AX = 18; // No more files
                    CPU_FL_CF = 1;
                }  else {
                    fresult_to_dos_error(find_result);
                }
            }
        }
        break;

        case 0x1120: // Flush All Disk Buffers
            for (int i = 0; i < MAX_FILES; i++) {
                if (open_files[i]) {
                    f_sync(open_files[i]);
                }
            }
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        case 0x1121: // Seek from File End
        {
            uint32_t sft_addr = ((uint32_t) CPU_ES << 4) + CPU_DI;
            uint16_t file_handle = readw86(sft_addr + offsetof(sftstruct, file_handle));

            if (file_handle < MAX_FILES && open_files[file_handle]) {
                int32_t offset_from_end = ((int32_t)CPU_CX << 16) | CPU_DX;
                debug_log("Seek from end: handle %d, offset %ld\n", file_handle, offset_from_end);

                long file_size = f_size(open_files[file_handle]);
                long new_position = file_size + offset_from_end;

                if (new_position < 0) new_position = 0;

                FRESULT seek_result = f_lseek(open_files[file_handle], new_position);
                if (seek_result != FR_OK) {
                    fresult_to_dos_error(seek_result);
                    break;
                }

                writedw86(sft_addr + offsetof(sftstruct, file_position), new_position);
                CPU_DX = (new_position >> 16) & 0xFFFF;
                CPU_AX = new_position & 0xFFFF;
                CPU_FL_CF = 0;

                debug_log("Seek result: new position %ld (DX:AX = %04X:%04X)\n", new_position, CPU_DX, CPU_AX);
            } else {
                CPU_AX = 6; // Invalid handle
                CPU_FL_CF = 1;
            }
        }
        break;

        default:
            if (CPU_AH == 0x11 && CPU_AL != 0x23)
                debug_log("UNIMPLEMENTED Redirector handler 0x%04x\n", CPU_AX);
            return false;
    }
    return true;
}
