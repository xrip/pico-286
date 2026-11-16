# RP2350 IRQ entry stress test

This runs two fixed math, fill, copy, and lookup loops. Core 1 has a direct SRAM IRQ: a 32 us timer by default, or the real HSTX driver with `TEST_HSTX=ON`. There is no PC or OPL code. Both CPU clock settings use VREG 1.6 V. Reference checksums come from flash data before the CPU clock increase.

**HSTX gives a black picture by design.** It sends sync and full black packets. USB gives the test results; there is no game screen.

Run these PowerShell commands from the repo root. They use the local SDK, Arm GCC, and saved picotool:

```powershell
$env:PICO_SDK_PATH = 'F:/pico-sdk'
$base = @('-G', 'Ninja', '-DPICO_TOOLCHAIN_PATH=C:/SysGCC/arm-eabi',
    '-DCMAKE_BUILD_TYPE=Release',
    '-Dpicotool_DIR=C:/Users/xr1p/CLionProjects/pc/build/m2p2-hdmi-ntsc/_deps/picotool')

cmake -S tests/irq-entry-stress -B build/irq-entry-new-504 @base -DTEST_CPU_MHZ=504 -DTEST_FLASH_MHZ=84
cmake --build build/irq-entry-new-504 --parallel 8

cmake -S tests/irq-entry-stress -B build/irq-entry-new-252 @base -DTEST_CPU_MHZ=252 -DTEST_FLASH_MHZ=84
cmake --build build/irq-entry-new-252 --parallel 8
```

The ELF and UF2 are in the build folder. Names start with `irq-entry-<CPU>-f<flash>-<flash|ram>`; the last field gives the core 1 code location. Options add `-hstx`, `-c0ram`, and `-psram126` or `-psram84`.

| CMake option | Default | Effect |
| --- | --- | --- |
| `TEST_CPU_MHZ` | `504` | CPU 504 or 252 MHz. |
| `TEST_FLASH_MHZ` | `84` | Flash 84 or 42 MHz. RX delay stays 6 at CPU504, 3 at CPU252. |
| `TEST_HSTX` | `OFF` | Real HSTX DMA IRQ1 replaces timer IRQ2. HSTX uses PLL_SYS /4 at CPU504 or /2 at CPU252: 126 MHz. USB keeps 48 MHz. |
| `TEST_CORE0_RAM` | `OFF` | Core 0 work code and its 64 KiB lookup move to SRAM. |
| `TEST_PROBE_RAM` | `OFF` | Core 1 work code moves to SRAM; its lookup stays in flash. |
| `TEST_PSRAM` | `OFF` | Needs `TEST_CORE0_RAM=ON`. Core 0 lookup moves to GPIO8 QMI PSRAM at `0x11000000`; code stays in SRAM. |
| `TEST_PSRAM_MHZ` | `126` | 126 or 84 MHz. At CPU504 these use divider/RX delay 4/5 or 6/6. |

For HSTX with core 0 SRAM work, add `-DTEST_HSTX=ON -DTEST_CORE0_RAM=ON`. For the PSRAM pair, also add `-DTEST_PSRAM=ON -DTEST_PSRAM_MHZ=126` or `84`, with separate build folders. Keep CPU504 and flash84 fixed. PSRAM setup fills and checks all 16,384 words before starting core 1; `verify_bad` must be zero. Expected checksums use the flash lookup at the boot clock, not PSRAM data.

USB starts without waiting for a host. Status is sent each second. Send `v` for status or `b` for ROM BOOTSEL. Opening the USB serial port at 1200 baud also gives a BOOTSEL reset. A core 1 fault is recorded by `src/fault_trace.c`; core 0 can still give its fault record.

Status fields `c0=rounds/checksum/errors` and `c1=rounds/checksum/errors` give each worker's count, last hex checksum, and mismatch count. The checksum changes as the loop goes through eight fixed seeds. Errors must stay zero. `irq` counts timer IRQs only; HSTX has a separate `frames/restarts/run/phase` line. `fault=2` means a core 1 fault record is present. A fixed frame count with `run=1` is not proof of a working IRQ.

Check code placement and keep each image's hash with its log:

```powershell
& C:/SysGCC/arm-eabi/bin/arm-none-eabi-nm.exe -S <test.elf> |
    Select-String 'flash_round|probe_round|flash_lookup|ram_lookup|timer_irq_handler|hstx_dma_irq_handler'
Get-FileHash <test.uf2> -Algorithm SHA256
```

Flash code/data has `0x100...` addresses; SRAM code/data has `0x200...` addresses. The IRQ must be in SRAM. `flash_round` is core 0 work despite its name; it must be in SRAM for `TEST_CORE0_RAM=ON`. With the default probe, `probe_round` and `flash_lookup` stay in flash. Builds use no LTO and no function folding to keep those locations.

## Known failure and exact image

On 2026-11-16 the CPU504/flash84 HSTX case with core 0 SRAM code/data, core 1 flash code/data, and **no PSRAM** failed at 15,629 frames (about 260 seconds), after 1,523,594 core 1 rounds. Mismatch and restart counts were zero. The fault was at `PC=1000011e` (`isr_svcall`, `BKPT`), with `LR=ffffffe9`, `CFSR=00000000`, `HFSR=80000000`, and `xPSR=8900001b`. See [fault.log](../../build/irq-entry-504-hstx-c0ram/fault.log).

The exact failed UF2 has SHA256:

```text
D67E4F2F6D2308C9EE4C387C9EAA8BAE9F8B964261885DFD3837110C22E9B5EA
```

Its local source, ELF, UF2, and [hash list](../../build/irq-entry-hstx-c0ram-baseline/hashes.json) are saved in `build/irq-entry-hstx-c0ram-baseline/`. This is an ignored local build folder: keep that copy with the logs. The normal test source now has PSRAM options and may give a different binary. Use the saved UF2 for the exact image, or the frozen source for a rebuild:

```powershell
cmake -S build/irq-entry-hstx-c0ram-baseline -B build/irq-entry-frozen-repro @base -DTEST_CPU_MHZ=504 -DTEST_FLASH_MHZ=84 -DTEST_HSTX=ON -DTEST_CORE0_RAM=ON -DTEST_PROBE_RAM=OFF
cmake --build build/irq-entry-frozen-repro --parallel 8
```

A rebuild also depends on the SDK and imported driver sources; check its hash. The frozen source hashes are `2DF551978E35FB3AC6A6174D6C69816C32484E942192E0C5CFA5C4EE51EC96D6` (`main.c`) and `473F33FDDAA2E1AAB85D9E5E0B15447E3879F2DC43810EF20E365B63EC03649E` (`CMakeLists.txt`).

The CPU504/flash42 control later failed at33,149 frames and1,739,579 core1 rounds (PC30000514, CFSR100, HFSR40000000).

**Isolation caveat:** these first no-PSRAM-access builds left GPIO8 unconfigured. They do not prove that external PSRAM was electrically inactive on the shared bus. The current test holds GPIO8 high before flash setup; the PSRAM option later switches it to QMI CS1. The earlier standalone control is saved in `build/irq-entry-504-hstx-c0ram-cs1high-latch`, with its source archived in `build/yagni-archive/irq-entry-cs1-high`. See [updated isolation notes](../graphics-perf/results/irq-entry-isolation.md). Also preserve imported driver sources when reproducing old binaries: `flash_config.c` has since changed to retain the ROM pad settings; its earlier copy is in `build/flash-sdk-profile/flash_config.previous.c`.
