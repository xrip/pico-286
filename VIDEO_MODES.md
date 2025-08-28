# Supported Video Modes

This document describes the video modes supported by the emulator, along with their restrictions.

## Text Modes

* **TEXTMODE_40x25_COLOR**: 40x25 text mode with 16 colors.
* **TEXTMODE_40x25_BW**: 40x25 text mode with 2 colors (black and white).
* **TEXTMODE_80x25_COLOR**: 80x25 text mode with 16 colors.
* **TEXTMODE_80x25_BW**: 80x25 text mode with 2 colors (black and white).

**Restrictions:**

* The font is fixed to an 8x16 font for 80-column modes and an 8x8 font for 40-column modes.
* Blinking text is supported, but the blink rate is fixed.
* The cursor is supported, but its blink rate is fixed.

## Graphics Modes

* **CGA_320x200x4**: 320x200 with 4 colors. The palette is one of the standard CGA palettes.
* **CGA_320x200x4_BW**: 320x200 with 2 colors (black and white).
* **CGA_640x200x2**: 640x200 with 2 colors (black and a configurable foreground color).
* **HERC_640x480x2**: 640x480 with 2 colors (black and white). This is a Hercules-compatible mode.
* **HERC_640x480x2_90**: 640x348 with 2 colors (black and white). This is a Hercules-compatible mode with a 90-column text mode layout.
* **TGA_160x200x16**: 160x200 with 16 colors. This is a Tandy Graphics Adapter (TGA) mode.
* **TGA_320x200x16**: 320x200 with 16 colors. This is a TGA mode.
* **TGA_640x200x16**: 640x200 with 16 colors. This is a TGA mode.
* **VGA_320x200x256**: 320x200 with 256 colors. This is a standard VGA mode (Mode 13h).
* **VGA_320x200x256x4**: 320x200 with 256 colors, using a planar memory layout. This is a VGA "Mode X" style mode.
* **VGA_640x480x2**: 640x480 with 2 colors. This is a standard VGA mode.
* **EGA_320x200x16x4**: 320x200 with 16 colors, using a planar memory layout. This is an EGA-compatible mode.
* **COMPOSITE_160x200x16**: 160x200 with 16 colors, emulating the color composite output of a CGA card.
* **COMPOSITE_160x200x16_force**: 160x200 with 16 colors, forcing the composite color mode even if the BIOS does not request it.

**Implementation Details & Restrictions**

The video signal generation is handled by `dma_handler_VGA` and has the following characteristics and limitations:

*   **Hardware Dependency:** The implementation is specific to the Raspberry Pi Pico and its PIO/DMA capabilities. It is not thread-safe and assumes a specific system clock frequency.
*   **Resolution Limits:**
    *   Maximum visible resolution is 640x480. Video modes with a higher vertical resolution will be cropped.
    *   Maximum horizontal resolution is 640 pixels. Video modes with a higher horizontal resolution will be cropped.
*   **Refresh Rate:** Fixed at 60Hz.
*   **Memory Layout:** Each video mode has a fixed, non-configurable video memory layout.
*   **Palette:** The color palette is fixed for most modes. Only TGA and VGA modes allow for a programmable palette.
*   **Special Modes:**
    *   `HERC_640x480x2_90`: Uses a non-standard 90-column text mode memory layout.
    *   Composite modes: An approximation of real CGA hardware; colors may not be an exact match.
    *   VGA "Mode X": This is a custom mode, not a standard VGA mode, that uses a planar memory layout.
*   **Debug Console:** A debug console may be displayed at the bottom of the screen, which is not part of the emulated video output.

## NTSC TV Output

The NTSC TV output mode generates a composite video signal.

*   **Resolution**: 320x200 pixels.
*   **Color**: 256-color programmable palette.
*   **Signal**: Composite video signal generated via PWM on GPIO 22.
*   **Text Mode**: 40x25 text mode is supported.

**Implementation Details & Restrictions**

*   The NTSC signal generation is highly dependent on the RP2040's PIO, DMA, and a specific system clock frequency (315MHz).
*   The refresh rate is ~60Hz (NTSC standard).
