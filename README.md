# 🕹️ Pico-286 Project

The Pico-286 project is an endeavor to emulate a classic PC system, reminiscent of late 80s and early 90s computers, on the Raspberry Pi Pico 2 (RP2350 microcontroller). It aims to provide a lightweight and educational platform for experiencing retro computing and understanding low-level system emulation. 🖥️✨

## ⭐ Key Features

*   **🧠 8086/8088/80186/286 CPU Emulation:** At its core, the project emulates an Intel cpu up to 286 family.
*   **🌐 Cross-platform:** Can be built for the RP2350, Windows, and Linux.
*   **🔌 Retro Peripheral Emulation:** Includes support for common peripherals from the era.
*   **🎨 Text and Graphics Modes:** Supports various display modes common in early PCs.
*   **🔊 Sound Emulation:** Recreates sound capabilities of classic sound cards.
*   **🍓 Designed for Raspberry Pi Pico:** Optimized for the RP2350 with HDMI and onboard PSRAM.

## 🎮 Supported Hardware Emulations

### 🧠 CPU Emulation
*   Intel 8086/8088/80186/286 processor family

### 🎵 Sound Card Emulations
*   **📢 PC Speaker (System Beeper):** Authentic emulation of the original PC's internal speaker system
*   **🎚️ Covox Speech Thing:** Compatible emulation of the simple parallel port DAC
*   **🎭 Disney Sound Source (DSS):** Emulation of the popular parallel port digital audio device
*   **🎹 Adlib / Sound Blaster (OPL2 FM Synthesis):** High-quality emulation of the Yamaha OPL2 chipset for classic FM music and sound effects.
*   **🔊 Sound Blaster Pro (Digital Audio):** Emulation of a Sound Blaster Pro 2 (DSP 3.02): 8-bit mono and stereo DMA playback, including high-speed DMA, and the SB Pro mixer. Use `SET BLASTER=A220 I5 D1 T4`. FM music uses the OPL2 above, also at 228h/229h.
*   **🎼 MPU-401 (MIDI Interface with General MIDI Synthesizer):** Provides a MIDI interface and includes an integrated General MIDI (GM) software synthesizer, allowing playback of GM scores without external MIDI hardware. This is a key feature for many later DOS games.
*   **📢 Tandy 3-voice / PCjr (SN76489 PSG):** Emulation of the Texas Instruments SN76489 Programmable Sound Generator.
*   **🎮 Creative Music System / Game Blaster (CMS/GameBlaster):** Emulation of the dual Philips SAA1099 based sound card.

### 🖼️ Graphics Card Emulations

#### 📝 Text Modes (Common to All Graphics Cards)
All graphics card emulations support standard text display modes for character-based applications:
- 16 foreground colors with 8 background colors
- Full color attribute support including blinking text

**📝 Standard Text Modes:**
*   **80×25 Text Mode:** Standard 80 columns by 25 rows character display
*   **40×25 Text Mode:** Lower resolution 40 columns by 25 rows display

**🚀 Advanced CGA Text Modes (8088 MPH Demo Techniques):**
*   **🎨 160×100×16 Text Mode:** Ultra-low resolution high-color text mode
    - Revolutionary technique showcased in the famous "8088 MPH" demo by Hornet
    - 16 simultaneous colors from CGA palette in text mode
    - Achieved through advanced CGA register manipulation and timing tricks
    - Demonstrates the hidden capabilities of original CGA hardware
*   **🌈 160×200×16 Text Mode:** Enhanced color text mode
    - Extended version of the 8088 MPH technique with double vertical resolution
    - Full 16-color support in what appears to be a text mode
    - Pushes CGA hardware beyond its original specifications
    - Compatible with software that uses advanced CGA programming techniques

#### 🎨 CGA (Color Graphics Adapter)
The CGA emulation provides authentic IBM Color Graphics Adapter functionality, supporting the classic early PC graphics modes:

**🎮 Graphics Modes:**
*   **🌈 320×200×4 Colors:** Standard CGA graphics mode with selectable color palettes
*   **⚫⚪ 640×200×2 Colors:** High-resolution monochrome mode (typically black and white)
*   **📺 Composite Color Mode (160×200×16):** Emulates the artifact colors produced by CGA when connected to composite monitors, creating additional color combinations through NTSC color bleeding effects

#### 📊 HGC (Hercules Graphics Card)
The Hercules Graphics Card emulation recreates the popular monochrome high-resolution graphics standard:

**🖥️ Graphics Mode:**
*   **⚫⚪ 720×348×2 Colors:** High-resolution monochrome graphics mode
    
#### 🖥️ EGA (Enhanced Graphics Adapter)
The Enhanced Graphics Adapter emulation provides IBM EGA compatibility with full 16-color support:

**🚀 Enhanced Graphics Modes:**
*   **🎨 320×200×16 Colors:** EGA mode 0x0D with planar memory access
*   **🌈 640×200×16 Colors:** EGA mode 0x0E high-resolution 16-color mode
*   **✨ 640×350×16 Colors:** EGA mode 0x10 professional graphics mode

#### 🖥️ TGA (Tandy Graphics Adapter)
The Tandy Graphics Adapter emulation recreates the enhanced graphics capabilities of Tandy 1000 series computers:

**🚀 Enhanced Graphics Modes:**
*   **🎨 160×200×16 Colors:** Low-resolution mode with full 16-color palette
*   **🌈 320×200×16 Colors:** Medium-resolution mode with 16 simultaneous colors from a larger palette
*   **✨ 640×200×16 Colors:** High-resolution mode with 16-color support

#### 🖼️ VGA (Video Graphics Array)
The VGA emulation provides comprehensive Video Graphics Array support with planar memory architecture and multiple advanced modes:

**📊 Standard VGA Modes:**
*   **🎮 320×200×256 Colors:** Mode 13h with chain4 memory access
*   **🖥️ 640×480×16 Colors:** Standard VGA high-resolution mode (Mode 12h)
*   **🖥️ 640×480×2 Colors:** Monochrome VGA mode (Mode 11h)
*   **📝 Text modes:** 80×25 and 80×50 with enhanced character sets

**🔧 VGA Technical Features:**
*   **Planar Memory Layout:** 256KB video memory organized as 4 color planes (0xP3P2P1P0 format)
*   **32-bit Latch System:** Hardware-accurate latch emulation for planar operations
*   **Chain4 Mode Support:** Proper VGA chain4 mode handling for 256-color modes
*   **Hardware Registers:** Full VGA register compatibility including sequencer and graphics controllers

## 💾 Storage: Disk Images and Host Access

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

-   **On Windows builds:** Drive H: maps to the `C:\\FASM` directory by default.
-   **On Linux builds:** Drive H: maps to the `/tmp` directory by default.
-   **On the RP2350 firmware:** Drive H: maps to the `//XT//` directory on the SD card.

#### `MAPDRIVE.COM` Utility

The `tools/mapdrive.asm` source file can be assembled into `MAPDRIVE.COM` using FASM. This utility registers drive H: with the DOS kernel as a network drive.

**Prerequisite:** Before using `MAPDRIVE.COM`, ensure your `CONFIG.SYS` file contains the line `LASTDRIVE=H` (or higher, e.g., `LASTDRIVE=Z`). This tells DOS to allocate space for drive letters up to H:, allowing `MAPDRIVE.COM` to successfully create the new drive.

To use it:

1.  Assemble `mapdrive.asm` to `mapdrive.com`.
2.  Copy `mapdrive.com` to your boot disk image (e.g., `fdd0.img` or `hdd.img`).
3.  Run `MAPDRIVE.COM` from the DOS command line.
4.  Add `MAPDRIVE.COM` to your `AUTOEXEC.BAT` to automatically map the drive on boot.


## 🔧 Hardware Configuration

Pico-286 supports one board configuration: an **RP2350 (Raspberry Pi Pico 2 class) M2P2 / Murmulator 2 board** with HSTX HDMI video, HDMI audio and 8 MB of onboard QMI PSRAM. 🍓

### 🎛️ Supported Components
*   ⌨️ PS/2 keyboard and mouse
*   💾 SD card for storage
*   📺 HDMI video through the RP2350 HSTX block, with the sound sent over HDMI
*   🧠 8 MB onboard QMI PSRAM (required: the firmware stops at boot without it)
*   🎮 NES gamepad

### 🛠️ Development Platform
*   This project uses the [MURMULATOR dev board](https://murmulator.ru) (M2P2) as its hardware base. 🎯

### 🔌 Pinout
The pins are set in `CMakeLists.txt`.

| Peripheral        | GPIO Pin(s)                     | Notes                              |
|-------------------|---------------------------------|------------------------------------|
| **HDMI Output**   | 12–19                           | RP2350 HSTX pins                   |
| **SD Card**       | CS: 5, SCK: 6, MOSI: 7, MISO: 4 | SPI0                               |
| **PS/2 Keyboard** | CLK: 2, DATA: 3                 |                                    |
| **PS/2 Mouse**    | CLK: 0, DATA: 1                 |                                    |
| **NES Gamepad**   | CLK: 20, DATA: 21, LAT: 26      | Used for mouse emulation if needed |
| **PSRAM**         | QMI CS1                         | Onboard, memory-mapped             |

### ⚙️ Memory and Sound
*   **Conventional RAM:** 640 KB, in PSRAM.
*   **EMS:** 2 MB, **XMS:** 4 MB, plus the HMA and UMBs, all in PSRAM.
*   **Sound:** 44,100 Hz stereo, sent over HDMI. The HDMI clock sets the rate.

## 🏛️ Platform Architecture
The emulator uses different architectures depending on the target platform to best utilize the available resources.

### Raspberry Pi Pico 2 (Dual-Core)
*   **Core 0:** Runs the main CPU emulation loop (`exec86`) and handles user input from the PS/2 keyboard and NES gamepad.
*   **Core 1:** Dedicated to real-time, time-critical tasks. It runs an infinite loop that manages:
    *   Video rendering (at ~60Hz).
    *   Audio sample generation and output.
    *   PIT timer interrupts for the emulator (at ~18.2Hz).

This division of labor ensures that the demanding CPU emulation does not interfere with smooth video and audio output.

### Windows & Linux (Multi-threaded)
The host builds (for Windows and Linux) are multi-threaded to separate tasks.
*   **Main Thread:** Runs the main CPU emulation loop (`exec86`) and handles the window and its events via the MiniFB library.
*   **Ticks Thread:** A dedicated thread that acts as the system's clock. It uses high-resolution timers (`QueryPerformanceCounter` on Windows, `clock_gettime` on Linux) to trigger events like PIT timer interrupts, rendering updates, and audio sample generation at the correct frequencies.
*   **Sound Thread:** A separate thread responsible for communicating with the host operating system's audio API (WaveOut on Windows, a custom backend on Linux) to play the generated sound without blocking the other threads.

This architecture allows for accurate timing and responsive I/O on a non-real-time desktop operating system.

## 🔨 Building and Getting Started

### 📋 Prerequisites

#### For the RP2350 firmware:
*   **Pico SDK:** Install the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) and set `PICO_SDK_PATH`
*   **CMake:** Version 3.22 or higher, and Ninja
*   **ARM GCC Toolchain:** For cross-compilation to ARM Cortex-M33

#### For Windows host builds:
*   **CMake:** Version 3.22 or higher
*   **GCC (MinGW):** C++20 compatible compiler

#### For Linux host builds:
*   **CMake:** Version 3.22 or higher
*   **GCC/Clang:** C++20 compatible compiler (GCC 11+ or Clang 13+)
*   **X11 development libraries:** Required for graphics output
*   **Threading support:** pthread library (usually included with GCC)

### 🚀 Build Commands

#### RP2350 firmware:
```bash
cmake --preset m2p2
cmake --build --preset m2p2
```
There are no display, audio or memory options: the firmware always uses HSTX HDMI, HDMI audio and onboard PSRAM. `-DMIDI_GM_DLS=ON` selects the GM.DLS wavetable MIDI engine (see `BUILDING.md`).

#### Linux Host Build:
```bash
sudo apt install build-essential cmake git libx11-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=host
cmake --build build -j
```

#### Windows Host Build:
```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=host
cmake --build build -j
```

### 📦 Build Outputs

After successful compilation, build artifacts are placed in the `bin/<platform>/<build_type>/` directory.

#### For host builds:
*   **Executable:** `286` (Linux) or `286.exe` (Windows); `286-GMDLS.exe` and `286-WINMM.exe` for the other MIDI variants.

#### For the RP2350 firmware:
`286-P<psram>-C<cpu_freq>MHz[-GMDLS].uf2`, for example `286-P166-C500MHz.uf2`.

*   **`P<psram>`**: PSRAM frequency (e.g., `P166` for 166MHz).
*   **`C<cpu_freq>MHz`**: CPU frequency in MHz (`C500MHz`).
*   **`-GMDLS`**: built with the GM.DLS wavetable MIDI engine.

The following files are generated:
*   `.uf2`: The firmware file for flashing to the Pico.
*   `.elf`: The executable file for debugging.
*   `.bin`: The raw binary file.

### 🎯 Flashing to Pico

1. **Hold the BOOTSEL button** while connecting your Pico to USB
2. **Copy the `.uf2` file** to the mounted RP2350 drive
3. **The Pico will automatically reboot** and start running the emulator

### 💾 Setting up Disk Images

#### For the RP2350 firmware:
Create the required directory structure on your SD card:
```
SD Card Root/
└── XT/
    ├── fdd0.img    # Floppy Drive A:
    ├── fdd1.img    # Floppy Drive B: (optional)
    ├── hdd.img     # Hard Drive C:
    └── hdd2.img    # Hard Drive D: (optional)
```

#### For Linux/Windows host builds:
Place disk images in the project root directory:
```bash
# From your project directory (pc/)
# Place disk images directly in the root:
cp your-boot-disk.img fdd0.img     # Floppy Drive A:
cp your-floppy2.img fdd1.img       # Floppy Drive B: (optional)
cp your-harddisk.img hdd.img       # Hard Drive C:
cp your-harddisk2.img hdd2.img     # Hard Drive D: (optional)

# Run from build directory
cd build
../bin/host/Release/286   # Linux
# or ../bin/host/Release/286.exe   # Windows
```

**Supported disk image sizes:**
*   **Floppy disks:** 360KB, 720KB, 1.2MB, 1.44MB
*   **Hard disks:** Any size (geometry calculated automatically)

### 🐛 Troubleshooting

**Linux build fails with "X11 not found":**
- Install X11 development headers: `sudo apt install libx11-dev`
- On other distributions: `sudo dnf install libX11-devel` (Fedora) or `sudo pacman -S libx11` (Arch)

**Host build shows "DISK: ERROR: cannot open disk file":**
- Ensure disk images are in the project root directory (not build directory)
- Check file permissions: `chmod 644 *.img`
- Verify disk images exist: `ls -la *.img`

**Linux emulator window appears but shows black screen:**
- Ensure you have a bootable disk image in `fdd0.img`
- Check disk image format is valid DOS/PC format
- Try running from terminal to see debug messages

**The board stops at boot with "requires at least 8 MB onboard QMI PSRAM":**
- The board has no working onboard PSRAM. This firmware needs it.

**HDMI not working:**
- Check HDMI cable and display compatibility
- Verify power supply can handle HDMI output requirements

### 📚 Additional Resources

*   **Pin configurations:** Defined in `CMakeLists.txt` compile definitions
*   **Development board:** [MURMULATOR](https://murmulator.ru) recommended for development
*   **Video modes reference:** See `VIDEO_MODES.md` for detailed mode specifications
*   **Build documentation:** See `BUILDING.md` for comprehensive build instructions

## 🤝 Contributing

Contributions to the Pico-286 project are welcome! Please refer to the `CONTRIBUTING.md` file (to be created) for guidelines. 💪

## Stargazers over time
[![Stargazers over time](https://starchart.cc/xrip/pico-286.svg?variant=adaptive)](https://starchart.cc/xrip/pico-286)

## 📄 License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
