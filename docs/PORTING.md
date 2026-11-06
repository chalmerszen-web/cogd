# 将 COGD 移植到 ESP32-S3 或另一块开发板

2026-11-06。交接候选基线：P4 `0.9.4-p4-tutor` 产品设计、当前共享核心、官方“你好小智”。本仓库现有可构建目标为 ESP-HI C3 与 Waveshare P4 3.5；S3 的板型、引脚、Flash/PSRAM、外设与固件尚未验收。

新板必须实现 start 新采集与 refine 同图改选的独立语义，校验当前题号/版本并返回照片来源、捕获身份、哈希和时间；语音不得使用测试图片 URL。0.9.4 的相机来源验证通过，用户确认新拍题正常；“重新读题”首次误路由仍是已知限制，明确打开摄像头后的成功不覆盖该失败。完整语音验收 DEFERRED_BY_USER，最终应用/资产/分区读回与正常监听核验 PASS，见 [来源修复报告](TUTOR_CAPTURE_SOURCE_20261106.md)。

教学移植须保留单题确认、题号/版本匹配、显示回执、通知一次性归属和阅读位置恢复；求解期间记录先看/先讲，完成后沿用已有选择，仅未选择时询问一次。历史 P4 0.9.3 七项修复在固定声学回放与屏幕复核范围通过，详见 [修复报告](TUTOR_FIXES_20261106.md)；视频并发、误打断、远场/噪声及整体稳定性未因此通过，不能转记为 S3 验收。

既有公共平台契约仍见 `platform/port_contract.h`、各插件操作表与 [历史移植契约](history/PORTING_before_multiboard_20261106.md)。重编译所有插件以匹配当前 ABI；不能强制转换旧操作表。HTTP body producer 的数据只在同步写回调期间有效，Flash/context 写入必须遵守原有所有权和取消契约。

## 先固定真实硬件

记录板名、芯片型号/修订、Flash/PSRAM、麦克风/编解码器/喇叭、LCD/触摸、摄像头、USB 与供电。建立独立工程和构建目录，保留 NVS/上下文等数据分区；不复制 P4 的 MIPI、C6 SDIO、引脚或分区假装它们存在于 S3。

## 接入边界

| 接口 | 现有参考 | S3 需要实现/核对 |
|---|---|---|
| 板型与工具能力 | C3 `boards/esp_hi/{board,hardware,control_board}.c`；P4 `board/` | 实际外设列表、IO 冲突、工具注册与不支持回执。不能把 C3 灯光或 P4 摄像头暴露成不存在的功能。 |
| 音频 | 共用 `boards/esp_hi/audio_board.c`；P4 `p4_audio.c` | 独立采集/播放驱动、16 kHz 输入、24 kHz 云音频输出/重采样、DAC 参考、DMA 内存、采样时钟、取消和队列所有权。不能继承 C3 ADC 私有时钟。 |
| 本地唤醒 | `speech_backend.h`；C3 WakeNet9s，P4 WakeNet10 | 官方支持该芯片的模型/库，默认词“你好小智”，测量堆与推理时长。P4 `wn10_nihaoxiaozhi` 是 PIE `p2` 权重，S3 应按官方选择 `p1`；当前 `pack_wake_model.py` 的 wn10 分支固定 P4，必须扩展显式目标选择后再用。 |
| 网络/配置 | `runtime.c`、P4 `p4_wifi.cpp` | S3 原生 Wi-Fi 与适合本板的配网界面；重连不重复工具，不泄漏配置。不能依赖 P4 的 C6 固件和 SDIO。 |
| 显示/触摸 | `display_board.c`、P4 `p4_vision.cpp` | 实际控制器、方向、分辨率、触摸坐标与 LVGL 资源预算。字体资产与分区需一同构建。 |
| 摄像头/媒体 | P4 `p4_vision.cpp`、`p4_media/` | 根据 S3 资源单独评估采集与解码；缺少外设则不注册功能，不能照搬 P4 原片解码性能结论。 |
| 实时对话/教学 | `p4_conversation.c`、`p4_agent.h`、`p4_cloud.cpp` | 抽取/实现所需能力 ABI，再接同一会话和工具协议；保持代数门控、音频优先、后台回执与教学状态语义。不要另建第二套 COGD 核心。 |
| 持久上下文 | `plugins/context/`、`platform/espidf/store.c` | 复用 WAL/CRC、双 bank 与真实容量边界；常规升级不清 NVS、历史或录音。 |

先补板型配置和驱动，再扩展 `main/CMakeLists.txt` 的目标守卫与统一构建入口。不能把 `AGENT_BOARD_P4=ON` 当成 S3 开关；该宏仍包含 P4 专属 BSP、媒体、教学和 PSRAM 依赖。

## 构建环境

克隆完整仓库，安装 ESP-IDF 6.1.0 与 `esp32s3` 工具链；Windows 可运行 `tools/bootstrap.ps1 -Targets esp32s3`。第三方运行源码/模型清单需通过 `python tools/verify_dependencies.py`。各项目的 `dependencies.lock` 用于重建其自身依赖，不要将 P4 的锁文件当作 S3 已验证配置。

禁止提交凭据、NVS/完整 Flash 备份、录音、工具链、managed_components 或构建缓存。需要新模型时记录官方来源提交、许可证、逐文件 SHA256；提交构建必需资产而非指向某台开发机的文件路径。

## 有界验收顺序

1. 主机共享回归；新板编译、分区/资源检查，保留日志。
2. 识别芯片与端口、备份数据、烧录该板专用镜像，读回校验应用/分区/必要资产。
3. 复位联网、本地官方唤醒、完整问答与返回监听；先确认实际采播，再做续问与插话。
4. 对本板存在的显示/触摸/相机/媒体/教学逐项验证；软件状态和现场可见行为分开记。
5. 报告 PASS / FAIL / NOT RUN、镜像哈希与剩余问题。C3、P4 的结果不能转记为 S3 结果，P4 现存误打断问题也不能因移植自动标为解决。
