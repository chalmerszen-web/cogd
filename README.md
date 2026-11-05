# COGD

一套共享核心、按开发板分别构建的嵌入式 AI 系统。**当前产品设计与功能基线是 ESP32-P4 3.5 英寸板的 `0.9.2-p4-tutor` 教学候选版**。ESP32-C3 使用同一核心的资源受限实现；芯片、外设、模型和分区不同，固件不能混刷。

## 当前平台

| 构建板型 | 芯片与工程 | 当前能力与状态 |
|---|---|---|
| `waveshare-p4-3.5` | ESP32-P4 + C6 联网；`ESP32-P4-WIFI6-Touch-LCD-3.5/` | 产品基线：官方 WakeNet10“你好小智”、实时对话/插话、上下文、异步工具、屏幕/触摸、摄像头、图片/视频、拍题与分步教学、配网恢复。有实机记录；**偶发误打断仍未解决**。 |
| `esp-hi-c3` | ESP32-C3 / ESP-HI；根工程 + `boards/esp_hi/` | 官方 WakeNet9s“你好小智”、原有 ASR/LLM/TTS 语音链路、上下文、灯光/硬件工具和 LCD 时钟。新官方唤醒构建与 P4 实测分开记录；没有 P4 的摄像头/视频/触摸教学及 PSRAM 实时语音能力。 |
| ESP32-S3 | 尚无经过验证的板级实现 | 后续移植目标；遵循同一设计与接口，不能仅改 `IDF_TARGET` 或冒用 P4 BSP。[移植交接](docs/PORTING.md)。 |

P4 4.3 英寸目录仅保留原适配参考，其实板未验收。`firmware/latest/` 是历史 C3 `0.12.5-clock` 回滚包，使用旧自研唤醒词，不代表当前官方唤醒构建；不要把该包安装到 P4。

## 设计与功能

- 待机本地唤醒，当前统一口语入口 **“你好小智”**；各芯片选择官方适配模型。
- P4 设备直接连接千问实时语音，复用 COGD 的上下文、工具校验和任务生命周期，不依赖电脑代理。
- 采音/播放优先，后台图片、视频、视觉任务按音频资源让路；已执行的工具不会因语音插话被重复执行。
- 拍题先确认，再求解；保留完整解法，支持先看/先讲、步骤追问、复核和静默暂停后续讲。教学材料只保留于本次开机内存。
- 同一产品设计不等于每块板具有同样的硬件能力。缺少的外设和模型能力必须明确报告，不能伪装执行成功。

[共享架构](ARCHITECTURE.md) · [当前规格](SPEC.md) · [P4 详细架构](ESP32-P4-WIFI6-Touch-LCD-3.5/ARCHITECTURE.md) · [P4 教学候选验收与已知问题](ESP32-P4-WIFI6-Touch-LCD-3.5/TUTOR_DIALOGUE_REPORT.md) · [整合验证](docs/INTEGRATION_20261106.md)

## 获取与构建

克隆整个仓库，不能单独复制 P4 子目录。固定 ESP-IDF 6.1.0、第三方源码与模型清单见 `third_party/manifest.json`；P4 依赖版本见其 `dependencies.lock`。字体和编解码运行源码随仓库提供，工具链及 `managed_components` 由安装/依赖管理器恢复。

```powershell
git clone https://github.com/chalmerszen-web/cogd.git
cd cogd
# 已有工具链的开发机无需重新安装
.\tools\bootstrap.ps1 -Targets esp32c3,esp32p4
# 两个入口使用独立配置与构建目录
.\tools\build.ps1 -Board esp-hi-c3
.\tools\build.ps1 -Board waveshare-p4-3.5
```

C3 输出 `build-c3-xiaozhi/esp_hi_agent.bin`；P4 输出 `ESP32-P4-WIFI6-Touch-LCD-3.5/build-p4/cogd_p4.bin` 和 `p4-assets.bin`。旧的 `tools/build_clock.ps1`、`tools/build_voice_test.ps1` 入口保留，常规构建默认官方唤醒；`-LegacyWake` 仅供明确复现历史自研模型。

C3 官方配置在原应用分区内关闭提前推测回复，并用 100 ms 提示音替代预录进度话术；正常 ASR、LLM、答案 TTS、上下文和工具保留。P4 和历史 C3 配置不使用这项裁剪。

`tools/idf.ps1` 优先使用显式 `IDF_PATH` / `IDF_TOOLS_PATH`，否则使用项目 `.toolchains/`。安装 S3 工具链可指定 `-Targets esp32s3`，这不表示 S3 固件已经适配。Linux/macOS 移植者应按同一锁定 SDK、板级选项和资产流程配置，不复用 Windows 构建缓存。

P4 新板安装使用子项目 `tools/flash.ps1 -Port <实际串口> -BuildDir build-p4`，包含 bootloader、分区表、应用和字体资源；不写 NVS、上下文和录音区。已有设备常规回归可只更新应用，但须先核对现有分区和字体。不要全片擦除，不要混用芯片或板型镜像。

联网和模型凭据在设备 NVS，源码不包含凭据。P4 `tools/provision.py --help` 提供显式端口、设备 ID、SSID 和凭据环境变量；成功后进入 natural 模式并核对官方本地监听，新机器应使用自己的配置。

## 验证与边界

`tools/test_host.ps1` 运行共享主机回归（Windows + WSL Ubuntu）。P4 固定声学/媒体冒烟入口见 `tools/p4_voice_smoke.py --help`，结果必须关联独立 Flash 读回哈希。自动声学回放、真实纸张识别、手指触摸及真人远场验收是不同证据。

**本次为可拉取的开发候选，整体验收未通过。** 共享主机回归 102/102、C3/P4 独立构建通过；P4 新运行的普通语音与独立媒体用例通过，但两轮教学虽完成业务步骤，均因音频连续性失败而保留 FAIL：欠载分别为 2/5 次，发送期限超时为 0/3 次，根因尚未确定。视频并发及结题收尾时的上游偶发误打断也未证明修复。镜像哈希、读回状态和完整边界见 [整合报告](docs/INTEGRATION_20261106.md)。

C3 官方“你好小智”仅完成构建，应用分区剩余 3,680 B，未烧录或实机验证；S3 仍仅提供移植指南。P4 本轮未完成实手指触摸、真实纸张、语音/视频并发、真人远场及长时耐久验收；500 ms 自然回复和广泛误唤醒率目标未因此达标。

历史报告保留原始 PASS / FAIL / NOT RUN。报告中的 `evidence/`、`firmware/` 原始实验链接为开发机本地存档，不随 Git 全量分发；本次可拉取的摘要位于 `docs/evidence/integration-20261106/`。现场录音、Flash 备份、凭据、工具链和构建缓存不上传。

[行动日志](ACTIONLOG.md) · [原 C3 项目说明](docs/history/README_before_multiboard_20261106.md) · [C3 IO/驱动手册](docs/ESP_HI_IO_DRIVER_MANUAL.md) · [硬件设计参考](hardware/esp_hi_chip/README.md)
