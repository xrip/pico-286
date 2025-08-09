#pragma once

#include <fileapi.h>

#include "emulator.h"

// Host filesystem passthrough base directory
#define HOST_BASE_DIR "C:\\FASM"

int hdcount = 0, fdcount = 0;

static uint8_t sectorbuffer[512];
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *pathname, const char *mode);
extern int fclose(FILE *stream);
extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int fseek(FILE *stream, long offset, int whence);
extern long ftell(FILE *stream);
extern void rewind(FILE *stream);

#define SEEK_CUR    1
#define SEEK_END    2
#define SEEK_SET    0

struct struct_drive {
    FILE *diskfile;
    size_t filesize;
    uint16_t cyls;
    uint16_t sects;
    uint16_t heads;
    uint8_t inserted;
    uint8_t readonly;
} disk[4];


static inline void ejectdisk(uint8_t drivenum) {
    if (drivenum & 0x80) drivenum -= 126;

    if (disk[drivenum].inserted) {
        disk[drivenum].inserted = 0;
        if (drivenum >= 0x80)
            hdcount--;
        else
            fdcount--;
    }
}

uint8_t insertdisk(uint8_t drivenum, const char *pathname) {
    if (drivenum & 0x80) drivenum -= 126;  // Normalize hard drive numbers

    FILE *file = fopen(pathname, "rb+");
    if (!file) {
        printf( "DISK: ERROR: cannot open disk file %s for drive %02Xh\n", pathname, drivenum);
        return 0;
    }

    // Find the file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    // Validate size constraints
    if (size < 360 * 1024 || size > 0x1f782000UL || (size & 511)) {
        fclose(file);
//        fprintf(stderr, "DISK: ERROR: invalid disk size for drive %02Xh (%lu bytes)\n", drivenum, (unsigned long) size);
        return 0;
    }

    // Determine geometry (cyls, heads, sects)
    uint16_t cyls = 0, heads = 0, sects = 0;

    if (drivenum >= 2) {  // Hard disk
        sects = 63;
        heads = 16;
        cyls = size / (sects * heads * 512);
    } else {  // Floppy disk
        cyls = 80;
        sects = 18;
        heads = 2;

        if (size <= 368640) {  // 360 KB or lower
            cyls = 40;
            sects = 9;
            heads = 2;
        } else if (size <= 737280) {
            sects = 9;
        } else if (size <= 1228800) {
            sects = 15;
        }
    }

    // Validate geometry
    if (cyls > 1023 || cyls * heads * sects * 512 != size) {
//        fclose(file);
//        fprintf(stderr, "DISK: ERROR: Cannot determine correct CHS geometry for drive %02Xh\n", drivenum);
//        return 0;
    }

    // Eject any existing disk and insert the new one
    ejectdisk(drivenum);

    disk[drivenum].diskfile = file;
    disk[drivenum].filesize = size;
    disk[drivenum].inserted = 1;  // Using 1 instead of true for consistency with uint8_t
    disk[drivenum].readonly = 0;  // Default to read-write
    disk[drivenum].cyls = cyls;
    disk[drivenum].heads = heads;
    disk[drivenum].sects = sects;

    // Update drive counts
    if (drivenum >= 2) {
        hdcount++;
    } else {
        fdcount++;
    }

//    printf("DISK: Disk %02Xh attached from file %s, size=%luK, CHS=%d,%d,%d\n",
//           drivenum, pathname, (unsigned long) (size >> 10), cyls, heads, sects);

    return 1;
}

// Call this ONLY if all parameters are valid! There is no check here!
static inline size_t chs2ofs(int drivenum, int cyl, int head, int sect) {
    return (
                   ((size_t)cyl * (size_t)disk[drivenum].heads + (size_t)head) * (size_t)disk[drivenum].sects + (size_t) sect - 1
           ) * 512UL;
}


static void readdisk(uint8_t drivenum,
              uint16_t dstseg, uint16_t dstoff,
              uint16_t cyl, uint16_t sect, uint16_t head,
              uint16_t sectcount, int is_verify
) {
    uint32_t memdest = ((uint32_t) dstseg << 4) + (uint32_t) dstoff;
    uint32_t cursect = 0;

    // Check if disk is inserted
    if (!disk[drivenum].inserted) {
//        printf("no media %i\r\n", drivenum);
        CPU_AH = 0x31;    // no media in drive
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Check if CHS parameters are valid
    if (sect == 0 || sect > disk[drivenum].sects || cyl >= disk[drivenum].cyls || head >= disk[drivenum].heads) {
//        printf("sector not found\r\n");
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Convert CHS to file offset
    size_t fileoffset = chs2ofs(drivenum, cyl, head, sect);

    // Check if fileoffset is valid
    if (fileoffset > disk[drivenum].filesize) {
//        printf("sector not found\r\n");
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Set file position
    fseek(disk[drivenum].diskfile, fileoffset, SEEK_SET);

    // Process sectors
    for (cursect = 0; cursect < sectcount; cursect++) {
        // Read the sector into buffer
        if (fread(&sectorbuffer[0], 512, 1, disk[drivenum].diskfile) != 1) {
//            printf("Disk read error on drive %i\r\n", drivenum);
            CPU_AH = 0x04;    // sector not found
            CPU_AL = 0;
            CPU_FL_CF = 1;
            return;
        }

        if (is_verify) {
            for (int sectoffset = 0; sectoffset < 512; sectoffset++) {
                // Verify sector data
                if (read86(memdest++) != sectorbuffer[sectoffset]) {
                    // Sector verify failed
                    CPU_AL = cursect;
                    CPU_FL_CF = 1;
                    CPU_AH = 0xBB;    // sector verify failed error code
                    return;
                }
            }
        } else {
            for (int sectoffset = 0; sectoffset < 512; sectoffset++) {
                // Write sector data
                write86(memdest++, sectorbuffer[sectoffset]);
            }
        }

        // Update file offset for next sector
        fileoffset += 512;
    }

    // If no sectors could be read, handle the error
    if (cursect == 0) {
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Set success flags
    CPU_AL = cursect;
    CPU_FL_CF = 0;
    CPU_AH = 0;
}

static void writedisk(uint8_t drivenum,
               uint16_t dstseg, uint16_t dstoff,
               uint16_t cyl, uint16_t sect, uint16_t head,
               uint16_t sectcount
) {
    uint32_t memdest = ((uint32_t) dstseg << 4) + (uint32_t) dstoff;
    uint32_t cursect;

    // Check if disk is inserted
    if (!disk[drivenum].inserted) {
        CPU_AH = 0x31;    // no media in drive
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Convert CHS to file offset
    size_t fileoffset = chs2ofs(drivenum, cyl, head, sect);

    // check if sector can be found
    if (
            ((sect == 0 || sect > disk[drivenum].sects || cyl >= disk[drivenum].cyls || head >= disk[drivenum].heads))
            || fileoffset > disk[drivenum].filesize
            || disk[drivenum].filesize < fileoffset
            ) {
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Check if drive is read-only
    if (disk[drivenum].readonly) {
        CPU_AH = 0x03;    // drive is read-only
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Set file position
    fseek(disk[drivenum].diskfile, fileoffset, SEEK_SET);


    // Write each sector
    for (cursect = 0; cursect < sectcount; cursect++) {
        // Read from memory and store in sector buffer
        for (int sectoffset = 0; sectoffset < 512; sectoffset++) {
            // FIXME: segment overflow condition?
            sectorbuffer[sectoffset] = read86(memdest++);
        }

        // Write the buffer to the file
        fwrite(sectorbuffer, 512, 1, disk[drivenum].diskfile);
    }

    // Handle the case where no sectors were written
    if (sectcount && cursect == 0) {
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        CPU_FL_CF = 1;
        return;
    }

    // Set success flags
    CPU_AL = cursect;
    CPU_FL_CF = 0;
    CPU_AH = 0;
}


static INLINE void diskhandler() {
    static uint8_t lastdiskah[4] = { 0 }, lastdiskcf[4] = { 0 };
    uint8_t drivenum = CPU_DL;

    // Normalize drivenum for hard drives
    if (drivenum & 0x80) drivenum -= 126;

    // Handle the interrupt service based on the function requested in AH
    switch (CPU_AH) {
        case 0x00:  // Reset disk system
            if (disk[drivenum].inserted) {
                CPU_AH = 0;
                CPU_FL_CF = 0;  // Successful reset (no-op in emulator)
            } else {

                CPU_FL_CF = 1;  // Disk not inserted
            }
            break;

        case 0x01:  // Return last status
            CPU_AH = lastdiskah[drivenum];
            CPU_FL_CF = lastdiskcf[drivenum];
//            printf("disk not inserted %i", drivenum);
            return;

        case 0x02:  // Read sector(s) into memory
            readdisk(drivenum, CPU_ES, CPU_BX,
                     CPU_CH + (CPU_CL / 64) * 256,  // Cylinder
                     CPU_CL & 63,                    // Sector
                     CPU_DH,                         // Head
                     CPU_AL,                         // Sector count
                     0);                             // Read operation
            break;

        case 0x03:  // Write sector(s) from memory
            writedisk(drivenum, CPU_ES, CPU_BX,
                      CPU_CH + (CPU_CL / 64) * 256,  // Cylinder
                      CPU_CL & 63,                   // Sector
                      CPU_DH,                        // Head
                      CPU_AL);                       // Sector count
            break;

        case 0x04:  // Verify sectors
            readdisk(drivenum, CPU_ES, CPU_BX,
                     CPU_CH + (CPU_CL / 64) * 256,   // Cylinder
                     CPU_CL & 63,                    // Sector
                     CPU_DH,                         // Head
                     CPU_AL,                         // Sector count
                     1);                             // Verify operation
            break;

        case 0x05:  // Format track
            CPU_FL_CF = 0;  // Success (no-op for emulator)
            CPU_AH = 0;
            break;

        case 0x08:  // Get drive parameters
            if (disk[drivenum].inserted) {
                CPU_FL_CF = 0;
                CPU_AH = 0;
                CPU_CH = disk[drivenum].cyls - 1;
                CPU_CL = (disk[drivenum].sects & 63) + ((disk[drivenum].cyls / 256) * 64);
                CPU_DH = disk[drivenum].heads - 1;

                // Set DL and BL for floppy or hard drive
                if (CPU_DL < 2) {
                    CPU_BL = 4;  // Floppy
                    CPU_DL = 2;
                } else {
                    CPU_DL = hdcount;  // Hard disk
                }
            } else {
                CPU_FL_CF = 1;
                CPU_AH = 0xAA;  // Error code for no disk inserted
            }
            break;

        default:  // Unknown function requested
            CPU_FL_CF = 1;  // Error
            break;
    }

    // Update last disk status
    lastdiskah[drivenum] = CPU_AH;
    lastdiskcf[drivenum] = CPU_FL_CF;

    // Set the last status in BIOS Data Area (for hard drives)
    if (CPU_DL & 0x80) {
        RAM[0x474] = CPU_AH;
    }
}


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

        case 0x1101: // Remove Remote Directory
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            get_full_path(path, dos_path);
            /*if (rmdir(path) == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else*/ {
                CPU_AX = 3; // Path not found
                CPU_FL_CF = 1;
            }
            break;

        case 0x1103: // Create Remote Directory
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            get_full_path(path, dos_path);
            /*if (mkdir(path) == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else */{
                CPU_AX = 5; // Access denied
                CPU_FL_CF = 1;
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
                    // fflush(open_files[handle].fp);
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
                uint16_t count = CPU_CX;
                printf("HANDLE COUNT %X %i\n", handle, count);
                if (handle < MAX_FILES && open_files[handle].fp) {
                    const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                    const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *)&RAM[sda_addr + 12];
                    size_t bytes_read = fread(&RAM[dta_addr], 1, count, open_files[handle].fp);
                    printf("bytes read %i %x\n", bytes_read, dta_addr);
                    
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
                if (handle < MAX_FILES && open_files[handle].fp) {
                    const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                    const uint32_t dta_addr = (*(uint16_t *) &RAM[sda_addr + 14] << 4) + *(uint16_t *)&RAM[sda_addr + 12];
                    size_t bytes_written = fwrite(&RAM[dta_addr], 1, count, open_files[handle].fp);
                    
                    // Update file position in SFT
                    sftptr->file_position += bytes_written;
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
                /*if (rename(old_path_host, new_path_host) == 0) {
                    CPU_AX = 0;
                    CPU_FL_CF = 0;
                } else*/ {
                    CPU_AX = 2; // File not found
                    CPU_FL_CF = 1;
                }
            }
            break;

        case 0x1113: // Delete Remote File
            mem_read_bytes(CPU_DS, CPU_DX, dos_path, sizeof(dos_path));
            get_full_path(path, dos_path);
            /*if (remove(path) == 0) {
                CPU_AX = 0;
                CPU_FL_CF = 0;
            } else*/ {
                CPU_AX = 2; // File not found
                CPU_FL_CF = 1;
            }
            break;

        case 0x1116: // Open Existing File
            {
                const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
                get_full_path(path, dos_path);
                // printf("Opening %s %s\n", dos_path, path);
                
                int handle = get_free_handle();
                if (handle != -1) {
                    open_files[handle].fp = fopen(path, "rb+");
                    if (!open_files[handle].fp) {
                        // Try read-only mode if read-write fails
                        open_files[handle].fp = fopen(path, "rb");
                        // printf("Tried rb+ failed, trying rb: %s\n", open_files[handle].fp ? "SUCCESS" : "FAILED");
                    }
                    if (open_files[handle].fp) {
                        strcpy(open_files[handle].path, path);
                        /*
                        const uint32_t sda_addr = ((uint32_t)sda_seg << 4) + sda_off;
                strcpy(dos_path, &RAM[sda_addr  + 0x9e]); // SDA First filename buffer
                */
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

                        printf("sss %x > %s\n", ((uint32_t)CPU_ES << 4) + CPU_DI, sftptr->file_name);

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
                        sftptr->attribute = 0x8;
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
                // if (GetDiskFreeSpaceEx("C:\\", &free_bytes_available, &total_number_of_bytes, &total_number_of_free_bytes)) {
                    CPU_AX = 512; // sectors per cluster
                    CPU_BX = 512; // total clusters
                    CPU_CX = 1024; // bytes per sector
                    CPU_DX = 1024; // available clusters
                // } else {
                    // CPU_AX = 1;
                    // CPU_FL_CF = 1;
                // }
            }
            break;

        case 0x110D: // Set File Attributes
            CPU_AX = 0;
            CPU_FL_CF = 0;
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