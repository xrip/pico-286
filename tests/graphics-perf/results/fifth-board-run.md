# Fifth board run

The parent session confirmed flashing
`04211CFA599639A1E1FE63B17E6237CFBDDFB74E1CE6A2E9BF12CA1FC12E8BD1`
on the same board on COM3. `fifth-board-run.csv` is the unchanged capture
from `build/graphics-perf-fifth.csv`. Matching ignored binaries are
`fifth-04211CFA.uf2` and `.elf`.

At 315 MHz, all 44 output comparisons passed with zero differing bytes. This
build has the additional wide-pair VGA text LUT and NTSC TGA 320 unroll change.
All kernel totals are faster than their old baselines except VGA 80-column
text modes 2 and 3, which take about 7.4% more cycles. That remaining regression
must be resolved before a full no-regression result can be stated.

The old baseline files and the first run's measurement limits are unchanged.
