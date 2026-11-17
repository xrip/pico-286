# Documents for the 386 work

Text copies of the documents we use for the 386SX CPU (see `386-migration.md`). They are © Intel and © Microsoft; they are kept here only for reference.

| File | What | Source |
|---|---|---|
| `intel/i386-prm-1986.txt` | Intel 80386 Programmer's Reference Manual, 1986, order 230985. Typed text, converted from cp437 to UTF-8 | `386INTEL.TXT` in http://cd.textfiles.com/freedom/CODE/TEXT/386INTEL.ZIP |
| `intel/240187-008_386SX_datasheet.pdf` | Intel386 SX Microprocessor data sheet, order 240187-008 (1995) | https://media.digikey.com/pdf/Data%20Sheets/Intel%20PDFs/Intel386%20SX.pdf |
| `intel/240187-008_386SX_datasheet.txt` | Text of the PDF above, from its text layer (`pypdf`, one `===== Page N =====` line per page) | made from the PDF |
| `specs/xms30.txt` | eXtended Memory Specification 3.0, January 1991 | https://www.phatcode.net/res/219/files/xms30.txt |

## Where to look in the manual (`i386-prm-1986.txt`)

| Line | Part | What we take from it |
|---|---|---|
| 4723 | Chapter 5, Memory Management | descriptors, segment loads, paging, page tables |
| 5434 | Chapter 6, Protection | privilege levels, call gates |
| 6653 | Chapter 7, Multitasking | TSS layout (SS0:ESP0), task switch |
| 7707 | Chapter 9, Exceptions and Interrupts | IDT gates, error codes (Table 9-7) |
| 8839 | Chapter 10, Initialization | reset state, going into and out of protected mode |
| 10569 | Chapter 14, Real-Address Mode | real-mode rules on a 386 |
| 11066 | Chapter 15, Virtual 8086 Mode | V86 mode, IOPL-sensitive instructions |
| 11759 | Chapter 16, Mixing 16-Bit and 32-Bit Code | operand and address size |
| 12202 | Chapter 17, Instruction Set | every instruction |
| 20322 | Appendix A, Opcode Map | one- and two-byte opcode tables |

From the 386SX data sheet: the physical address is 24-bit (16 MB), and after reset DH = 23h (Intel386 SX) and DL = the stepping (08h for steps C to E), so DX = 2308h (section 5.6, Table 5.7). The registers after reset are in Table 2.8.

## Test data (not in git)

These are too big for git, or have their own license. Each test runner takes the path as an argument. The MOO files come gzipped: get them with `gh api repos/SingleStepTests/80286/contents/v1_real_mode --jq '.[].download_url'` and `curl`, then unpack with `gzip -d *.MOO.gz`.

- SingleStepTests 80286 and 80386 (real mode, MOO format): https://github.com/SingleStepTests/80286, https://github.com/SingleStepTests/80386
- test386.asm ROM: https://github.com/barotto/test386.asm. Build it with NASM and run `tests/test386_test.exe test386.bin`. Changes to a copy of the source before the build:
  - NASM 3 needs the size on the memory side: `mov [x], word y` becomes `mov word [x], y` (`sed -i -E 's/mov(\s+)\[([^]]*)\],\s*(byte|word|dword)\s+/mov\1\3 [\2], /' src/*.asm src/tests/*.asm`);
  - in `src/test386.asm`, put `%if 0` / `%endif` around the test 12 checks (from "#GP(0) If the destination operand is in a non-writable segment" to the LOCK test) and around the ENTER page-fault test ("The ENTER instruction causes a page fault" to its `testCPL 0`): the emulator leaves these out on purpose;
  - then `nasm -isrc/ -f bin src/test386.asm -l test386.lst -o test386.bin`. With `ROM128 equ 1` in `src/configuration.asm` the ROM is 128 KB and has more tests (test 22: task switches); the runner takes both sizes. The runner prints the POST codes, and on an error the last EIPs, which can be found in `test386.lst`.
