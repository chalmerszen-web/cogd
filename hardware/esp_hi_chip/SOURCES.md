# 厂家资料与来源

本次设计使用的资料入口如下。厂家 PDF/网页下载件留作本地参考，未随公开仓库重复发布；有下载快照的资料给出 SHA-256，以区分以后更新的版本。在线入口没有下载哈希。访问日期为 2026-10-10。

原始 ESP-HI 图来自二次转载，不能当作新板厂家认证。最终 GPIO 合同还与当前仓库代码交叉核对。尺寸和针序主要依据厂家资料；FPC 实物折叠、模拟/RF 性能尚待样板验证。

## ESP32-C3 datasheet

[资料链接](https://documentation.espressif.com/esp32-c3_datasheet_en.html) — FH4X pinout, supply, flash variant

下载快照 `c3-datasheet.html`，132782 字节；SHA-256：`ad8f69433f7ae1a4c5396ef02c77ebf0d5c474a8146b2ce625233dacd6750bba`。

## Espressif C3 schematic checklist

[资料链接](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html) — Reset, decoupling, crystal, RF reference

下载快照 `c3-guidelines.html`，58707 字节；SHA-256：`4d1a63e9d67d85d53e26a376a16525b7ec0a068bcd3b77b5664e9be3c6a39db3`。

## Espressif C3 layout guidance

[资料链接](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/pcb-layout-design.html) — Online layout reference

## Newhaven NHD-1.8-128160EF-SSXN-F

[资料链接](https://newhavendisplay.com/content/specs/NHD-1.8-128160EF-SSXN-F.pdf) — Mechanical drawing, 24-pin SPI interface, supply, backlight

下载快照 `NHD-1.8-128160EF-SSXN-F.pdf`，1603184 字节；SHA-256：`ba83c6b4660a2d17efa6ecce480e340f2e82dffe83426208b34e9437d453d2c2`。

## GCT USB4110 drawing

[资料链接](https://gct.co/files/drawings/usb4110.pdf) — All-SMT USB footprint and plastic locating posts

下载快照 `usb4110-drawing.pdf`，494431 字节；SHA-256：`b083cd7825c1d58d590e9b0ec6e8533ec43711b385d448d7b7a0fd25bd7f4d71`。

## Molex SD52435016 sales drawing

[资料链接](https://www.farnell.com/cad/2174151.pdf) — Manufacturer drawing, distributor-hosted copy; 52435 family including 24 circuits

下载快照 `molex52435-sales.pdf`，377539 字节；SHA-256：`3936247d94b4163627a2c0139991d38a2041a08b84696e7c8608226530d3115e`。

## Molex 52435 family drawing

[资料链接](https://www.molex.com/content/dam/molex/molex-dot-com/products/automated/en-us/salesdrawingpdf/524/52435/524352671_sd.pdf?inline=) — Top-contact connector family; dimension formula checked against 24-circuit version

下载快照 `molex52435-family.pdf`，36258 字节；SHA-256：`53fdea536860e316244bd24cbc7631fb7141a728a1d2965a5b5c83f0fbeafd78`。

## Epson FA-20H crystal

[资料链接](https://download.epsondevice.com/td/pdf/brief/FA-20H_en.pdf) — 2.5x2.0 package and crystal parameters

下载快照 `fa20h.pdf`，663064 字节；SHA-256：`ffa6359c4cb0d9569e649f2c52cffb1a7970b20526f816319644a38441cd05d1`。

## AP63200 buck regulator

[资料链接](https://www.diodes.com/assets/Datasheets/AP63200-AP63201-AP63203-AP63205.pdf) — Combined family datasheet; selected adjustable AP63200, not AP63203

下载快照 `ap63203.pdf`，1226090 字节；SHA-256：`ef99daa3789d835bc025dfcb4c605c5c2e6d3e7223e86d40b33e6b497ea5a722`。

## TI LMV321

[资料链接](https://www.ti.com/lit/ds/symlink/lmv321.pdf) — DBV pinout and analog operating limits

下载快照 `lmv321.pdf`，2605821 字节；SHA-256：`3d4cf8ec2ba5554d778c9e1bd45331c7841e04938ff2f8c65aa505c54af7c597`。

## TI SN74LVC3G17

[资料链接](https://www.ti.com/lit/ds/symlink/sn74lvc3g17.pdf) — DCU pinout and LCD logic buffering

下载快照 `sn74lvc3g17.pdf`，851618 字节；SHA-256：`bcb966e7fbb682bb7f330c544326e32865abd9eb87c5ce7b6606f3447cb4a84c`。

## TI TLV700

[资料链接](https://www.ti.com/lit/ds/symlink/tlv700.pdf) — DDC pinout and selected 3.0V LDO

下载快照 `tlv700.pdf`，1123994 字节；SHA-256：`fdb008365c01d2be8c61a79a9087b133cbcb93b5e86a40764981d574e3a882aa`。

## NS4150B

[资料链接](https://datasheet.lcsc.com/datasheet/pdf/75c9de26d1ce6a24e56ace3c6df586e7.pdf?productCode=C189961) — Manufacturer datasheet via distributor; recommended supply maximum and BTL pinout

下载快照 `ns4150b.pdf`，3341583 字节；SHA-256：`8f26ce641fd0ead96cc949a23e7ebc8d3d51d066ed7de7f958ac6327a9fbb7c5`。

## Worldsemi WS2812B-2020-V6

[资料链接](https://datasheet.lcsc.com/datasheet/pdf/a29e59ba4bc7e47e236b4d50b6c26345.pdf?productCode=C52917434) — V6-specific supply range, pinout and current

下载快照 `ws2812b-2020-v6.pdf`，910888 字节；SHA-256：`f40ae448755a808d090d780ac7a71b265d1541f22347a15bdb7d2c68da88da49`。

## JST SH connectors

[资料链接](https://www.jst-mfg.com/product/pdf/eng/eSH.pdf) — Horizontal SMT wire-to-board connector family

下载快照 `jstsh.pdf`，84427 字节；SHA-256：`ea3071ca5ee5a6069eba534fa39a10f34c9fb742ee42135a69ab4156bfa0f5de`。

## KiCad footprints

[资料链接](https://github.com/KiCad/kicad-footprints/tree/7ebfa6b23cc292a56f751b7b5f4a0e12eeef69dd) — Pinned footprints; license and design-use exception included in libraries/

## ESP-HI original schematic reproduction

[资料链接](https://www.pcbway.com/project/shareproject/ESP_HI_Integrating_Xiaozhi_AI_into_a_Robot_Dog_3410577d.html) — Secondary-hosted original schematic image, checked against current firmware GPIO contract

下载快照 `pcbway-original.html`，349742 字节；SHA-256：`45004ad936f294b4484f834bf0ac701f68795935b152eb8bb32f3be51af8c515`。

## LCEDA Pro official download

[资料链接](https://lceda.cn/page/download) — Official Windows installer; signed binary hash in ACTIONLOG

下载快照 `lceda-download.html`，537121 字节；SHA-256：`ae7b7a378d18715eb64851d4c97f5d76f314178fe3bec9f5007d983966d8a7fe`。

## LCEDA official activation

[资料链接](https://lceda.cn/page/desktop-client-activation) — Free offline activation completed with user authorization; license excluded

## JLCPCB fabrication capabilities

[资料链接](https://jlcpcb.com/capabilities/pcb-capabilities) — Online manufacturing constraints; accessed 2026-10-10

机器可读清单：[references/sources.json](references/sources.json)。封装许可：[libraries/KICAD-LICENSE.md](libraries/KICAD-LICENSE.md)。
