# PC video modes

The output drivers come from `rp2040-ntsc-tv` commit `611c4f8`. PC mode readers
keep the old layouts from `519a857001220a54ed0c9224be1d741a84b362a1`.

PC VRAM is 65,536 **32-bit words**. CGA, Tandy, text, and linear VGA bytes live
in the low byte of each word. EGA planes and Mode X use all four bytes. Mode
changes do not clear VRAM. Page, bank, cursor, and blink state are read per line.

| Mode | Size and colors | Source layout |
|---|---|---|
| 00, 01 | 40x25 BW/color text | Character and attribute in two words |
| 02, 03 | 80x25 BW/color text | Character and attribute in two words |
| 04, 05 | CGA 320x200, 4 colors | 2 bits/pixel; two 8 KB banks |
| 06 | CGA 640x200, 2 colors | 1 bit/pixel; two 8 KB banks |
| 07 | Hercules 640x480 | Four banks, 90-word pitch, first 640 of 720 pixels |
| 1E | Hercules 640x348 | Same banks; five-word crop from 720 pixels |
| 08 | Tandy 160x200, 16 colors | Two nibbles/word; two banks |
| 09 | Tandy 320x200, 16 colors | Two nibbles/word; four banks |
| 0A | Tandy 640x200, 16 colors | Two nibbles/word; 320-word pitch |
| 0D | EGA 320x200, 16 colors | Four plane bytes/word; 40-word pitch |
| 0E | EGA 640x200, 16 colors | Four plane bytes/word; 80-word pitch |
| 10 | EGA 640x350, 16 colors | Four plane bytes/word; 80-word pitch |
| 11 | VGA 640x480, 2 colors | 1 bit/pixel; 80-word pitch |
| 12 | VGA 640x480, 16 colors | Four plane bytes/word; 80-word pitch |
| 13 | VGA 320x200, 256 colors | One low-byte pixel/word |
| FF | VGA Mode X 320x200, 256 colors | Four adjacent pixels/word |
| 74, 76 | CGA composite color | RGB: 160-pixel nibble colors; NTSC: raw CGA 2-bit signal |

## Output size and text

HSTX HDMI and VGA give a 640x480 canvas. The 200-row modes double each row;
348/350/480-row modes keep their source rows. Low-resolution pixels double in
hardware where possible. Unused rows are black. All 480 rows are usable: the
old debug display no longer covers the bottom 80 rows.

VGA keeps the old active CRTC profiles: EGA 640x350 uses 449 total lines and
VSync at 387..388; other PC modes use 525 total lines and VSync at 490..491.
Both have 800 samples per line and a 25.175 MHz pixel clock. Changes take
effect at frame start. NTSC keeps its own timing. The old disabled 400-line
profile is not enabled.

VGA also uses the existing `src/emulator/video/mc6845.h` model for CRTC text
layout, following the approach in RP8086. R1 sets pitch, R6/R9 set text height,
R12/13 set the display start, and R14/15 set the hardware cursor. Layout is
cached on register writes. Standard text keeps the measured fast path;
custom layouts use a bounded renderer. This is CGA/MC6845 behavior, not a full
EGA/VGA CRTC model. The RP8086 R8 interlace shortcut is not used.

HDMI and VGA text use an 8x16 font, with doubled cell width for 40 columns.
All four PC text modes have colors, blink, paging, and a cursor. CGA cursor
rows 0..7 are doubled; rows 8..15 use the native 16-line cell. The emulator's
default cursor at rows 12/13 is visible.

NTSC gives 640 samples across 240 active rows. High-resolution modes sample
every other source row. Forty-column text keeps color; 80-column text uses
luminance for clear fine detail. TFT gives 320x240: wide graphics sample every
other pixel, and 80-column text combines glyph-bit pairs so thin strokes stay
visible.

HSTX has all 256 palette entries. PIO HDMI has 252 color entries and four
control codes. Its fast Mode X path keeps the old valid range 0..251; indices
252..255 remain reserved. Linear mode 13 maps those four values to black.
PIO 640-wide modes use the 16-color
path. HSTX is the default M2P2 HDMI path; PIO is kept for RP2040 and is
available on M2P2 through the explicit `m2p2-hdmi-pio` comparison preset.

## Dual output and board resources

HDMI+NTSC, VGA+NTSC, and TFT+NTSC read the same PC VRAM. There is no full-screen
copy or second framebuffer. Palette changes reach both outputs. NTSC raw CGA
and RGB artifact colors have separate mapping. The primary output owns the
emulated raster status.

M2P2 uses HDMI/VGA GPIO12..19, NTSC GPIO22, and QMI PSRAM CS GPIO8 on RP2350A
(GPIO47 on RP2350B). `ONBOARD_PSRAM=ON` requires at least 8 MB and omits the
176 KB SRAM fallback cache. SPI PSRAM on GPIO18/19 cannot share the video pins.
HSTX-only uses a 500 MHz system clock; builds with NTSC use 315 MHz.
HSTX takes PLL_USB at 126 MHz independently of the CPU in both profiles.
USB and ADC clocks are stopped, so USB diagnostics and USB-triggered BOOTSEL are
unavailable. Saved CPU settings cannot change this clock plan.
Hardware sound on GPIO22 needs a
different NTSC pin.

## Checks and speed

`tests/graphics_modes_test.c` checks all modes, packing, planes, banks, text,
blink, cursor, bounds, offsets, and fused output formats.
`tests/hdmi_codec_test.c` checks palette storage and repeated-row DMA lifetime.
`tests/graphics-build` checks that the RP2040 PIO driver builds in isolation.

`tests/graphics-perf` builds a board test with old and new driver loops at
315 MHz. Its README gives the test scope and limits. Host tests check pixel
output, not target speed. Kernel cycle counts alone do not prove dual-output
deadlines under emulator and DMA load.

The new paths write palette values straight into DMA line buffers. VGA text
keeps its pair lookup. Doubled VGA/HDMI rows reuse the prior line. TFT uses two
small RGB565 line buffers and DMA. The optional compact NTSC palette uses a
slower staging path; M2P2 presets use the full palette path.

Board results and HDMI capture images are in
[the test results](tests/graphics-perf/results/README.md). These include the
initial slow version, the corrected version, and the exact firmware hashes.
