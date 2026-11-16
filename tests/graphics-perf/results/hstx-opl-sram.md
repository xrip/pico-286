# HSTX and OPL2 board test

The Frank/M2P2 board showed a black screen in Mode 13h games with OPL2.
Core 0 and USB remained live; core 1, which runs audio and HDMI interrupts,
had entered HardFault. Text mode and a game without OPL2 worked.

## Controls

| CPU | OPL assembly | Result |
| --- | --- | --- |
| 504 MHz | Flash, shared HDMI IRQ | Fault during DMA IRQ1; stored vectors and shared handler slots unchanged |
| 504 MHz | Flash, direct HDMI IRQ | Same failure; fault-time core 1 VTOR and vector matched the installed handler |
| 252 MHz | Flash, direct HDMI IRQ | Game visible; 25-second sample had no fault or HDMI restart |
| 504 MHz | Normal SRAM, direct HDMI IRQ | Initial game checks passed; later Dune II fault at frame 72,142 |

The final capture shows Monkey Island. Its Mode 13h frame count advanced
from 41,105 to 44,437 during the recorded sample; the core 1 service count
also advanced. The user then confirmed Dune II also works on this build.
A live sample during that test showed Mode 13h, frame count 71,926, zero
HDMI restarts, and no recorded fault. Prince of Persia and audible OPL2
output still need confirmation on this exact build.

Dune II subsequently stopped at frame 72,142. Three repeated samples showed
the same frame count and core 1 HardFault: PC `30001416`, CFSR `00000100`
(instruction bus error), HFSR `40000000`, xPSR `6900001b` (DMA IRQ1).
Fault-time VTOR `20000000` and vector `2000bc85` still matched the installed
handler. Core 0 and USB remained live. This supersedes the earlier claim of
a working fix: SRAM placement did not eliminate the failure.

Both clock controls kept HSTX at 126 MHz, USB at 48 MHz, and VREG at 1.60 V.
The configured QMI flash and PSRAM rates were 84 and 126 MHz in both controls.
The 252 MHz changes were temporary; normal build settings remain 504 MHz.

The kept change selects `.time_critical.slot_render_asm` on RP2350 in
`src/emu8950/slot_render_pico.S`. It moves 2,048 bytes of assembly and its
function table into normal SRAM. The assembly logic and scratch RAM usage
are unchanged. RP2040 keeps its original flash section.

The exact failure mechanism remains unresolved. The short 252 MHz run is
also insufficient to establish long-term stability. These are functional
checks, not new performance benchmarks.

## Exact builds and evidence

| Build | UF2 SHA256 |
| --- | --- |
| 504 MHz, flash OPL, direct IRQ | `3EA388795DAA5A78E96702E16693A4E977AD72286312663ED64B8FBB3EAC10F9` |
| 252 MHz, flash OPL, direct IRQ | `D111CA9FE2464014A04685C4EAAF76DA055F2D35276B1F9B2B4E724667BF8DB8` |
| 504 MHz, SRAM OPL, direct IRQ | `9FBB9110B4800A470C0D27BAF3B3F155DCA52CD4034864660BAC925FDBBA9259` |

Raw final status: [game log](hstx-opl-sram-run.txt).
The matching ELF/UF2 files, captures, and earlier fault logs are local ignored
artifacts in `build/mode13-exclusive-live`, `build/mode13-252-live`, and
`build/mode13-opl-sram-live` at the repository root.

These are historical diagnostic builds. The normal output path is
`bin/rp2350-arm-s/Release/m2p2-286-F100-P166-504MHz-HDMI-HSTX-PWM.uf2`,
but that file can be replaced by later builds. Use the hashes and local
snapshots above for this experiment. See [later isolation work](irq-entry-isolation.md).
