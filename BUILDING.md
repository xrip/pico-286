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

### Display Options (mutually exclusive)
- `ENABLE_VGA=ON`: Use VGA output.
- `ENABLE_HDMI=ON`: Use HDMI output.
- `ENABLE_TFT=ON`: Use TFT display output.

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
The build system sets a `PICO_BOARD` variable to `pico2` internally. This is a low-level setting for the Pico SDK and is not intended to be changed by the user. The primary way to select the target hardware is via the `PICO_PLATFORM` variable (`rp2040` or `rp2350`).
