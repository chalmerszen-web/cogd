# Hardware audit

The evidence table is `pinmap.csv`; machine-readable facts and conflicts are `hardware_facts.json` and `conflicts.json`. Source code is not treated as a schematic.

Confirmed on the actual board: ESP32-C3 revision v0.4, 4 MB XMC Flash, native USB COM5, and four LEDs physically observed red after a real model tool call. The GPIO8 RMT driver was exercised; USB remained active. The original M0 did not initialize body pins. The current GPIO profile exposes GPIO20/21; USB18/19 remain protected.

The later M1 extension exercised PDM GPIO6/inverted GPIO7 with PA GPIO3 and ADC1 CH2/GPIO2. The user directly confirmed normal melody and suitable volume. ADC samples progress while enabled and stop while disabled; relative levels respond during speaker playback. Part identities and electrical design remain unknown. Significant ADC clipping during speaker playback is recorded in audio-hardware.json; no acoustic echo cancellation or full-duplex speech-recognition quality is claimed.

The reference at ac6deed3d8e75348475364bf40ad953c6cd48054 defines USB-overlapping body outputs on GPIO18/19 and contradictory LCD naming/initialization. These are explicit conflicts, not silently resolved electrical facts. M0 excludes both body and LCD code.

Unverified: board revision, schematic net names, PCB destinations, microphone model/preamp/bias/filter, amplifier/speaker impedance, regulator/current rating, supply filtering/decoupling, FPC revision and unused-pin expansion safety. No local EDA export/BOM was provided. The original research report recorded a blocked hardware page; no fresh EDA/netlist inspection is claimed here.

The independent layout was generated and checked by ESP-IDF's partition tool. The original 4 MiB Flash was read and hashed before any write. Only the dedicated context partition was initialized by the one-time command. No full-chip erase, eFuse write, PCB change or EDA extension was performed.

0.6.1 adds a minimal LCD driver using the pinned custom register table. Its port mapping and software initialization profile are known; physical controller identity, electrical nets and visible pixel correctness are distinct evidence. The C implementation uses no controllable CS/RST/backlight pin. Hardware prompt and display contract are in `DISPLAY_SPEC.md`; current results are in `LCD_CLOCK_REPORT.md`.
