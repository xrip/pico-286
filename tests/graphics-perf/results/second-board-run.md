# Second board run

Captured by the parent session on 2026-11-16 from COM3, using the same board
treated as M2P2/RP2350 in the first run. The parent checked the flashed UF2 hash:
`A883861D9AFB52AFAD5316DCE96D8F32C8A6941DA10B78886B5E59907A6C677A`.
The raw capture is `second-board-run.csv`, copied from
`build/graphics-perf-second.csv`. Matching ignored firmware files are
`native-first-A883861D.uf2` and `native-first-A883861D.elf`.

The clock remains 315 MHz, with 128 samples per kernel and 5 cycles of reported
counter overhead. All 44 pixel comparisons passed with zero differing bytes.
The old baseline sources are unchanged from the first run.

This build uses direct native graphics paths. All nine PIO HDMI graphics cases
are faster than their old kernels. Several VGA and NTSC cases are still slower:

- VGA CGA 320: about 9.8% more cycles.
- VGA TGA 640: about 24.4% more cycles.
- VGA EGA 640: about 11–12.5% more cycles.
- VGA text: about 36–52% more cycles.
- NTSC TGA 160: about 8.7% more cycles.
- NTSC EGA 320: about 1.9% more cycles.
- NTSC 40-column text: about 2.7–3% more cycles.

Ratios use `new_total / old_total` in this run. Smaller regressions in other
VGA cases are also present in the raw table. This run is not a pass for the
no-regression requirement. Later source changes must get a new table and hash.
