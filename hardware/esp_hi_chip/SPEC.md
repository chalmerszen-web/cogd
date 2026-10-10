# ESP-HI chip board - Rev A

Status: Rev A prototype CAD completed and checked, 2026-10-10. Native ERC/DRC and independent geometry checks PASS. Manufacturing exports are available for prototype review; no physical board has been built or validated. See [VALIDATION.md](VALIDATION.md).

## Scope and decisions

2026-10-10: replace the ESP-HI ESP32-C3 module with an ESP32-C3 chip, retain the peripheral GPIO assignment, design an editable LCEDA schematic and two-layer PCB. All soldered components and connectors are on the component face. The opposite face carries the display on insulating adhesive. Microphone and speaker use right-angle surface-mount wire connectors.

The user subsequently authorized choosing a new screen autonomously, superseding the earlier request to reuse the existing screen. No physical dimensions of the existing screen are assumed.

Selected design target: Newhaven NHD-1.8-128160EF-SSXN-F, 1.8-inch 128 x 160 SPI TFT, ILI9163V. Its manufacturer drawing specifies a 34.70 x 46.75 mm backlight outline, a 24-way 0.50 mm FPC, and a 0.30 mm nominal mating thickness. The board fits within this outline: nominal PCB 34.30 x 46.35 mm, 1.0 mm FR-4, two copper layers. The 1.0 mm thickness replaces the earlier 0.8 mm draft to cover the USB connector locating-post tolerance. FPC assembly uses the recommended Molex 52435-2471 top-contact connector; fold direction and contact numbering are documented explicitly.

The chip target is ESP32-C3FH4X, QFN32 5 x 5 mm, internal 4 MB flash. Pins 19-24 are NC on this variant. Do not substitute a module footprint or wire them as external GPIO. GPIO11/VDD_SPI is reserved for flash supply.

## Interface contract

| Function | GPIO | Chip pad | Connection |
| --- | ---: | ---: | --- |
| Movement wake | 0 | 4 | Wake switch / edge connector |
| External audio wake | 1 | 5 | Edge connector |
| Microphone ADC | 2 | 6 | Original-style biased analog amplifier |
| Speaker amplifier enable | 3 | 8 | Active-high enable, default off |
| Display MOSI | 4 | 9 | 4-wire SPI data |
| Display SCLK | 5 | 10 | Idle-low SPI clock |
| PDM positive | 6 | 12 | Analog reconstruction filter |
| PDM negative | 7 | 13 | Complementary reconstruction filter |
| RGB chain | 8 | 14 | Four WS2812B-2020-V6 LEDs, direct drive on the shared rail |
| Boot | 9 | 15 | Pull-up and momentary switch to ground |
| Display D/C | 10 | 16 | SPI data/command |
| Native USB D- | 18 | 25 | USB-C D- via series resistor / ESD |
| Native USB D+ | 19 | 26 | USB-C D+ via series resistor / ESD |
| UART RX / expansion | 20 | 27 | SMT test contact |
| UART TX / expansion | 21 | 28 | SMT test contact |

GPIO8 and GPIO9 must meet ROM download strap requirements. The microphone path on GPIO2 must not be used as a digital output. Native USB owns GPIO18/19; no parallel servo load is fitted.

## Circuit requirements

- USB-C 5 V power and native USB data; separate 5.1 kohm Rd on CC1 and CC2; no PD negotiation.
- AP63200 buck, 3.392 V nominal, set by 32.4 kohm / 10 kohm 0.1% resistors, with local RF/digital decoupling. This keeps the selected V6 LEDs above their 3.3 V minimum while staying below the ESP32-C3 3.6 V maximum. Initial static tolerance estimate is 3.353-3.431 V; ripple and transient margin require measurement. The NS4150B shares this regulated supply because its recommended operating maximum is 5.0 V.
- Datasheet-qualified display supply and logic levels; current-limited backlight.
- 40 MHz crystal with manufacturer-qualified load network, EN RC reset, boot access, grounded exposed pad.
- RF matching footprints and a short feed targeting 50 ohms to an external antenna connector. Two-layer impedance is an initial geometric estimate, not a factory impedance guarantee. The LCD metal backplate makes an antenna hidden under the display unsuitable.
- Reuse the original ESP-HI PDM filter / NS4150B and LMV321 analog topology after checking component datasheets. Preserve amplifier shutdown and the ADC DC bias range.
- Two keyed, horizontal SMT connectors for microphone and speaker; document polarity. Neither speaker terminal is ground.
- Display reset sequencing, idle-low clock, and explicit SPI mode are documented, incorporating the prior LCD investigation.

## Mechanical requirements

- Top = components and connector mouths; bottom = display bonding face.
- No bottom-side components, solder joints, exposed test pads, or protruding connector stakes within the display bonding area. Vias tented on the bottom; add insulating adhesive between copper/solder mask and display metal.
- FPC bend area kept free of components. Avoid a sharp crease over the board edge. Connector contact orientation and pin 1 must match the actual screen drawing.
- In the component-face view, the folded display tail has LCD pin 1 on the right; J2 contact 1 is on the left when the mouth faces the lower edge. Therefore LCD pin n connects to J2 contact 25-n. This is a drawing-derived mapping, with an explicit unpowered continuity/fit check at first assembly.
- The Molex signal lands are 0.30 x 0.80 mm, not the 1.85 mm dimension for mounting-land clearance. The open slider envelope is reserved. The lower 9.35 mm area has no components.
- RF antenna is external to the display metal footprint. USB and wire plugs exit the board edges.

## Firmware compatibility

GPIO assignment is retained. The new 128 x 160 ILI9163V panel is not binary-compatible with the old 80 x 160 initialization/window profile. A separate board profile is required; the working ESP-HI firmware must remain available unchanged.

## Acceptance evidence

All CAD criteria below are complete; prototype electrical/mechanical measurements remain NOT RUN. Final board has 109 top-side footprint positions: 101 fitted parts, 2 DNP matching parts, and 6 bare test pads. Actual exported plated-hole count is 418. Bottom-side mask is entirely closed. Full evidence and the custom USB ground-land adjustment are recorded in [VALIDATION.md](VALIDATION.md).

1. LCEDA installed from official media; downloaded file hash and publisher recorded.
2. Native editable project opens with associated schematic symbols and PCB footprints.
3. Schematic netlist checked against this GPIO contract, supply pins, connector pinout, and chip variant.
4. Layout uses exactly two copper layers and zero bottom-side components.
5. All intended nets routed, native DRC reviewed, no unreviewed shorts/clearance violations.
6. Export schematic, BOM, placement, fabrication data and board previews only after checks; keep explicit status for any unverified item.
7. RF matching, crystal frequency, analog gain/noise, thermal load, LCD assembly and operation require prototype measurements; CAD checks are not physical validation.

## Sources

- [ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html)
- [Espressif hardware schematic checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html)
- [Espressif layout guidance](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/pcb-layout-design.html)
- [Selected display and manufacturer specification](https://newhavendisplay.com/1-8-inch-sunlight-readable-spi-tft-without-touchscreen/)
- [Original ESP-HI project](https://oshwhub.com/esp-college/esp-hi)
- Original Espressif-branded schematic image retrieved from a [PCBWay reproduction](https://www.pcbway.com/project/shareproject/ESP_HI_Integrating_Xiaozhi_AI_into_a_Robot_Dog_3410577d.html); provenance is distinguished from direct manufacturer datasheets.
