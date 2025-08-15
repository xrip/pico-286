// For linux build use https://github.com/MathieuTurcotte/findfirst
// Forward declaration for the network redirector handler
#pragma once
// #define DEBUG_2F
#include <ctype.h>
#include <sys/stat.h>
#if WIN32
// Host filesystem passthrough base directory
#define HOST_BASE_DIR "C:\\FASM"
#else
// Host filesystem passthrough base directory
#define HOST_BASE_DIR "/tmp/"
#include "findfirst.h"
#endif
#if defined(DEBUG_2F)
#define debug_log(...) printf(__VA_ARGS__)
#else
#define debug_log(...)
#endif



// Maximum number of open files
#define MAX_FILES 32
FILE *open_files[MAX_FILES] = {0};

#ifdef WIN32
#define mkdir(path, mode) mkdir(path)
#endif

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

// Helper to get full path from guest path
static void get_full_path(char *dest, const char *guest_path) {
    if (guest_path[1] == ':') {
        // Absolute path with drive letter (e.g., "H:\file.txt" or "H:\TOOLS\file.txt")
        const char *path_part = guest_path + 2; // Skip "H:"
        if (path_part[0] == '\\') {
            path_part++; // Skip leading backslash, so "\TOOLS\file.txt" becomes "TOOLS\file.txt"
        }
        if (strlen(path_part) > 0) {
            sprintf(dest, HOST_BASE_DIR "\\%s", path_part);
        } else {
            sprintf(dest, HOST_BASE_DIR);
        }
    } else if (guest_path[0] == '\\') {
        // Root-relative path (e.g., "\subdir\file.txt")
        sprintf(dest, HOST_BASE_DIR "%s", guest_path);
    } else {
        // Relative path (e.g., "file.txt" or "subdir\file.txt")
        if (strlen(current_remote_dir) > 0) {
            sprintf(dest, HOST_BASE_DIR "\\%s\\%s", current_remote_dir, guest_path);
        } else {
            sprintf(dest, HOST_BASE_DIR "\\%s", guest_path);
        }
    }
#ifndef WIN32
    for (char *p = dest; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
#endif
    // printf("Path conversion: guest='%s', current_dir='%s' -> host='%s'\n", guest_path, current_remote_dir, dest);
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
    static sdbstruct *dta_ptr;
    static intptr_t handle = -1;

    switch (CPU_AX) {
        // Check if network redirector is installed
        case 0x1100:
            if (!sda_addr) {
                // Set swappable data address, cause in the emulator we don't have it
                sda_addr = ((uint32_t) CPU_BX << 4) + CPU_DX;
            }
            CPU_AL = 0xFF; // Indicate that the redirector is installed
            CPU_AH = 0x50; // Product identifier ('P' for Pico-286)
            break;

        // Remove Remote Directory
        case 0x1101: {
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);
            debug_log("Removing directory %s\n", path);

            const int result = rmdir(path); // TODO recursive remove
            if (result == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 5; // Access denied (typical for rmdir failure)
                CPU_FL_CF = 1;
            }
        }
        break;

        // Create Remote Directory
        case 0x1103: {
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);
            debug_log("Creating directory %s\n", path);
            const int result = mkdir(path, 0777);
            if (result == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 3; // Path isn't found (typical for mkdir failure)
                CPU_FL_CF = 1;
            }
        }
        break;

        // Change Remote Directory
        case 0x1105: {
            const char *dos_path = &RAM[sda_addr + FIRST_FILENAME_OFFSET];
            debug_log("Change directory to: '%s'\n", dos_path);

            // Handle different path formats
            if (dos_path[0] == '\\' && dos_path[1] == '\0') {
                // Root directory "\"
                strcpy(current_remote_dir, "");
            } else if (dos_path[0] == '\\') {
                // Absolute path from root, remove leading backslash
                strcpy(current_remote_dir, dos_path + 1);
            } else {
                // Relative path
                strcpy(current_remote_dir, dos_path);
            }

            debug_log("Current remote dir set to: '%s'\n", current_remote_dir);
            CPU_AX = 0;
            CPU_FL_CF = 0;
        }
        break;

        // Commit Remote File
        case 0x1107:
        // Close Remote File
        case 0x1106: {
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            const uint16_t file_handle = sftptr->file_handle;
            if (file_handle < MAX_FILES && open_files[file_handle]) {
                fclose(open_files[file_handle]);
                sftptr->total_handles = 0xffff;
                open_files[file_handle] = NULL;
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 6; // Invalid handle
                CPU_FL_CF = 1;
            }
        }
        break;


        // Read Remote File
        case 0x1108: {
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            const uint16_t file_handle = sftptr->file_handle; // We store our handle here
            if (file_handle < MAX_FILES && open_files[file_handle]) {
                uint16_t bytes_to_read = CPU_CX;
                debug_log("HANDLE COUNT %X %i (file_pos: %ld)\n", file_handle, bytes_to_read, sftptr->file_position);

                // Ensure file pointer is at the correct position
                if (fseek(open_files[file_handle], sftptr->file_position, SEEK_SET) != 0) {
                    debug_log("Seek error to position %ld\n", sftptr->file_position);
                    CPU_AX = 6; // Invalid handle or seek error
                    CPU_FL_CF = 1;
                    break;
                }

                const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *) &RAM[sda_addr + 12];
                size_t bytes_read = fread(&RAM[dta_addr], 1, bytes_to_read, open_files[file_handle]);
                debug_log("bytes read %i at offset %ld -> %x\n", (int) bytes_read, sftptr->file_position, dta_addr);

                // Update file position in SFT
                sftptr->file_position += bytes_read;
                CPU_AX = 0;
                CPU_CX = bytes_read;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 6; // Invalid handle
                CPU_FL_CF = 1;
            }
        }
        break;

        // Write Remote File
        case 0x1109: {
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            uint16_t file_handle = sftptr->file_handle; // We store our handle here

            if (file_handle < MAX_FILES && open_files[file_handle]) {
                uint16_t bytes_to_write = CPU_CX;
                debug_log("WRITE HANDLE %X %i (file_pos: %ld)\n", file_handle, bytes_to_write, sftptr->file_position);

                // Ensure file pointer is at the correct position
                if (fseek(open_files[file_handle], sftptr->file_position, SEEK_SET) != 0) {
                    debug_log("Write seek error to position %ld\n", sftptr->file_position);
                    CPU_AX = 6; // Invalid handle or seek error
                    CPU_FL_CF = 1;
                    break;
                }

                const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *) &RAM[sda_addr + 12];
                size_t bytes_written = fwrite(&RAM[dta_addr], 1, bytes_to_write, open_files[file_handle]);
                debug_log("bytes written %i at offset %ld\n", (int) bytes_written, sftptr->file_position);

                // Update file position in SFT and force write to disk
                sftptr->file_position += bytes_written;
                fflush(open_files[file_handle]); // Ensure data is written to disk
                CPU_CX = bytes_written; // RBIL6: return bytes written in CX, not AX
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 6; // Invalid handle
                CPU_FL_CF = 1;
            }
        }
        break;

        // Rename Remote File
        case 0x1111: {
            char old_path[256], new_path[256];

            // Get old filename from first filename buffer in SDA
            get_full_path(old_path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);

            // Get new filename from second filename buffer in SDA (offset 0x16A for DOS 4+)
            // For DOS 3.x it's at offset 0x15E, but we'll use DOS 4+ layout
            get_full_path(new_path, &RAM[sda_addr + 0x16A]);

            debug_log("Renaming '%s' to '%s'\n", old_path, new_path);

            int result = rename(old_path, new_path);
            if (result == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 5; // Access denied (typical for rename failure)
                CPU_FL_CF = 1;
            }
        }
        break;

        // Delete Remote File
        case 0x1113: {
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);
            int result = unlink(path);
            if (result == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 2; // File not found (typical for unlink failure)
                CPU_FL_CF = 1;
            }
        }
        break;

        // Open Existing File
        case 0x1116: {
            const char *dos_path = &RAM[sda_addr + FIRST_FILENAME_OFFSET];
            get_full_path(path, dos_path);
            debug_log("Opening %s %s\n", dos_path, path);

            const int8_t file_handle = get_free_handle();
            if (file_handle != -1) {
                open_files[file_handle] = fopen(path, "rb+");
                if (!open_files[file_handle]) {
                    // Try read-only mode if read-write fails
                    open_files[file_handle] = fopen(path, "rb");
                    debug_log("Tried rb+ failed, trying rb: %s\n", open_files[file_handle] ? "SUCCESS" : "FAILED");
                }
                if (open_files[file_handle]) {
                    // Get file size
                    fseek(open_files[file_handle], 0, SEEK_END);
                    const size_t file_size = ftell(open_files[file_handle]);
                    rewind(open_files[file_handle]);

                    sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];

                    // Extract just the filename from the path for SFT
                    const char *filename = strrchr(dos_path, '\\');
                    if (filename) {
                        filename++; // Skip the backslash
                    } else {
                        filename = dos_path; // No backslash found, use whole string
                    }

                    // Convert to DOS 8.3 format
                    to_dos_name(filename, sftptr->file_name);
                    sftptr->open_mode &= 0xff00;
                    sftptr->open_mode |= 0xff02;

                    sftptr->attribute = 0x8;
                    sftptr->device_info = 0x8040 | 'H';
                    sftptr->file_handle = file_handle; // Store our handle here
                    sftptr->file_size = file_size;
                    sftptr->file_time = 0x1000;
                    sftptr->file_date = 0x1000;
                    sftptr->file_position = 0;
                    sftptr->unk0 = 0;
                    sftptr->unk1 = 0xffFF;
                    sftptr->unk2 = 0xffff;
                    sftptr->unk3 = 0;
                    sftptr->unk4 = 0xff;

                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    debug_log("not found\n");
                    CPU_AX = 2; // File not found
                    CPU_FL_CF = 1;
                }
            } else {
                debug_log("too many open files\n");
                CPU_AX = 4; // Too many open files
                CPU_FL_CF = 1;
            }
        }
        break;

        // Create/Truncate File
        case 0x1117: {
            const int8_t file_handle = get_free_handle();
            if (file_handle != -1) {
                const char *dos_path = &RAM[sda_addr + FIRST_FILENAME_OFFSET];
                get_full_path(path, dos_path);

                open_files[file_handle] = fopen(path, "wb+");
                if (open_files[file_handle]) {
                    // Initialize SFT structure
                    sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];

                    // Extract just the filename from the path for SFT
                    const char *filename = strrchr(dos_path, '\\');
                    if (filename) {
                        filename++; // Skip the backslash
                    } else {
                        filename = dos_path; // No backslash found, use whole string
                    }

                    // Convert to DOS 8.3 format
                    to_dos_name(filename, sftptr->file_name);
                    sftptr->open_mode &= 0xff00;
                    sftptr->open_mode |= 0x0002; // Create/truncate file
                    sftptr->attribute = 0x08;
                    sftptr->device_info = 0x8040 | 'H';
                    sftptr->file_handle = file_handle; // Store our handle here
                    sftptr->file_size = 0; // New file
                    sftptr->file_time = 0x1000;
                    sftptr->file_date = 0x1000;
                    sftptr->file_position = 0;
                    sftptr->unk0 = 0;
                    sftptr->unk1 = 0xffFF;
                    sftptr->unk2 = 0xffff;
                    sftptr->unk3 = 0;
                    sftptr->unk4 = 0xff;

                    CPU_AX = 0;
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
        // Lock/Unlock File Region (stub implementation)
        case 0x110A:
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        // Get Disk Information (stub implementation)
        case 0x110C: {
            CPU_AH = 2;
            CPU_AL = 255;
            CPU_BX = 4096;
            CPU_CX = 4096;
            CPU_DX = 4096;
            CPU_FL_CF = 0;
        }
        break;

        // Set File Attributes (stub implementation)
        case 0x110e:
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        // Get File Attributes and Size
        case 0x110F: {
            // INT 2F AX=110Fh -
            // Input: AX=110Fh, SDA+9Eh → filename
            // Output: CF=0 if success with AX=attributes, BX:DI=file size, CX=time, DX=date
            //         CF=1 if error with AX=DOS error code
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);

            // Get file attributes
            struct stat file_info;
            if (stat(path, &file_info)) {
                CPU_AX = 2; // Not found
                CPU_FL_CF = 1;
            } else {
                // Convert POSIX file attributes to DOS attributes
                uint16_t dos_attributes = 0x20;

                // Check if file is read-only
                if (!(file_info.st_mode & S_IWUSR)) {
                    dos_attributes |= 0x01; // FILE_ATTRIBUTE_READONLY
                }

                // Check if it's a directory using S_ISDIR macro
                if (S_ISDIR(file_info.st_mode)) {
                    dos_attributes = 0x10; // FILE_ATTRIBUTE_DIRECTORY
                }

                CPU_AX = dos_attributes;
                CPU_BX = (file_info.st_size >> 16) & 0xFFFF; // High word
                CPU_DI = file_info.st_size & 0xFFFF; // Low word
                CPU_CX = 0x1000; // Default time stamp
                CPU_DX = 0x1000; // Default date stamp
                CPU_FL_CF = 0;
            }
        }
        break;

        // Find First File
        case 0x111B: {
            struct _finddata_t fileinfo;
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);
            debug_log("find first file: '%s'\n", path);


            if (handle) {
                _findclose(handle);
            }

            if ((handle = _findfirst(path, &fileinfo)) != -1) {
                // Set actual DTA pointer

                dta_ptr = (sdbstruct *) &RAM[(*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *) &RAM[sda_addr + 12]];
                dta_ptr->drive_letter = 'H' | 128; /* bit 7 set means 'network drive' (RBIL6 compliance) */

                to_dos_name(fileinfo.name, dta_ptr->foundfile.fname);
                dta_ptr->foundfile.fsize = fileinfo.size;
                dta_ptr->foundfile.fattr = fileinfo.attrib;

                // Other attributes can be set here if needed
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                debug_log("error finding file: '%s'\n", path);
                CPU_AX = 18; // No more files
                CPU_FL_CF = 1;
            }
        }
        break;

        // Find Next File
        case 0x111C: {
            // INT 2F AX=111Ch - Find Next File
            // Input: AX=111Ch, DTA contains search data from Find First
            // Output: CF=0 if file found with DTA updated, CF=1 if no more files with AX=18
            //         Must preserve bit 7 in DTA first byte (RBIL6 requirement)
            struct _finddata_t fileinfo;
            if (handle != -1 && _findnext(handle, &fileinfo) == 0) {
                dta_ptr->drive_letter |= 128; // Ensure bit 7 remains set (RBIL6 compliance)
                to_dos_name(fileinfo.name, dta_ptr->foundfile.fname);
                // Set file size
                dta_ptr->foundfile.fattr = fileinfo.attrib;
                dta_ptr->foundfile.fsize = fileinfo.size;
                dta_ptr->foundfile.start_clstr = 0;

                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                debug_log("no more files for: '%s'\n", path);
                CPU_AX = 18; // No more files
                CPU_FL_CF = 1;
            }
        }
        break;

        // Flush All Remote Disk Buffers
        case 0x1120:
            for (int i = 0; i < MAX_FILES; i++) {
                if (open_files[i]) {
                    fflush(open_files[i]);
                }
            }
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        // Seek from File End
        case 0x1121: {
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            const uint16_t file_handle = sftptr->file_handle;

            if (file_handle < MAX_FILES && open_files[file_handle]) {
                // CX:DX contains offset from end (signed 32-bit)
                int32_t offset_from_end = ((int32_t) CPU_CX << 16) | CPU_DX;

                debug_log("Seek from end: handle %d, offset %ld\n", file_handle, offset_from_end);

                // Get current file size
                if (fseek(open_files[file_handle], 0, SEEK_END) != 0) {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                    break;
                }

                long file_size = ftell(open_files[file_handle]);
                if (file_size == -1) {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                    break;
                }

                // Calculate new position: file_size + offset_from_end
                long new_position = file_size + offset_from_end;

                // Ensure new position is not negative
                if (new_position < 0) {
                    new_position = 0;
                }

                // Seek to new position
                if (fseek(open_files[file_handle], new_position, SEEK_SET) != 0) {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                    break;
                }

                // Update SFT position and return new position in DX:AX
                sftptr->file_position = new_position;
                CPU_DX = (new_position >> 16) & 0xFFFF; // High word
                CPU_AX = new_position & 0xFFFF; // Low word
                CPU_FL_CF = 0;

                debug_log("Seek result: new position %ld (DX:AX = %04X:%04X)\n",
                          new_position, CPU_DX, CPU_AX);
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
