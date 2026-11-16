# 500 MHz with independent HSTX clock

The user requested CPU 500 MHz with HSTX on PLL_USB, then requested the
merged HDMI-audio output. The build keeps VREG at 1.60 V and HSTX at 126 MHz.
USB and ADC clocks are stopped. Scratch banks remain dedicated to guarded
4 KB stacks.

HDMI audio is selected with PWM, I2S, and hardware sound disabled. The new
DMA row-copy path explicitly enables write-address increment; Pico SDK's
default is a fixed destination address.

The full HDMI-audio build and paired HDMI+NTSC build pass. Host packet and
flash-timing checks pass. The final image was copied to the main RP2350
after checking its BOOTSEL board ID. The user confirmed Dave 2 could be
started, played, exited, and started again without a hang.
The user later also reported Doom 8088 ran without hangs on this build.

No further audio test was made after the user said audio was already
verified. Stability remains under user observation. This profile changes
both CPU frequency and the HSTX clock source, so the result does not isolate
the 4 MHz reduction alone.

Final UF2 SHA256:
`235EF1934FC9E04A69119EE63209D0F8A4A638F84CDFF196F2EB9F07673DDAB8`.

Matching ELF/UF2, build logs, and source patch are saved locally under
`build/hstx-500-usbpll`. Output:
`bin/rp2350-arm-s/Release/m2p2-286-F100-P166-500MHz-HDMI-HSTX-HDMIAUDIO.uf2`.
The next flash requires BOOTSEL on reset/reconnect because USB is disabled.
