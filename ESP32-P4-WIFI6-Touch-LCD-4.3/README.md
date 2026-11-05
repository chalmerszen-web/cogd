# ESP32-P4-WIFI6-Touch-LCD-4.3 参考工程

用户最初指定本型号，随后确认已连接实物为 **3.5**。实物固件及调试结果请看相邻 `ESP32-P4-WIFI6-Touch-LCD-3.5/`。

本目录保留 4.3 官方资料、可编辑嘉立创 EDA 原理图，以及最初的 4.3 固件适配源码。

- 原理图工程入口及验证结果见 `hardware/README.md`。
- `hardware/native/` 包含 17 张原生可编辑图纸，346 个器件、1,113 个引脚、268 个网络。
- `towel_machine/` 是最初要求的毛巾机预留目录；实际 3.5 平台也另建了同名子目录。
- 4.3 固件已经编译；**没有对应的 4.3 实板验收结果，不能将 3.5 的测试算到本项目上**。

最初识别设备时保存的 16 MB 原厂备份仍在 `.local/backups/factory-full.bin`，其来源实际是 3.5 板。哈希与设备识别记录保留于 `evidence/factory-backup.json`、`evidence/chip-flash-id.log`。后续未删除或覆盖原厂备份。

官方资料：[产品文档](https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-4.3/)、[示例仓库](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3)、[Espressif P4 数据手册](https://documentation.espressif.com/esp32-p4_datasheet_en.pdf)。
