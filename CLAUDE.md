# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Pico-286 is a PC emulator targeting Intel 8086/8088/80186/286 CPUs, designed to run on Raspberry Pi Pico (RP2040/RP2350) microcontrollers. The project emulates classic PC hardware including various graphics cards (CGA, TGA, EGA, VGA), sound cards (PC Speaker, Adlib, Sound Blaster, MPU-401), and peripherals.

## Build System

The project uses CMake with two main build targets:

### Cross-platform builds:
- **Host build (Windows/Linux)**: `cmake -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=host`
- **Pico RP2040**: `cmake -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=rp2040`  
- **Pico RP2350**: `cmake -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=rp2350`

### Essential build options:
All builds require exactly one display and one audio option:

**Display options (mutually exclusive):**
- `-DENABLE_TFT=ON` - TFT display via ST7789
- `-DENABLE_VGA=ON` - VGA output
- `-DENABLE_HDMI=ON` - HDMI output (forces CPU to 378MHz)

**Audio options (mutually exclusive):**
- `-DENABLE_I2S_SOUND=ON` - I2S audio output
- `-DENABLE_PWM_SOUND=ON` - PWM audio output  
- `-DENABLE_HARDWARE_SOUND=ON` - Hardware audio output

### Memory configuration:
- **PSRAM**: Default for RP2350. Use `-DONBOARD_PSRAM=ON -DONBOARD_PSRAM_GPIO=19` for onboard PSRAM
- **Virtual Memory**: Use `-DTOTAL_VIRTUAL_MEMORY_KBS=512` to enable virtual memory instead of PSRAM
- **CPU Frequency**: Set with `-DCPU_FREQ_MHZ=378` (default varies by platform)

### Example build commands:
```bash
# RP2350 with VGA and PWM audio
cmake -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=rp2350 -DENABLE_VGA=ON -DENABLE_PWM_SOUND=ON

# Windows/Linux host build (Linux requires X11 development libraries)
cmake -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=host

# RP2040 with TFT and I2S
cmake -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=rp2040 -DENABLE_TFT=ON -DENABLE_I2S_SOUND=ON
```

## Architecture

### Core Components

**Emulator Core** (`src/emulator/`):
- `cpu.c/cpu.h` - Intel x86 CPU emulation (8086-286)
- `memory.c` - Memory management and addressing 
- `ports.c` - I/O port handling
- `emulator.h` - Main emulator definitions and interfaces

**Platform Abstraction**:
- `src/pico-main.c` - Raspberry Pi Pico entry point and hardware initialization
- `src/win32-main.cpp` - Windows host build entry point with MiniFB graphics
- `src/linux-main.cpp` - Linux host build entry point with X11-based MiniFB graphics

**Memory Layout**:
- `RAM[RAM_SIZE]` - Main system RAM (size varies by platform, see `README.md` for details).
- `VIDEORAM[64KB]` - Video memory buffer at 0xA0000-0xC0000.
- See the "Platform-specific Details" and "Extended Memory (EMS/XMS)" sections in `README.md` for a full breakdown.

**Extended Memory Systems**:
- **PSRAM**: High-speed hardware-based extended memory using an external PSRAM chip. This is the default and recommended option. It uses a custom PIO and DMA driver.
- **Virtual Memory**: A slower, file-based fallback for systems without PSRAM. It uses a `pagefile.sys` on the SD card. Enabled with `-DTOTAL_VIRTUAL_MEMORY_KBS > 0`.

### Video System

**Graphics Drivers** (`drivers/graphics/`):
- Abstracted graphics interface supporting multiple output types
- Hardware-specific implementations: VGA, HDMI, TFT (ST7789)
- Font rendering with multiple font sizes (4x6, 6x8, 8x8, 8x16)

**Video Mode Support**:
- Text modes: 40x25, 80x25 (both color and monochrome)
- CGA: 320x200x4, 640x200x2, composite color modes
- TGA: 160x200x16, 320x200x16, 640x200x16  
- EGA: 320x200x16, 640x200x16, 640x350x16
- VGA: 320x200x256, 640x480x16, 640x480x2
- Hercules: 720x348x2

### Audio System

**Sound Drivers** (`drivers/audio/`):
- Multi-platform audio abstraction (I2S, PWM, hardware DAC)
- Real-time audio mixing and output

**Sound Card Emulation** (`src/emulator/audio/`):
- PC Speaker/System Beeper
- Adlib/Sound Blaster (OPL2 FM synthesis via emu8950)
- MPU-401 MIDI with General MIDI synthesizer
- Covox Speech Thing, Disney Sound Source
- Tandy 3-voice (SN76489), Creative Music System

### Host Platform Graphics (MiniFB)

**MiniFB Implementation** (`src/MiniFB.h`, `src/WinMiniFB.c`, `src/LinuxMiniFB.c`):
- Unified framebuffer interface for Windows and Linux host builds
- Cross-platform API with same function signatures: `mfb_open()`, `mfb_update()`, `mfb_close()`
- 32-bit RGBA buffer rendering with optional scaling
- Palette support for indexed color modes
- Keyboard and mouse input handling with PC scancode translation
- FPS limiting and frame timing control

**Platform-Specific Implementations**:
- **Windows**: Uses Win32 API with `CreateWindow()`, `StretchDIBits()` for rendering
- **Linux**: Uses X11 with `XCreateWindow()`, `XPutImage()` for rendering

### Platform-specific Architectures
See `README.md` for a more detailed explanation.

**Pico (Dual-Core)**:
- **Core 0**: Runs the `exec86()` emulation loop and handles input.
- **Core 1**: Manages real-time tasks: rendering, audio, and PIT timer interrupts.

**Host - Windows & Linux (Multi-threaded)**:
- **Main thread**: Runs `exec86()` and manages the window.
- **Ticks thread**: Simulates hardware timers for interrupts, rendering, and audio generation.
- **Sound thread**: Handles audio output to the OS.

### Driver System

**Hardware Drivers** (`drivers/`):
- `ps2/` - PS/2 keyboard and mouse support
- `sdcard/` - SD card filesystem (FAT32 via FatFS)
- `psram/` - External PSRAM via SPI
- `nespad/` - NES gamepad for mouse emulation
- `hdmi/`, `vga-nextgen/`, `st7789/` - Display outputs
- `audio/` - Audio output drivers

**Driver Architecture**:
- Each driver has its own CMakeLists.txt and can be conditionally compiled.
- PIO (Programmable I/O) extensively used for hardware interfaces.
- Drivers provide hardware abstraction for the emulator core.
- **Note**: Default hardware pinouts are now documented in the main `README.md`.

## Development Workflow

### Testing
No automated test framework is present. Testing relies on running actual DOS software and games.

### Debugging
- Serial debug output available via printf (redirected on Pico)
- Win32/Linux builds support standard debugging tools
- Debug video RAM overlay for system messages
- Linux build outputs to terminal/console for debugging

### Memory Optimization
- Optimized for limited RAM environments  
- Virtual memory swapping available for RP2040
- PSRAM support for extended applications
- Aggressive compiler optimizations (`-O2`, `-flto`)

## Important Notes

- Build system enforces exactly one display and one audio option
- HDMI builds lock CPU frequency to 378MHz
- RP2350 builds have access to more RAM and faster operation
- Font and video data is embedded in headers, not external files

### Disk Image Paths:
- **Pico builds**: `\XT\fdd0.img`, `\XT\hdd.img` (on SD card)
- **Host builds**: `../fdd0.img`, `../hdd.img` (relative to executable, i.e., project root directory)

### Linux Build Requirements:
- X11 development libraries: `sudo apt install libx11-dev` (Ubuntu/Debian)
- pthread support (included with GCC)
- CMake 3.22+ and GCC 11+ for C++20 support