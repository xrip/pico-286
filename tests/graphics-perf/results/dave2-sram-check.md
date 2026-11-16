# Dave 2 fault and scratch-bank audit

The user reported another hang after the EGA page/stride change. This was
not just a guest waiting in DOS: core 1 entered HardFault and stopped HDMI
while core 0 continued running.

## Fault controls

The matching original ELF/UF2 is saved in `build/game-relaunch-audit`.
Its UF2 hash is `13F230D41C3DD13976BCFCA0D7C2EBF93CE95740C3A46AE7BF983E03E22B2625`.
At 504 MHz SYS / 50.4 MHz flash, the faulting CMS instruction was
`STRH r0,[r2,r3,LSL #1]` at `20008780`. Its fixed pointer should have been
`20054a08`; the saved r2 was `61014100`. The index was valid. RAM code still
matched its flash load image.

A control changed only two UF2 payload bytes, replacing the flash timing
initializer with `6381970b` (45.82 MHz). All instruction addresses and code
remained identical. It failed at the same instruction with the same bad
base pointer. Its hash is
`771083BA6E4849142F2A7B7F578213FA9D41E991EF8A29BE8328308EEEB5C4A6`.

A host test compared 76,800 EGA rows against independent plane decoding,
including start addresses `4f80`, `9000`, wrap edges, and
CRTC strides. Output canaries and inaccessible VRAM guard pages detected
no out-of-bounds access. This does not establish hardware timing safety.

## SRAM migration

RP2350 scratch placement was removed from the OPL C++ and assembly tables,
swap-page metadata, legacy TV palettes/code, and optional NTSC palettes.
RP2040 branches remain unchanged. The two RP2350 linker scripts now reject
nonempty `.scratch_x` and `.scratch_y` sections. Each scratch bank holds only
its 4 KB stack, with `PICO_USE_STACK_GUARDS=1`.

Normal HDMI firmware uses 364,460 bytes of the 512 KB main SRAM region.
The 1,024-byte log-sine table and 656-byte assembly table block were verified
byte-for-byte against the original image at their new main-SRAM addresses.
HDMI, HDMI+NTSC, VGA+NTSC, and TFT+NTSC builds pass with empty scratch code/data
sections and 4 KB stacks.

## Guarded interrupt-frame test

A diagnostic copy based on EGA revision `64abf9b` compares the eight basic
exception-frame words before and after each HSTX interrupt. It also records
stack high-water usage. This extra tracing is not in normal production code.

At 504 MHz it still faulted, now on a fixed `tone_counter` pointer in the
SN76489 sampler. The stored interrupt frames were unchanged, stack limits
were active, and peak stack usage was 1,424 bytes on core 0 and 500 bytes on
core 1. No stack-limit fault or RAM-code mismatch was recorded. The mixer
calls CMS and SN76489 code even for a PC Speaker-only game.

The diagnostic hash is
`18FB60112A00D49A57EF4A4AFC1F3DA431FE47966A1E842616FA729D6EFD17D3`.
Its source copy, ELF, and logs are under `build/irq-frame-audit-source` and
`build/game-relaunch-audit`.

The 378 MHz control retains 1.60 V, HSTX 126 MHz, PSRAM 126 MHz, the same EGA
logic, guards, and tracing. Derived flash timing gives 42 MHz. It completed
a 303-second observation without faults or changed interrupt frames; core 0
and core 1 stack usage remained 1,424 and 492 bytes. Its hash is
`CE72E02A55178DE5EC148AAB6FFE478388FD21DDE2046C01A72840E65904B4FE`.
A later read at frame 56,905 still showed no fault or changed interrupt
frame, with unchanged stack high-water marks and intact RAM code.

This points toward a 504 MHz timing problem. It does not prove the exact
electrical cause or unlimited stability at 378 MHz. At this stage, the board
was left on the 378 MHz diagnostic. The later 500 MHz profile and board
results are recorded in [the next test](hstx-500-audio.md).
