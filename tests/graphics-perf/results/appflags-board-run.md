# Application compiler flags: board run

The parent session confirmed flashing
`57D10DA705C6F9AD076209AA0E25FA12D0289598A8CDA82374894A8DBCCC63E2`
on the same board on COM3. The unchanged complete capture is
`appflags-board-run.csv`, from `build/graphics-perf-appflags.csv`. Matching
ignored binaries are `appflags-57D10DA7.uf2` and `.elf`.

This separate build adds the application's `-frename-registers
-fomit-frame-pointer -ffast-math -ffunction-sections -fdata-sections
-fms-extensions` flags to both old and new kernels, retaining `-O3 -flto`.
The source and all five old baseline loop files are unchanged.

At 315 MHz, all 44 output comparisons passed with zero differing bytes. All
44 new cycle totals and sampled maxima are lower than the old baseline. The
smallest total-cycle margin is 2.36%, rather than the normal profile's 5.87%.

| Backend | Per-case cycle reduction range |
| --- | ---: |
| VGA graphics | 3.82–46.39% |
| VGA text | 21.84–33.98% |
| PIO HDMI graphics | 8.34–37.86% |
| NTSC graphics | 2.36–52.25% |
| NTSC text | 18.22–26.32% |

The same kernel-only limits apply: this does not measure whole-emulator FPS,
video DMA contention, or RP2040 hardware. Old and new use the same 315 MHz
clock in this test, not their potentially different application clock plans.
