# HDMI scanout captures

The parent session captured these live from the board's HSTX output, with
NTSC enabled in the standalone video test. These are scanout tests, not whole
PC emulator performance tests. The HDMI capture does not verify the analog
NTSC picture on a separate display.

| Capture | Visual test UF2 SHA256 | PNG SHA256 |
| --- | --- | --- |
| [80x25 text](hdmi-80x25.png) | `28235340CB22143DD9B27B76F1D4A832C3A898661565F2D8B0A3DF155CB2AC11` | `3D3DA5A98F22FFF96EF33B966605BC9BAEDD42409EF7985F0A2FA712E5BB63FA` |
| [640x480 graphics](hdmi-640x480.png) | `15F9D9716A20D0D25FFF2540533B43759CB09436442938E962CF4635AAE2F834` | `A30CBE39B8EDBEF54948E055F71BE412E42FDD1D5786744F97F0E85EBFB46AD4` |

The 80x25 image came through the USB 3.0 UHD capture device at 1920x1080;
that capture size is not the source video mode's resolution. The parent
reported a full graphics border and visible bottom rows in the 640x480 test.

## Actual PC firmware

The final M2P2 HDMI+NTSC PC firmware was also loaded on the board. It booted
DR-DOS 8.1 and Volkov Commander from the existing SD card. The capture shows
the full 80x25 text screen with the emulator running, not just the video test.

- Firmware: `m2p2-286-F100-P166-315MHz-HDMI-HSTX-NTSC-PWM.uf2`.
- Firmware SHA256: `A0EC874CE21B03DB09CC254C4069054A1C0883B2395BE83482923B2F4E3FF189`.
- [PC 80x25 capture](pc-hdmi-80x25.png), SHA256
  `E9E7237C5F85820A7EF299681FCDFF2B40F8FA76881AB574BF7904BFD6BFFB12`.
- A further 20-second capture sampled 100 frames at 5 fps. No sample was a
  black frame. This is a sampled picture check, not a count of every HDMI frame.

All seven M2P2 presets built with exit code 0: HDMI, VGA, TFT, NTSC, and each
of HDMI/VGA/TFT with NTSC. The separate RP2040 PIO driver test also built.
The live NTSC analog signal, VGA connector, and TFT panel were not captured.
