# Building the Project

This document provides instructions for building the project for different target platforms: `host` (Linux/Windows), `rp2040`, and `rp2350`.

## Prerequisites

### All Builds
- **CMake**: A cross-platform build system generator. Version 3.22 or higher is required.
- **Git**: For cloning the repository.

### Host Build (Linux)
- **C/C++ Compiler**: `gcc`/`g++` (or `clang`). Install with `sudo apt-get install build-essential`.
- **X11 Libraries**: Required for the display. Install with `sudo apt-get install libx11-dev`.

### Host Build (Windows)
- **Visual Studio**: With the "Desktop development with C++" workload installed.
- **CMake**: Make sure `cmake` is in your system's PATH.

### Pico Builds (rp2040 & rp2350)
- **Pico SDK**: You need to have the Raspberry Pi Pico SDK installed. Follow the official documentation at https://github.com/raspberrypi/pico-sdk to set it up.
- **ARM GCC Toolchain**: The ARM embedded toolchain is required for cross-compilation.
- **PICO_SDK_PATH Environment Variable**: Make sure the `PICO_SDK_PATH` environment variable is set to the location of your Pico SDK installation.

## Build Instructions

First, clone the repository:
```sh
git clone <repository-url>
cd <repository-directory>
```

### 1. Host Build (Linux/Windows)

The host build runs the emulator on your PC.

1.  **Create a build directory:**
    ```sh
    mkdir build
    cd build
    ```

2.  **Configure CMake:**
    -   Pass `-DPICO_PLATFORM=host` to select the host build.
    -   On Windows, you might need to specify the generator (e.g., `"Visual Studio 17 2022"`).

    **Linux:**
    ```sh
    cmake .. -DPICO_PLATFORM=host
    ```

    **Windows (Command Prompt):**
    ```sh
    cmake .. -DPICO_PLATFORM=host -G "Visual Studio 17 2022" -A Win32
    ```

3.  **Build the project:**

    **Linux:**
    ```sh
    make
    ```

    **Windows:**
    ```sh
    cmake --build .
    ```

    The executable will be located in `bin/host/Debug` or `bin/host/Release`.

### 2. Pico Builds (rp2040 & rp2350)

These builds target the Raspberry Pi Pico boards. The following instructions create a build with VGA video and I2S audio output, as recommended for a simple default.

#### RP2040 Build

1.  **Create a build directory:**
    ```sh
    mkdir build-rp2040
    cd build-rp2040
    ```

2.  **Configure CMake:**
    -   Set `PICO_PLATFORM` to `rp2040`.
    -   Enable VGA and I2S sound.

    ```sh
    cmake .. -DPICO_PLATFORM=rp2040 -DENABLE_VGA=ON -DENABLE_I2S_SOUND=ON
    ```

3.  **Build the project:**
    ```sh
    make
    ```
    The output file (`.uf2`) will be in `bin/rp2040/Debug/`.

#### RP2350 Build

1.  **Create a build directory:**
    ```sh
    mkdir build-rp2350
    cd build-rp2350
    ```

2.  **Configure CMake:**
    -   Set `PICO_PLATFORM` to `rp2350`.
    -   Enable VGA and I2S sound.

    ```sh
    cmake .. -DPICO_PLATFORM=rp2350 -DENABLE_VGA=ON -DENABLE_I2S_SOUND=ON
    ```

3.  **Build the project:**
    ```sh
    make
    ```
    The output file (`.uf2`) will be in `bin/rp2350/Debug/`.

## Build Options

You can customize the build by enabling different options in the CMake command.

### Display Options
- `ENABLE_NTSC-TV=ON`: Use NTSC TV output.
- `ENABLE_VGA=ON`: Use VGA output.
- `ENABLE_HDMI=ON`: Use HDMI output.
- `ENABLE_TFT=ON`: Use TFT display output.

One of HDMI, VGA, or TFT may run with `ENABLE_NTSC-TV=ON`. NTSC may also run
alone. M2P2 HDMI selects HSTX; RP2040 HDMI keeps the PIO driver. HDMI+NTSC
requires M2P2 HSTX.

M2P2 HDMI always uses HSTX. An M2P2 HDMI build for a platform other than
`rp2350-arm-s` is rejected; it cannot silently select PIO. RP2040 keeps PIO HDMI.

For M2P2 with at least 8 MB onboard QMI PSRAM, use the presets:

```sh
cmake --preset m2p2-hdmi-ntsc
cmake --build --preset m2p2-hdmi-ntsc
```

Other presets: `m2p2-hdmi`, `m2p2-vga`, `m2p2-vga-ntsc`, `m2p2-tft`,
`m2p2-tft-ntsc`, and `m2p2-ntsc`. They use PWM sound and NTSC GPIO22.
Set `PICO_SDK_PATH` and make the ARM compiler, Ninja, and a host C++ compiler
available. A compiler outside PATH may be given with
`-DPICO_TOOLCHAIN_PATH=/path/to/arm-toolchain` at configure time.

These presets set `ONBOARD_PSRAM=ON` and omit the SRAM fallback cache.
`m2p2-hdmi` builds HSTX-only at 504 MHz (1.60 V). HSTX+NTSC stays at 315 MHz
(1.30 V). HDMI-only takes its 126 MHz HSTX clock from PLL_SYS / 4 and keeps
PLL_USB at 48 MHz for USB. HSTX+NTSC reserves PLL_USB at 126 MHz and stops USB/ADC.

On RP2350, the OPL2 assembly runs from normal SRAM (2 KB), with its logic
unchanged. Both flash and SRAM versions have faulted during HDMI interrupts
at 504 MHz; the SRAM change is not a complete fix. See the
[OPL2 board record](tests/graphics-perf/results/hstx-opl-sram.md).

Every M2P2 build checks the flash Quad Enable bit before overclocking. If the
bit is clear, the startup code sets and verifies it while running from RAM;
if it is already set, no status-register write is made. The RP2350 ROM selects
the initial flash read mode, then startup selects quad reads before
PSRAM or core 1 starts. See [flash setup](drivers/flash-config/README.md).
The flash timing now preserves the ROM boot profile's physical intervals
and maximum rate when SYS changes. On the tested board, 50 MHz stock flash
becomes 45.82 MHz at 504 MHz SYS. This also retains the ROM QSPI pad settings.

For the HDMI-only file:

```sh
cmake --preset m2p2-hdmi
cmake --build --preset m2p2-hdmi
```

Output: `bin/rp2350-arm-s/Release/m2p2-286-F100-P166-504MHz-HDMI-HSTX-PWM.uf2`.
The 504 MHz build passed a boot and display check on the user's Frank/M2P2
board; see the [board record](tests/graphics-perf/results/hstx-504-board.md).
This does not establish long-term stability on every RP2350 module.
See [VIDEO_MODES.md](VIDEO_MODES.md) for layouts and the board timing test.

### Audio Options (mutually exclusive)
- `ENABLE_I2S_SOUND=ON`: Use I2S for audio.
- `ENABLE_PWM_SOUND=ON`: Use PWM for audio.
- `ENABLE_HARDWARE_SOUND=ON`: Use a hardware sound chip.

Example of a different configuration (HDMI for rp2040):
```sh
cmake .. -DPICO_PLATFORM=rp2040 -DENABLE_HDMI=ON -DENABLE_I2S_SOUND=ON
```

## Technical Notes

### Boot Process
The project uses a custom boot stage 2 (`slower_boot2`) for Pico builds. This configuration slows down the flash clock divider, which can improve system stability, especially on startup.

### `PICO_BOARD` Variable
Set `PICO_BOARD=pico` for RP2040 or `PICO_BOARD=pico2` for RP2350 before the SDK
is loaded. The M2P2 presets set `pico2` and `PICO_PLATFORM=rp2350-arm-s`.
