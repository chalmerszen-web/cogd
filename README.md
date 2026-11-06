# COGD

一套共享核心、按开发板分别构建的嵌入式 AI 系统。**当前产品设计与功能基线是 ESP32-P4 3.5 英寸板的 `0.9.7-p4-tutor` 拍题清晰度修复候选**。ESP32-C3 使用同一核心的资源受限实现；芯片、外设、模型和分区不同，固件不能混刷。

2026-11-07，0.9.7 在本轮拍题范围验证通过：两次默认整页真实相机读取三式均正确，包括1.6；新捕获身份与源图更新，LCD可见预览对应。0.9.5采集FAIL和0.9.6首次1.6读成16的FAIL保留。真人语音重拍与完整对话 DEFERRED_BY_USER；跨页换题、触摸及支架遮挡的底部文字未验收。本版仍是开发候选，不代表全场景稳定通过。见 [本轮报告](docs/TUTOR_CAPTURE_READABILITY_20261106.md)。

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
- start 每次重新采集板载镜头；refine 只在保留的同一张照片上选题或纠正，必须匹配当前题号/版本。拍题先确认所选单题，再求解；求解期间可记录先看/先讲，完成后遵从已有选择，仅未选择时询问一次。保留完整解法，支持步骤追问、复核、图片返回时恢复阅读位置及静默暂停后续讲。教学材料只保留于本次开机内存。
- 同一产品设计不等于每块板具有同样的硬件能力。缺少的外设和模型能力必须明确报告，不能伪装执行成功。

[共享架构](ARCHITECTURE.md) · [当前规格](SPEC.md) · [P4 详细架构](ESP32-P4-WIFI6-Touch-LCD-3.5/ARCHITECTURE.md) · [0.9.4 拍题来源修复](docs/TUTOR_CAPTURE_SOURCE_20261106.md) · [0.9.3 历史七项修复](docs/TUTOR_FIXES_20261106.md) · [0.9.2 历史整合验证](docs/INTEGRATION_20261106.md)

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

**历史 0.9.4 拍题来源修复在本轮范围验证通过，作为开发候选交付；完整语音验收由用户延期。** 构建、安装读回与真实相机来源/同图选题检查通过，用户确认新拍题正常；“重新读题”首次误路由仍是已知问题，明确要求打开摄像头后才成功新采集。最终应用/资产/分区读回与正常监听核验 PASS，LCD 视觉 NOT_RUN。C3 构建和分区隔离通过，但应用哈希与上一轮不同，未找到旧镜像，不能证明仅元数据差异。见 [本轮来源报告](docs/TUTOR_CAPTURE_SOURCE_20261106.md)。

**历史 0.9.3 的七项教学问题在固定声学回放及独立屏幕复核范围通过，整体验收仍未通过。** 该版共享主机回归 105/105、定向回归 3/3 及 C3/P4 构建通过；镜像哈希、最终读回、逐项结果和早期失败保留在 [本轮修复报告](docs/TUTOR_FIXES_20261106.md)。这些结果不等于真人自由交互、实际触摸或所有数学题正确性验收。

0.9.2 历史整合的 102/102 主机回归、普通语音和独立媒体结果仍保留；两轮教学因音频连续性失败而为 FAIL：欠载分别 2/5 次，发送期限超时 0/3 次，根因未确定。视频并发及结题收尾时的偶发误打断亦未由本轮修复证明解决。原始记录见 [整合报告](docs/INTEGRATION_20261106.md)。

C3 官方“你好小智”仅完成构建，应用分区剩余 3,680 B，未烧录或实机验证；S3 仍仅提供移植指南。P4 本轮未完成实手指触摸、真实纸张、语音/视频并发、真人远场及长时耐久验收；500 ms 自然回复和广泛误唤醒率目标未因此达标。

历史报告保留原始 PASS / FAIL / NOT RUN。报告中的 `evidence/`、`firmware/` 原始实验链接为开发机本地存档，不随 Git 全量分发；可拉取的摘要位于 `docs/evidence/integration-20261106/` 和 `docs/evidence/tutor-fixes-20261106.json`。现场录音、Flash 备份、凭据、工具链和构建缓存不上传。

[行动日志](ACTIONLOG.md) · [原 C3 项目说明](docs/history/README_before_multiboard_20261106.md) · [C3 IO/驱动手册](docs/ESP_HI_IO_DRIVER_MANUAL.md) · [硬件设计参考](hardware/esp_hi_chip/README.md)
