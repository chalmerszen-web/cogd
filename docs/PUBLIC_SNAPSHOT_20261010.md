# ESP-HI 公开项目快照：2026-10-10

仓库：https://github.com/chalmerszen-web/cogd

当前设备固件为 0.12.4-clock。本次版本完成设备 USB 发出时钟需求、电脑转入 Codex、实现、编译、仅应用烧录以及摄像头实物验收。旧版语音质量和性能问题仍按原记录保留。

## 内容位置

| 内容 | 仓库位置 |
|---|---|
| 当前 C 固件、驱动、模型与构建配置 | core/、plugins/、platform/、boards/、components/、main/、cmake/ |
| 测试、工具、训练代码及实验程序 | host_tests/、tests/、tools/、training/、experiments/、diagnostics/、prototypes/ |
| 项目规格与完整开发行动记录 | SPEC.md、ACTIONLOG.md |
| 硬件、GPIO、芯片与 ESP-IDF 手册 | docs/ESP_HI_IO_DRIVER_MANUAL.md、docs/reference/esp32c3/ |
| 各阶段方案、失败和验收报告 | docs/ |
| 时钟原始照片与公开验证结果 | docs/evidence/clock-20261010/ |
| 早期源码快照与整理记录 | history/ |
| 当前可安装应用、分区校验副本和清单 | firmware/latest/ |
| 固件完整包及较大的过程记录 | 同仓库 Release v0.12.4-clock 的下载附件 |

Release 的 development-records-20261010.zip 保存日志、实验结果、脚本、源码片段和参数记录的公开副本，保留原 artifacts/ 层级。record-manifest.json 记录每个原文件与公开副本的大小、SHA-256 以及脱敏计数。文件名和旧记录中的本机路径作为追溯信息保留。

凭据、原始 Flash/上下文备份、原始录音、工具链和依赖缓存、下载的训练模型及重复构建产物保留本机。公开副本不会替换或删除原始证据。SHA-256 和未通过结果不会因公开发布而改成通过。

## 使用当前固件

在配置好 Python 和项目依赖的终端运行：

```powershell
python tools/install_latest.py --check
python tools/install_latest.py --port COM5
```

第二条命令会访问设备：先完整备份及验证分区，再只更新应用，最后读回核对。重新检测实际串口号，不保证其他电脑也为 COM5。凭据通过本机隐藏输入配置，见 tools/provision_secret.ps1，不写入源码或提交记录。

## 构建依据

本次使用 ESP-IDF v6.1，提交 fff9895c82d744c7237be8847347bdd1b07c6643。Windows 构建入口为 tools/build_clock.ps1；它调用 tools/build_voice_test.ps1 和 tools/idf.ps1。脚本按 .toolchains/esp-idf、.toolchains/tools 查找私有安装，并沿用本机 IDF Python 引导路径；新电脑须先安装对应环境或按实际安装位置调整入口。

```powershell
./tools/build_clock.ps1
```

发布固件包中的 source.zip 保存与已烧录应用对应的源码、reproduce/sdkconfig、reproduce/build-options.json 和逐文件哈希；仓库同时包含此后补充的公开资料导出工具与发布说明。当前应用 SHA-256 为 917c869e62681c1c0f2197a69311c2233fdffcb086c887ee48a21acbe04a000d。

摄像头照片确认时分、秒的位置与变化以及软件重启恢复。完全断电后须联网校时。屏幕控制器身份未通过 ID 读回确认；驱动修复的证据边界见 [LCD 报告](CLOCK_LCD_REPAIR_REPORT.md)。
