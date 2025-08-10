// Forward declaration for the network redirector handler

#define INVALID_HANDLE_VALUE ((HANDLE) (LONG_PTR)-1)
// Define a structure to hold file information
typedef struct {
    FILE* fp;
    char path[256];
} DosFile;

// Maximum number of open files
#define MAX_FILES 32
DosFile open_files[MAX_FILES] = {0};

// Current working directory for the remote drive (relative to HOST_BASE_DIR)
char current_remote_dir[256] = "";

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
        // Absolute path with drive letter (e.g., "H:\file.txt" or "H:\TOOLS\file.txt")
        const char* path_part = guest_path + 2; // Skip "H:"
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

typedef struct  {
    unsigned char fname[11];
    unsigned char fattr; /* (1=RO 2=HID 4=SYS 8=VOL 16=DIR 32=ARCH 64=DEVICE) */
    unsigned char f1[10];
    unsigned short time_lstupd; /* 16 bits: hhhhhmmm mmmsssss */
    unsigned short date_lstupd; /* 16 bits: YYYYYYYM MMMDDDDD */
    unsigned short start_clstr; /* (optional) */
    unsigned long fsize;
}  foundfilestruct;
/* called 'srchrec' in phantom.c */
typedef struct  __attribute__((packed, aligned))  {
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
typedef struct __attribute__((packed)) { // DOS 4.0+ System File Table and FCB Table
    uint16_t handels;
    uint16_t open_mode;
    uint8_t attribute;
    uint16_t device_info;
    uint32_t ptr;
    uint16_t starting_cluster;
    uint16_t file_time;
    uint16_t file_date;
    uint32_t file_size;
    uint32_t file_position;
    uint16_t unk1;
    uint16_t unk2;
    uint16_t unk3;
    uint8_t unk4;
    char file_name[11];
} sftstruct ;

// Network Redirector Handler
bool redirector_handler() {
    char path[256];
    char dos_path[128];
    static uint16_t sda_seg = 0, sda_off = 0;
    static uint16_t dta_seg = 0, dta_off = 0;
    static HANDLE find_handle = INVALID_HANDLE_VALUE;

    static sdbstruct *dta_ptr;

    if (CPU_AH == 0x11 && CPU_AL != 0x23)
        printf("Redirector handler 0x%04x\n", CPU_AX);

    switch (CPU_AX) {
        case 0x1100: // Installation Check
            if (!sda_seg) {
                sda_seg = CPU_BX;
                sda_off = CPU_DX;
            }
            printf("SDA seg %x offs %x\n", sda_seg, sda_off);
            CPU_AL = 0xFF; // Indicate that the redirector is installed
            break;

        case 0x1101: {
            // Remove Remote Directory
            const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
            strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
            get_full_path(path, dos_path);
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
            const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
            strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
            get_full_path(path, dos_path);
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

        case 0x1105: // Change Directory
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            // printf("Change directory to: '%s'\n", dos_path);
            
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
            
            // printf("Current remote dir set to: '%s'\n", current_remote_dir);
            CPU_AX = 0;
            CPU_FL_CF = 0;
            break;

        case 0x1106: // Close Remote File
            {
                sftstruct *sftptr = (sftstruct*)&RAM[((uint32_t)CPU_ES << 4) + CPU_DI];
                int handle = sftptr->starting_cluster; // We store our handle here
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
                sftstruct *sftptr = (sftstruct*)&RAM[((uint32_t)CPU_ES << 4) + CPU_DI];
                int handle = sftptr->starting_cluster; // We store our handle here
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
                sftstruct *sftptr = (sftstruct*)&RAM[((uint32_t)CPU_ES << 4) + CPU_DI];
                int handle = sftptr->starting_cluster; // We store our handle here
                uint16_t bytes_to_read = CPU_CX;
                printf("HANDLE COUNT %X %i (file_pos: %ld)\n", handle, bytes_to_read, sftptr->file_position);
                if (handle < MAX_FILES && open_files[handle].fp) {
                    // Ensure file pointer is at the correct position
                    if (fseek(open_files[handle].fp, sftptr->file_position, SEEK_SET) != 0) {
                        printf("Seek error to position %ld\n", sftptr->file_position);
                        CPU_AX = 6; // Invalid handle or seek error
                        CPU_FL_CF = 1;
                        break;
                    }
                    
                    const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                    const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *)&RAM[sda_addr + 12];
                    size_t bytes_read = fread(&RAM[dta_addr], 1, bytes_to_read, open_files[handle].fp);
                    printf("bytes read %i at offset %ld -> %x\n", (int)bytes_read, sftptr->file_position, dta_addr);
                    
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
                sftstruct *sftptr = (sftstruct*)&RAM[((uint32_t)CPU_ES << 4) + CPU_DI];
                int handle = sftptr->starting_cluster; // We store our handle here
                uint16_t count = CPU_CX;
                printf("WRITE HANDLE %X %i (file_pos: %ld)\n", handle, count, sftptr->file_position);
                if (handle < MAX_FILES && open_files[handle].fp) {
                    // Ensure file pointer is at the correct position
                    if (fseek(open_files[handle].fp, sftptr->file_position, SEEK_SET) != 0) {
                        printf("Write seek error to position %ld\n", sftptr->file_position);
                        CPU_AX = 6; // Invalid handle or seek error
                        CPU_FL_CF = 1;
                        break;
                    }
                    
                    const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                    const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *)&RAM[sda_addr + 12];
                    size_t bytes_written = fwrite(&RAM[dta_addr], 1, count, open_files[handle].fp);
                    printf("bytes written %i at offset %ld\n", (int)bytes_written, sftptr->file_position);
                    
                    // Update file position in SFT and force write to disk
                    sftptr->file_position += bytes_written;
                    fflush(open_files[handle].fp); // Ensure data is written to disk
                    CPU_AX = bytes_written;
                    CPU_FL_CF = 0;
                } else {
                    CPU_AX = 6; // Invalid handle
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1111: // Rename Remote File
            {
                char old_path_dos[128], new_path_dos[128];
                char old_path_host[256], new_path_host[256];
                mem_read_bytes(CPU_DS, CPU_DX, old_path_dos, sizeof(old_path_dos));
                mem_read_bytes(CPU_ES, CPU_DI, new_path_dos, sizeof(new_path_dos));
                get_full_path(old_path_host, old_path_dos);
                get_full_path(new_path_host, new_path_dos);
                if (MoveFileA(old_path_host, new_path_host)) {
                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else {
                    DWORD error = GetLastError();
                    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                        CPU_AX = 2; // File not found
                    } else if (error == ERROR_ACCESS_DENIED || error == ERROR_ALREADY_EXISTS) {
                        CPU_AX = 5; // Access denied
                    } else {
                        CPU_AX = 2; // Default to file not found
                    }
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1113: {
            // Delete Remote File
            const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
            strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
            get_full_path(path, dos_path);
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
                const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
                get_full_path(path, dos_path);
                printf("Opening %s %s\n", dos_path, path);
                
                int handle = get_free_handle();
                if (handle != -1) {
                    open_files[handle].fp = fopen(path, "rb+");
                    if (!open_files[handle].fp) {
                        // Try read-only mode if read-write fails
                        open_files[handle].fp = fopen(path, "rb");
                        printf("Tried rb+ failed, trying rb: %s\n", open_files[handle].fp ? "SUCCESS" : "FAILED");
                    }
                    if (open_files[handle].fp) {
                        strcpy(open_files[handle].path, path);

                        // Get file size
                        fseek(open_files[handle].fp, 0, SEEK_END);
                        long file_size = ftell(open_files[handle].fp);
                        rewind(open_files[handle].fp);
                        
                        sftstruct  *sftptr = (sftstruct*)&RAM[((uint32_t)CPU_ES << 4) + CPU_DI];
                        
                        // Extract just the filename from the path for SFT
                        const char* filename = strrchr(dos_path, '\\');
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
                        sftptr->starting_cluster = handle; // Store our handle here
                        sftptr->file_size = file_size;
                        sftptr->file_time = 0x1000;
                        sftptr->file_date = 0x1000;
                        sftptr->file_position = 0;
                        sftptr->ptr = 0;
                        sftptr->unk1 = 0xffFF;
                        sftptr->unk2 = 0xffff;
                        sftptr->unk3 = 0;
                        sftptr->unk4 = 0xff;

                        // printf("sss %x > %s\n", ((uint32_t)CPU_ES << 4) + CPU_DI, sftptr->file_name);

                        CPU_AX = 0;
                        CPU_FL_CF = 0;
                    } else {
                        printf("not found\n");
                        CPU_AX = 2; // File not found
                        CPU_FL_CF = 1;
                    }
                } else {
                    printf("too many open files\n");
                    CPU_AX = 4; // Too many open files
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1117: // Create/Truncate File
            {
                const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
                get_full_path(path, dos_path);
                int handle = get_free_handle();
                if (handle != -1) {
                    open_files[handle].fp = fopen(path, "wb+");
                    if (open_files[handle].fp) {
                        strcpy(open_files[handle].path, path);
                        
                        // Initialize SFT structure
                        sftstruct *sftptr = (sftstruct*)&RAM[((uint32_t)CPU_ES << 4) + CPU_DI];
                        
                        // Extract just the filename from the path for SFT
                        const char* filename = strrchr(dos_path, '\\');
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
                        sftptr->starting_cluster = handle; // Store our handle here
                        sftptr->file_size = 0; // New file
                        sftptr->file_time = 0x1000;
                        sftptr->file_date = 0x1000;
                        sftptr->file_position = 0;
                        sftptr->ptr = 0;
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
                    
                    DWORD total_clusters = (DWORD)(total_number_of_bytes.QuadPart / (bytes_per_sector * sectors_per_cluster));
                    DWORD free_clusters = (DWORD)(free_bytes_available.QuadPart / (bytes_per_sector * sectors_per_cluster));
                    
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
            const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
            strcpy(dos_path, &RAM[sda_addr + 0x9e]); // SDA First filename buffer
            get_full_path(path, dos_path);
            printf("setattr %s %s\n", dos_path, path);
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
                const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                strcpy(dos_path, &RAM[sda_addr + 0x9e]); // SDA First filename buffer
                get_full_path(path, dos_path);
                
                // Get file attributes
                DWORD win_attributes = GetFileAttributesA(path);
                if (win_attributes == INVALID_FILE_ATTRIBUTES) {

                    DWORD error = GetLastError();
                    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                        CPU_AX = 2; // File not found
                        printf("not found\n");
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
                        FILE* fp = fopen(path, "rb");
                        if (fp) {
                            fseek(fp, 0, SEEK_END);
                            uint32_t  file_size = (uint32_t)ftell(fp);
                            fclose(fp);

                            CPU_AX = dos_attributes;
                            CPU_BX = (file_size >> 16) & 0xFFFF; // High word
                            CPU_DI = file_size & 0xFFFF;         // Low word
                            CPU_CX = 0x1000; // Default time stamp
                            CPU_DX = 0x1000; // Default date stamp
                            CPU_FL_CF = 0;
                        } else {
                            printf("not found\n");
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
                char search_path[256];

            // printf("cur ES %x, DI %x %x\n", CPU_ES, CPU_DI, ((uint32_t)CPU_ES << 4) + CPU_DI);
                // mem_read_bytes(CPU_ES, CPU_DI, dos_path, 128);
                const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
                get_full_path(search_path, dos_path);
                printf("dospath %s\n", dos_path);

                if (find_handle != INVALID_HANDLE_VALUE) {
                    FindClose(find_handle);
                }

                find_handle = FindFirstFile(search_path, &find_data);


                if (find_handle != INVALID_HANDLE_VALUE) {
                    printf("SDA addr: %x\n", sda_addr);
                    const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *)&RAM[sda_addr + 12];
                    dta_ptr = (sdbstruct *)&RAM[dta_addr];
                    printf("dta_addr %x\n", dta_addr);

                    dta_ptr->drv_lett = 'H' | 128; /* bit 7 set means 'network drive' */
                    to_dos_name(find_data.cFileName, (char *) dta_ptr->foundfile.fname);
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
                    to_dos_name(find_data.cFileName, (char *) dta_ptr->foundfile.fname);

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
                if (open_files[i].fp) {
                    // fflush(open_files[i].fp);
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