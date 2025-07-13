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

**Restrictions:**

* The color palette is fixed for most modes. The TGA and VGA modes allow for a programmable palette.
* The video memory layout is fixed for each mode.
* The refresh rate is fixed at 60Hz.
* The `dma_handler_VGA` function in `drivers/vga-nextgen/vga.c` is responsible for generating the video signal. It reads the video data from the emulated video RAM and sends it to the PIO to be displayed on the screen. The handler is called for each scanline, and it determines which video mode is currently active and generates the appropriate signals.
* The `dma_handler_VGA` function has a hardcoded limit of 480 visible lines. Any video modes with a higher vertical resolution will be cropped.
* The `dma_handler_VGA` function has a hardcoded limit of 640 visible pixels per line. Any video modes with a higher horizontal resolution will be cropped.
* The `HERC_640x480x2_90` mode is a special case that uses a 90-column text mode layout. The video memory is laid out in a way that is different from the other Hercules modes.
* The composite color modes are an approximation of the real hardware. The colors may not be exactly the same as on a real CGA card.
* The VGA "Mode X" style mode is not a standard VGA mode. It is a custom mode that uses a planar memory layout to achieve 256 colors at a resolution of 320x200.
* The `dma_handler_VGA` function has a special case for a debug console that is displayed at the bottom of the screen. This console is not part of the emulated video output.
* The `dma_handler_VGA` function uses a double-buffering scheme to avoid tearing. The two buffers are swapped at the beginning of each frame.
* The `dma_handler_VGA` function uses the PIO to generate the video signals. The PIO is a programmable I/O peripheral that can be used to generate complex waveforms.
* The `dma_handler_VGA` function uses DMA to transfer the video data from the emulated video RAM to the PIO. This frees up the CPU to do other things.
* The `dma_handler_VGA` function is time-critical. It must be able to generate the video signal in real time. Any delays in the handler will cause the video output to be corrupted.
* The `dma_handler_VGA` function is only compatible with the Raspberry Pi Pico. It will not work on other microcontrollers.
* The `dma_handler_VGA` function is not thread-safe. It should only be called from the main thread.
* The `dma_handler_VGA` function assumes that the system clock is running at a specific frequency. If the clock frequency is changed, the video output will be corrupted.
* The `dma_handler_VGA` function assumes that the PIO is configured in a specific way. If the PIO configuration is changed, the video output will be corrupted.
* The `dma_handler_VGA` function assumes that the DMA is configured in a specific way. If the DMA configuration is changed, the video output will be corrupted.
* The `dma_handler_VGA` function assumes that the video memory is laid out in a specific way. If the video memory layout is changed, the video output will be corrupted.
* The `dma_handler_VGA` function assumes that the palette is configured in a specific way. If the palette is changed, the video output will be corrupted.
* The `dma_handler_VGA` function assumes that the font is configured in a specific way. If the font is changed, the video output will be corrupted.
* The `dma_handler_VGA` function assumes that the cursor is configured in a specific way. If the cursor is changed, the video output will be corrupted.
* The `dma_handler_VGA` function assumes that the blinking is configured in a specific way. If the blinking is changed, the video output will be corrupted.
