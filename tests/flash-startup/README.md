# RP2350 startup test

This test keeps USB at 48 MHz and does not start HSTX. It prints the flash ID
and status registers, runs the real QE setup, then checks uncached flash
reads at 504 MHz and 1.60 V. It reads clock settings from the SD card without
applying them. A watchdog records the last stage if startup hangs.

```sh
cmake -S tests/flash-startup -B build/flash-startup -G Ninja
cmake --build build/flash-startup
```

Use the usual `PICO_SDK_PATH` and ARM toolchain settings. Load
`build/flash-startup/flash_startup.uf2`, then open its USB serial port with
DTR enabled. Send `b` to return to BOOTSEL.

The default uses the RP2350 ROM's initial flash setup. For the old embedded
W25 boot stage, set `TEST_EMBED_BOOT=ON`. Adding `TEST_SERIAL_BOOT=ON` selects
the generic-03h stage that failed before USB startup on the tested board.

The QE check is the production implementation, so it can set SR2.QE when
clear. It skips status writes when QE is already set. No flash erase is used.
