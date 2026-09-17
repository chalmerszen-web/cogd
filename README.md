# ESP-HI Agent 0.6.2-repair

ESP32-C3 上的纯 C11 Agent。当前项目基线合并了 1000 次同板实验中的 B 版语音链路，以及精简版的 GPIO、流式缓冲和内存改进。“升级版”是本项目名称，不是 Espressif 官方发布的整机固件。

包含硬件提示词、屏幕查询/开关和持续时钟。0.6.2 增加开机校时等待、无效工具参数反馈及当前显示状态快照，并改用 ESP-IDF LCD 传输。用户已报告 0.6.1 实物黑屏；新传输的软件检查与实际可见效果分开记录，见 [显示说明](docs/DISPLAY_SPEC.md)。

支持 USB 文本对话、Wi-Fi、DeepSeek 流式/非流式、灯光、GPIO/PWM、有界动作链、Flash 上下文、四声部音乐、录音回放和本地唤醒。语音只负责唤醒后保存录音，没有转写或语音上传。监听默认关闭。

正式入口：[项目规格](SPEC.md)、[基线说明](docs/BASELINE.md)、[1000 次对比](docs/VOICE_AB_1000_REPORT.md)、[清理与剩余文件](docs/CLEANUP_REPORT.md)、[行动日志](ACTIONLOG.md)。对比结果属于冻结的 B 镜像；合并后的 0.6.0 单独记录构建及有界回归，不冒充再次完成 1000 次实验。原 M0 实机 Gateway/真实断电项目仍未完成。

## 构建

在项目目录的 PowerShell 中执行：

```powershell
.\tools\bootstrap.ps1
.\tools\build_agent.ps1
.\tools\test_host.ps1
```

ESP-IDF v6.1 位于项目私有 `.toolchains/`。必要语音库、模型和许可证位于 `third_party/`；构建不依赖旧实验目录。`tools/verify_dependencies.py` 校验固定依赖。主机测试使用已有 WSL Ubuntu GCC/CMake，启用 C11、ASan/UBSan。

已经采用本工程当前分区布局的设备，只更新应用：

```powershell
.\tools\idf.ps1 -B build-agent -p COM5 app-flash
```

此命令不用于旧 M0/M1 布局迁移。当前布局为 1.5 MiB 应用、2 MiB 上下文、448 KiB 录音。更新前保存完整 Flash；不用整片擦除。音频关闭构建：`tools/build_agent.ps1 -BuildDir build-agent-noaudio -NoAudio`。

## 对话

最简单的方式：在项目文件夹双击 `打开设备对话.cmd`，在弹出的 `ESP-HI Chat - COM5` 窗口输入文字并回车。需要手动启动时，在项目目录 PowerShell 执行以下命令：

```powershell
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe -X utf8 -m serial.tools.miniterm COM5 115200 --encoding UTF-8 --eol LF --echo --dtr 0 --rts 0
```

输入一句话并回车，看到 `@done` 后继续。`Ctrl + ]` 退出并释放串口。普通文字就是对话，例如“把四颗灯调成红色”。

开机联网后若出现 `@wait clock`，正在等待校时以校验 HTTPS 证书，稍候即可；`agent cancel` 可取消。完整工具参数不合规时，设备会把具体原因反馈模型，最多纠正两次；看到 `executed:false` 表示该批调用尚未执行。

```text
agent status
agent wifi status
agent chat 你好
agent chat --no-stream 请查询设备状态
agent cancel
agent light set 0 0 0
agent context stats
agent context budget 128
agent context mode LOCAL
```

`tools/provision_secret.ps1` 优先读取本机 `WIFI_SSID`、`WIFI_PASSWORD`、`DEEPSEEK_API_KEY`，缺少时交互输入。按用户要求密码和 Key 默认回显；空输入保留已有配置。不要用诊断日志脚本传凭据。

## 屏幕时钟

直接对话：“请在屏幕显示北京时间，每秒刷新，持续显示。”模型调用一次显示工具后，设备本地维持时钟，无需每秒向云端请求。也可以直接诊断：

```text
agent hardware
agent display set {"mode":"clock","utc_offset_minutes":480}
agent display set {"mode":"clock","foreground":0,"background":65535,"utc_offset_minutes":480}
agent display status
agent display off
```

第二条设置是白底黑字，便于检查亮屏。时区默认 UTC+8；设备联网校时，未校时显示 `--:--:--`。屏幕开启时 GPIO4/5/10 由显示驱动占用，直接 GPIO 操作返回 busy。`agent cancel` 也会关闭显示；重启默认关闭。SPI 帧数只能证明软件已发送，实际画面以实物为准。

## GPIO 和动作链

LLM 可查询能力，再读写 GPIO4/5/10/20/21，或设置两路 10–5000 Hz PWM。GPIO0/1/9 为只读按钮；灯、音频、USB 和 Flash 引脚由对应驱动管理。开放引脚有原板用途，接外设前核对 [硬件表](docs/HARDWARE_FACTS.md)。

```text
agent control capabilities
agent gpio get {"pin":10}
agent gpio set {"pin":10,"mode":"output","value":1}
agent gpio set {"pin":10,"mode":"pwm","hz":1000,"duty":500}
agent gpio set {"pin":10,"mode":"input"}
```

占空比 0–1000，500 为 50%。直接设置持续到更改或重启。动作链支持最多 32 步、8 次重复、60 秒，结束/取消恢复之前状态；冲突返回 `busy`。例：“GPIO10 拉高 200 毫秒，再恢复，同时闪一下灯。”完整参数见 [GPIO schema](protocol/device-gpio.schema.json) 和 [动作链 schema](protocol/device-plan.schema.json)。

## 音乐和本地录音

可对话：“创作并播放一分钟明快器乐，128 BPM、32 小节，有主旋律、低音、琶音和轻鼓。复用乐句，中段变化，最后落稳。”设备校验乐谱后异步合成，USB、灯光和取消继续可用。固定曲谱在 `examples/music/sunny_walk.json`，可用 `tools/music_link.py` 上传。

```text
agent audio status
agent audio volume 80
agent audio stop
agent mic on
agent mic status
agent mic off
agent audio capture 5000
agent audio replay
agent wake on
agent wake status
agent wake off
```

启用监听后说“嗨乐鑫”，听到“叮”再说话。结束并成功保存后发出下降提示音。单段最长 10 秒；唤醒后无指令有 4 秒等待边界。监听可由 `wake off` 或 `cancel` 关闭。播放/对话期间暂停检测。`mic on` 只测音量，手动录音及唤醒录音会替换单个本地录音槽；半段数据不会被当成有效录音，旧录音不保证保留。

调优版在本次已知样本中减少了误触发、误录和长等待，同时启动录音更慢。不是普适识别率承诺。操作与边界见 [唤醒说明](docs/WAKE_VAD_SPEC.md)、[音乐协议](docs/MUSIC_SPEC.md)、[录音格式](docs/AUDIO_CLIP_FORMAT.md)。

## 上下文和 Gateway

2 MiB 上下文分区采用双区轮换，每个活动区 1 MiB，正文还要扣除元数据。最近最多 128 个完整轮次，历史请求默认 64 KiB、可设 128 KiB，从 Flash 分块发送，不占同等大小 RAM。更早的保留事件可检索、摘要；这不是模型 token 数量。出厂默认 DIRECT/HYBRID，已配置设备保留其模式和预算。

```text
agent context search {"query":"之前约定的颜色","limit":3}
agent context summary
agent context remember color warm
agent context memory
agent context sync
```

开发 Gateway 使用 `tools/mock_gateway.py --san <电脑局域网IP>`，证书和数据库保存在 `.local/mock/`。详见 [协议](docs/GATEWAY_PROTOCOL.md)。没有 Gateway 时继续保留未同步事件；空间耗尽会明确返回 `full`，不会自动丢弃待同步历史。

## 目录

| 路径 | 用途 |
|---|---|
| `core/`、`plugins/` | 可移植 Agent、LLM、上下文、工具、控制、音频 |
| `platform/`、`boards/`、`main/` | ESP-IDF / POSIX 适配和板级入口 |
| `components/`、`third_party/` | 当前语音链路、固定模型和第三方许可证 |
| `host_tests/`、`tools/` | 主机检查、构建、USB、配置、录音与 mock |
| `protocol/`、`examples/`、`docs/` | 协议、示例、当前说明和必要历史报告 |
| `artifacts/baseline/` | 当前固件、参考固件、源码归档和回滚材料，不进 Git |
| `artifacts/voice-ab-1000-v1/` | 完整 1000 次实验及两次设施中断证据，不进 Git |
| `artifacts/baseline-cleanup-v1/` | 本次迁移、删除清单、构建和设备验证，不进 Git |

OpenWrt、杰里和本地模型运行时仅有移植契约。LCD、舵机、完整 ASR/TTS、生产云服务不在当前基线内。
