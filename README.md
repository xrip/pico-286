# Pico-286 Project

The Pico-286 project is an endeavor to emulate a classic PC system, reminiscent of late 80s and early 90s computers, on the Raspberry Pi Pico (RP2040 microcontroller). It aims to provide a lightweight and educational platform for experiencing retro computing and understanding low-level system emulation.

## Key Features

*   **8086/8088/80186/286 CPU Emulation:** At its core, the project emulates an Intel up to 286 processor.
*   **Retro Peripheral Emulation:** Includes support for common peripherals from the era.
*   **Text and Graphics Modes:** Supports various display modes common in early PCs.
*   **Sound Emulation:** Recreates sound capabilities of classic sound cards.
*   **Designed for Raspberry Pi Pico:** Optimized for the RP2040/RP2350 with minimal external components.

## Hardware Configuration

The Pico-286 emulator is designed to run on Raspberry Pi Pico (RP2040) based hardware.

*   **Minimal Configuration:**
    *   Raspberry Pi Pico (RP2040)
    *   External PSRAM chip (minimum 8MB recommended for broader compatibility) connected via SPI.

*   **Recommended Configuration for Maximum Performance:**
    *   Raspberry Pi Pico 2 (RP2350)
    *   QSPI PSRAM for faster memory access.

*   **Development Platform:**
    *   This project primarily uses the [MURMULATOR dev board](https://murmulator.ru) as its hardware base. This board provides an RP2040, PSRAM, and various peripherals suitable for the emulator's development and testing.

## Supported Hardware Emulations

### Sound Card Emulations

The emulator supports the following sound card emulations:

*   Tandy 3-voice
*   Creative Music System (Gameblaster)
*   Sound Blaster
*   Adlib

### Graphical Adapter Emulations

The emulator supports the following graphical adapter emulations:

*   CGA (Color Graphics Adapter)
*   TGA (Targa Graphics Adapter)
*   VGA (Video Graphics Array)

## Getting Started

Information on how to build and run the Pico-286 emulator will be provided in a separate document or a Wiki page (link to be added).

## Contributing

Contributions to the Pico-286 project are welcome! Please refer to the `CONTRIBUTING.md` file (to be created) for guidelines.

## License

The Pico-286 project is typically released under an open-source license (e.g., MIT, GPL). The specific license details will be found in a `LICENSE` file in the repository. (To be confirmed by checking repository)
