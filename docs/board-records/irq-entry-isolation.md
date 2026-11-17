# Interrupt-entry fault isolation

The full firmware still faulted after moving OPL assembly to SRAM. The next
test removes the emulator, OPL, SD card, and software PSRAM accesses.
**The first tests did not hold the external PSRAM chip-select high.** They
therefore do not rule out interference from the physical PSRAM chip on the
shared bus. A separate CS1-high control is recorded below.

`tests/irq-entry-stress` runs deterministic loops on both cores and compares
each result with checksums computed at the boot clock. A 64 KiB lookup table
causes flash cache traffic. It can use either a 32-us timer interrupt or the
real HSTX driver with static black packets and no renderer callbacks.

All results below use CPU 504 MHz, flash 84 MHz, and VREG 1.60 V. HSTX, when
enabled, is 126 MHz. These are observations, not long-term stability claims.

| Interrupt | Core 0 work/data | Observation |
| --- | --- | --- |
| Timer | Flash/flash | 8,411,504 IRQs with no checksum error or recorded fault before stopping the test |
| HSTX | Flash/flash | 3,278 frames, no error or restart before stopping the test |
| HSTX | SRAM/SRAM | Fault at frame 15,629, about 260 seconds after scanout start |

The last case reproduces the game's failure without OPL or PSRAM accesses:

```text
core1 rounds=1523594 checksum=c2a9811a mismatches=0
hstx frames=15629 restarts=0 run=1 phase=0
fault pc=1000011e lr=ffffffe9 sp=20080d40 xpsr=8900001b exc=fffffff1
fault cfsr=00000000 hfsr=80000000 masks=0/0/0 limit=00000000 vtor=20000000
```

PC `1000011e` is the default exception handler's BKPT, reached while DMA IRQ1
was active. Core 0 kept running. The compiler used FP registers for a local
copy loop, explaining the extended exception frame in this test; the game
faults also occurred with basic frames, so FP is not a necessary condition.

Guest memory writes and OPL logic are not needed to trigger this failure.
The exact hardware/software mechanism is still under investigation.

The frozen CPU 504 / flash 42 control also failed: frame 33,149 (about 552 s),
core 1 round 1,739,579, PC 30000514, CFSR 100, HFSR 40000000. It kept RXDELAY 6,
unlike the application's old formula, which would overflow that field at
divider 12. Lower flash frequency alone did not prevent the fault.

The CPU 252 / flash 84 control was stopped early at frame 10,261 to check CS1;
it had no recorded fault but is not a completed stability test.

## Chip-select and SDK reference

The stock SDK diagnostic found GPIO8 at FUNC 31 with pad 0x116: input disabled
and isolation enabled. Its zero SIO input is not a measurement of the
physical CS1 voltage. The new control explicitly latches GPIO8 high, enables
output, then selects SIO before flash setup; the measured input is then 1.
That control is in `build/irq-entry-504-hstx-c0ram-cs1high-latch`, hash
`E25DCFCCA587107E9B5649DC38F66135EC266AF34FA8F8AF304EA6B532383ED6`.
Do not interpret earlier runs as electrically isolating PSRAM.
The corrected control completed a 600-second observation: 36,061 frames,
3,518,086 core 1 work rounds, zero checksum errors, faults, or restarts.
This is evidence that chip-select configuration matters in this test, not
proof that every earlier application failure had the same cause.

Stock Pico SDK 2.3 on this board, at 150 MHz, reported M0_TIMING 60007203,
RFMT 492a8, RCMD EB. This is 50 MHz flash, RXDELAY 2 (6.67 ns), and about 60 ns
minimum deselect time. QSPI pads 0..2 were 56 and 3..5 were 5a. The SDK normally
uses ROM flash setup on RP2350 (`PICO_EMBED_XIP_SETUP` defaults to 0).

The application candidate preserves those minimum physical timing intervals
when raising SYS to 504 MHz, giving M0_TIMING 6381970b and 45.82 MHz flash. It
keeps ROM pad settings, holds PSRAM CS inactive before QE setup, and masks
interrupts while QMI is in direct mode. Timing updates wait for idle QMI and
perform an uncached read with barriers before the clock change. Host timing
and QE tests pass; these changes still need a full board/game check.

The application candidate was then flashed and reached DOS. USB confirmed
SYS 504 MHz, HSTX 126 MHz, USB 48 MHz, M0_TIMING 6381970b, M1_TIMING 607e8504,
stock QSPI pads 56/56/56/5a/5a/5a, RFMT 492a8, and RCMD EB. All 63,128 bytes
of RAM code matched the flash load image. No fault was recorded at boot.
The next 602-second observation recorded text/EGA/VGA switches and advancing
frames from 5,760 to 41,300, with zero recorded CPU faults or HDMI restarts.
The user confirmed Dune II and Monkey Island ran without hangs. A later
read at frame 45,897 again found no fault, and all 63,128 bytes of RAM code
still matched the flash image. See [game log](hstx-sdk-profile-run.txt) and
[post-run state](hstx-sdk-profile-state.txt). DOTT was announced as an extra
user test; its result is not yet confirmed here.

All seven M2P2 video profiles build, including the 500 MHz VGA/TFT profiles.
The helper lowers the flash clock further if needed to preserve setup time;
its host tests cover 150, 252, 315, 378, 500, 501, and 504 MHz. The running
HDMI image was not replaced during the game test.

These results support the startup/timing correction for the reported case.
They do not isolate one field as the sole original cause or establish
unlimited stability on every flash/board combination.

Candidate UF2 SHA256:
`3E564A9D79369A07AC850C4472A74D5D03F351BC62BF245BFCDA9B02606F69BE`.

The review of [frank-386 PSRAM startup](https://github.com/rh1tech/frank-386/blob/main/drivers/psram/psram_init.c)
found useful IRQ protection and warm-reset handling, but its timing formulas
also use an unchecked divider-based RX delay. They were not copied wholesale.

## Local evidence

All paths below are relative to the repository root. The builds and full logs
are ignored local artifacts; they are retained for address decoding.

- `build/irq-entry-504/board-run.log`: timer control.
- `build/irq-entry-504-hstx/board-run.log`: HSTX with flash work on both cores.
- `build/irq-entry-504-hstx-c0ram/board-run.log` and `fault.log`: reproduced fault.
- `build/irq-entry-hstx-c0ram-baseline/hashes.json`: frozen source and ELF/UF2 hashes.
- `build/irq-entry-252-hstx-c0ram-control`: lower CPU clock control.
- `build/irq-entry-504-f42-hstx-c0ram-control`: lower flash clock control.
- `build/flash-sdk-default-gpio8/board-run.log`: actual untouched SDK startup registers.
- `build/flash-sdk-profile`: stock-derived application candidate and saved earlier sources.

Failing UF2 SHA256:
`D67E4F2F6D2308C9EE4C387C9EAA8BAE9F8B964261885DFD3837110C22E9B5EA`.

Separately, the application linker rules missed this compiler's
`libg.a(libc_a-mem*.o)` objects. The corrected rules put memory helpers in
normal SRAM as intended. That placement change is not yet a demonstrated
fix for the fault. Diagnostic USB command `m` in the next application build
compares RAM code with its flash load image to detect persistent corruption;
`c` reports clock and QMI settings.
