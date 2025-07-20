# 🕹️ Pico-286 Project

The Pico-286 project is an endeavor to emulate a classic PC system, reminiscent of late 80s and early 90s computers, on the Raspberry Pi Pico (RP2040/RP2350 microcontroller). It aims to provide a lightweight and educational platform for experiencing retro computing and understanding low-level system emulation. 🖥️✨

## ⭐ Key Features

*   **🧠 8086/8088/80186/286 CPU Emulation:** At its core, the project emulates an Intel cpu up to 286 family.
*   **🌐 Cross-platform:** Can be built for Raspberry Pi Pico and Windows.
*   **🔌 Retro Peripheral Emulation:** Includes support for common peripherals from the era.
*   **🎨 Text and Graphics Modes:** Supports various display modes common in early PCs.
*   **🔊 Sound Emulation:** Recreates sound capabilities of classic sound cards.
*   **🍓 Designed for Raspberry Pi Pico:** Optimized for the RP2040/RP2350 with minimal external components.

## 🎮 Supported Hardware Emulations

### 🧠 CPU Emulation
*   Intel 8086/8088/80186/286 processor family

### 🎵 Sound Card Emulations
*   **📢 PC Speaker (System Beeper):** Authentic emulation of the original PC's internal speaker system
*   **🎚️ Covox Speech Thing:** Compatible emulation of the simple parallel port DAC
*   **🎭 Disney Sound Source (DSS):** Emulation of the popular parallel port digital audio device
*   **🎹 Adlib / Sound Blaster (OPL2 FM Synthesis):** High-quality emulation of the Yamaha OPL2 chipset for classic FM music and sound effects.
*   **🔊 Sound Blaster (Digital Audio):** Support for Sound Blaster's digital sound capabilities, including DMA-based playback.
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
    
#### 🖥️ TGA (Tandy Graphics Adapter)
The Tandy Graphics Adapter emulation recreates the enhanced graphics capabilities of Tandy 1000 series computers:

**🚀 Enhanced Graphics Modes:**
*   **🎨 160×200×16 Colors:** Low-resolution mode with full 16-color palette
*   **🌈 320×200×16 Colors:** Medium-resolution mode with 16 simultaneous colors from a larger palette
*   **✨ 640×200×16 Colors:** High-resolution mode with 16-color support

#### 🖼️ VGA (Video Graphics Array)
The VGA emulation provides comprehensive Video Graphics Array support with multiple advanced modes:

**📊 Standard VGA Modes:**
*   **🎮 320×200×256 Colors:** Mode 13h - the famous "Mode X" used by many DOS games
*   **🖥️ 640×480×16 Colors:** Standard VGA high-resolution mode
*   **📺 720×480×16 Colors:** Extended VGA mode
*   **📝 Text modes:** 80×25 and 80×50 with enhanced character sets

## 💾 Floppy and Hard Disks

The emulator supports up to two floppy disk drives (A: and B:) and up to two hard disk drives (C: and D:). Disk images are stored on the SD card.

The emulator expects the following file paths and names for the disk images:

*   **Floppy Drive 0 (A:):** `\\XT\\fdd0.img`
*   **Floppy Drive 1 (B:):** `\\XT\\fdd1.img`
*   **Hard Drive 0 (C:):** `\\XT\\hdd.img`
*   **Hard Drive 1 (D:):** `\\XT\\hdd2.img`

**Important Notes:**

*   The disk type (floppy or hard disk) is determined by the drive number it is assigned to in the emulator, not by the filename itself.
*   The emulator automatically determines the disk geometry (cylinders, heads, sectors) based on the size of the image file. Ensure your disk images have standard sizes for floppy disks (e.g., 360KB, 720KB, 1.2MB, 1.44MB) for proper detection. For hard disks, the geometry is calculated based on a standard CHS (Cylinder/Head/Sector) layout.

## 🔧 Hardware Configuration

The Pico-286 emulator is designed to run on Raspberry Pi Pico (RP2040) based hardware. 🍓

### 🎛️ Supported Components
*   ⌨️ PS/2 keyboard and mouse
*   💾 SD card for storage
*   📺 VGA and HDMI for video output
*   🔊 Audio output
*   🎮 NES gamepad

### 🏗️ Minimal Configuration
*   🍓 Raspberry Pi Pico (RP2040)
*   🧠 External PSRAM chip (minimum 8MB recommended for broader compatibility) connected via SPI.

### 🚀 Recommended Configuration for Maximum Performance
*   🍓 Raspberry Pi Pico 2 (RP2350)
*   ⚡ QSPI PSRAM for faster memory access.

### 🛠️ Development Platform
*   This project primarily uses the [MURMULATOR dev board](https://murmulator.ru) as its hardware base. This board provides an RP2040, PSRAM, and various peripherals suitable for the emulator's development and testing. 🎯

## 🔨 Building and Getting Started

### 📋 Prerequisites

#### For Raspberry Pi Pico builds:
*   **Pico SDK:** Install and configure the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
*   **CMake:** Version 3.22 or higher
*   **ARM GCC Toolchain:** For cross-compilation to ARM Cortex-M0+/M33
*   **Git:** For cloning the repository and submodules

#### For Windows/Linux host builds:
*   **CMake:** Version 3.22 or higher  
*   **GCC/Clang/MSVC:** C++20 compatible compiler
*   **Git:** For cloning the repository

### 🛠️ Build Configuration

The project uses CMake with platform-specific configurations. All builds require exactly **one display option** and **one audio option**.

#### 🖥️ Display Options (Choose exactly one):
*   `ENABLE_TFT=ON` - TFT display output via ST7789
*   `ENABLE_VGA=ON` - VGA output  
*   `ENABLE_HDMI=ON` - HDMI output (locks CPU frequency to 378MHz)

#### 🔊 Audio Options (Choose exactly one):
*   `ENABLE_I2S_SOUND=ON` - I2S digital audio output
*   `ENABLE_PWM_SOUND=ON` - PWM audio output
*   `ENABLE_HARDWARE_SOUND=ON` - Hardware DAC audio output

#### 🧠 Memory Configuration:
*   **PSRAM (Default for RP2350):**
    - `ONBOARD_PSRAM=ON` - Use onboard PSRAM (RP2350 only)
    - `ONBOARD_PSRAM_GPIO=19` - GPIO pin for onboard PSRAM
*   **Virtual Memory:** 
    - `TOTAL_VIRTUAL_MEMORY_KBS=512` - Enable virtual memory instead of PSRAM
*   **CPU Frequency:**
    - `CPU_FREQ_MHZ=378` - Set CPU frequency (default varies by platform)

### 🚀 Build Commands

#### Raspberry Pi Pico 2 (RP2350) - Recommended:
```bash
# Clone the repository
git clone <repository-url>
cd pc

# Create build directory
mkdir build && cd build

# Configure for RP2350 with VGA and PWM audio
cmake -DCMAKE_BUILD_TYPE=Release \
      -DPICO_PLATFORM=rp2350 \
      -DENABLE_VGA=ON \
      -DENABLE_PWM_SOUND=ON \
      ..

# Build
make -j$(nproc)
```

#### Raspberry Pi Pico (RP2040):
```bash
# Configure for RP2040 with TFT and I2S audio
cmake -DCMAKE_BUILD_TYPE=Release \
      -DPICO_PLATFORM=rp2040 \
      -DENABLE_TFT=ON \
      -DENABLE_I2S_SOUND=ON \
      ..

# Build  
make -j$(nproc)
```

#### Windows/Linux Host Build:
```bash
# Configure for host platform (development/testing)
cmake -DCMAKE_BUILD_TYPE=Release \
      -DPICO_PLATFORM=host \
      ..

# Build
make -j$(nproc)
# On Windows with Visual Studio: cmake --build . --config Release
```

### 🔧 Advanced Build Options

#### Memory-constrained RP2040 with Virtual Memory:
```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DPICO_PLATFORM=rp2040 \
      -DTOTAL_VIRTUAL_MEMORY_KBS=512 \
      -DENABLE_VGA=ON \
      -DENABLE_PWM_SOUND=ON \
      ..
```

#### High-performance RP2350 with HDMI:
```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DPICO_PLATFORM=rp2350 \
      -DENABLE_HDMI=ON \
      -DENABLE_I2S_SOUND=ON \
      -DONBOARD_PSRAM=ON \
      -DONBOARD_PSRAM_GPIO=19 \
      ..
```

### 📦 Build Outputs

After successful compilation, you'll find:

#### For Pico builds:
*   `286-<platform>-<frequency>-<display>-<audio>.uf2` - Firmware file for flashing
*   `286-<platform>-<frequency>-<display>-<audio>.elf` - ELF binary for debugging
*   `286-<platform>-<frequency>-<display>-<audio>.bin` - Raw binary

#### For host builds:
*   `286` (Linux) or `286.exe` (Windows) - Executable for testing

### 🎯 Flashing to Pico

1. **Hold the BOOTSEL button** while connecting your Pico to USB
2. **Copy the `.uf2` file** to the mounted RPI-RP2 drive  
3. **The Pico will automatically reboot** and start running the emulator

### 💾 Setting up Disk Images

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

### 🐛 Troubleshooting

**Build fails with "display/audio option required":**
- Ensure exactly one `ENABLE_*` option is set for both display and audio

**Out of memory errors on RP2040:**
- Try enabling virtual memory: `-DTOTAL_VIRTUAL_MEMORY_KBS=512`
- Use smaller disk images
- Disable unused emulation features

**HDMI not working:**
- Ensure CPU frequency is set to 378MHz (automatic with `ENABLE_HDMI=ON`)
- Check HDMI cable and display compatibility

### 📚 Additional Resources

*   **Hardware setup:** See `boards/` directory for reference designs
*   **Pin configurations:** Defined in `CMakeLists.txt` compile definitions
*   **Development board:** [MURMULATOR](https://murmulator.ru) recommended for development

## 🤝 Contributing

Contributions to the Pico-286 project are welcome! Please refer to the `CONTRIBUTING.md` file (to be created) for guidelines. 💪

## 📄 License

The Pico-286 project is typically released under an open-source license (e.g., MIT, GPL). The specific license details will be found in a `LICENSE` file in the repository. (To be confirmed by checking repository) ⚖️