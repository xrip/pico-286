# Fourth board run

The parent session flashed and checked
`CE51E9F805D45DDF0ABEFD60E42A1D9F3E5AAD60F652AA294A6F05BDC67DED4F`
on the same board on COM3. `fourth-board-run.csv` is the unchanged capture
from `build/graphics-perf-fourth.csv`. Matching ignored binaries are
`fourth-CE51E9F8.uf2` and `.elf`.

At 315 MHz, all 44 output comparisons passed with zero differing bytes. The
spread LUT is in SRAM at `0x20008bfc`. All VGA graphics and PIO HDMI graphics
kernel totals are faster than their old baselines. VGA text still takes about
12.5% more cycles at 40 columns and 4.5% more at 80 columns. NTSC TGA 320 mode 9
is close to parity but its total is about 0.07% slower (roughly 1.7 cycles per
row); the other NTSC cases are faster.

This build predates the extra wide-pair VGA text LUT and the later NTSC TGA
320 unroll change. The remaining regressions are retained in the raw table.
The old baseline files and the first run's measurement limits are unchanged.
