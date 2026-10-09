# 语音对话使用与维护

> 当前状态（2026-10-02）：设备已恢复 `0.11.72-summary`，自动唤醒和自动语音关闭。
> 误唤醒尚未修复；USB 普通文本仍可对话，上下文和凭据保留。
> 重新执行 `agent voice on` 会开启仍有误唤醒风险的监听，并保存开关。
> 当前结果见[误唤醒诊断](WAKE_FALSE_TRIGGER_DIAGNOSIS.md)。以下保留旧版本使用说明。
> 逐字ASR复核会严重漏醒，未部署；[文本复核实验](WAKE_TRANSCRIPT_CONFIRM_REPORT.md)保留失败数据。

> 2026-09-27：正式安装包仍为0.9.0。当前设备自测应用为0.11.68-tcp-window，
> fast/capture监听，reuse开启、prefetch关闭。明确基本颜色指令在完整识别后执行并本地确认；
> 保留结束咻音、原生短句接话及实际保存操作提示，新增部分ASR与本地人声
> 共同确认发音，并加快本地记忆来源校验。逐帧USB诊断关闭。
> `agent voice mode fast` 是实验功能，1秒响应、
> 内存余量和长句完整性尚未通过，不作为正式交付默认值。
> 保留改口保护与DeepSeek会话缓存。68三轮完整成功，TCP发送队列减半后最低堆38532B，
> 仍低于48KiB；外录回答约11.8–13.0秒，不能声称提速。原诊断截句和FULL失败均保留。
> 最新实测见[TCP队列报告](VOICE_TCP_QUEUE_REPORT.md)，归档见[HTTP会话报告](VOICE_HTTP_SESSION_REPORT.md)。下文0.9.0说明
> 仍描述正式安装包；重新安装它会恢复该已保留版本。

此前开启监听的实验版可直接说“你好，小言”，听到开始提示后说“请把四颗灯设为蓝色”，
或“请记住，我给这盏灯取名叫小星星”。说完后有短促咻音。保存任务可能先说
“嗯，小星星，我来记一下”，实际保存前再说“我来保存”，最后报告结果。
仍存在录音超时/截句及数秒等待，不代表每轮都会成功或一秒内完成。

唤醒后已经并行录音和连接ASR，边录边传；部分转写用于暂定路由，也可以
辅助确认本地碎片化人声，但不会延长录音上限或改变结束静音时间。最终转写
覆盖草稿。当前**没有**根据半句话并行发起DeepSeek请求，也不会把半句话
执行为GPIO动作。完整识别后的改口、否定及复合灯控交完整Agent处理。

提前生成并缓存回复已有电脑协议及有界板上实验，包括“蓝色→绿色”改口作废旧包。
路径已接入但未通过稳定性和资源验收，当前关闭预取，仍在本地收音完成后请求回复。
61开启普通会话连接复用，最多三轮，空闲保留窗口60秒，后续唤醒不再重复握手。该临时开关重启
默认关闭，重新开启的命令见[连接复用报告](VOICE_REUSE_REPORT.md)。
提前准备答案的约束与失败记录见[预响应说明](VOICE_PREFETCH_SPEC.md)。

正式安装包 `0.9.0-qwen-stream` 在现有 Agent 上接入千问实时 ASR/TTS，继续使用原来的 DeepSeek、Wi-Fi、GPIO、灯、音乐和上下文。验证采用电脑合成语音经扬声器播放给设备，并用外部麦克风检查设备回复；没有要求真人录音。

## 使用

设备联网且凭据已配置后，在 USB 终端输入：

```text
agent voice on
agent voice status
```

说“你好，小言”，听到设备提示音后说指令，例如“请把四颗灯设为蓝色”。说完停顿，设备自动结束录音、识别、调用 DeepSeek，再播放回答。回答结束后自动回到监听，可以再次唤醒。支持普通话和粤语读法的唤醒；本次云端对话声学用例以普通话为主。

现在使用 Fun-ASR-Realtime：唤醒后先准备识别连接，再发开始提示音，请听到提示音后说话。录音期间发送 16 kHz PCM；只把最终识别结果交给 DeepSeek。TTS 使用 Qwen-Audio-3.0-TTS-Flash，收到音频即播放，免去文件上传、异步进度轮询和整段语音下载。实际延迟及组成见 [实时语音报告](VOICE_STREAM_REPORT.md)。等待和播放期间暂停唤醒，避免自己的扬声器触发下一轮；本版没有播放时打断、回声消除或连续全双工对话。

交付时语音已开启，开关会保存。正常使用无需电脑打开串口；已验证关闭 COM5 后仍可独立唤醒、改灯、回答并恢复监听。电脑只需给设备供电并保持设备所用 Wi-Fi 可用。

常用命令：

| 命令 | 作用 |
|---|---|
| `agent voice on` | 开启唤醒后的 ASR/DeepSeek/TTS，保存开关 |
| `agent voice off` | 取消当前语音并关闭自动语音模式，保存开关 |
| `agent voice status` | 查看配置状态、阶段、成功/失败次数和播放欠载次数 |
| `agent cancel` | 取消当前操作；语音模式开启时，释放资源后自动恢复监听 |
| `agent voice say 你好，小言已经准备好了。` | 单独诊断设备 TTS |
| `agent voice run` | 使用已有录音诊断 ASR→DeepSeek→TTS，不重新录音 |
| `agent audio volume 80` | 调节播放音量；当前实测使用 80 |
| `agent context stats` | 查看历史预算、实际日志占用和分区容量 |
| `agent help` | 查看全部命令 |

`agent wake on` 是保留的旧版仅唤醒录音入口。完整语音对话使用 `agent voice on`。USB 文本对话仍然可以使用；串口只允许一个终端或测试程序连接。

## 凭据

千问 Key 从用户指定的本地 qianwen-model-suite 技能配置读取，只发往配置已核对的 DashScope 官方域名。脚本经 USB 将 Key 和 `longanhuan_v3.6` 音色写入独立 NVS；原 Wi-Fi、DeepSeek 和旧版 Vocalign 凭据保持不变，不把 Key 写入日志或源码。

```powershell
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe -X utf8 tools\voice\qianwen_provision.py --out artifacts\voice-provision-new --enable
```

当前设备已经配置，无需重复运行。重新烧录仅应用固件时无需再次配置。平台需要网络连接和可用账户；关闭语音模式后不会自动上传新的唤醒录音。`voice status` 会显示提供方、模型和流式能力；旧版文件语音适配仍保留为显式兼容路径。

## 资源和扩展

历史预算是 204800 字节，指一次 DeepSeek 请求选择的历史上限，按完整记录裁剪，按需从 Flash 读取。物理历史分区仍为 2 MiB；双区日志的单区为 1 MiB。没有为 200 KiB 历史另分配 RAM。

实时 ASR 从录音日志中已写入的不可变片段逐块发送 16 kHz 单声道 PCM，最长录音 10 秒。独立解码游标和 8 KiB 工作区避免与 VAD 冲突；录音校验成功后才允许最终结果进入 Agent。TTS 限制文本 1800 UTF-8 字节，接收 24 kHz 单声道 PCM16，播放最长 90 秒。借用空闲 LLM 消息区中的 12 KiB 作为 PCM 环形队列，结束或取消后回收使用权。

`plugins/speech/speech.h` 定义 ASR/TTS 的 `begin/feed/finish/cancel` 接口和独立 provider_state，千问适配实现了这些钩子。TTS 的文本和 PCM 协议支持双向流；当前 Agent 等 DeepSeek 工具轮次完成后提交最终回答，**没有同时运行 DeepSeek 和 TTS**。单网络 worker 同时只保留一条活 TLS 连接。固定域名的已关闭 TLS 会话票据最多缓存 60 秒，修改凭据时清除；不自动重连重放收费会话。

## 自动化测试

`tools/voice/conversation_test.py` 从本机测试语料执行声学多轮对话，轮数上限 30，任何失败停止并保留现场。`analyze_acoustic.py` 使用本地 SenseVoice，仅识别外部麦克风录到的设备回复段，避免把电脑播放的指令当作设备回答。需要现有的扬声器/麦克风、FFmpeg、串口 Python，以及 `.local/tts-python` 分析环境和本地模型。

`clip_dialogue.py` 可测试已有录音、TTS、取消和麦克风音量检测并发；`standalone_test.py` 验证串口关闭后的独立对话，`cancel_rearm.py` 验证取消及关闭的区别。平台异步任务的 ID 保留在本地日志；失败后不自动重发收费请求。原始录音、平台响应、凭据、备份和编译产物排除在 Git 外。

## 回滚

回到上一语音版 0.8.1 可使用完整备份/应用守护脚本，历史预算仍为 200 KiB，旧版会读取原来的 Vocalign 配置：

```powershell
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe -X utf8 tools\kws\flash_guard.py --phase voice-stream --build firmware\rollback\0.8.1-voice-speed
```

旧版 0.6.3 不接受 200 KiB 预算的检查点。回滚前必须先在新固件上保存 128 KiB 兼容检查点，再仅写旧应用。项目提供守护入口，先校验保留的安装包，再检查上下文，完整备份 Flash 并验证应用以外区域没有变化：

```powershell
# 仅校验本地回滚包，不连接设备
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe -X utf8 tools\voice\rollback.py
# 执行兼容处理、备份、应用回滚和启动后检查
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe -X utf8 tools\voice\rollback.py --apply
```

不要直接使用历史包的旧一键入口覆盖一个仍保存 200 KiB 检查点的设备。重新安装语音版后，输入 `agent context budget 200` 和 `agent voice on` 恢复本版配置。旧版源码、配置和依赖继续保留在 `history/0.6.3-context/`。

实时模式还会结合云端静音断句结束录音：云端收到约 1 秒静音后返回最终句子，
且本地 VAD 已确认有人声，才允许结束采集；未确认、失败或取消的录音不执行指令。
因此长句中停顿超过约 1 秒可能被当作一句结束，适合分句提出简短指令。
