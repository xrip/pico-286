# Graphics test evidence

All timings here are measured RP2350 kernel cycles at the same 315 MHz clock.
They do not establish full emulator FPS or RP2040 hardware speed. Old PIO HDMI
application clocks could be 504 MHz; the new HSTX+NTSC clock plan is 315 MHz.
The benchmark excludes video DMA, second-core load, and lock contention.

| Evidence | Record |
| --- | --- |
| Final normal profile, 44 cases, all faster with zero output differences | [Final report](final-results.md), [CSV](final-unroll8-board-run.csv) |
| Final application compiler flags, 44 cases, all faster with zero differences | [Final report](final-results.md), [CSV](appflags-final-board-run.csv) |
| VGA blink and cursor stress, both pass | [Report](text-stress-board-runs.md), [blink CSV](blink-board-run.csv), [cursor CSV](cursor-board-run.csv) |
| Final HSTX source locks, 61 cases; original 44 faster, row budgets met | [Final report](final-results.md), [CSV](hstx-final-board-run.csv) |
| Live HDMI 80x25 and 640x480 scanout | [Capture notes](visual-captures.md), [text PNG](hdmi-80x25.png), [graphics PNG](hdmi-640x480.png) |
| Actual PC firmware: DR-DOS and Volkov Commander in HDMI 80x25, with NTSC running | [Boot record](visual-captures.md), [PC screen](pc-hdmi-80x25.png) |
| 504 MHz HSTX-only firmware: startup fix, live display and wireless keyboard | [Board check](hstx-504-board.md), [CheckIt screen](hstx-504-working.png) |
| 504 MHz Mode 13h with OPL2 in normal SRAM: short runs passed, later Dune II fault | [Fault controls and results](hstx-opl-sram.md) |
| Shared-bus isolation, stock SDK reference, and startup correction | [Interrupt isolation](irq-entry-isolation.md) |
| SDK-derived flash profile at 504 MHz: Dune II/Monkey Island and 602-second observation pass | [Board results](irq-entry-isolation.md), [game log](hstx-sdk-profile-run.txt) |
| 50.4 MHz flash trial: user confirms stable game runs | [Trial settings and hashes](flash-50m4-trial.md) |
| Later Dave 2 faults, scratch migration, and 378 MHz control | [Stack and SRAM checks](dave2-sram-check.md) |
| 500 MHz, separate HSTX PLL, HDMI audio: Dave 2 exit/relaunch passes user test | [Build and board result](hstx-500-audio.md) |

Earlier slower runs are retained as `first-board-run`, `second-board-run`,
`third-diagnostic-run`, `fourth-board-run`, `fifth-board-run`, and
`hstx-first-board-run` reports and CSVs. Their firmware hashes are recorded in
each report. Matching ELF/UF2 files are local ignored artifacts.
The earlier normal/appflags passes and the small HSTX Mode X regression are
also retained in `final-board-run`, `appflags-board-run`, and
`hstx-mode-x-check`; use `final-results.md` for the final recommended set.

`../check-baseline.py` checks that the five old kernel files still match the
first measured sources and the loops in git revision `519a857`. No baseline
was weakened to improve the new results. PIO HDMI's old reserved indices
252–255 and intentional cursor fixes have explicit limits in the benchmark
README. VGA text speed uses an additional 16 KiB lookup table.
