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

# Windows host build  
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

**Memory Layout**:
- `RAM[RAM_SIZE]` - Main system RAM (size varies by platform: 116-350KB)
- `VIDEORAM[64KB]` - Video memory buffer at 0xA0000-0xC0000
- External PSRAM or virtual memory for extended memory (EMS/XMS)

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

### Driver System

**Hardware Drivers** (`drivers/`):
- `ps2/` - PS/2 keyboard and mouse support
- `sdcard/` - SD card filesystem (FAT32 via FatFS)
- `psram/` - External PSRAM via SPI
- `nespad/` - NES gamepad for mouse emulation
- `hdmi/`, `vga-nextgen/`, `st7789/` - Display outputs
- `audio/` - Audio output drivers

**Driver Architecture**:
- Each driver has its own CMakeLists.txt and can be conditionally compiled
- PIO (Programmable I/O) extensively used for hardware interfaces
- Drivers provide hardware abstraction for the emulator core

### Dual-core Architecture (Pico builds)

**Core 0**: CPU emulation main loop
- Runs `exec86()` with configurable throttling
- Handles keyboard/gamepad input 
- Mouse emulation via NES gamepad when no PS/2 mouse detected

**Core 1**: Real-time rendering and audio
- Graphics rendering at ~60Hz
- Audio sample generation and output
- Timer interrupt generation
- Hardware interrupt handling

## Development Workflow

### Testing
No automated test framework is present. Testing relies on running actual DOS software and games.

### Debugging
- Serial debug output available via printf (redirected on Pico)
- Win32 build supports standard debugging tools
- Debug video RAM overlay for system messages

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
- Emulator expects disk images in specific paths: `\XT\fdd0.img`, `\XT\hdd.img`