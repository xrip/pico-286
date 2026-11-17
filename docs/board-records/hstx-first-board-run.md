# First HSTX source-lock run

The parent session flashed and checked firmware
`EC53FF9C97AE4F112B8E6DEC81E61164D3B28EBAE5B6604BD13932CCFFAFEEDC`
on the same board on COM3, treated as M2P2/RP2350. The complete 61-case serial
table is `hstx-first-board-run.csv`, copied from
`build/graphics-perf-hstx-first.csv` after the final mode 118 row was captured.
Matching ignored binaries are `hstx-first-EC53FF9C.uf2` and `.elf`.

This profile uses the actual HSTX source spin locks, initialized before tests.
The EGA lookup is in SRAM (`pc_spread_lut` at `0x200091a0`). It has no video
DMA, concurrent second core, or lock contention. CPU clock is 315 MHz, with
128 samples per kernel and reported counter overhead of 5 cycles.

All 61 output comparisons passed with zero differing bytes. HSTX mapped16
graphics rows (backend 5) took at most 3,789 cycles in this sample, below the
10,000-cycle HDMI line budget. The largest sampled NTSC row cost was 3,882
cycles. Two such HSTX rows plus one such NTSC row total 11,460 cycles, before
IRQ/DMA overhead, against roughly 20,000 cycles of two-line time. This is a
kernel budget estimate, not a dual-output timing proof.

Old VGA timings in backend 5 are reference values, not equivalent output
hardware work. Several original 44 cases remain slower than their baselines,
including VGA CGA/text and NTSC TGA 160. Small NTSC CGA/composite regressions
also appear with the source locks. The no-regression task is not complete.
