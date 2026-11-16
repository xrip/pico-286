# VGA text stress board runs

The parent session ran both profiles on the same board on COM3 at 315 MHz.
Each complete table has 44 cases and zero differing output bytes. All 44 new
kernel cycle totals are below their old baseline totals in both runs.

| Profile | Flashed UF2 SHA256 | VGA text cycle reduction |
| --- | --- | ---: |
| Hidden blink | `27E2E128A82A9AD76F389B3CF85808C6820A4126EA15F662F0E683B3FF20C1CC` | 30.2–38.6% |
| Full-block cursor | `FAE31E5708276C56B51C6B33B068F7C8B500EC3EE646777A3392C9E6F63A6C80` | 37.6–46.6% |

Raw captures are `blink-board-run.csv` and `cursor-board-run.csv`, copied from
the matching files under `build/`. Ignored firmware artifacts are
`blink-27E2E128.uf2/.elf` and `cursor-FAE31E57.uf2/.elf`.

Blink uses random attributes with bit 7 hidden. Cursor uses `start=0,end=15`,
which has identical full-block output in both cursor models; each timed VGA
text sample is on the cursor row. The state and row are set outside the timer
for both kernels. Old baseline code is unchanged. These are kernel tests with
no video DMA or second core, not full-system performance claims.
