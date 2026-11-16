# First board run

Captured by the parent session on 2026-11-16 from the user's board on COM3.
The board is treated as M2P2/RP2350 from the user/session context; this test
does not read a board identity. The raw serial capture is
`first-board-run.csv`, copied without changes from `build/graphics-perf-first.csv`.
The matching `.uf2` and `.elf` are also kept beside it as local ignored build
artifacts, so a later rebuild cannot replace this first firmware.

- Firmware SHA256: `DBA4BDE3DB21261B0A878C0F6B570A2F3F7096931518148F832ECEC1C1CAF377`.
- CPU: 315,000,000 Hz, core 0, core-clock SysTick, IRQs off during each call.
- 128 samples per old/new kernel; call order alternates.
- Reported timer overhead: 5 cycles; not subtracted.
- GCC 15.2.1, Pico SDK 2.3.0, `-O3 -g3 -flto`.
- All 44 comparison cases passed with zero differing bytes.
- All 44 new kernel totals were slower than their old baseline totals.
- No video DMA or second core was running; USB started after the tests.

This is the initial generic fused-renderer result, before the direct native
fast paths. Keep it as evidence of the regression rather than replacing it
with a later faster table. This run does not establish output signal quality
or full emulator speed.

The baseline loop files will stay fixed during the next tests:

| File | SHA256 |
| --- | --- |
| `old_vga.c` | `6BEFDD8780FD9EDC07565EDD7FFE0B5F22737435E95853B1C9472E45EAA63BB7` |
| `old_text.c` | `76274B6C26B2B8766E5FAEADDB2AB1FEC85E1979825AF7DCFC292D7265ED6539` |
| `old_hdmi.c` | `E920E7E757F0386CA6452C846974814A7033A6D98608C24CD600C11C5FE94F18` |
| `old_ntsc.c` | `BF50F98CAE6E9C120E61915655F2A069D3D1DBFF13402CF0417817E0853AD92B` |
| `old_ntsc_text.c` | `46910FD199DF609B691A85774D133A266DC4A899A1A902B9410144858A39D38C` |
