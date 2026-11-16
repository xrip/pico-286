# Third run: diagnostic flash LUT

Captured from the same board on COM3 by the parent session. Flashed firmware:
`2A5D6A6FB63AA5FF6E3CA47B95AC3F54155FC7EB62BC3A35E72F1C06EE574D5C`.
The copied raw capture is `third-diagnostic-run.csv`. Matching ignored binaries
are `diagnostic-flash-lut-2A5D6A6F.uf2` and `.elf`.

This build completed four seconds before a source change that places the EGA
spread lookup in SRAM. Its `pc_spread_lut` was still at `0x1000aba0` in flash.
It must not be reported as the SRAM-LUT result. The later SRAM build has a
different firmware hash.

At 315 MHz, all 44 comparisons passed with zero differing bytes. Most kernel
totals were faster than the old baseline. Remaining regressions were VGA CGA
320 modes 4/5, all four VGA text cases, and NTSC TGA 160 mode 8. This is a
diagnostic table, not a pass for the no-regression requirement.

The old baseline files are unchanged. All limits of the first run still apply:
no active video DMA, no second core, and no hardware output verification.
