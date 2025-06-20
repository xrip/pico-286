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
*   **🎹 Adlib / Sound Blaster (OPL2 FM Synthesis):** High-quality emulation of the Yamaha OPL2 chipset for classic FM music and sound effects.
*   **🔊 Sound Blaster (Digital Audio):** Support for Sound Blaster's digital sound capabilities, including DMA-based playback.
*   **🎼 MPU-401 (MIDI Interface with General MIDI Synthesizer):** Provides a MIDI interface and includes an integrated General MIDI (GM) software synthesizer, allowing playback of GM scores without external MIDI hardware. This is a key feature for many later DOS games.
*   **📢 Tandy 3-voice / PCjr (SN76489 PSG):** Emulation of the Texas Instruments SN76489 Programmable Sound Generator.
*   **🎮 Creative Music System / Game Blaster (CMS/GameBlaster):** Emulation of the dual Philips SAA1099 based sound card.

### 🖼️ Graphics Card Emulations

#### 🎨 CGA (Color Graphics Adapter)
The CGA emulation provides authentic IBM Color Graphics Adapter functionality, supporting the classic early PC graphics modes:

**📝 Text Modes:**
*   80×25 character text mode with 16 foreground and 8 background colors
*   40×25 character text mode with full color support

**🎮 Graphics Modes:**
*   **🌈 320×200×4 Colors:** Standard CGA graphics mode with selectable color palettes
    - 🎨 Palette 0: Black, Green, Red, Brown/Yellow
    - 🎨 Palette 1: Black, Cyan, Magenta, White
    - ✨ High/Low intensity variants for different color brightness
*   **⚫⚪ 640×200×2 Colors:** High-resolution monochrome mode (typically black and white)

**🌟 Special Features:**
*   **📺 Composite Color Mode (160×200×16):** Emulates the artifact colors produced by CGA when connected to composite monitors, creating additional color combinations through NTSC color bleeding effects
*   🎨 Authentic CGA color palette reproduction
*   📺 Support for both RGB and composite monitor output characteristics

#### 🖥️ TGA (Tandy Graphics Adapter)
The Tandy Graphics Adapter emulation recreates the enhanced graphics capabilities of Tandy 1000 series computers:

**🚀 Enhanced Graphics Modes:**
*   **🎨 160×200×16 Colors:** Low-resolution mode with full 16-color palette
*   **🌈 320×200×16 Colors:** Medium-resolution mode with 16 simultaneous colors from a larger palette
*   **✨ 640×200×16 Colors:** High-resolution mode with 16-color support

**🌟 Features:**
*   🎨 Enhanced color palette with more vibrant colors compared to CGA
*   🌈 Better color reproduction and smoother gradients
*   🔄 Backward compatibility with CGA modes
*   🎮 Support for Tandy-specific software and games

#### 🖼️ VGA (Video Graphics Array)
The VGA emulation provides comprehensive Video Graphics Array support with multiple advanced modes:

**📊 Standard VGA Modes:**
*   **🎮 320×200×256 Colors:** Mode 13h - the famous "Mode X" used by many DOS games
*   **🖥️ 640×480×16 Colors:** Standard VGA high-resolution mode
*   **📺 720×480×16 Colors:** Extended VGA mode
*   **📝 Text modes:** 80×25 and 80×50 with enhanced character sets

**🚀 Advanced Features:**
*   **⚡ 320×200×256×4 (Mode X variant):** Optimized planar mode for faster rendering
*   🔧 Full VGA register compatibility for software that directly programs VGA hardware
*   🌈 256-color palette support with 18-bit color depth (262,144 possible colors)
*   📝 VGA-compatible text modes with enhanced fonts and character attributes
*   🖱️ Hardware cursor support with blinking capability

**🎨 Color Support:**
*   🌈 18-bit color depth allowing selection from 262,144 colors
*   🎨 256 simultaneous colors in graphics modes
*   ✨ Accurate VGA palette reproduction
*   🎬 Support for palette animation and color cycling effects

**⚙️ Technical Implementation:**
*   ⏱️ Authentic VGA timing and refresh rates
*   🔧 Compatible with VGA BIOS calls and direct register programming
*   🔄 Support for both text and graphics mode switching
*   🚀 Hardware-accelerated rendering optimized for RP2040/RP2350 architecture

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

Information on how to build and run the Pico-286 emulator will be provided in a separate document or a Wiki page (link to be added). 📚

## 🤝 Contributing

Contributions to the Pico-286 project are welcome! Please refer to the `CONTRIBUTING.md` file (to be created) for guidelines. 💪

## 📄 License

The Pico-286 project is typically released under an open-source license (e.g., MIT, GPL). The specific license details will be found in a `LICENSE` file in the repository. (To be confirmed by checking repository) ⚖️