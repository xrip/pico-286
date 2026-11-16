# 504 MHz HSTX-only board check

The fixed firmware was loaded on the user's Frank/M2P2 board and observed
through the USB3.0 UHD HDMI capture device.

- Firmware: `m2p2-286-F100-P166-504MHz-HDMI-HSTX-PWM.uf2`.
- SHA256: `6C8443A939B3FFE8EB04B111105DFA1A24FA5B6A0E012EF6ECB836BF09F6F45E`.
- CPU: 504 MHz at 1.60 V; HSTX: 126 MHz; NTSC disabled.
- Flash ID: `EF4016` (Winbond, 4 MB), SR1 `00`, SR2 `02`.
  QE was already set, so the automatic check made no status write.

## Startup fault and fix

The first build (`72881CAE...`) gave no HDMI picture. A diagnostic with its
embedded generic-03h boot stage also failed to enumerate USB while still at
the normal 150 MHz clock, before the QE check or overclock.

Diagnostics using the old W25 boot stage and the RP2350 ROM's default setup
both reached main, passed QE setup, reached 504 MHz, and passed 100 uncached
64 KiB flash-read checks. The final firmware uses the ROM setup and prepares
QE, quad reads, and flash timing from RAM before raising the clock.

The QE command session also restores the fast 8 mA SCLK pad and disables
input Schmitt on the flash data pins. These settings were previously made by
the old W25 boot stage and were lost when that stage was removed.

- [ROM-startup diagnostic log](hstx-504-startup.log)
- [SD check log](hstx-504-sd.log): SD mounted; neither searched config file existed.

## Running firmware

The full firmware booted DR-DOS and Volkov Commander in HDMI 80x25. A
45-second capture sampled 225 frames at 5 fps, with no black samples. This
does not count every HDMI frame or establish long-term overclock stability.

The user corrected the board switches and confirmed wireless USB keyboard
input through Hecate. A later capture shows CheckIt completing one pass:
3501 Dhrystones, 9467 video CPS, and 1710.3K Whetstones. These are the
emulator benchmark's displayed results, not the RP2350 clock measurement.

[Working screen](hstx-504-working.png), SHA256:
`11F6F55BFE017F874AF79A8B961FCCD6F8E5C79451C9D099E8E90CC44A99831C`.

The 315 MHz HSTX+NTSC regression build and host QE protocol tests also passed.
