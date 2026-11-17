# Plan: upgrade the emulator CPU from 286 to 386SX (fast, not exact), RP2350 only

## Context

Our CPU is a 286 in real mode only. Protected mode is not there, and nothing above 1 MB + 64 KB is mapped. The user wants a 386SX that runs:
- DOS extender games (DOS/4GW, PMODE/W, DOS32A, CWSDPMI);
- EMM386;
- Windows 3.1 in 386 enhanced mode.

So the job needs protected mode, 32-bit operations, paging and V86 mode.

**Rules:**
- Speed over exactness. YAGNI, KISS, DRY.
- One 386 core, with no CPU switch.
- **Targets: one firmware configuration, RP2350 M2P2 with HSTX HDMI, HDMI audio and 8 MB onboard QMI PSRAM, plus the host builds.** RP2040, VGA, TFT, NTSC, PIO HDMI, PWM/I2S/hardware sound, SPI PSRAM and the swap file are removed.
- We write the code ourselves from the Intel documents in `docs/`, so the MIT license stays. We do not read the PCulator, 86Box or Bochs source while writing; their programs may be used only as black boxes for comparing results. Note: I have already seen parts of PCulator in the earlier check.
- The old branch `origin/386-exp` is not used.

## Documents: stored in the repo as text, committed

Step 0 downloads them, converts them to UTF-8 text, and commits them. All sources below were checked to be online and readable.

| File in repo | What | Source | Text quality |
|---|---|---|---|
| `docs/intel/i386-prm-1986.txt` | Intel 80386 Programmer's Reference Manual, 1986 (230985) | `386INTEL.TXT` in http://cd.textfiles.com/freedom/CODE/TEXT/386INTEL.ZIP (cp437, convert to UTF-8) | Typed text, 878 KB, 20,874 lines, tables in ASCII (checked: Table 9-7 error codes reads clean) |
| `docs/intel/240187-008_386SX_datasheet.pdf` + `.txt` | Intel386 SX Microprocessor data sheet, 1995 | https://media.digikey.com/pdf/Data%20Sheets/Intel%20PDFs/Intel386%20SX.pdf | Digital text layer (Acrobat Distiller, not a scan), 102 pages; `.txt` made with `pypdf` in a temp venv, then checked by eye |
| `docs/specs/xms30.txt` | eXtended Memory Specification 3.0 (lock 0Ch, move 0Bh, functions 88h/89h) | https://www.phatcode.net/res/219/files/xms30.txt | Plain text, 48 KB |
| `docs/README.md` | One line per file: order number, source URL, which parts we use | — | — |

Parts of the manual we use:
- 5 (segments, paging);
- 6 (protection);
- 7 (TSS);
- 9 (interrupts, exceptions, error codes);
- 10 (reset state, mode switch);
- 14 (real mode);
- 15 (V86);
- 16 (mixed 16/32-bit code);
- 17 (instructions).

From the data sheet we use what is different from a 386DX: the 24-bit address (16 MB) and the DX value at reset.

**Test data** is too big for git, or has another license, so it stays outside git. `docs/README.md` says where to get it, and each runner takes a path:
- SingleStepTests/80386 (real mode, MOO format, ~576 MB) and SingleStepTests/80286;
- the test386.asm ROM.

## Limits we design for (RP2350 M2P2 firmware)

- **SRAM:** 364,424 B of 512 KB in use (369,720 B with GM.DLS). VIDEORAM is 256 KB of it, and `exec86` is 24.6 KB. About 145 KB is free for new code and data.
- **PSRAM:** 8 MB, of which 7.19 MB is in use:
  - RAM 640 KB;
  - HMA 64 KB;
  - UMB 176 KB;
  - EMS 2 MB;
  - XMS 4 MB.

  So a 386SX sees about 5 MB of the 16 MB it can address.
- **Cache:** PSRAM and flash share one 16 KB XIP cache. Hot code must be in SRAM (`__not_in_flash`), so that the cache is used for PSRAM data.

**Speed rules:**
- Real-mode 16-bit code (most DOS games) must not get slower by more than ~5%.
- No new checks on every memory access (see memory "No general checks in hot paths").
- New 386 code starts in flash, and moves to SRAM only after profiling shows it is hot.

## Design in short

- **Segment registers**: each has a selector plus a cached base (32-bit), limit, D/B bit and access byte.
  - A real-mode load sets base = sel << 4 and keeps the limit, so "unreal mode" works for free.
  - `segbase(x)` and `useseg << 4` (`cpu.h:65`, `cpu.c:268`, `cpu.c:402`) read the cached base. `useseg` becomes a segment index.
- **Operand and address size**: each instruction sets `opsize32 = CS.D ^ prefix66` and `addrsize32 = CS.D ^ prefix67`.
  - Handlers that have a word form get one `if (opsize32)`, and the 32-bit body is a function outside `exec86`.
  - The 16-bit `modregrm`/`getea` path stays as it is. A new `getea32` (with SIB) is used only when `addrsize32` is set.
  - The broken `CPU_386_EXTENDED_OPS` stub (`cpu.c:71`, `cpu.c:135`, `cpu.c:2307`) is removed. Its 32-bit addressing uses 16-bit register pairs, which is wrong.
- **Registers already there**: `dwordregs[8]` and a 32-bit `x86_flags` are already in place (`emulator.h:94`). We add the names IOPL, NT, RF and VM, and add CR0/CR2/CR3. DR0–7 and TR6/7 are only stored.
- **Memory**:
  - Physical addresses are 24-bit (wrap at 16 MB).
  - `XMS[]` sits right after the HMA in the memory functions in `memory.c`. The existing `readw_linear()` (`xms.c:216`) uses the same layout.
  - When CR0.PG changes, the `read86`/`write86`… pointers (set in `pico-main.c:965` and `win32-main.cpp:1198`) switch to paging versions. So paging costs nothing while it is off.
- **Paging**:
  - A direct-mapped TLB with 64 entries in SRAM (≤1 KB), flushed when CR3 is written.
  - On a miss, a two-level walk sets the A and D bits.
  - The present bit is checked, and a miss raises #PF with CR2 and an error code. U/S and R/W checks are added only if a target needs them.
  - A word or dword access that crosses a page boundary goes byte by byte.
- **Faults**:
  - `setjmp` once per `exec86()` call. A fault inside a memory access or a segment load does a `longjmp` back to the loop.
  - The loop then puts back the instruction start EIP and ESP and delivers the exception.
  - Partial register changes are not undone. #DF is minimal; a triple fault calls `reset86()`.
- **Interrupts in protected mode**:
  - IDT gates (286 and 386 interrupt and trap gates) and error codes.
  - On a privilege change, the new stack comes from SS0:ESP0 in the TSS.
  - From V86 mode, GS/FS/DS/ES are pushed and then cleared.
  - The C BIOS hooks in `intcall86()` (`cpu.c:479`) run only in real mode.
- **BIOS calls under V86** (EMM386 and Windows send INT 13h and others to the vector themselves, so `intcall86()` never runs):
  - INT 10h/13h/15h/19h/1Ah/2Fh vectors point to a small stub ROM area.
  - A stub runs an escape opcode, which calls the same C hook and then returns with `RETF 2` so CF stays set.
  - If the C hook does not handle the call, the stub jumps to the old vector.
  - The BIOS is a binary blob (`includes/bios.h`), so the stub is set up from C at the INT 19h hook (`cpu.c:675`).
- **Things we leave out on purpose (fast, not exact)**:
  - segment limit checks on access;
  - privilege checks on data segments (only the present bit → #NP, which Win16 needs);
  - the I/O permission bitmap and IOPL checks for IN/OUT in protected mode;
  - alignment check, debug traps, the prefetch queue, cycle timing, and undefined flags.
  - In V86 mode, the IOPL-sensitive instructions do fault: CLI, STI, PUSHF, POPF, INT n and IRET.

## Steps

Each step is one or more commits on branch `cpu386`, with a `/yagni` review. Each step must keep the Win32 and RP2350 (M2P2) builds working.

0. **Documents, one configuration, base line.**
   - Download and convert the documents and write `docs/README.md` (table above), then commit.
   - **One configuration**: everything except RP2350 M2P2 + HSTX HDMI + HDMI audio + QMI PSRAM is removed (RP2040, swap/virtual memory, SPI PSRAM, VGA, TFT, NTSC, PIO HDMI, PWM/I2S/hardware sound, M1P2 pins, the launcher layout). The memory functions now have one backend (`_ob`).
   - Add a host test runner `tests/sst_test.c` with its own small MOO reader (the format is simple chunks, see github.com/dbalsom/moo), and a host speed check `tests/cpu_bench.c`. Both use `tests/cpu_host.h`: the CPU alone with flat 16 MB memory.
   - Write down the pass count for SingleStepTests/80286 on the current CPU.
   - Write down the speed on host (`cpu_bench`) and of one fixed DOS benchmark on the M2P2 board.
1. **Segment descriptor cache, real mode only.** Done:
   - each segment register has a cached base (`segment_base[]`); every load goes through `load_segment()`; `getmem*`/`putmem*` and `useseg` take a base. Limit and access rights wait for step 4, a 32-bit EIP for step 2 (YAGNI);
   - nothing from the old 32-bit attempts is kept: the `CPU_386_EXTENDED_OPS` stub, the unused 32-bit helpers and macros, the AC/VIF/VIP/ID flag fields, FS/GS and the 64h/65h prefixes are gone;
   - `MOV Sreg` with reg 4–7, and `MOV CS`, raise #UD;
   - gate: 80286 tests 1,370,634 → 1,372,115 (only 8C/8E changed, both up); host speed equal or better (see the base line note); `exec86` 24.6 → 23.6 KB of SRAM. DOS and games on the board: to be checked with CheckIt.
2. **32-bit instruction set in real mode.** Done:
   - 66h and 67h set `opsize32`/`addrsize32`; after the prefixes one branch sends the instruction to `exec386()` (`src/emulator/cpu386.c.inl`, flash), which handles only the opcodes whose meaning depends on the size and otherwise returns to the 16-bit switch. `modregrm`/`getea` have one branch for 32-bit addressing (SIB, `modregrm32`/`getea32` in flash);
   - FS/GS (prefixes, MOV Sreg, PUSH/POP, LFS/LGS/LSS); new 0F instructions: Jcc near, SETcc, MOVZX/MOVSX, BT/BTS/BTR/BTC, BSF/BSR, SHLD/SHRD, IMUL r,rm. MOV CRx/DRx come with step 4;
   - 386 behaviour found with the test data: shift counts masked to 5 bits, OF from the last one-bit step, DAA/DAS test the old AL and CF, a SIB byte with no index scales the base, 16-bit SHLD/SHRD with counts over 16 fill from the source again, POPAD keeps the high half of the ESP slot, PUSHF/POPF/IRET keep IOPL/NT (bits 12-14: this is how programs tell a 386 from a 286), SMSW gives the 386 CR0 low word (ET), DX = 2308h at reset;
   - also fixed for both CPUs: XCHG with memory, XLAT wrap, #UD for register LEA/LES/LDS/BOUND and C6/C7/8F with reg ≠ 0, BOUND returns to the instruction;
   - gate: SingleStepTests 80386 real mode 91.93% overall; **99.98%** without the cases left for later on purpose (109k limit faults → step 4, 31.6k LOCK #UD, 1.1k EIP past FFFFh). 80286 suite 98.25% (was 92.84%): the 286 tests of POPF and IRET (9D, CF) expect the 286 rule that clears bits 12-15, so 8.7k of them fail on purpose. Host speed: within noise of step 1 (−9% to +8% depending on compiler settings); the board (CheckIt) decides. RP2350 SRAM +484 B over step 1. Board (CheckIt v3): 3,400 Dhrystones against 3,489 for step 1 (−2.6%).
3. **Memory above 1 MB and a real XMS driver** (from `docs/specs/xms30.txt`).
   - Add XMS to the address space.
   - In `xms.c`:
     - allocate (09h) gives each handle its own block;
     - add lock (0Ch) returning the linear address, unlock (0Dh), info (0Eh) and realloc (0Fh);
     - move (0Bh) works with handles on both sides;
     - INT 15h 88h agrees with it.
   - Gate: a new host test `tests/xms_test.c`, and the existing INT 15h 87h path still works.
4. **Protected mode.**
   - CR0 PE/MP/EM/TS/ET; GDT, LDT, IDT; segment loads in protected mode.
   - Far JMP/CALL/RET/IRET with privilege change and call gates.
   - IDT interrupts and exceptions, TSS stack switch.
   - LTR/STR/LLDT/SLDT/LAR/LSL/VERR/VERW/ARPL.
   - The longjmp fault path; return to real mode by clearing PE.
   - Gate:
     - the test386.asm protected-mode parts pass in a host runner `tests/test386_test.c`, which loads the ROM at F0000 and reads the POST port;
     - a DOS/4GW program (Doom shareware), PMODE/W and DOS32A programs run on host;
     - Windows 3.1 `win /s` starts.
5. **Paging.**
   - CR3, page walk, TLB, A/D bits, #PF, pointer switch.
   - Gate: the test386 paging part passes, and a DJGPP program with CWSDPMI runs.
6. **V86 mode.**
   - EFLAGS.VM, real-mode style segment loads, IOPL-sensitive faults, interrupt entry and exit for V86.
   - The BIOS stub ROM with the escape opcode.
   - Gate:
     - EMM386 (MS-DOS 6.22) loads, `MEM` shows UMBs, and games run under it;
     - Windows 3.1 starts in 386 enhanced mode and opens a DOS box.
7. **Hardware task switch: only if a target fails without it.**
   - JMP/CALL/INT to a TSS or task gate, and IRET with NT.
8. **Speed pass on the M2P2 board.**
   - Profile.
   - Move only the measured hot 386 code to SRAM.
   - Add a fast code-fetch pointer for the current page if fetch through the TLB costs a lot.

## Files to change

- `docs/intel/`, `docs/specs/`, `docs/README.md`: new, committed.
- `CMakeLists.txt`: one firmware configuration (step 0, done).
- `src/emulator/cpu.c`, `cpu.h`: decoder, segment cache, 32-bit ops, `intcall86` real-mode guard.
- New `src/emulator/protected.c.inl` (descriptors, gates, protected-mode and V86 interrupts, TSS) and `src/emulator/paging.c.inl` (TLB, walk), in the same `.c.inl` way the audio code uses.
- `src/emulator/emulator.h`: flag names, CR registers, memory map constants.
- `src/emulator/memory.c`: XMS in the address space, 24-bit wrap, paging versions of the memory functions.
- `src/emulator/xms.c`: real allocator and lock.
- `src/pico-main.c`, `src/win32-main.cpp`, `src/linux-main.cpp`: only if the pointer setup moves.
- `tests/`: `sst_test.c`, `cpu_bench.c`, `cpu_host.h` (step 0), `xms_test.c`, `test386_test.c`. Test data stays outside git.

## Base line (step 0, master `efe313e`, 2026-11-17)

- **SingleStepTests/80286, real mode** (`tests/sst_test.exe <folder>`): 1,367,098 passed, 110,792 failed, 110 skipped (92.50%); 196 of 326 files with no failures. The same every run. Most failures are undefined flags (shifts, IMUL, AAD, DAS), missing #UD (`MOV CS`, `BOUND`/`LEA`/`LES` with a register), ESC, and far JMP/CALL through memory. Per-file numbers: run the tool again on master.
- **Host speed** (`tests/cpu_bench.exe`, MinGW gcc 15.2 -O2): 205–218 M steps/s. One run can be ~6% off, and code layout alone moves it by ±5%, so compare old and new in turns, built with `-falign-functions=64 -falign-jumps=32 -falign-loops=32 -falign-labels=16`. With these flags step 1 gives 250 M steps/s against 222–235 for step 0.
- **Since step 1 the runners link the real `fpu.c`** (the stub let failed ESC tests run into random bytes). The step 0 count with `fpu.c` is 1,370,634 passed.
- **The 80386 suite** (SingleStepTests/80386 v1, real mode, 386EX): the runner masks EFLAGS bits 18-31 (set by the 386EX test board) and the flags `80386.csv` marks undefined (`f_umask`), and skips `revocation_list.txt`. Step 1 passes 13.77% before the 386 work.
- **Board after step 1** (CheckIt v3): 3,489 Dhrystones against 3,545 for step 0 measured the same day (−1.6%).
- **M2P2 board, CheckIt v3 Main System Benchmark** (the board gate for real-mode speed): CPU 3,584 Dhrystones = 10.42 times IBM PC-XT, shown as "80286, 46.25 MHz"; math 1,785.5K Whetstones = 270.52 times PC-XT (80287). Measured by the user on the firmware before the one-configuration clean-up.
- **M2P2 firmware after the clean-up** (`286-P166-C500MHz`): SRAM 364,424 B of 512 KB (69.51%), PSRAM 7,192,560 B (85.74%).

## Checks for each step

- Build Win32 (CLion MinGW) and RP2350 (M2P2 HSTX). `arm-none-eabi-size` shows that SRAM still fits.
- Run the host test runners for that step and compare with the step 0 numbers.
- Boot DOS on host and run 2–3 known real-mode games.
- From step 4 on, run the target programs for that step on host first, then on the M2P2 board.

## Risks

- **Speed:** how fast the emulated 386 is on the RP2350 is unknown. Windows 386 enhanced mode and Doom may run but be too slow to use.
- **XMS size:** with 8 MB of PSRAM, XMS stays at 4 MB, unless our EMS (2 MB) is dropped later.
- **EMS conflict:** our EMS window at C0000 and EMM386 may conflict. This is to be checked at step 6; the first try is EMM386 with NOEMS.
- **Ports in Windows:** with the I/O bitmap left out, Windows cannot catch port access from DOS boxes. One DOS box should be fine.
- **Copyright:** the committed Intel and Microsoft documents carry their copyright. You chose to commit them.
