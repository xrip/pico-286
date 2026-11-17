# Building the Project

There are two kinds of build:
- the **RP2350 firmware** for the M2P2 board: HSTX HDMI video, HDMI audio and 8 MB onboard QMI PSRAM. This is the only supported board configuration;
- the **host build** (Windows/Linux), used for development and the tests.

## Prerequisites

### All Builds
- **CMake** 3.22 or higher, and **Git**.

### Host Build (Linux)
- **C/C++ Compiler**: `gcc`/`g++` (or `clang`). Install with `sudo apt-get install build-essential`.
- **X11 Libraries**: Required for the display. Install with `sudo apt-get install libx11-dev`.

### Host Build (Windows)
- **MinGW GCC** (for example the one that comes with CLion), with the `MinGW Makefiles` generator.

### RP2350 Firmware
- **Pico SDK**, with the `PICO_SDK_PATH` environment variable set to it.
- **ARM GCC toolchain** and **Ninja**. A compiler outside PATH may be given with `-DPICO_TOOLCHAIN_PATH=/path/to/arm-toolchain`.
- A **host C++ compiler** in PATH: the SDK builds `pioasm` and `picotool` with it.

## Host Build (Linux/Windows)

```sh
# Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=host
cmake --build build -j
# Windows (MinGW)
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=host
cmake --build build -j
```

The executable goes to `bin/host/<build type>/`.

## RP2350 Firmware

```sh
cmake --preset m2p2
cmake --build --preset m2p2
```

Output: `bin/rp2350-arm-s/Release/286-P166-C500MHz.uf2`. There are no display, audio or memory options. Other configurations (RP2040, VGA, TFT, NTSC, PIO HDMI, PWM/I2S/hardware sound, SPI PSRAM, swap file) are no longer supported; CMake stops for any platform other than `rp2350-arm-s` and `host`.

Notes on the M2P2 firmware:
- **Clocks:** SYS runs at 500 MHz (1.60 V). HSTX takes its 126 MHz clock from a separate PLL_USB, so USB and ADC are stopped. USB debugging and USB-triggered BOOTSEL are not available; use the board's BOOTSEL button for the next flash.
- **Sound:** 44.1 kHz 16-bit stereo over HDMI data islands. The HDMI clock sets the rate.
- **PSRAM:** at least 8 MB onboard QMI PSRAM is required; the firmware stops at boot without it. Conventional RAM, UMB, HMA, EMS and XMS all live in PSRAM.
- **SRAM:** code, lookup tables and palettes use normal SRAM. Scratch X and Y are reserved for separate 4 KB core stacks, with hardware stack-limit checks. The linker rejects other scratch allocations.
- **OPL2:** the OPL2 assembly runs from normal SRAM (2 KB). Both flash and SRAM versions have faulted during HDMI interrupts at 504 MHz; the SRAM change is not a complete fix. See the [OPL2 board record](docs/board-records/hstx-opl-sram.md).
- **Flash:** every boot checks the flash Quad Enable bit before overclocking. If the bit is clear, the startup code sets and verifies it while running from RAM; if it is already set, no status-register write is made. The RP2350 ROM selects the initial flash read mode, then startup selects quad reads before PSRAM or core 1 starts. See [flash setup](drivers/flash-config/README.md). The flash timing keeps the ROM boot profile's physical intervals and maximum rate when SYS changes. On the tested board, 50 MHz stock flash becomes about 45.45 MHz at 500 MHz SYS.
- **16 MB flash:** the SDK settings and the linker script use 16 MB flash; the last 4 KB is reserved for CMOS. This requires a board with 16 MB physical flash.
- Earlier 504 MHz checks are in the [historical board record](docs/board-records/hstx-504-board.md). See [VIDEO_MODES.md](VIDEO_MODES.md) for layouts.

### Optional GM.DLS MIDI on RP2350 and Win32

`MIDI_GM_DLS=ON` selects the GM.DLS wavetable engine.
The default remains the sine/noise MIDI engine. GM.DLS waves are 22,050 Hz,
so MIDI is made at 22,050 Hz and each sample is sent twice to the 44,100 Hz output.
The packed bank must use 22,050 Hz; any other bank is rejected.

Pack your local `gm.dls` with a host C compiler:

```sh
cc -std=c11 -O2 tools/dls_pack.c -lm -o dls_pack
./dls_pack gm.dls src/emulator/audio/gm_bank_22050.bin 22050
```

Then build the firmware with GM.DLS:

```sh
cmake --preset m2p2 -DMIDI_GM_DLS=ON
cmake --build --preset m2p2
```

Output: `bin/rp2350-arm-s/Release/286-P166-C500MHz-GMDLS.uf2`.
`MIDI_GM_BANK` can point to another packed v4 bank at 22,050 Hz.
The bank stays in flash; the engine has 32 voices and preserves stereo pan.
MIDI commands go through a queue to the audio core. MPU-401 reset clears its voices.

### MIDI on Win32

The Win32 build (`-DPICO_PLATFORM=host`) has three MIDI choices:

| Options          | MIDI                                   | Output          |
|------------------|----------------------------------------|-----------------|
| (none)           | sine/noise engine                      | `286.exe`       |
| `MIDI_GM_DLS=ON` | GM.DLS wavetable from the packed bank  | `286-GMDLS.exe` |
| `MIDI_WINMM=ON`  | the Windows default MIDI output device | `286-WINMM.exe` |

The files go to `bin/host/<build type>/`. `MIDI_WINMM` and `MIDI_GM_DLS` cannot both be on.

## Technical Notes

### Boot Process
The firmware uses its own boot stage 2 (`slower_boot2`), built from the SDK default file. On M2P2 the ROM finds a working read mode; `main()` then enables QE and sets the quad-read timing from RAM before raising the system clock.

### `PICO_BOARD` Variable
The `m2p2` preset sets `PICO_BOARD=pico2` and `PICO_PLATFORM=rp2350-arm-s` before the SDK is loaded.
