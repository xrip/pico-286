# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Pico-286 is a PC emulator targeting Intel 8086/8088/80186/286 CPUs, designed to run on the RP2350 (M2P2 board: HSTX HDMI with HDMI audio, or HSTX VGA, 8 MB onboard QMI PSRAM). The CPU is being upgraded to a 386SX: see `386-migration.md`. The project emulates classic PC hardware including various graphics cards (CGA, TGA, EGA, VGA), sound cards (PC Speaker, Adlib, Sound Blaster, MPU-401), and peripherals.

## Build System

The project uses CMake with two targets. There is only one firmware configuration: RP2350 M2P2 with HSTX HDMI and HDMI audio (or HSTX VGA, found at boot or set by `VIDEO=VGA|HDMI` in `config.286`) and 8 MB onboard QMI PSRAM. VGA has no sound until a PWM/I2S output is added. RP2040, PIO VGA, TFT, NTSC, PIO HDMI, PWM/I2S/hardware sound, SPI PSRAM and the swap file were removed.

```bash
# RP2350 firmware (M2P2)
cmake --preset m2p2
cmake --build --preset m2p2

# Windows/Linux host build (Linux requires X11 development libraries)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=host
cmake --build build -j
```

Options: `MIDI_GM_DLS=ON` (GM.DLS wavetable MIDI, RP2350 and Win32), `MIDI_WINMM=ON` (Win32 only: MIDI to the Windows MIDI device). See `BUILDING.md`.

## Architecture

### Core Components

**Emulator Core** (`src/emulator/`):
- `cpu.c/cpu.h` - x86 CPU emulation (386SX); `cpu386.c.inl` 32-bit instructions, `protected.c.inl` protected mode, paging, V86
- `memory.c` - Memory management and addressing 
- `ports.c` - I/O port handling
- `emulator.h` - Main emulator definitions and interfaces

**Platform Abstraction**:
- `src/pico-main.c` - Raspberry Pi Pico entry point and hardware initialization
- `src/win32-main.cpp` - Windows host build entry point with MiniFB graphics
- `src/linux-main.cpp` - Linux host build entry point with X11-based MiniFB graphics

**Memory Layout**:
- `RAM[RAM_SIZE]` - 640 KB conventional memory.
- `VIDEORAM[64KB]` - Video memory buffer at 0xA0000-0xC0000.
- On the RP2350, RAM, UMB (C0000-FBFFF), HMA and XMS (7 MB) are all in the memory-mapped onboard PSRAM (`.psram` section). There is no EMS board: a 386 gets EMS from EMM386.

### Video System

**Graphics Drivers** (`drivers/graphics/`, `drivers/hstx-vga-hdmi-audio/`):
- Shared PC mode renderers (`graphics_modes.c`) and text (`graphics_text.c`)
- HSTX output on GPIO12-19: HDMI with audio data islands, or VGA (RGB222 resistor DAC, colors by 4-step PWM) (`drivers/hstx-vga-hdmi-audio/`)
- Fonts (4x6, 6x8, 8x8, 8x16)

**Video Mode Support**:
- Text modes: 40x25, 80x25 (both color and monochrome)
- CGA: 320x200x4, 640x200x2, composite color modes
- TGA: 160x200x16, 320x200x16, 640x200x16  
- EGA: 320x200x16, 640x200x16, 640x350x16
- VGA: 320x200x256, 640x480x16, 640x480x2
- Hercules: 720x348x2

### Audio System

**Sound output**: on the RP2350 the mixed samples go over HDMI (`drivers/hstx-vga-hdmi-audio/hstx_audio.c`) at 44,100 Hz.

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
- `sdcard/`, `fatfs/` - SD card filesystem (FAT32 via FatFS)
- `nespad/` - NES gamepad for mouse emulation
- `graphics/`, `hstx-vga-hdmi-audio/` - HSTX HDMI or VGA video, HDMI audio
- `flash-config/` - M2P2 flash Quad Enable and timing setup

Each driver has its own CMakeLists.txt. Pinouts are in `CMakeLists.txt` and `README.md`.

## Development Workflow

### Testing
Host tests are in `tests/` (each file gives its build line): CPU (`sst_test.c` with SingleStepTests, `test386_test.c` with the test386.asm ROM, `cpu_bench.c`), sound (`sb_test.c`, `mpu401_test.c`), chips (`i82_controllers_test.c`), memory and XMS (`xms_test.c`), video (`graphics_modes_test.c`, `graphics_raster_test.c`, `hstx_packet_test.c`, `vga_palette_port_test.c`). Real DOS software is the final check.

### Debugging
- Serial debug output available via printf (redirected on Pico)
- Win32/Linux builds support standard debugging tools
- Debug video RAM overlay for system messages
- Linux build outputs to terminal/console for debugging

### Memory Optimization
- Hot code in SRAM (`__not_in_flash`); PSRAM and flash share the 16 KB XIP cache
- Aggressive compiler optimizations (`-O2`, `-flto`)

## Important Notes

- The RP2350 runs at 500 MHz; HSTX uses a separate 126 MHz PLL_USB, so USB is stopped
- At least 8 MB onboard PSRAM is required; the firmware stops at boot without it
- Font and video data is embedded in headers, not external files

### Disk Image Paths:
- **RP2350 firmware**: `\XT\fdd0.img`, `\XT\hdd.img` (on SD card)
- **Host builds**: `../fdd0.img`, `../hdd.img` (relative to executable, i.e., project root directory)

### Linux Build Requirements:
- X11 development libraries: `sudo apt install libx11-dev` (Ubuntu/Debian)
- pthread support (included with GCC)
- CMake 3.22+ and GCC 11+ for C++20 support

## AI Team Configuration

This project benefits from using specialized AI agents for different development tasks:

**Primary Agents:**
- `backend-developer` - For core emulator logic, CPU emulation, memory management, and platform-specific code
- `performance-optimizer` - Critical for this resource-constrained embedded system; use when addressing memory usage, CPU performance, or timing issues
- `code-archaeologist` - Essential for understanding the complex emulator architecture and legacy x86 instruction handling

**Secondary Agents:**
- `documentation-specialist` - For updating README.md, hardware documentation, and API references
- `code-reviewer` - For security and correctness review of memory management and hardware interface code
- `project-analyst` - For analyzing new subsystems or understanding driver interactions