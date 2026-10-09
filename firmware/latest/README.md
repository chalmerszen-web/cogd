# ESP-HI 0.12.4-clock

本包含应用、分区表校验副本、完整源码快照 source.zip、manifest.json 与 verification.json。解压 source.zip 后，从 docs/ESP_HI_IO_DRIVER_MANUAL.md 阅读 IO/驱动/芯片手册，从 docs/CLOCK_LCD_REPAIR_REPORT.md 阅读实物验收与原始照片索引。

当前项目可运行 python tools/install_latest.py --check 只检查安装包；重新安装运行 python tools/install_latest.py --port COM5。安装器先完整备份并校验分区，再仅写应用并核对数据区域。独立使用源码快照时，先将本包应用、分区表和 manifest.json 放入 firmware/latest/。分区表仅用于核对，勿作为本次重写分区表的指令。

构建入口为 tools/build_clock.ps1，所需 ESP-IDF 提交及选项见 manifest.json 和 source.zip 内 reproduce/。工具链本体需另行配置。冷断电后须联网校时；此次通过范围为 USB 请求到可见时钟。
