# Fruit Jam 286

The Fruit Jam 286 (based on Pico-286) is an emulator for early class PC stystems.

**This software overclocks your Fruit Jam's RP2350 CPU, its flash chip, and its psram chip. It also overvolts the RP2350 CPU.  Just like PC overclocking, there’s some risk of reduced component lifespan, though the extent (if any) can’t be precisely quantified and could vary from one chip to another. Proceed at your own discretion.** 

## Key Features

*   **8086/8088/80186/286 CPU Emulation:** At its core, the project emulates an Intel cpu up to 286 family.
*   **Text and Graphics Modes:** Supports various display modes common in early PCs.
*   **Sound Emulation:** Recreates sound capabilities of classic sound cards.
*   **Designed for Adafruit Fruit Jam**

## Supported Hardware Emulations

Pico-286 lists support for more features. These are the ones we think are at least somewhat working in the FruitJam-286.

### CPU Emulation
*   Intel 8086/8088/80186/286 processor family

### Sound Card Emulations
*   **PC Speaker (System Beeper):** Authentic emulation of the original PC's internal speaker system
*   **Adlib / Sound Blaster (OPL2 FM Synthesis):** High-quality emulation of the Yamaha OPL2 chipset for classic FM music and sound effects.
*   **Sound Blaster (Digital Audio):** Support for Sound Blaster's digital sound capabilities, including DMA-based playback.

### Graphics Card Emulations

#### Text Modes (Common to All Graphics Cards)
* **80×25 Text Mode:** Standard 80 columns by 25 rows character display
* chunky 4x6 character graphics occupying the top 3/4 of the full display
* 16 foreground colors with 8 background colors
* Full color attribute support including blinking text

####  CGA (Color Graphics Adapter)
The CGA emulation provides authentic IBM Color Graphics Adapter functionality, supporting the classic early PC graphics modes:

**Graphics Modes:**
*   **320×200×4 Colors:** Standard CGA graphics mode with selectable color palettes

#### VGA (Video Graphics Array)
The VGA emulation provides comprehensive Video Graphics Array support with multiple advanced modes:

** Standard VGA Modes:**
*   **320×200×256 Colors:** Mode 13h

## Storage: Disk Images and Host Access

The emulator supports two primary types of storage: virtual disk images for standard DOS drives (A:, B:, C:, D:) and direct access to the host filesystem via a mapped network drive (H:).

### Virtual Floppy and Hard Disks (Drives A:, B:, C:, D:)

The emulator supports up to two floppy disk drives (A: and B:) and up to two hard disk drives (C: and D:). Disk images are stored on the SD card.

The emulator expects the following file paths and names for the disk images:

*   **Floppy Drive 0 (A:):** `\\XT\\fdd0.img`
*   **Floppy Drive 1 (B:):** `\\XT\\fdd1.img`
*   **Hard Drive 0 (C:):** `\\XT\\hdd.img`
*   **Hard Drive 1 (D:):** `\\XT\\hdd2.img`

**Important Notes:**

*   The disk type (floppy or hard disk) is determined by the drive number it is assigned to in the emulator, not by the filename itself.
*   The emulator automatically determines the disk geometry (cylinders, heads, sectors) based on the size of the image file. Ensure your disk images have standard sizes for floppy disks (e.g., 360KB, 720KB, 1.2MB, 1.44MB) for proper detection. For hard disks, the geometry is calculated based on a standard CHS (Cylinder/Head/Sector) layout.

### Host Filesystem Access (Drive H:)

For seamless file exchange, the emulator can map a directory from the host filesystem and present it as drive **H:** in the DOS environment. This feature is implemented through the standard **DOS network redirector interface (INT 2Fh, Function 11h)**.

This is ideal for development, allowing you to edit files on your host machine and access them instantly within the emulator without modifying disk images.

#### How It Works

The emulator intercepts file operations for drive H: and translates them into commands for the host's filesystem. To enable this drive, you must run the `MAPDRIVE.COM` utility within the emulator.

The mapped directory depends on the platform:

-   **On Pico builds (RP2040/RP2350):** Drive H: maps to the `//XT//MAPDRIVE` directory on the SD card.

#### `MAPDRIVE.COM` Utility

The `tools/mapdrive.asm` source file can be assembled into `MAPDRIVE.COM` using FASM. This utility registers drive H: with the DOS kernel as a network drive.

**Prerequisite:** Before using `MAPDRIVE.COM`, ensure your `CONFIG.SYS` file contains the line `LASTDRIVE=H` (or higher, e.g., `LASTDRIVE=Z`). This tells DOS to allocate space for drive letters up to H:, allowing `MAPDRIVE.COM` to successfully create the new drive.

To use it:

1.  Assemble `mapdrive.asm` to `mapdrive.com`.
2.  Copy `mapdrive.com` to your boot disk image (e.g., `fdd0.img` or `hdd.img`).
3.  Run `MAPDRIVE.COM` from the DOS command line.
4.  Add `MAPDRIVE.COM` to your `AUTOEXEC.BAT` to automatically map the drive on boot.


## Hardware Configuration

The Pico-286 emulator is designed to run on Raspberry Pi Pico (RP2040) based hardware. 🍓

### Supported Components
*   USB Keyboard
*   SD card for storage
*   HDMI for video output
*   I2S Audio output

### Build Commands
```./fruitjam-build.sh```
### Build Outputs

After successful compilation, build artifacts are placed in the `bin/<platform>/<build_type>/` directory.

### Flashing to Pico

1. **Hold the BOOTSEL button** while connecting your Pico to USB
2. **Copy the `.uf2` file** to the mounted RPI-RP2 drive  
3. **The Pico will automatically reboot** and start running the emulator

### Setting up Disk Images

#### For Raspberry Pi Pico builds:
Create the required directory structure on your SD card:
```
SD Card Root/
└── XT/
    ├── fdd0.img    # Floppy Drive A:
    ├── fdd1.img    # Floppy Drive B: (optional)
    ├── hdd.img     # Hard Drive C:
    └── hdd2.img    # Hard Drive D: (optional)
```

**Supported disk image sizes:**
*   **Floppy disks:** 360KB, 720KB, 1.2MB, 1.44MB
*   **Hard disks:** Any size (geometry calculated automatically)

## 📄 License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
