# Final results after the Mode X change

The parent session flashed each firmware below on the same board on COM3,
treated as M2P2/RP2350, and checked the copied UF2 hash. All runs use 315 MHz
and 128 samples per kernel. The reported empty-call cost is 5 cycles and is
not subtracted. All old baseline source files remain unchanged.

| Profile | Cases | Output differences | Original 44 cases: smallest cycle reduction |
| --- | ---: | ---: | ---: |
| Normal | 44 | 0 | 5.61% |
| Application compiler flags | 44 | 0 | 1.34% |
| HSTX source locks | 61 | 0 | 1.89% |

In all three runs, every original case has a lower new total **and** a lower
sampled maximum than its old baseline. HSTX's 17 additional backend-5 cases
have an absolute output-row budget; their old VGA reference is not equivalent
output hardware work and is excluded from that no-regression comparison.

| Profile | UF2 SHA256 | Complete raw capture |
| --- | --- | --- |
| Normal | `CBDBDFC92D6F36A6392E40D9F76165C426EC0B495C3A8BDDDA00DD7FDCDBC8EC` | [CSV](final-unroll8-board-run.csv) |
| Application flags | `8CB3ECFB5F8E7EE850C9110260AD485674D2D55254F72E02566C66F690CF5E13` | [CSV](appflags-final-board-run.csv) |
| HSTX source locks | `29CB2EECCAB60DEB217E5B6F76CBCAF253573093473746D4E15996FA3EF9C28F` | [CSV](hstx-final-board-run.csv) |

Matching ignored ELF/UF2 files are `final-CBDBDFC9`,
`appflags-final-8CB3ECFB`, and `hstx-final-29CB2EEC` beside this report. The CSVs
are unchanged copies of `build/graphics-perf-final-unroll8.csv`,
`build/graphics-perf-appflags-final.csv`, and `build/graphics-perf-hstx-final.csv`.
The EGA spread LUT addresses are respectively `0x20008d2c`, `0x20008dd4`, and
`0x200093f8`, all SRAM.

Normal-profile cycle reductions, per case:

| Backend | Cases | Reduction range |
| --- | ---: | ---: |
| VGA graphics | 17 | 5.9–44.6% |
| VGA text | 4 | 20.3–29.6% |
| PIO HDMI graphics | 9 | 10.1–34.6% |
| NTSC graphics | 10 | 5.6–54.3% |
| NTSC text | 4 | 16.1–27.9% |

In the final HSTX profile, the largest sampled HSTX row cost is 3,770 cycles,
below the 10,000-cycle HDMI line budget. The largest NTSC row is 3,880 cycles.
Two such HSTX rows plus one NTSC row total 11,420 cycles before IRQ/DMA cost,
against roughly 20,000 cycles of two-line time. This budget estimate excludes
active DMA, second-core load, and lock contention.

[Blink and cursor stress](text-stress-board-runs.md) also passed. Those runs
precede only the final NTSC Mode X unroll change; the tested VGA text code is
unchanged. The extra VGA text LUT costs 16 KiB of RAM in production. All five
old kernel files pass `../check-baseline.py` against the first source hashes
and git revision `519a857`.

These are matched-clock kernel results, not whole-emulator FPS or RP2040
hardware results. The old PIO HDMI application could use 504 MHz, while the
new HSTX+NTSC clock plan uses 315 MHz. PIO HDMI's old reserved indices 252–255
remain outside the valid-range comparison. Corrected cursor behavior is
tested separately from the matching old/new full-block cursor case.
