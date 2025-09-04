#include <hardware/gpio.h>
#include "emulator.h"
#include "ff.h"

extern FATFS fs;

int hdcount = 0, fdcount = 0;

static uint8_t sectorbuffer[512];

struct struct_drive {
    FIL diskfile;
    size_t filesize;
    uint16_t cyls;
    uint16_t sects;
    uint16_t heads;
    uint8_t inserted;
    uint8_t readonly;
} disk[4];

static int led_state = 0;

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
    FIL file;

    if (drivenum & 0x80) drivenum -= 126;  // Normalize hard drive numbers

    if (FR_OK != f_open(&file, pathname, FA_READ | FA_WRITE )) {
        //draw_text("Cant mount drive", 0, 0, 12, 0);
        //while (1);
        return 0;
    }

    size_t size = f_size(&file);

    // Validate size constraints
    if (size < 360 * 1024 || size > 0x1f782000UL || (size & 511)) {
        f_close(&file);
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
    f_lseek(&disk[drivenum].diskfile, fileoffset);

    // Process sectors
    for (cursect = 0; cursect < sectcount; cursect++) {
        // Read the sector into buffer
        size_t br;
        f_read(&disk[drivenum].diskfile, &sectorbuffer[0], 512, &br);

        if (!br) {
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


        gpio_put(PICO_DEFAULT_LED_PIN, led_state);
        led_state ^= 1;

        // Update file offset for next sector
        fileoffset += 512;
    }
    led_state = 0;
    gpio_put(PICO_DEFAULT_LED_PIN, led_state);



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
    f_lseek(&disk[drivenum].diskfile, fileoffset);


    // Write each sector
    for (cursect = 0; cursect < sectcount; cursect++) {
        // Read from memory and store in sector buffer
        for (int sectoffset = 0; sectoffset < 512; sectoffset++) {
            // FIXME: segment overflow condition?
            sectorbuffer[sectoffset] = read86(memdest++);
        }

        // Write the buffer to the file
        size_t bw;
        f_write(&disk[drivenum].diskfile, sectorbuffer, 512, &bw);
        gpio_put(PICO_DEFAULT_LED_PIN, led_state);
        led_state ^= 1;
    }
    led_state = 0;
    gpio_put(PICO_DEFAULT_LED_PIN, led_state);

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


static inline void diskhandler() {
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
        FIRST_RAM_PAGE[0x474] = CPU_AH;
    }
}
