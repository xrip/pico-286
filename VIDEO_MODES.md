# PC video modes

The output is HSTX HDMI on the RP2350. PC mode readers keep the old layouts
from `519a857001220a54ed0c9224be1d741a84b362a1`.

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
| 74, 76 | CGA composite color | 160-pixel nibble colors (artifact colors) |

## Output size and text

HSTX HDMI gives a 640x480 canvas. The 200-row modes double each row;
348/350/480-row modes keep their source rows. Low-resolution pixels double in
hardware where possible. Unused rows are black. All 480 rows are usable.

HDMI text uses an 8x16 font, with doubled cell width for 40 columns.
All four PC text modes have colors, blink, paging, and a cursor. CGA cursor
rows 0..7 are doubled; rows 8..15 use the native 16-line cell. The emulator's
default cursor at rows 12/13 is visible.

HSTX has all 256 palette entries.

## Board resources

M2P2 uses the HSTX pins GPIO12..19 for HDMI and the QMI PSRAM chip select
(GPIO8 on RP2350A, GPIO47 on RP2350B). At least 8 MB of onboard PSRAM is required.
The system clock is 500 MHz; HSTX takes PLL_USB at 126 MHz independently of the CPU.
USB and ADC clocks are stopped, so USB diagnostics and USB-triggered BOOTSEL are
unavailable. Saved CPU settings cannot change this clock plan.

## Checks

`tests/graphics_modes_test.c` checks all modes, packing, planes, banks, text,
blink, cursor, bounds, offsets, and the 16- and 32-bit output forms. Build from the root:

```sh
gcc -std=gnu17 -O2 -Itests/graphics-stubs -Idrivers/graphics -Idrivers/hdmi-hstx tests/graphics_modes_test.c drivers/graphics/graphics_modes.c drivers/graphics/graphics_text.c -o graphics_modes_test
```

`tests/hstx_packet_test.c` checks the HDMI line packets, `tests/graphics_raster_test.c` the emulated raster status.
Host tests check pixel output, not target speed.

The renderers write palette values straight into DMA line buffers, and
doubled rows reuse the prior line. HSTX board records are in
[docs/board-records](docs/board-records).
