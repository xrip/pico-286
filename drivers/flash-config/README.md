# Automatic flash QE setup

`flash_config_init()` checks SR2 bit 1 at each M2P2 boot. If it is set, no
write command is sent. Otherwise the code sends WREN, checks WEL, and writes
only `old_sr2 | 2` with command `31h`. It waits for WIP to clear, then checks
SR2 and the unchanged SR1 protection bits. There is no JEDEC gate.

The protocol follows the [flash_config utility](https://github.com/fhoedemakers/flash_config/blob/main/nuke.c).
The register definitions are in the manufacturer datasheets for
[Winbond W25Q32JV](https://www.winbond.com/resource-files/w25q32jv%20revg%2003272018%20plus.pdf),
[Winbond W25Q128JV](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf),
and [Puya PY25Q128HA](https://www.puyasemi.com/download_path/%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/Flash/PY25Q128HA_Datasheet_V2.3.pdf).

## Startup order

1. Boot with the RP2350 ROM's read-mode detection (`PICO_EMBED_XIP_SETUP=0`).
2. Hold PSRAM CS high and save the ROM flash timing and system clock. Call
   `flash_config_init()` on core 0 before clock changes, DMA, core 1, or
   QMI PSRAM setup. Keep the ROM's QSPI pad settings.
3. Continue only if it returns true; quad XIP is then set at divider 12.
4. Use `flash_timing_for_clock()` to preserve the boot flash rate and minimum
   physical timing intervals. Apply the timing from SRAM with IRQs masked
   and QMI idle; make an uncached read with barriers before raising SYS.
5. Configure PSRAM with IRQs masked during QMI direct mode.

On the measured board the SDK boot profile was 150 MHz SYS / 50 MHz flash
(`60007203`). At 504 MHz SYS, the derived profile is `6381970b`, giving
45.82 MHz flash. RXDELAY is in half-SYS cycles and is limited to 0..7;
using the SCK divider as RXDELAY can overflow the field. Unrepresentable
derived timing is rejected. A user flash frequency limit can lower the boot
rate, but does not raise it. Explicit raw timing overrides remain explicit.

All code in the command session runs in SRAM with IRQs off. It exits XIP once
and uses QMI direct mode throughout the transaction. It does not call
`flash_do_cmd()`, which would replay boot2 after each command. The SDK's
W25Q080 boot2 can rewrite both status registers and is therefore unsuitable
for this sequence. The first embedded generic-03h boot test failed before
USB startup on the test board, so no embedded boot2 runs on M2P2.

On WEL or verification failure, the function returns false with serial `03h`
XIP restored. On a 100 ms busy timeout it enters ROM BOOTSEL; it never returns
to flash code while the flash is busy. The ROM read-mode call changes both
QMI windows, which is why PSRAM setup must come later.

## Host test

```sh
gcc -std=c17 -O2 -Wall -Wextra -Werror -DFLASH_CONFIG_TEST=1 \
  -Idrivers/flash-config -Idrivers/flash-config/tests \
  drivers/flash-config/tests/flash_config_test.c \
  drivers/flash-config/flash_config.c -o flash_config_test
./flash_config_test
```

The test covers every combination of the other SR2 bits, an already-set QE
bit, repeated boots, WEL failure, rejected writes, and busy timeouts. The test
port is used only by host builds. No test writes to a physical flash chip.
