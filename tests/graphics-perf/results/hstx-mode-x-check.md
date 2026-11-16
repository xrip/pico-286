# HSTX source locks: Mode X check

The parent session flashed
`CB1E91F22B729DA754191145A2D7E9B1F97D83AE07475C1E95BDB21ED6F95193`
and captured the complete 61-case table in `hstx-mode-x-check.csv` (original
capture: `build/graphics-perf-hstx-final.csv`). Ignored matching binaries are
`hstx-final-CB1E91F2.uf2` and `.elf`. The filename of the original capture does
not make this a fully passing result.

At 315 MHz, all 61 output comparisons passed. HSTX graphics took at most 3,771
cycles per sampled row, below the 10,000-cycle line budget. NTSC's sampled
maximum was 3,841 cycles. Video DMA and lock contention remain outside the test.

One original case still had a small regression: NTSC Mode X (backend 3, mode
255) took 235,762 new cycles versus 235,229 old cycles for 128 rows: 0.227% more,
or 4.16 cycles per row. Its sampled maximum was 1,865 versus 1,848 cycles. The
other original cases had lower new totals. This result triggered one focused
Mode X unroll change and another target run; the raw table is retained.
