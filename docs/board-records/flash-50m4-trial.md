# 50.4 MHz flash trial

At the user's request, a separate trial increases flash from 45.82 to
50.4 MHz while keeping CPU 504 MHz, HSTX 126 MHz, PSRAM 126 MHz, and VREG
1.60 V. The flash clock rises 10%; this is not a measured emulator speed gain.

Live M0_TIMING is `6381a70a`: divider 10, RX 7, setup 1, hold 3, and minimum
deselect 26. Stock pad settings and the read format remain in use. Relative
to the prior `6381970b` profile, RX/hold/deselect times are unchanged, but
chip-select setup drops from 13.889 ns to 11.905 ns. This is a trial profile,
not strict preservation of every conservative stock-derived interval.

Boot passed with no fault; all 63,160 RAM-code bytes matched the flash load
image. The user confirmed games remained stable at 50.4 MHz and asked
to leave this version running for their own monitoring. No further reset
or tuning was performed. This is a board-specific observation, not a
guarantee for every flash part.

Running UF2 SHA256:
`A5E897022252A988F19A025299BBF14E935BDAB14725EB4135BEDFB42E2E6946`.
Matching ELF, UF2, test sources, and live register log are retained locally
in `build/flash-50m4-test`.

The normal source configuration still uses the conservative profile. The
tested 45.82 MHz fallback is in `build/flash-sdk-profile`, SHA256
`3E564A9D79369A07AC850C4472A74D5D03F351BC62BF245BFCDA9B02606F69BE`.
Use the ELF matching the running trial when decoding any later fault.

The later source review corrected extended FP exception-frame decoding.
The running trial predates that logger fix; its basic-frame records remain
valid, while extended-frame PC/LR decoding needs that correction.

## Reusing the setting

At 504 MHz SYS, the normal firmware can use the same timing through the
existing `config.286` setting on the SD card (root or `xt` directory):

```ini
FLASH_T=6381a70a
```

The trial image embedded this override at build time. The committed default
keeps ROM-derived timing; no extra build option or timing policy is needed.
