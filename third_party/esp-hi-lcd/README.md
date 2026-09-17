# ESP-HI panel initialization reference

The register table in `boards/esp_hi/display_board.c` derives from the MIT-licensed
ESP-HI BSP in 78/xiaozhi-esp32, commit `ac6deed3d8e75348475364bf40ad953c6cd48054`:

https://github.com/78/xiaozhi-esp32/blob/ac6deed3d8e75348475364bf40ad953c6cd48054/main/boards/espressif/esp-hi/esp_hi.cc

Only register values and board wiring are reused. The C11 SPI adapter, asynchronous
state machine and row renderer are local code. The exact fitted LCD controller is
not inferred from the conflicting ST7789 macro and ILI9341 API names in the BSP.
See the adjacent upstream LICENSE.
