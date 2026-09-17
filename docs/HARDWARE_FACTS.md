# Hardware facts

| Fact | Value | Evidence / confidence |
|---|---|---|
| Target | ESP32-C3 QFN32, revision v0.4 | Live esptool ROM probe, confirmed |
| Crystal | 40 MHz | ROM probe, confirmed |
| Flash | 4 MB XMC, JEDEC 0x20 / 0x4016 | Live flash_id, confirmed |
| Native USB | COM5, USB Serial/JTAG | Windows PnP + ROM probe, confirmed |
| Device MAC | 94:a9:90:8a:05:48 | Live ROM probe, confirmed |
| USB pins | GPIO18 D-, GPIO19 D+ | ESP32-C3 documentation; reserved |
| LED | GPIO8, 4 WS2812 | BSP + actual tool call; user confirmed all four LEDs red |
| BOOT / buttons | GPIO9 / GPIO0 / GPIO1 | BSP; M2 exposes input-only GPIO reads/waits |
| Microphone | GPIO2 / ADC1 CH2, nominal 16 kHz | M1 live sampling and on/off tests; analog parts unverified |
| Speaker | PDM GPIO6/7, PA GPIO3, nominal 24 kHz | M1 playback; user confirmed normal melody and suitable volume |
| LCD | MOSI4 / SCLK5 / DC10, CS/RST NC, 160x80 | Pinned BSP register profile implemented in 0.6.1; visible pixels pending |
| Body | GPIO18/19/20/21 | BSP; excluded from firmware |
| PCB revision / BOM | UNKNOWN | Cannot infer from USB identifiers |
| LCD controller | CONFLICT / UNCONFIRMED | ST7789 macro conflicts with ST7735-named custom table passed to ILI9341 API; fitted chip not read back |

Reference: https://github.com/78/xiaozhi-esp32 at `ac6deed3d8e75348475364bf40ad953c6cd48054`.

The reference config lists both USB console and console-none; M0 explicitly selects USB and never copies the body-control condition. No external electrical-instrument or EDA inspection is claimed. M1 microphone values are relative ADC level measurements, not calibrated voltage or sound pressure.

Original full flash backup: `artifacts/backups/esp32c3-original-4mb.bin` (ignored, may contain prior private configuration).
SHA-256: `27689ECCB1E895413641FD35AA6A593D4545EBFD86F1C345D92C18531A009FAA`.

M2 migration preserves the same hardware and NVS, with a second full backup documented in `M2_MIGRATION.md`. No spare general-purpose output is verified: GPIO4/5/10 belong to the LCD (available as general GPIO only when display is off), 18/19/20/21 to the excluded body interface, 12–17 to Flash and 11 is unverified. M2 granted only GPIO0/1/9 input access. Under the user's 2026-09-15 GPIO expansion, the lean profile also exposes LCD4/5/10 and body20/21 for input/output/PWM; these are documented board signals, not electrically unconnected pads. They boot as floating inputs; buttons retain pull-ups. USB18/19, Flash12–17 and unverified11 remain protected. Managed drivers own LED/audio pins; shared resource claims prevent raw GPIO from reconfiguring them.

The five-second PCM clip committed and replayed with correct sample counts. The user initially reported heavy hiss; biquad/spectral processing alone did not resolve it. Digital silence was quiet, and the identical processed PCM was clear on the PC. Switching the board to IDF's direct-DAC PDM defaults with unity hardware scales and master volume 80 produced the user report “人声清楚，沙沙声减轻”. This establishes intelligible replay of the tested clip, not exact microphone/amplifier BOM, calibrated sound output or complete noise removal.

The 0.6.1 LCD extension dynamically owns GPIO4/5/10 as resource owner 5. It uses the pinned BSP profile rather than claiming an inferred chip ID; see [display contract](DISPLAY_SPEC.md). GPIO20/21 are now exposed as general GPIO with their original body-interface role recorded; no servo driver is enabled.
