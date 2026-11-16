# Final normal-profile board run

The parent session confirmed flashing
`E1280D89D0C6A916F72165C1C681C3B585016CE316DBC32D5BFA8A694535B3D3`
on the user's board on COM3, treated as M2P2/RP2350. The complete capture is
`final-board-run.csv`, copied unchanged from `build/graphics-perf-final.csv`.
Matching ignored binaries are `final-E1280D89.uf2` and `.elf`.

All 44 output comparisons passed with zero differing bytes. All 44 new kernel
cycle totals and all 44 sampled maximum costs are lower than their old
baselines. Clock is 315 MHz, with 128 samples per kernel and 5 cycles of
reported counter overhead. The old kernel source files are unchanged;
`check-baseline.py` verifies their hashes and original loop text.

The ranges below are the smallest and largest reduction in per-case total
cycles, calculated as `100 * (1 - new_total / old_total)`:

| Backend | Cases | Cycle reduction range | Largest new sampled row |
| --- | ---: | ---: | ---: |
| VGA graphics | 17 | 5.9–44.6% | 3,740 cycles |
| VGA text | 4 | 20.3–29.5% | 1,732 cycles |
| PIO HDMI graphics | 9 | 10.1–34.6% | 2,147 cycles |
| NTSC graphics | 10 | 7.9–52.2% | 2,373 cycles |
| NTSC text | 4 | 19.0–29.1% | 3,767 cycles |

The new VGA text path uses an additional 16 KiB four-pixel LUT, built outside
the timed calls. This RAM cost is part of the production implementation.
The ordinary timing state has no active cursor and blink disabled. Separate
blink/cursor profiles cover those states rather than silently treating this
table as their result.

These are matched-clock kernel results, not full emulator FPS results. The old
PIO HDMI application could run at 504 MHz, while the new HSTX+NTSC clock plan
uses 315 MHz; lower kernel cycle counts do not prove equal application speed
across those clocks. IRQ/DMA overhead, contention, and the full emulator are
outside this test. No RP2040 hardware performance claim is made. PIO HDMI's
legacy reserved control indices 252–255 are excluded from the comparison as
documented in the benchmark README; HSTX supports a separate output path.
