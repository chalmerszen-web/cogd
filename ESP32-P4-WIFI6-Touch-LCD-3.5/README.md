# ESP32-P4-WIFI6-Touch-LCD-3.5 / cogd

当前候选为 **`0.9.4-p4-tutor` 拍题来源修复版**，按用户缩窄范围交付，不能视为完整语音验收通过。start 每次使用新镜头图像，refine 仅在当前保留照片中选题/纠正；相机来源与同图检查通过，用户确认新拍题正常。“重新读题”首次误路由仍保留；完整语音验收 DEFERRED_BY_USER，最终应用/资产/分区读回与正常监听核验 PASS，LCD 视觉 NOT_RUN。详见 [来源修复报告](../docs/TUTOR_CAPTURE_SOURCE_20261106.md)。

历史 **`0.9.3-p4-tutor` 教学修复版**：七项问题在固定声学回放及独立屏幕复核范围通过，最终应用、资产、分区读回与正常监听核验通过；逐项结果、候选区分及早期失败见 [本轮修复报告](../docs/TUTOR_FIXES_20261106.md)。**整体验收仍未通过**：视频并发、偶发误打断、远场/噪声、耐久及 500 ms 自然回复目标未因这次教学修复达标。软件滚动与固定回放不等于实手指或真人自由交互验收。

历史 `0.9.2-p4-tutor` 教学核心通过、视频并发语音 FAIL 的结果保留在 [教学对话报告](TUTOR_DIALOGUE_REPORT.md)和 [历史候选固件](firmware/0.9.2-p4-tutor-candidate-20261106/FLASH.md)；其中本地 VAD 误打断的声学根因仍未证实。[0.9.1 固件](firmware/0.9.1-p4-audio-20261105/FLASH.md)保留供回退。当前实现见 [软件架构](ARCHITECTURE.md)。

说 **“你好小智”** 唤醒，听到提示音后即可接话；P4 独立直连千问 `qwen-audio-3.1-realtime-flash`，同一音色回答，支持播音中插话、后台任务、断线恢复、明确结束和约 30 秒静默告别。摄像头、绘图、WAN3 本机播放、配网和 LCD 方向沿用。[0.8.0 实施记录](REALTIME_REPORT.md)与[实施方案原件](docs/ESP32P4_千问实时语音终端实施方案.docx)保留。

口语入口：**“教我做这道题”**。把题目放到板载镜头前，听所选单题摘要；同一张图选题不对可说“我要第二题”；要换照片或重新采集须说“重新拍题”，走新 start，不能复用旧题。正确后说“是的”。确认后摄像头关闭并开始求解；期间可说“你讲一下”或“我先看”，完成后遵从已有选择，仅未选择时询问一次。解法实际显示后才确认显示；可追问“第二步为什么这样算”“换个例子”，从图片返回时恢复同题阅读位置。教学阅读中不触发普通 30 秒告别，五分钟无语音/触摸后静默暂停，再次唤醒可继续原题。题目材料在本次开机的 RAM 中保留，重启不恢复。真实纸张识别和手指触摸仍需现场验收。

上一版 [0.7.2 预览和串联语音报告](SMOOTH_REPORT.md)保留作对照：预览软件提交约 12 fps、识别负载约 10.6–11 fps。原版 ASR 完成到正式答案出声与本轮“测试音源说完到板端开始播音”的计时口径不同。用户暂时无法调整镜头，**真实场景清晰度延期验收**，不能将固定素材检测数当作真人识别率。

本轮未重训或重测官方唤醒的正负样本成功率，基线及限制见 [官方唤醒报告](WAKENET10_REPORT.md)；此前实现见 [四项报告](INTERACTION_REPORT.md)、[对话报告](CONVERSATION_REPORT.md)。以下 `0.1.0-p4-3.5` 内容是初次适配的历史记录，旧唤醒词和参数不代表当前配置。

已按用户确认的 **3.5 实物**完成语音链路适配并烧录。版本 `0.1.0-p4-3.5`，设备 `esp-hi-e8f60aeb7a3f`，USB TO UART 为 COM3。最初要求的 4.3 目录保留为独立资料和原理图参考。

设备已连接 **gion**，开机自动进入监听。可说 **“你好，小言”**，听到提示后提问；回复结束后可再次唤醒。屏幕默认显示北京时间。Wi-Fi 和语音服务凭据保存在设备 NVS，固件包不包含这些凭据。

## 交付入口

- [经过设备读回校验的固件](firmware/0.1.0-p4-3.5/manifest.json)
- [实物验收记录](evidence/acceptance.json)
- [最终三轮连续问答](evidence/voice-09-tail1200/report.json)
- [实际扬声器录音片段](evidence/final-speaker-check.wav)及[独立转写](evidence/final-speaker-check.json)
- [嘉立创 EDA 原理图及使用边界](hardware/README.md)
- [后续毛巾机项目目录](towel_machine/README.md)
- [调试经过和问题定位](ACTIONLOG.md)

## 实测结果

2026-11-01，在当前主机扬声器播放固定语音、板载麦克风实际采音的环境中：

| 检查 | 结果 |
|---|---|
| 连续三轮：唤醒、完整问题识别、DeepSeek 回复、TTS、重新监听 | 3/3，首次唤醒均命中 |
| 知识问题 | “请用一句话回答太阳系最大的行星是什么”，三次回答均为木星 |
| 外部麦克风独立录音转写 | “收到，我来处理。太阳系最大的行星是木星。” |
| RTS 硬件复位、Wi-Fi 重连、自动监听恢复 | 3/3 |
| 普通话/粤语非唤醒词固定样本 | 12 条，0 次误唤醒 |
| 安静环境继续监听 | 30 秒，0 次误唤醒 |
| LCD 开关、填色、时钟控制 | 驱动状态与已应用状态通过；未做肉眼/触摸验收 |
| Bootloader、分区表、应用读回 | 三份均与构建文件 SHA-256 一致 |
| 共享代码主机回归 | 96/96 |
| C3 `build-clock` 回归编译 | 通过；未烧录 C3、未替换 `firmware/latest` |

这些是有限范围的实物固定语音测试。真人自由对话、远场/强噪声、长时间运行、拔电重上电及摄像头/SD/以太网尚未验收。

## 硬件与运行配置

- ESP32-P4 v3.2，16 MB Flash、32 MB PSRAM；ESP32-C6 经 SDIO 联网。
- ES8311：16 kHz 双工，麦克风增益 42 dB；保持麦克风时钟持续运行，播放端重采样。
- ST7796 320×480 SPI 屏，FT5x06 触摸驱动初始化。
- 保留现有双模型“你好，小言”唤醒，阈值 740、输入增益 1；未重训模型。
- P4 快速模式静音收尾为 1200 ms，C3 仍为 700 ms；本机实测修复了问题末尾漏字。
- 当前通过 ESP-IDF 电源管理 API 固定运行 **200 MHz**。400 MHz 在本实物上出现过间歇性 Load access fault；具体物理原因尚未确定。200 MHz 配置通过上述测试，不能据此宣称所有频率或长期稳定性均已验证。
- ASR：`qwen3-asr-flash-realtime`；LLM：`deepseek-flash`；TTS：`qwen-audio-3.0-tts-flash`，音色 `longanhuan_v3.6`。

## 构建与烧录

在 `C:\Users\PC\Documents\ChatGPT\cogd` 的 PowerShell 中运行，使用已有 ESP-IDF 6.1 工具链：

```powershell
# 增量构建；首次构建自动装入本项目配置
.\ESP32-P4-WIFI6-Touch-LCD-3.5\tools\build.ps1

# 烧录已经验收的固定版本；保留 NVS 配置
.\ESP32-P4-WIFI6-Touch-LCD-3.5\tools\flash.ps1 -Port COM3 -BuildDir firmware/0.1.0-p4-3.5

# 有界串口监视
python .\ESP32-P4-WIFI6-Touch-LCD-3.5\tools\monitor.py --port COM3 --seconds 30 --out .\ESP32-P4-WIFI6-Touch-LCD-3.5\evidence\manual-monitor.log
```

完整的构建参数在 `tools/build.ps1`，组件版本在 `dependencies.lock`。固件包包含实际 `sdkconfig`、文件 SHA-256 及参与构建的本地源文件哈希。代码复用上级 cogd，不应只复制本子目录后脱离上级源码构建。

初次识别时保存的原厂完整 16 MB 备份仍位于相邻 `ESP32-P4-WIFI6-Touch-LCD-4.3/.local/backups/factory-full.bin`；来源实际是本 3.5 板。备份哈希 `dcb0e036164ded03846dd44c6e70afaebed1061f6e640ea5246f4d1df9ea8f16`，没有覆盖。

## 复测

`tools/validate_device.py` 执行有界复位、显示控制、12 条非唤醒词和 30 秒监听检查。`tools/voice/continuous_rewake.py --port COM3` 位于上级工程，执行三轮声学问答；可使用本目录 `fixtures/manifest.json` 的 `knowledge` 问题。音频测试选择当前主机的 Misiom-Shooter 播放/录音端点，位置改变后需要重新验收。

毛巾机目录目前只有平台起点和项目说明，尚未加入执行器驱动。
