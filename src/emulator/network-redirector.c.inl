// Forward declaration for the network redirector handler
#pragma once
#define DEBUG_2F
#if defined(DEBUG_2F)
#define debug_log(...) printf(__VA_ARGS__)
#else
#define debug_log(...)
#endif
#define INVALID_HANDLE_VALUE ((HANDLE) -1)

// Maximum number of open files
#define MAX_FILES 32
FILE *open_files[MAX_FILES] = {0};

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
void get_full_path(char *dest, const char *guest_path) {
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
    // printf("Path conversion: guest='%s', current_dir='%s' -> host='%s'\n", guest_path, current_remote_dir, dest);
}

// Convert filename to DOS 8.3 format
static void to_dos_name(const char *input, char *output) {
    int i, j;
    memset(output, ' ', 11); // Fill with spaces

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
    unsigned char drv_lett;
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
    uint16_t file_handle;  // We store our handle here. Originally it was cluster_no
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


// Network Redirector Handler
static inline bool redirector_handler() {
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

    char path[256];
    static HANDLE find_handle = INVALID_HANDLE_VALUE;

    static sdbstruct *dta_ptr;

    if (CPU_AH == 0x11 && CPU_AL != 0x23)
        debug_log("Redirector handler 0x%04x\n", CPU_AX);

    switch (CPU_AX) {
        case 0x1100: // Installation Check
            if (!sda_addr) { // Set swappable data address, cause in emulator we don't have it
                sda_addr = ((uint32_t) CPU_BX << 4) + CPU_DX;
            }
            CPU_AL = 0xFF; // Indicate that the redirector is installed
            break;

        case 0x1101: {
            // Remove Remote Directory
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);
            if (RemoveDirectoryA(path)) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                DWORD error = GetLastError();
                if (error == ERROR_PATH_NOT_FOUND || error == ERROR_FILE_NOT_FOUND) {
                    CPU_AX = 3; // Path not found
                } else if (error == ERROR_ACCESS_DENIED || error == ERROR_DIR_NOT_EMPTY) {
                    CPU_AX = 5; // Access denied
                } else {
                    CPU_AX = 3; // Default to path not found
                }
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x1103: {
            // Create Remote Directory
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);
            if (CreateDirectoryA(path, NULL)) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                DWORD error = GetLastError();
                if (error == ERROR_PATH_NOT_FOUND) {
                    CPU_AX = 3; // Path not found
                } else if (error == ERROR_ALREADY_EXISTS) {
                    CPU_AX = 5; // Access denied (directory exists)
                } else if (error == ERROR_ACCESS_DENIED) {
                    CPU_AX = 5; // Access denied
                } else {
                    CPU_AX = 5; // Default to access denied
                }
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x1105: {
            // Change Directory
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

        case 0x1106: // Close Remote File
        {
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            int8_t handle =  sftptr->file_handle;
            if (handle < MAX_FILES && open_files[handle]) {
                fclose(open_files[handle]);
                open_files[handle] = NULL;
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
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            int8_t handle =  sftptr->file_handle; // We store our handle here
            if (handle < MAX_FILES && open_files[handle]) {
                fflush(open_files[handle]);
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
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            int8_t handle =  sftptr->file_handle; // We store our handle here
            if (handle < MAX_FILES && open_files[handle]) {
                uint16_t bytes_to_read = CPU_CX;
                debug_log("HANDLE COUNT %X %i (file_pos: %ld)\n", handle, bytes_to_read, sftptr->file_position);

                // Ensure file pointer is at the correct position
                if (fseek(open_files[handle], sftptr->file_position, SEEK_SET) != 0) {
                    debug_log("Seek error to position %ld\n", sftptr->file_position);
                    CPU_AX = 6; // Invalid handle or seek error
                    CPU_FL_CF = 1;
                    break;
                }

                const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *) &RAM[sda_addr + 12];
                size_t bytes_read = fread(&RAM[dta_addr], 1, bytes_to_read, open_files[handle]);
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

        case 0x1109: // Write Remote File
        {
            sftstruct *sftptr = (sftstruct *) &RAM[((uint32_t) CPU_ES << 4) + CPU_DI];
            int8_t handle =  sftptr->file_handle; // We store our handle here

            if (handle < MAX_FILES && open_files[handle]) {
                uint16_t bytes_to_write = CPU_CX;
                debug_log("WRITE HANDLE %X %i (file_pos: %ld)\n", handle, bytes_to_write, sftptr->file_position);

                // Ensure file pointer is at the correct position
                if (fseek(open_files[handle], sftptr->file_position, SEEK_SET) != 0) {
                    debug_log("Write seek error to position %ld\n", sftptr->file_position);
                    CPU_AX = 6; // Invalid handle or seek error
                    CPU_FL_CF = 1;
                    break;
                }

                const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *) &RAM[sda_addr + 12];
                size_t bytes_written = fwrite(&RAM[dta_addr], 1, bytes_to_write, open_files[handle]);
                debug_log("bytes written %i at offset %ld\n", (int) bytes_written, sftptr->file_position);

                // Update file position in SFT and force write to disk
                sftptr->file_position += bytes_written;
                fflush(open_files[handle]); // Ensure data is written to disk
                CPU_AX = bytes_written;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 6; // Invalid handle
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x1111: // TODO: Rename Remote File
            CPU_FL_CF = 1;
            break;

        case 0x1113: {
            // Delete Remote File
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);
            if (DeleteFileA(path)) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                    CPU_AX = 2; // File not found
                } else if (error == ERROR_ACCESS_DENIED) {
                    CPU_AX = 5; // Access denied (read-only, in use, etc.)
                } else {
                    CPU_AX = 2; // Default to file not found
                }
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x1116: // Open Existing File
        {
            const char *dos_path = &RAM[sda_addr + FIRST_FILENAME_OFFSET];
            get_full_path(path, dos_path);
            debug_log("Opening %s %s\n", dos_path, path);

            int8_t handle =  get_free_handle();
            if (handle != -1) {
                open_files[handle] = fopen(path, "rb+");
                if (!open_files[handle]) {
                    // Try read-only mode if read-write fails
                    open_files[handle] = fopen(path, "rb");
                    debug_log("Tried rb+ failed, trying rb: %s\n", open_files[handle] ? "SUCCESS" : "FAILED");
                }
                if (open_files[handle]) {
                    // Get file size
                    fseek(open_files[handle], 0, SEEK_END);
                    long file_size = ftell(open_files[handle]);
                    rewind(open_files[handle]);

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
                    sftptr->file_handle = handle; // Store our handle here
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

        case 0x1117: // Create/Truncate File
        {
            int8_t handle =  get_free_handle();
            if (handle != -1) {
                const char *dos_path = &RAM[sda_addr + FIRST_FILENAME_OFFSET];
                get_full_path(path, dos_path);

                open_files[handle] = fopen(path, "wb+");
                if (open_files[handle]) {

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
                    sftptr->file_handle = handle; // Store our handle here
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

        case 0x110A: // Lock/Unlock Region
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        case 0x110C: // Get Disk Information
        {
            ULARGE_INTEGER free_bytes_available, total_number_of_bytes, total_number_of_free_bytes;
            char drive_path[4];
            sprintf(drive_path, "%c:\\", HOST_BASE_DIR[0]); // Extract drive letter from HOST_BASE_DIR

            if (GetDiskFreeSpaceExA(drive_path, &free_bytes_available, &total_number_of_bytes, &total_number_of_free_bytes)) {
                // Convert to DOS format (sectors per cluster, bytes per sector, etc.)
                const DWORD bytes_per_sector = 512;
                const DWORD sectors_per_cluster = 1;

                DWORD total_clusters = (DWORD) (total_number_of_bytes.QuadPart / (bytes_per_sector * sectors_per_cluster));
                DWORD free_clusters = (DWORD) (free_bytes_available.QuadPart / (bytes_per_sector * sectors_per_cluster));

                // Limit values to 16-bit ranges for DOS compatibility
                if (total_clusters > 65535) total_clusters = 65535;
                if (free_clusters > 65535) free_clusters = 65535;

                CPU_AX = sectors_per_cluster;
                CPU_BX = total_clusters;
                CPU_CX = bytes_per_sector;
                CPU_DX = free_clusters;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 1; // Error
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x110e: // Set File Attributes
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);

            // Convert DOS attributes to Windows attributes
            DWORD attributes = 0;
            if (CPU_CX & 0x01) attributes |= FILE_ATTRIBUTE_READONLY;
            if (CPU_CX & 0x02) attributes |= FILE_ATTRIBUTE_HIDDEN;
            if (CPU_CX & 0x04) attributes |= FILE_ATTRIBUTE_SYSTEM;
            if (CPU_CX & 0x20) attributes |= FILE_ATTRIBUTE_ARCHIVE;

            if (attributes == 0) attributes = FILE_ATTRIBUTE_NORMAL;

            if (SetFileAttributesA(path, attributes)) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                    CPU_AX = 2; // File not found
                } else {
                    CPU_AX = 5; // Access denied
                }
                CPU_FL_CF = 1;
            }
            break;

        case 0x110F: // Get Remote File's Attributes and Size
        {
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);

            // Get file attributes
            DWORD win_attributes = GetFileAttributesA(path);
            if (win_attributes == INVALID_FILE_ATTRIBUTES) {
                DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                    CPU_AX = 2; // File not found
                    debug_log("not found\n");
                } else {
                    CPU_AX = 5; // Access denied
                }
                CPU_FL_CF = 1;
            } else {
                // Convert Windows attributes to DOS attributes
                uint16_t dos_attributes = 0;
                if (win_attributes & FILE_ATTRIBUTE_READONLY) dos_attributes |= 0x01;
                if (win_attributes & FILE_ATTRIBUTE_HIDDEN) dos_attributes |= 0x02;
                if (win_attributes & FILE_ATTRIBUTE_SYSTEM) dos_attributes |= 0x04;
                if (win_attributes & FILE_ATTRIBUTE_DIRECTORY) dos_attributes |= 0x10; // Directory
                if (win_attributes & FILE_ATTRIBUTE_ARCHIVE) dos_attributes |= 0x20;

                // For now, use a simpler approach to get file size
                FILE *fp = fopen(path, "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END);
                    uint32_t file_size = (uint32_t) ftell(fp);
                    fclose(fp);

                    CPU_AX = dos_attributes;
                    CPU_BX = (file_size >> 16) & 0xFFFF; // High word
                    CPU_DI = file_size & 0xFFFF; // Low word
                    CPU_CX = 0x1000; // Default time stamp
                    CPU_DX = 0x1000; // Default date stamp
                    CPU_FL_CF = 0;
                } else {
                    debug_log("not found\n");
                    CPU_AX = 2;
                    CPU_FL_CF = 1;
                }
            }
        }
        break;
        // https://fd.lod.bz/rbil/interrup/network/2f111b.html#4376
        case 0x111B: // Find First File
        {
            WIN32_FIND_DATA find_data;
            get_full_path(path, &RAM[sda_addr + FIRST_FILENAME_OFFSET]);

            if (find_handle != INVALID_HANDLE_VALUE) {
                FindClose(find_handle);
            }

            find_handle = FindFirstFile(path, &find_data);


            if (find_handle != INVALID_HANDLE_VALUE) {
                // Set actual DTA pointer
                dta_ptr = (sdbstruct *) &RAM[(*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *) &RAM[sda_addr + 12]];

                dta_ptr->drv_lett = 'H' | 128; /* bit 7 set means 'network drive' */
                to_dos_name(find_data.cFileName, dta_ptr->foundfile.fname);
                dta_ptr->foundfile.fattr = 0x8;

                // Other attributes can be set here if needed
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else {
                CPU_AX = 18; // No more files
                CPU_FL_CF = 1;
            }
        }
        break;

        case 0x111C: // Find Next File
        {
            WIN32_FIND_DATA find_data;

            if (find_handle != INVALID_HANDLE_VALUE && FindNextFile(find_handle, &find_data)) {
                to_dos_name(find_data.cFileName, dta_ptr->foundfile.fname);

                // Convert attributes
                dta_ptr->foundfile.fattr = 0;
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) dta_ptr->foundfile.fattr |= 0x01;
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) dta_ptr->foundfile.fattr |= 0x02;
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) dta_ptr->foundfile.fattr |= 0x04;
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dta_ptr->foundfile.fattr |= 0x10;
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) dta_ptr->foundfile.fattr |= 0x20;

                // Set file size
                dta_ptr->foundfile.fsize = find_data.nFileSizeLow;
                dta_ptr->foundfile.start_clstr = 0;

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
                if (open_files[i]) {
                    fflush(open_files[i]);
                }
            }
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        default:
            return false;
    }
    return true;
}
