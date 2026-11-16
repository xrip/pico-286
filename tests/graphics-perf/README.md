# Graphics kernel test

This standalone RP2350 program has 44 cases: all 17 PC VGA graphics modes,
four VGA text modes, nine old PIO HDMI graphics modes, and ten old NTSC graphics
modes, plus four NTSC text modes. The old loops come from commit
`519a857001220a54ed0c9224be1d741a84b362a1`.
`old_*.c` keeps those loops; only the IRQ/DMA shell is replaced by arguments.
The new side calls the real native graphics, NTSC, and VGA text-pair paths,
with their general fallback kept in the call wrapper. No emulator, video DMA, PSRAM, or
second core is started. This is a kernel test, not a full output timing test.

Each mode gets three deterministic VRAM patterns, three page/bank settings,
and a byte comparison of every active source row. Low-resolution modes write
320 packed VGA pixel pairs; 640-wide modes write 640 VGA bytes. Both outputs
are 640 bytes. HDMI gives 320 command bytes; NTSC gives 640 16-bit samples.
VGA palette phases are equal in this test. Both old and new wide paths use
the low palette byte; low-resolution paths keep packed pixel pairs.
HDMI data uses indices below 252: old Mode X emitted reserved control codes
for the last four indices. Its TGA foreground is zero because the old path
did not substitute zero-color pixels. NTSC uses full phase tables, not the
optional compact palette. Its old EGA loop gets the active output pointer,
so the old sync-overwrite defect is outside this pixel test. VGA text includes
blink and page changes, with the cursor off; cursor shape fixes are covered
by the shared mode tests. Pair LUTs are prepared outside the timed call.
The new VGA text path has the same additional 16 KiB four-pixel LUT as the real
backend. The old baseline keeps its original 2 KiB pair table and loops.
NTSC text keeps the old color output at 40 columns and monochrome output at
80 columns. Its comparison uses page zero with blink/cursor off because the
old 40-column path had no blink/cursor and both old paths ignored the page.

After the comparisons, each mode gets 128 timed calls at different rows. Old
and new call order alternates. Both kernels are in SRAM, built with the same
`-O3 -flto` flags. IRQs are off during each call. USB starts only after all
measurements are complete. CPU clock is 315 MHz. The 24-bit core-clock SysTick
counter gives cycle counts; DWT CYCCNT is not assumed to exist on RP2350.

The old debug display and broken mode timing selection are outside the copied
kernels. Thus the comparison includes all 480 graphics rows where the old
debug display hid the last 80. HDMI text, TFT, hardware encoding,
DMA deadlines, mode changes, and dual-output contention need separate checks.

## Build on this Windows host

Run from the `pc` root. The `picotool_DIR` below is a tool already built in
this workspace; a normal Pico SDK setup may use its installed picotool.

```powershell
$env:PICO_SDK_PATH = 'F:\pico-sdk'
cmake -S tests/graphics-perf -B tests/graphics-perf/build -G Ninja `
  -DPICO_TOOLCHAIN_PATH=C:/SysGCC/arm-eabi `
  -DCMAKE_MAKE_PROGRAM=C:/Python314/Scripts/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release `
  -Dpicotool_DIR=C:/Users/xr1p/CLionProjects/pc/build/m2p2-hdmi-ntsc/_deps/picotool
cmake --build tests/graphics-perf/build --parallel 8
```

Output: `tests/graphics-perf/build/graphics_perf.uf2` and `.elf`.
For an ELF-only build without picotool, add `-DPICO_NO_PICOTOOL=ON`.
The build does not flash the board.

For a separate check with the application's extra compiler flags, use the same
configure command with `-B tests/graphics-perf/build-appflags` and
`-DBENCH_APP_FLAGS=ON`, then build that directory. This adds
`-frename-registers -fomit-frame-pointer -ffast-math -ffunction-sections
-fdata-sections -fms-extensions` to both old and new code, with `-O3` retained.
Its CSV identifies `profile=app-compile-flags`. It does not replace the normal
profile's build or results.
The final actual board run is `results/appflags-final-board-run.csv`, described
in `results/final-results.md`: all 44 cases have lower new cycle totals and
sampled maxima, with zero output differences. Its smallest margin is 1.34%.

## HSTX source-lock profile

Use a separate build directory and add `-DBENCH_HSTX_SOURCE=ON`:

```powershell
cmake -S tests/graphics-perf -B tests/graphics-perf/build-hstx -G Ninja `
  -DPICO_TOOLCHAIN_PATH=C:/SysGCC/arm-eabi `
  -DCMAKE_MAKE_PROGRAM=C:/Python314/Scripts/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release -DBENCH_HSTX_SOURCE=ON `
  -Dpicotool_DIR=C:/Users/xr1p/CLionProjects/pc/build/m2p2-hdmi-ntsc/_deps/picotool
cmake --build tests/graphics-perf/build-hstx --parallel 8
```

This profile compiles the actual `HDMI_HSTX` source lock and calls its init
functions. It runs the original 44 cases with that source-access cost, then
adds 17 HSTX mapped16 graphics cases as backend 5 (`bench_result_count = 61`).
The source locks are not under contention: there is still no second core.

For backend 5, old VGA timings are reference values only. HSTX 640 writes
1,280 output bytes while old VGA 640 writes 640 bytes, so their cycle ratio is
not a like-for-like performance verdict. The output comparison expands old VGA
bytes to the test palette's 16-bit values outside the measured call. HSTX 320
and VGA 320 both write 640 bytes but represent different physical color data.

Backend 5 remains a mapped16 kernel test. The current low-resolution HSTX
packet path uses mapped32 output. Its pixel values, wrapping, and bounds are
checked by `tests/graphics_modes_test.c`; stream and DMA timing still need
full board checks.

At 315 MHz, one 640x480 HDMI line gives 10,000 CPU cycles (800 pixel clocks at
25.2 MHz). Compare HSTX `new_max` with that absolute budget. Dual output also
needs room for an NTSC row roughly every two HDMI rows, plus IRQ/DMA work.
Passing this kernel budget alone does not prove dual-output timing.

## VGA text stress profiles

The default timing state has blink disabled and no cursor. For more text work,
use a separate `build-blink` directory with `-DBENCH_TEXT_STRESS=1`, or a
separate `build-cursor` directory with `-DBENCH_TEXT_STRESS=2`. All other build
arguments are the same as the normal profile. The CSV states which profile ran.

Profile 1 hides glyphs with attribute bit 7 set, testing random blink attributes.
Profile 2 uses a full-block cursor (`start=0`, `end=15`) whose old/new output is
identical, and each timed sample is on the cursor row. State and row selection
are set outside the timer for both sides. Only the four VGA text cases change;
all 44 cases still run and check output. The copied baseline files stay fixed.
Do not compare the intentionally corrected default cursor shape against the
old shape as if they were meant to be identical.

## Run and read

Put the RP2350 into BOOTSEL mode. Once its drive letter is known, copy the UF2
to that drive. For example, if the BOOTSEL drive is `X:`:

```powershell
Copy-Item -LiteralPath tests/graphics-perf/build/graphics_perf.uf2 -Destination X:\
```

The board runs the tests once, then gives the CSV table on USB CDC every two
seconds. Connect to its COM port with any serial reader; 115200 baud is fine.
Send the character `b` to enter BOOTSEL for the next test build. The SDK's
USB reset interface also supports the usual 1200-baud reset when enabled.
No GPIO UART is active by default. An optional UART0 build uses
`-DBENCH_UART=ON -DBENCH_UART_TX_PIN=0 -DBENCH_UART_RX_PIN=1`; on M2 these pins
are shared with PS/2, so disconnect that device for this standalone test.
`-DBENCH_USB=OFF -DBENCH_UART=OFF` leaves results only in RAM.

With an SWD GDB server on port 3333, inspect RAM after the program has run:

```text
arm-none-eabi-gdb tests/graphics-perf/build/graphics_perf.elf
(gdb) target extended-remote localhost:3333
(gdb) interrupt
(gdb) p bench_status
(gdb) p bench_clock_hz
(gdb) p bench_mismatch_count
(gdb) p bench_result_count
(gdb) p bench_results
```

`bench_status`: 0 = not started; 1 = running; 2 = complete; 3 = clock or timer
check failed. Completion alone is not a pass: `bench_mismatch_count` must be
zero, and `bench_result_count` must be 44. Each result also gives the first bad
row and byte when a comparison fails.

The CSV `backend` column is 0 for VGA graphics, 1 for VGA text, 2 for PIO HDMI,
3 for NTSC graphics, and 4 for NTSC text. CSV totals are cycles for 128 calls.
Divide `old_total` or `new_total` by 128
for mean cycles per source row. `new_total / old_total` is the normalized cost
ratio: above 1 means more cycles in this test. `old_min/max` and `new_min/max`
give individual row bounds. The empty-call counter overhead is reported but
not subtracted. At 315 MHz, cycles divided by 315 give microseconds.
Do not use host execution time as RP2350 performance evidence. Even equal
kernel times do not prove that dual-output deadlines are met under DMA load.

## Host output comparison

This runs the same pixel comparisons without making timing claims:

```powershell
python tests/graphics-perf/check-baseline.py
& 'C:\Users\xr1p\AppData\Local\Programs\CLion\bin\mingw\bin\gcc.exe' `
  -std=c17 -O3 -DBENCH_HOST=1 -DVGA=1 -DBENCH_USB=0 -DBENCH_UART=0 `
  -Itests/graphics-stubs -Idrivers/graphics -Idrivers/vga `
  tests/graphics-perf/main.c tests/graphics-perf/old_vga.c `
  tests/graphics-perf/old_text.c tests/graphics-perf/old_hdmi.c `
  tests/graphics-perf/old_ntsc.c tests/graphics-perf/old_ntsc_text.c `
  drivers/graphics/graphics_modes.c drivers/graphics/graphics_pc_hdmi.c `
  -o tests/graphics-perf/build/host_compare.exe
& tests/graphics-perf/build/host_compare.exe
```

The local host run passed all 44 cases with zero differing bytes. The RP2350
ELF and UF2 build passed with GCC 15.2.1 and Pico SDK 2.3.0. The first actual
board run is in `results/first-board-run.csv`, with its firmware hash and
limits in `results/first-board-run.md`. It found all 44 new kernel totals
slower; that table is kept unchanged. The final recommended runs are in
`results/final-results.md`: normal and application-flag profiles each pass all
44 cases; the HSTX source-lock profile passes 61 output cases and its original
44 cases are faster. All compared new totals and sampled maxima are lower.
These are kernel results at the same 315 MHz clock, not whole-emulator FPS or
RP2040 results. The evidence index is `results/README.md`.
