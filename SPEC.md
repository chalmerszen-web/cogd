# ESP-HI Agent 当前项目规格

> 当前执行范围（2026-10-10，设备请求到时钟）：先交付 [IO与驱动手册](docs/ESP_HI_IO_DRIVER_MANUAL.md)，再让C3通过USB发出指定原文，接收后实现并烧录24小时制大字HH:MM与右下角小字SS。0.12.4-clock已编译、仅应用烧录、读回及重启检查通过；摄像头原始照片确认正向大字HH:MM、右下小字SS持续变化，重启后自动显示，**本次有限时钟验证完成**。不扩大为常驻自动USB桥接或旧语音质量验收。范围见 [实验规格](experiments/usb_clock/SPEC.md)，当前证据见 [LCD验收报告](docs/CLOCK_LCD_REPAIR_REPORT.md)。

> 当前执行范围（2026-10-10）：验证已连接 ESP32-C3 的 USB 消息能否经电脑转入当前 Codex 聊天并触发提问。一次串口回传、聊天转发和用户可见确认已通过；常驻自动桥接及回复回写设备未实现、未验收。保留现有 0.12.0-rc2，证据见 [USB 到 Codex 测试](docs/USB_CODEX_TEST_REPORT.md)。

> 当前执行范围（2026-10-09）：保留并记录现有 `0.12.0-rc2`，完成 USB 连接 C3 的原生机器码 RAM 灯光实验：每 3 秒换色，共 60 秒，核验 Flash 并恢复原固件。规格见 [原生 RAM 实验](experiments/native_led_ram/SPEC.md)；这是独立实验，不提升现有固件版本。

> 当前执行范围（2026-10-05）：按用户“接手查看串口，解决 error limit”的要求，在 rc1 测试版上修复定时控制任务的状态查询与工具轮数收尾，形成 `0.12.0-rc2`。构建、证据和范围见 [本版说明](docs/FIRMWARE_0.12.0_RC2.md)。下面旧阶段的“当前”描述为历史记录；本次不开展安全诊断、长期训练或宣称完成所有历史性能目标。

## 满盘对话恢复（2026-09-20，0.6.3-context）

用户联网后普通对话立即返回 `full`。实机 LOCAL 活动区只剩124B，1611事件/440完整轮次全部保留为未同步，DeepSeek 请求尚未发出。恢复工作在原2MiB分区和128KiB历史预算下进行。

- CTX-01：先备份并校验完整 Flash、导出所有事件；归档保存在独立的 `context-archives/`，不进入Git或此前清理脚本的删除清单。
- CTX-02：增加仅USB维护可调用的LOCAL归档操作；要求匹配WAL代数和明确记录边界，不自动清空，不向LLM开放。保留最近128完整轮次、记忆/tombstone、摘要及其仍存在的原始来源轮次；保留导出边界之后的全部事件。
- CTX-03：归档水位独立于云端ACK；不伪造同步结果。使用原双区提交协议，失败保留旧区和旧水位。未归档的待同步记录继续保留，容量不足仍明确报错。
- CTX-04：仅烧录应用；核对凭据、分区、录音和归档/保留历史。验证真实DeepSeek流式、非流式、连续对话、灯控及重启恢复后交付，不展开新的长循环或声音调优。

## 当前修复（2026-09-16，0.6.2-repair）

用户实测报告 0.6.1 屏幕不显示，串口出现 `config` / `limit`。此前 SPI 帧计数不能作为可见显示通过；LCD 实物验收改记失败，修复后仍须区分软件传输和真实像素。

- FIX-01：保留 TLS 校验，开机联网后等待有界校时，并显示等待提示、支持取消。
- FIX-02：完整工具调用批次先全部校验；无效乐谱的具体原因和原调用 ID 返回模型，在同轮最多纠正两次、仍受四轮总预算限制。截断、未知工具、重复 ID 和已执行副作用不得自动重放。
- FIX-03：每轮注入当前 LCD 状态，禁止把旧历史当成现状。LCD 使用 ESP-IDF panel IO 处理传输队列与 DC 时序，沿用已核对 BSP 初始化和最小行缓冲；不以发送成功冒充实物修复成功。
- FIX-04：备份新鲜 Flash 后仅烧录应用；保留现有凭据、实际历史及最新录音。音频开/关构建、主机边界测试、有限实机请求和显示启停后收尾，不扩展为新的长期声学实验。
- FIX-05：修复日志接近满时的误压缩：按实际记录长度判断剩余空间，避免音乐播放期间因不必要的擦除返回 busy；真正满仍明确报告，不丢弃历史或降低容量。

## LCD 扩展历史（2026-09-16，0.6.1-lcd；用户随后报告实物黑屏）

用户要求确认硬件IO映射并写入固件基础提示词，烧录后通过对话开启持续时间显示。

- LCD-01：固定参考BSP版本，核对GPIO4/MOSI、5/SCLK、10/DC、160×80、CS/RST无软件控制；ST7735指令配置与实物芯片鉴定分开记录。
- LCD-02：板级硬件提示词随每次DIRECT请求进入system消息；提供硬件查询和显示工具，保留按钮、音频、LED、USB/Flash及身体接口映射和冲突。
- LCD-03：最小C11屏幕驱动和行缓冲，支持时钟、纯色、关闭。使用现有控制任务推进初始化/刷新，不引入LVGL、整屏RAM或新任务；默认UTC+8，未校时显示占位。
- LCD-04：LCD按组占用GPIO4/5/10；启用时与直接GPIO/动作链互斥，关闭后恢复输入并释放。USB18/19不动。持续刷新由设备本地执行。
- LCD-05：先备份当前4MiB Flash，仅更新应用，保留NVS、上下文与录音。主机检查参数/时间/像素边界和资源互斥，实机有限刷新/取消/版本与数据核对；屏幕可见效果由用户测试，未观察不能称通过。
- LCD-06：保留0.6.0回滚产物，更新操作说明、硬件事实与ACTIONLOG；不重跑1000次语音实验或减少上下文。

LCD-01..06 的代码、构建、有界实机及数据保留检查已完成；真实 DeepSeek 流式/非流式调用通过，显示帧持续递增。用户实物黑屏报告说明这些软件检查不足以验证可见显示。旧证据见 `docs/LCD_CLOCK_REPORT.md`；0.6.2 修复及剩余限制见 `docs/LCD_FIX_REPORT.md`。

基线：**0.6.0-upgrade**，2026-09-15。用户接受 1000 次同板对比后，选用调优 B 的语音链路作为项目升级基线，并要求删除无用调试材料、保留当前代码和必要证据。本规格取代此前继续训练、候选探索和重复声学验收的安排。历史过程保留在 ACTIONLOG；本项目版本不代表 Espressif 官方发布。

## 要求和边界

| ID | 当前要求 | 验证方式 |
|---|---|---|
| BASE-01 | 自有固件 C11，静态版本插件，当前 ABI major 5（LCD 操作表）；ESP-IDF 位于适配层 | 实际编译命令和主机测试 |
| BASE-02 | 保留 B 的 WakeNet9s、关键词筛选、TEN 后台确认、采集/端点和紧凑录音 | 来源哈希、同板对比证据、移植回归 |
| BASE-03 | 保留 lean 的 GPIO/PWM、资源仲裁、请求缓冲复用和乐谱解析简化 | GPIO/计划/SSE 主机及设备检查 |
| BASE-04 | USB 文本、Wi-Fi、DeepSeek DIRECT/GATEWAY、流式/非流式和工具续轮 | 原有实现及分阶段验收，失败如实保留 |
| BASE-05 | 上下文分区和预算不缩减，NVS/录音保留，常规更新仅应用 | 更新前后完整 Flash 比对 |
| BASE-06 | 删除旧构建、废弃候选、训练环境和中间材料；无运行源码依赖实验目录 | 逐项删除清单、依赖校验、清理后全新构建 |
| BASE-07 | 按 2026-09-19 用户要求，仅保留当前安装包、当前/历史源码、第三方许可证和必需编译依赖；删除旧对比数据及 Flash 备份 | 哈希、归档完整性、历史重编译和目录清单 |
| BASE-08 | 更新当前操作入口，有限回归后收尾，不新增研究和 1000 次重复测试 | 基线/清理报告和行动日志 |

## 架构和资源预算

内核负责注册、事件、消息和资源策略。platform、transport、LLM、context、USB、tool 通过类型明确的函数表连接。主循环管理状态，单个网络 worker 处理一个对话；状态和取消可用。语音和网络复用大工作区，以资源所有权排斥冲突。

| 资源 | 上限 |
|---|---:|
| 普通 USB 输入 | 2 KiB |
| 单记录/请求工作缓冲 | 24 KiB |
| 流式发送的完整 HTTP 请求 | 160 KiB |
| 单 SSE 事件 / 回答 | 6 KiB / 8 KiB |
| 工具参数合计 | 4 KiB |
| 每个回复工具数 / 工具续轮 | 4 / 4 |
| 历史候选 / 历史预算 | 128 个完整轮次 / 默认 64 KiB、可设 128 KiB |
| 上下文 Flash | 2 MiB，双 1 MiB 区轮换 |
| 应用槽 / 录音区 | 1.5 MiB / 448 KiB |
| 控制计划 | 32 步、8 次重复、60 秒 |
| 手动/唤醒录音 | 最长 10 秒 |
| 四声部音乐 | 16 乐句、128 存储音符、32 编排段、75 秒 |

CRC 追加日志、版本/序号、提交后轮换、去重、Lamport/LWW/tombstone 和 LOCAL/CLOUD/HYBRID 保留。待同步事件不因压缩丢弃。默认 DIRECT、deepseek-flash、thinking 关闭，出厂 HYBRID；升级保留用户 NVS 设置。凭据本机输入按用户要求回显，不进入 LLM 上下文或 Git。

GPIO4/5/10/20/21 支持输入/输出、两路 10–5000 Hz PWM；按钮0/1/9只读。USB18/19、Flash12–17、未知11受保护；灯8、ADC2、功放3、PDM6/7通过板驱动管理。直接输出持续；计划结束/取消恢复原状态。不执行模型生成的机器码/C代码。

## 验收口径

1000 次实验已经完成：A/B 各500、500配对，另2次电脑采集中断单列。A 是官方算法板级适配，B 是冻结调优镜像。在已知输入中，误唤醒8/100→0/100，无指令误录22/100→2/100，10秒上限19→0；唤醒正例成功率未证明显著提高，启动中位数351→602ms。不能把这些数据当成0.6.0重新跑过1000次，也不能当作盲测或普遍识别率。

本次集成验收为：音频开启/关闭构建、24/12组主机测试、来源与许可证校验、应用更新、GPIO/取消/音乐/监听有界检查、NVS/上下文/录音保留。结果见 [BASELINE](docs/BASELINE.md)。不要求再次完成三天研究流程。

原 M0 实机 Gateway/同步和真实 USB 断电验收仍未完成；原 M3 主观音乐体验、M4 苛刻语音目标和历史失败不被本次基线采纳改写。详见 [1000次对比](docs/VOICE_AB_1000_REPORT.md)、[历史测试](docs/TEST_REPORT.md)和[音乐报告](docs/MUSIC_REPORT.md)。

## 待实施：你好，小言（2026-09-21）

最新阶段结果：Phase 2已完成约定的基线及两次调整。最终独立合成材料测试为
普通话241/243、粤语107/147，粤语未达到80%门槛，按预注册条件停止训练。
未烧录新模型，设备仍为0.6.3-context；双语离线唤醒部署目标尚未完成。
冻结结果、失败诊断和后续未执行项见
[Phase 2报告](docs/WAKE_XIAOYAN_PHASE2_REPORT.md)。以下保留早期决策过程。

用户随后批准的20条有限诊断已完成：定位到裁切末端与词尾混用的标注问题，另有
部分词/近似词区分不足。未新增训练、修改原成绩或烧录；结论和具体修正提案见
[有限诊断报告](docs/WAKE_XIAOYAN_BOUNDARY_DIAGNOSIS_REPORT.md)。修正提案待单独确认。

同日后续授权：用户要求完成下一步固定预算训练并由Codex自行确定预算。已建立
[Phase3范围](docs/WAKE_XIAOYAN_PHASE3_SPEC.md)：一次6000步训练，修正端点与样本
构造，新的来源隔离保留测试，量化/C一致性检查，失败如实交付且不自动追加训练。
当前进展见[Phase3报告](docs/WAKE_XIAOYAN_PHASE3_REPORT.md)。

Phase3最终：单次6000步完成，主机独立合成测试普通话232/234、粤语149/155，
负例3/85、部分词0/7，量化下降和实际权重C一致性通过。未烧录，不能当成真人
泛化或实机声学验收；新端点定义与旧计分不同，旧结果保持原样。

用户确认目标为“你好，小言”的普通话＋粤语两种读法，在ESP32-C3本地离线唤醒。当前只完成代码、USB状态和公开训练途径预检；权重未训练、固件未更换。先按用户提出的ChatGPT调研交接方式确定可获得的模型或可复现训练链，详见[研究交接](docs/WAKE_XIAOYAN_RESEARCH_BRIEF.md)。后续同时处理旧词二次筛选，保留2 MiB上下文、128 KiB历史预算、分区和可编译回滚版本；两种语言分别统计效果，回放测试不充当真人盲测。此项是新需求，不追认既有语音验收或启动无界调优。

同日追加：用户将本地自主训练和导出确定为必要条件；官方定制仅保留背景说明，不作为实施主线。先验证本地训练/导出最小链及C3资源可行性，再扩大数据训练。Codex执行按medium日常实现、high关键设计/诊断、low重复运行分配，属于建议，未自动修改任务设置。

### 已批准路线与阶段门槛

采用本地PyTorch训练的轻量因果DS-TCN和专用静态INT8 C11推理；普通话、粤语共同输出一个唤醒类别。先在完整Agent中验证推理样机资源和三端数值一致性，再扩大数据训练。以下均为新后端设计/验收要求，Phase 0通过不代表已经实现：

| ID | 要求/门槛 |
|---|---|
| XY-00 | 保存当前未提交源码、固定依赖及完整Flash；旧0.6.3归档独立重编译，准备仅应用回滚。2026-09-21已完成，见[Phase 0报告](docs/WAKE_XIAOYAN_PHASE0_REPORT.md) |
| XY-01 | 基于现有Agent接入；自有固件C11，保留VAD、录音、DeepSeek、GPIO、音乐、上下文和取消；不引入通用C++模型解释器 |
| XY-02 | 保持2 MiB上下文、128 KiB历史预算、448 KiB录音槽和现有分区；只更新应用，保留旧模型源码以供回滚 |
| XY-03 | 初版xiaoyan_ds_tcn24_v1：40维前端、24通道、因果深度可分离时间卷积；参数及量化/缓存细节在Phase 1实现前核算并锁定 |
| XY-04 | 同源C整数前端，16 kHz单声道、512样本输入块；无逐帧堆分配；整数参考、主机C及板上结果逐层一致 |
| XY-05 | 新增替换后端Flash预算≤64 KiB（含模型/代码/表）；总应用≤1,540,096 B，槽位至少留32 KiB；实际以链接报告为准 |
| XY-06 | KWS状态/工作区≤16 KiB、额外栈≤4 KiB、合计≤20 KiB；全场景最低internal heap≥32 KiB，监听最大连续块≥24 KiB |
| XY-07 | 每512样本完整KWS处理（前端/两次步进/判定）p99≤16 ms、观测最大<32 ms；无DMA丢失、持续积压或WDT，USB等待不混入计算计时 |
| XY-08 | 普通话和粤语分别评价；训练/验证/盲测按说话人及源录音隔离，合成回放成绩与真人成绩分开；不以删除功能或缩减上下文达标 |

XY-00已完成。随后按“继续干下一步”完成[Phase 1计算与资源样机](docs/WAKE_XIAOYAN_PHASE1_REPORT.md)：C11后端三端一致性、Flash/RAM、600秒实机时序及有限功能回归通过，测试后恢复0.6.3。使用未训练测试权重，禁止触发实际唤醒；正式训练与XY-08双语识别效果仍未开始。备份在排除Git且不属于旧清理入口目标的backups目录；回滚包在firmware/rollback。原语音代码保留为默认构建。

### Phase 2：已批准的小规模双语训练

用户已确认执行[Phase 2范围与验收](docs/WAKE_XIAOYAN_PHASE2_SPEC.md)。最多一个训练基线加两次有原因的调整；普通话和粤语各至少100条保留样本，实验目标识别率各80%，量化下降不超过5个百分点；另做30分钟背景观察与实机回放。来源分组先于增强，合成样本不证明真人泛化。先过本地语音生成质量门槛，再扩大数据和正式训练；失败材料保留。原0.6.3回滚、上下文及分区预算不变。当前仍在数据生成与训练流程准备阶段，新词尚未烧录。

## 可复现材料

入口 README、构建 tools/build_agent.ps1、依赖 third_party/manifest.json 与 components/agent_vad/sources.lock.cmake。当前源码不依赖 `_ref`、`hardware_tests` 或实验产物。当前安装包在 firmware/latest；完整历史源码/配置在 history，tools/build_history.ps1 支持独立重编译。2026-09-19 依用户要求将原 artifacts 中的旧镜像、Flash 备份、录音和测试数据列入清理范围；自动删除被审批拦截，待用户运行清理入口，结果见 history/cleanup-result.json。清理完成后历史报告保留结论，旧证据路径仅为历史记录。此次文件整理不操作设备，也不改变未完成验收的状态。

Phase4 frozen-model acoustic acceptance: docs/WAKE_XIAOYAN_PHASE4_SPEC.md.

Phase4 outcome 2026-09-21: physical recognition FAILED (zh0/20,yue1/20); approved bounded failure exit, 0.6.3 restored. Negative window limitation retained. See docs/WAKE_XIAOYAN_PHASE4_REPORT.md; experimental deployment is not accepted.

Phase5 active objective: working Mandarin/Cantonese wake plus weak-signal tiers and real5m validation; docs/WAKE_XIAOYAN_PHASE5_SPEC.md.

2026-09-22 user steering: first optimize autonomously with locally synthesized
speech and the nearby speaker; do not require human recordings for this stage.
See [Phase 6](docs/WAKE_XIAOYAN_PHASE6_SPEC.md). Keep prior failures and resource
limits; real5m/human evidence remains separate and is not claimed from attenuation.

## Device cloud voice extension (2026-09-22)

Current user objective is [device voice conversation](docs/VOICE_CLOUD_SPEC.md):
local Xiaoyan wake/VAD, device-side VocalignTech ASR and TTS, original DeepSeek
and hardware tools, synthetic speaker/microphone testing, and streaming-provider
contracts. Effective selected history is200KiB without a200KiB RAM allocation;
existing physical partition and records remain. This supersedes waiting for
human recordings in the current development stage. Progress and retained
failures are in [the integration report](docs/VOICE_CLOUD_REPORT.md).

### Voice integration acceptance — 2026-09-22

VC-01 through VC-07 are verified for the bounded synthetic acoustic integration
stage in docs/VOICE_CLOUD_REPORT.md. Installed0.8.0-voice-exp,200KiB history
selection,unchanged2MiB context partition. Final source and build provenance are
under artifacts/voice-cloud/delivery; normal use is documented in
VOICE_CLOUD_USAGE.md. Human/distance wake generalization,subjective audio,
model streaming and previous M0 external acceptance remain separate.

### Voice response speed — 2026-09-22

Installed0.8.1-voice-speed per docs/VOICE_SPEED_SPEC.md. Six matched synthetic
conversations and bounded resource/cancel/cache checks are documented in
VOICE_SPEED_REPORT.md. The measured58.3% median input-end-to-playback reduction
includes provider queue variability and is not an SLA or a human benchmark.
Preserve200KiB history selection,partitions,credentials and0.8.0 rollback.

### Realtime voice integration — 2026-09-22

User requested the local qianwen-model-suite credential and actual streaming
ASR/TTS on the current device, with measured response-time composition.
Requirements VS-01 through VS-08 are defined in docs/VOICE_STREAM_SPEC.md;
implementation and evidence are in docs/VOICE_STREAM_REPORT.md. Final0.9.0
six-turn acoustic tests use live PCM upload and streamed TTS, retaining200KiB
history and current partitions. Median input-end-to-first-PCM is9.7345s versus
19.031s in0.8.1. Startup preparation is separately measured at1.789s median.
Minimum heap47480B falls1672B short of the historical48KiB target; that resource
criterion is NOT marked passed. No new1000-turn, human-generalization or M0
external acceptance claim. Preserve0.8.1 application and source rollback.

### Long-history request latency — 2026-09-25

- LH-01: Keep the 200 KiB selected-history budget and existing 2 MiB context
  partition. The built-in composer may reuse the exact serialized byte count
  from locked selection. Actual sending must replay and CRC-check every
  selected turn and match HTTP Content-Length. Custom context/composer plugins
  retain the general measurement path.
- LH-02: Record selection, length measurement, connection, body sending,
  response headers, first response byte and transport return separately for
  planning and final-answer requests. These are board timings, not acoustic
  response latency or pure provider compute time.
- LH-03: Verify wire equality with >190 KiB history, escaped UTF-8, plugin
  fallback, tool pairing, corruption and cancellation. Then use continuous
  three-turn hardware groups without voice toggles between turns.
- LH-04: Preserve Mandarin/Cantonese wake, asynchronous capture/upload,
  endpointing, streamed final speech and contextual acknowledgements. Native
  acknowledgement/DeepSeek overlap and the one-second useful-speech objective
  remain required; request-count optimization alone does not fulfill them.

### Native acknowledgement playback overlap — 2026-09-26

- NA-01: A fully validated native delegate may release the network worker while
  its contextual acknowledgement continues playing. Close the fast WSS session
  first. Join playback before tool execution, final-answer audio, or turn release;
  cancellation and every error exit must reclaim the audio owner.
- NA-02: Cache at most six seconds of 24 kHz mono acknowledgement in a volatile
  IMA spool. Use only the last 96 KiB of the existing clip partition when the
  committed recording ends before it; refuse overlap before any erase. Preserve
  ten-second recording, the partition table, 2 MiB context and 200 KiB history.
  Blocks carry count, codec state and CRC. No old spool is reopened after reset.
- NA-03: Add no worker, TLS session overlap, or heap audio allocation. Producer
  and consumer have separate bounded state; engine scratch is free after seal.
  Test arbitrary chunking, full capacity, corruption, interrupted NOR writes,
  cancellation, unchanged cached fallback PCM, and reopening the user recording.
- NA-04: Verify actual overlap with queued-sample counts and DeepSeek stage
  timestamps preceding acknowledgement completion. Retain acoustic and content
  checks in continuous three-turn groups, and preserve failures. The one-second
  useful-answer target and minimum-heap target remain independent requirements.

### Voice feedback and early intent — user feedback 2026-09-26

- UX-01: After a successful local speech endpoint, play a short descending
  completion cue. Run it on the audio owner while the already-uploaded input
  is submitted; never record/upload the cue or count it as a spoken answer.
  Failed/cancelled/no-speech capture must not start it. It confirms capture end,
  not model/tool success; later provider errors are still reported. Preserve cancellation.
- UX-02: Measure connection, capture end, final ASR and first meaningful audio
  separately. Optimize real waiting, without hiding it behind cues. Retain
  ordinary speech pauses and correction commands in regression fixtures.
- UX-03: During a slow delegated turn, announce the actual processing stage
  with a short action phrase. Bound frequency and audio ownership; no fabricated
  tool success. Final speech supersedes pending announcements. Distinguish
  generated acknowledgements, cached stage notices, cues and useful answers.
- UX-04: Verify the current provider's streaming transcription and cancellation
  behavior before speculative inference. Interim text is provisional; final
  transcription wins, changes invalidate preliminary work, and no physical
  effect executes from a prefix. Do not claim hot-editing an active model
  generation without protocol evidence. Keep bounded diagnostics and raw errors.
- UX-05: Once final ASR requires delegation, reject at100ms unusable fast audio
  (at most one128-sample decoding block of overrun)
  before closing that response and handing off without effects. Native ACKs
  with usable metadata keep their existing six-second bound. Record this
  rejected-route fallback separately; it is not speculative prefix execution.
  Plain reception candidates follow UX-11; recognized completion claims reject
  immediately from text instead of waiting for a PCM threshold.
- UX-06: Trace transient capture allocation minima with an optional fixed-size,
  no-allocation diagnostic. Identify skipped/overwritten observations. Diagnostic
  overhead is not production latency acceptance; normal builds have no hooks.
  Preserve full backups and restore a normal application after measurements.
- UX-07: Voice planning tool schemas may carry an optional boolean final_batch
  only true on the last call of a complete batch. Strip the field atomically
  before ordinary argument validation/invocation or persistence. Direct/USB
  schemas stay unchanged; malformed metadata rejects the whole batch.
  After successful invocation and durable original-ID result
  pairing, omit only the redundant READY planning request. Keep a separate
  tools-disabled streamed final answer based on real results. Empty/rejected/pending
  results retain planning, dependent operations retain all required rounds, and
  invalid batches/cancellation/storage errors cannot start final speech. Never
  speak or persist the internal marker. Verify actual provider adoption on the
  device before claiming a latency improvement.
- UX-08: Bound fast-response startup by an independent2500ms absolute cap from
  commit, also enforced during writable/BUSY waits and partial frames. Provider
  response-created/done events cannot extend it. Accepted PCM clears this
  startup cap; the existing response-duration limit remains. A completed tool
  gets a fresh bounded confirmation window. On cap expiry, only final ASR with
  no prior speech/effects/cancellation may hand off once to the full agent,
  after closing the old session. Missing final ASR or previous effects fail
  without replay; existing cached fallback speech is distinct from an answer.
- UX-09: The fast model may query local history through the existing read-only
  agent_context_search tool and answer from real hits in the same connection.
  Use one search per batch, bounded by the existing shared tool-round budget.
  Validate the whole call before reading; search only before PCM/effects and
  after complete WebSocket frames. Reuse reply.text for results while the WAL
  owns engine.buffer; retain the 2 KiB session/writer limits and no new arena.
  Empty/malformed/failed results cannot authorize guessed history speech.
  Lookup never clears the route for memory writes or complex hardware tasks.
  Read-only lookup may still delegate; hardware effects/audio prohibit replay.
  Verify actual provider use, persisted ID pairing, and uninterrupted groups
  of at least three turns. Preserve partitions/history capacity and rollback.
  When a search matches a user message, its 240-byte UTF-8 excerpt also includes
  that turn's last nonempty assistant text before the next user message. Do not
  return only the repeated question, include tool payloads, or cross turns.
- UX-10: A new syllable at the fast endpoint boundary may wait at most 60 ms
  for the existing four-vote continuation guard. Apply only after accepted
  speech and a positive frame within the last 60 ms; never reset quiet time
  from this grace, lower energy/onset thresholds, reopen a completed capture,
  or extend the 10-second input limit. Normal silent endings remain 700 ms;
  classic confirmation is unchanged. Record grace frames and test resumed
  speech, sparse noise, cancellation and continuous groups on the device.
- UX-11: Final ASR requiring the full agent may accept a plain, generated
  reception phrase without a delegate tool. Buffer the entire candidate in
  the existing Flash spool; do not play any sample before complete response
  validation. Require one audio message, identical final/incremental text,
  a24-character prompt target and hard limit32 characters/96UTF-8 bytes,
  and conservative future-intent wording without
  completion claims. A keyword gate is not proof of semantic correctness.
  Rejected phrases use the existing cached fallback once; malformed provider
  envelopes, storage errors and cancellation preserve their errors. Keep the
  2500ms startup and six-second PCM bounds while collecting. Late tool calls
  cannot execute or license the buffered audio. Publish EOF only on approval;
  discard before EOF on rejection. Reuse existing memory/partitions and join
  before tools/final speech. Record generated versus fallback speech and
  verify actual DeepSeek overlap separately from a delayed join timestamp.
- UX-12: Present complex-work handoff as agent_task_start, whose result means
  acceptance rather than completion. Preserve the old delegate_deepseek wire
  alias for retained fixtures, but a native call's exact name/ID/arguments
  must remain consistent through all fragments and completion. Both aliases
  follow the same full-turn handoff and no-mixed-effects rules. Keep simple
  greeting/light/history behavior, the existing six-second PCM bound,
  96-byte text storage and32-character limit; add no buffers or workers.
- UX-13: During silent ACK collection, reuse the released8KiB PCM arena for
  batches of complete140-byte codec records. Do not allocate a second arena
  or change Flash format. Publish samples only after successful batch writes;
  seal flushes the last batch before releasing the borrowed arena. Sticky
  write/erase failures cannot expose partial data, and the saved user clip
  must remain byte-identical and reopen successfully. Keep unbuffered native
  playback,2500ms startup limit and six-second maximum. Measure device store
  time separately from provider waiting; host write counts are not latency.

2026-09-26 execution status: UX-12/13 remain experimental in the0.11.36
working tree and frozen candidate. Device restored to frozen0.11.34 after
the new reception route failed3/3 on-device attempts and showed no verified
latency gain. No stable-one-second, dynamic-ACK,48KiB-heap or long-input
acceptance. Retained experiments and rollback evidence are recorded in
docs/VOICE_FEEDBACK_REPORT.md; formal firmware/latest remains0.9.0.

- UX-14: Optional, volatile endpoint tracing reads existing immutable320-sample
  frame records only after capture and worker stop, before borrowed memory is
  released. No extra PCM/metadata arena. USB export has a single150-ms budget;
  disabled tracing has no USB output. CRC, consecutive offsets and exact count
  are required before host replay. Host must reproduce all endpoint counters
  with the same C code. Trace-enabled runs are classification diagnostics and
  cannot certify response latency. Off/on requires voice disabled and idle;
  automatic tests disable tracing during cleanup. Preserve700-ms rule until
  frame evidence supports a bounded change; no threshold sweep on hardware.

- UX-15: Keep strict onset (seven strong frames in160ms) and700-ms end silence.
  In an admitted fast turn, four weak frames can support two strong frames in
  the same160-ms window, only when the current frame is strong and within320ms
  of a strict continuation. Weak-only audio cannot reset quiet; support cannot
  renew its own expiry. Strong threshold remains max(240,noise*2), weak support
  max(240,noise*3/2), both with spectral and cleaned-energy checks. Classic
  confirmation and the existing60-ms boundary grace stay unchanged. Retained
  frame replay and continuous on-device complete-input checks are required;
  this rule is not evidence that arbitrary long pauses are preserved.

2026-09-26 latest status supersedes the earlier rollback snapshot: installed
0.11.38, diagnostics off. Nine consecutive-test turns in three groups first-wake
and core-task9/9, full input8/9; normal blue group2/3 input only. No overall
acceptance. Input checks now distinguish exact fixture transcription from core
task completion; retained reports get separate immutable-source audits.

- UX-16: A single complete, validated fast light setter may end with cached
  completion speech only after the real tool succeeds. Advertise this terminal
  behavior in the model tool contract; attached questions/other work delegate.
  Ambiguous colours, failures, queries and multi-tool batches do not take this
  shortcut. Close the completed WS session before decoding; reuse the existing
  24-kHz stream/ring, with a 240-sample stack block and no new heap buffer.
  Persist original user, tool call/result and actual spoken completion together.
  Cancellation or playback failure records an error and never retries effects.
  Mandarin/Cantonese Flash assets are limited to32KiB, independently decoded
  against FFmpeg; language chosen from explicit transcript words is heuristic.
  This is a useful completed-operation answer, distinct from interim progress.
  Resource cleanup must not inspect the audio backend for empty/light-only
  direct leases; pending audio leases retain existing cleanup/error semantics.

- UX-17: Once final ASR requires the full engine, a complete bounded future-tense
  reception sentence may intentionally cancel fast generation before its audio.
  It grants no tool authority, never publishes original PCM, and ignores all
  later response material. No receipt from a partial ASR, after audio/effects,
  during a function output, or with a past completion claim. Current user
  cancellation takes precedence. Keep exact final input for the full engine;
  existing TTS may speak the retained sentence after the planning body is sent,
  then must join before tools/final speech/scratch reuse. Persist only actual
  final answers; mark this separately from completed native response validation.
  Measure first spoken receipt, final useful answer and actual cloud overlap;
  none can be inferred from a text prefix, session echo or enqueue timestamp.


### UX-18 Native reception stream
After accepted final ASR has selected the full route, a complete bounded future-tense sentence may stream its original Omni PCM through the existing Flash spool. This authorizes no tool effect. Any appended transcript, changed final transcript, extra function output, timeout or cancellation stops the stream. Full response validation remains required before DeepSeek handoff. Incomplete sentences keep the deferred path. Collection after first admitted PCM is bounded to six seconds. No second TTS connection for this receipt; original recording, partitions and history budgets remain unchanged. UX-17 text-only handoff is superseded after measured extra TTS latency.


### UX-19 Context operation notice
At most one cached context-operation notice per turn, only after earlier acknowledgement joins and a homogeneous context-search or summary-set batch is selected. Memory notice says 我来保存。 / 我记低先。, never successful completion. Mixed/hardware tool batches emit no extra notice. Existing audio owner, cancellation and final-answer join apply. Two16kHz ADPCM memory assets <=16KiB Flash, fixed decoder, no heap. Receipt prompt asks <=14 characters (parser still retains32-character safety budget); WS receive chunks1024B reuse existing task stack, on-board stack watermarks must be checked.


### UX-20 Literal light fast path
Only an accepted final ASR after capture joins may use a closed Mandarin grammar for one lamp-colour setter. Optional polite/count words and a simple-answer suffix; no negation, condition, multi-action, relative/dimmer request or unmatched text. Close original generation before synthesizing one local_light tool call; existing batch validator and hardware tool still execute it exactly once. Persist user/call/result/success-only spoken confirmation. Other language/wording stays on the model path. No effect or response from draft intent; cancellation on session close prevents action.

### UX-21 Receipt metadata recovery
When final ASR already selects the full engine and a single native delegate has
finished with matching identities and argument bytes, invalid optional receipt
metadata may be discarded. Recovery requires no effect and an intact, bounded,
complete future-tense transcript for the already admitted receipt. Derive the
display language locally, keep the actual receipt and send only final ASR to
the full engine. This is not approval of rejected parameters or a replay of
tools. Mixed calls, changed bytes, past claims, missing final intent, user
cancellation and storage/transport errors remain errors. Emit a distinct
metadata-rejection/recovery diagnostic; never hide the original failure.

Historical0.11.47 deployment snapshot (superseded by UX-25 below):
fast/capture/listening, endpoint diagnostics off.41/41 host checks, both builds,
application-only flash/readback and preserved data verified. Latest continuous
memory and greeting groups complete2/3 and3/3; one capture timeout remains in
the denominator. Recent summary-source validation takes39ms in both successful
memory turns, vs about2.35s previously, without reducing retained history.
An additional unclassified wake/capture timeout is preserved outside these
controlled groups. Full response under one second,
48KiB minimum heap, continuous natural handoff and reliable whole-utterance capture
remain unaccepted. Details/evidence in VOICE_FEEDBACK_REPORT; formal0.9.0 unchanged.

### UX-22 ASR corroborates fragmented onset
Only within the current fast capture, meaningful partial ASR (at least two
basic Han characters or three Latin letters) may corroborate local onset.
Require at least120ms of locally positive source frames, a still-waiting endpoint
and the original4s waiting deadline. Partial text is speech evidence only:
no final intent, end timestamp, reply or tool authority. Admission changes only
WAIT to SPEECH and records the source time; it never resets silence, relaxes
frame thresholds, extends10s, reopens a terminal capture or affects classic mode.
The worker owns endpoint mutation; ASR publishes only an atomic flag reset each
capture. Expose the admission time in metadata/status so exact C replay can
separate old local decisions from remotely corroborated ones. No extra task or
audio buffer. Verify original trace parity without corroboration, punctuation/
short/repeated/late text, silence-only audio, cancellation and continuous turns.

### UX-23 Verify summary sources through the existing recent-turn index
Keep all stored history and the200KiB request budget. For a summary's local
through_seq, examine the existing recent-turn index newest first, re-read the
selected WAL record with CRC/identity/length validation, and match device plus
device sequence and turn type. Never infer source validity from sequence alone.
If absent from the index, retain the full WAL fallback for older retained turns.
Read/corruption/cancellation/deadline errors are terminal, not cache misses.
Open and compaction still validate/rebuild the log and index. No additional
resident index or buffer; test foreign same-sequence records, older sources,
stale descriptors, faults, reboot/compaction and a bounded read-count comparison.
Measure the actual delegated-turn tool interval and end-to-end speech on the
board; do not count host read reduction as a measured latency improvement.

### UX-24 Prepare cancellable responses while listening

2026-09-26 extension: speculation must not own microphone acquisition. A
protocol failure on the speculative connection invalidates its endpoint and
cache, but a healthy local capture continues to its own verified end. One
whole-clip recovery is allowed only after capture succeeds and before speech
or effects; source/storage/cancel failures cannot enter recovery. Trace the
accepted upload prefix separately from the complete local recording. Orphan
audio completion markers carry no authority; final response identity, final
input coverage and effect policy remain required. This does not waive the
three-turn, whole-input, meaningful-latency or resource acceptance gates.
Rejected speculative text must also be removed from the fallback receipt field;
invalidating cached PCM alone does not prevent later TTS from recycling it.
Whole-input recovery must emit at most one completion cue, scheduled before
reconnection/upload. An already-started cue must drain before its ring is reused.
Cue timestamps survive plain-stream replacement within the same turn; old-turn
stamps remain excluded. An unusable completed candidate need not consume the
remaining speculative wait budget. Full input validation and one-shot recovery
remain mandatory, and cue timing never counts as useful-answer latency.
The speaker must retain the verified200ms zero lead before the180ms cue; the
20ms experiment removed its initial half in recorded output. Network work
continues concurrently. Acoustic verification uses unchanged spectral gates.
Duplex transport reads must preserve partial TLS/WS records on would-block.
Do not reset a partial header or replay bytes. Reads have at most two header
steps and one payload step; only the first may wait. A single owner restores
socket flags before writes; control frames are fully assembled and remain
outside ASR/model callbacks. RSV/mask/opcode/length/fragment errors stay fatal.
No new audio buffer or second TLS connection. Read timing includes scheduler
preemption; board measurement must establish improvement, not merely host tests.
HTTP-upgrade bytes retained inside the SDK must drain through the SDK reader
before raw TLS ownership changes. Hand off only after a complete frame and an
empty SDK poll; preserve fragmentation state. Do not inspect private SDK state.
A cache-path hit is distinct from audio arriving before capture completion:
report first cached PCM relative to local endpoint, and retain the possibility
of leading silence. Do not infer effective speculative overlap from hit alone.
59 measured9/9 complete inputs/tasks and first wakes,2 prefix-to-live hits,
7 whole-clip recoveries,DMA0,minheap28544B. Both hit prefixes arrived383/534ms
AFTER local endpoint; neither proves useful pre-endpoint audio. Three greeting
external answer candidates1.487/6.894/1.476s,remember15.162/16.205/15.505s;
correction acoustic alignment failed and stays unavailable. Stable1s,resources
and protocol cancellation remain open.43 host checks pass. Diagnostic app is
896B above normal guard and was rolled back to47 fast/capture/listening.
Selective-Oz experiment60 increased size and was not flashed; source restored59.

54 physical groups:correction3/3,greeting3/3,remember2/3 full/off-greeting0/3,
first wakes12/12.56:correction3/3,greeting3/3,remember1/3 full/tasks3/3,
first wakes9/9,minheap22112B. Rejected drafts do not enter fallback TTS.57
greeting3/3,one candidate/live-stream hit,all3 recorded cues pass after restoring
200ms startup silence. External answer candidates6.563/1.624/7.234s,minheap
43048B. Whole-input recovery and low memory still fail acceptance. No stable
1s claim. Physical device restored47 fast/capture/listening; goal stays active.
Use one existing realtime connection and keep uploading every captured sample
through provisional provider endpoints. Each committed input item and generated
response has a bounded identity/revision. New speech revokes previous cached
output immediately; delayed response.created remains bound to its original
committed input, never whichever speech happens to be latest. Ambiguous identity,
missing final ASR, cancellation, overflow or damaged cache cannot release audio.
Keywords may prepare routing, but neither keywords nor partial ASR authorize
GPIO, memory writes or claims of completed actions. Release only after successful
local endpoint, complete latest input and a matching qualified response; retain
the original complete-intent fallback. Cue/progress/useful-answer timings stay
separate. Implementation0.11.48 adds an opt-in `agent voice prefetch on|off`
(voice must be off; reboot defaults off), server-VAD200ms on one continuous
connection, at most8 input segments and16 response identities. A40KiB IMA cache
borrows the released TEN prefix; no extra PCM heap or speculative Flash writes.
Preserve it across capture handoff, drain before message/reply reuse. Ordinary
messages may stream after complete input acceptance; tool-associated speech
requires complete validation and never authorizes effects. Cache overflow is a
miss, not permission to play a truncated answer. Current status:42 host checks
including5 prefix-to-live stream/cancel/error cases and3 recorded protocol
replays pass. Audio/noaudio builds pass; board acceptance remains pending.
0.11.49 additionally requires a revision-matched source-time fence for noisy
tails (700ms source hold,160ms observation, no new local/cloud onset). First
device greeting group2/3 complete, correction0/3, prefetch-off baseline1/3;
first wakes9/9, no DMA loss, cumulative minimum34452B. Two successful prefix
streams start speaker181/182ms after local endpoint, but external whole-turn
answer candidates3.097/3.563s. Not accepted; physical device rolled back0.11.47.
Keep the opt-in development code/candidate archives, do not promote or claim1s.
See docs/VOICE_PREFETCH_SPEC.md for measured evidence, memory ownership conflicts,
official protocol constraints and the bounded integration sequence.

0.11.50-52 additionally diagnose empty finals and premature source fences.
Keep collecting empty-final segments but latch the draft ineligible; one complete
committed-clip replay is allowed only before an answer/effect, never recursive.
Continuous local dense speech beyond the cloud endpoint revokes the fast fence.
An exact cancelled-response audio terminal can be discarded; unknown IDs and
late audio still fail closed. Latest three correction groups complete0/3,1/3,0/3;
first wakes9/9, DMA0, minima38144/32728/30956B. The one complete recovery set
green only; it is not evidence for1s or stable correction. Latest application
1540064B under unchanged guard, no context/history reduction. Host42 checks,
three saved protocol replays and audio/noaudio builds pass; board acceptance
fails and0.11.47 is restored. Next work must capture the exact rejected response
identity/state before changing protocol gates. No more broad repeated tuning
from these results. Formal release and overall goal remain incomplete.

### UX-25 Reuse a completed manual conversation without pausing wake

Opt-in `agent voice reuse on|off`, voice off, volatile defaultoff. A maximum of
3 manual inputs may share one connection. Idle retention expires60s after the
initial connection preparation; an in-flight turn keeps its existing deadlines.
Retain only after a successful
ordinary response, final input transcription, drained speaker and persisted WAL,
with no effects, delegated receipt, active/pending response or partial message.
Send input_audio_buffer.clear and require its acknowledgement within1s. This
clears uncommitted audio, not provider conversation history. Preserve input and
response identities across turns; reject stale ASR/audio, duplicated IDs and
unexpected activity during the clear barrier. Reuse failure must not replay an
already spoken answer or erase the successful task result. Close on timeout,
cancel, other jobs/configuration changes or the bounded conversation limit.
Rearming the retained connection must not pause keyword inference or allocate
another TLS connection. Separate protocol/host evidence from acoustic timing.
Candidate61 uses the original SDK receiver by default; the larger incremental
reader and detailed transport counters remain optional diagnostics. This does
not claim the SDK read path meets the experimental bounded-read gate above.
Keep context/history budgets and normal1540096B app guard unchanged. Require
continuous3-turn board results, idle wake/heap data and existing task regressions
before promotion; no new formal release based solely on protocol success.

61 evidence:44 sanitized checks and audio/noaudio builds pass; actual113-event
wire replay covers3 same-session responses. Three physical groups give9/9 first
wakes,8/9 complete input/tasks; correction round2 times out during local capture
without effects. Greetings reuse readiness1/0ms after1855ms cold; acoustic
answer candidates1.699/1.420s, first round unknown. Memory12.238-15.844s,
correction9.094/9.142s for the two complete rounds. Minheap35296B, DMA0;
receipt underruns2/2/1 remain. Normal app1539216B fits unchanged guard.
Current device61 fast/capture/listening, reuse on, prefetch off, LED0. Formal
0.9.0 unchanged; no promotion or overall completion. Full evidence and hashes:
docs/VOICE_REUSE_REPORT.md. Historical current-device snapshots above describe
their respective stages; this result supersedes deployment status only.

### UX-26 Reject low-frequency capture tails without changing uploaded speech

The62 diagnostic preserves three complete frame traces and reproduces the
10s limit on host C replay. A second completed clip still ends at9.42s; its
7-9s tail has no lexical content in local ASR and broad low-frequency energy.
Do not raise the general energy threshold. Candidate63 applies one fixed
300Hz Butterworth high-pass, Q=sqrt(1/2), Q30, only to fast-mode clean-energy
measurement after the existing tonal filter. Preserve original spectral input,
raw/Flash/uploaded PCM,700ms endpoint, onset/continuation votes, deadlines and
classic path. State is zeroed per capture; no heap, new task or audio buffer.
Noisy low-frequency input may be vetoed; a real quiet word must not be truncated.
Require exact host filter/metadata replay, bounded arithmetic and reset tests,
full-input3-turn physical correction/quiet-speech regression, unchanged context
and normal flash guard. A replay-predicted endpoint is not a measured board
improvement. Report failed/unknown cases and retain diagnostic/raw evidence.

63 finite evidence:45 sanitized host checks, audio/noaudio builds and normal
app guard pass. Correction3/3 complete with green-only effects; quieter
greeting3/3 complete at gain.25 (usual.35), same synthetic voice, not independent
speaker generalization. First wakes6/6, DMA0, no observed reset. Greeting
external answer candidates2.058/1.160/1.284s, correction8.365/unknown/8.516s;
unaligned round remains present. Cumulative minheap38632B below48KiB. Current
device63 fast/capture/listening,reuseon,prefetchoff,LED0. Formal0.9.0 unchanged.
Full evidence/hashes in docs/VOICE_ENDPOINT_FILTER_REPORT.md. Reliable1s,
natural full-engine handoff and resource headroom remain unproven; goal active.
# UX-27: unchanged history selection and bounded request writes

Experimental 0.11.64 keeps the 200 KiB history budget and original partition.
Repeated requests with the same history boundary, bank generation, budget and
recent-turn index may reuse selection metadata only. Every transmitted record
is still read, CRC-checked and serialized; cancellation, exact Content-Length,
and tool-result checks remain mandatory. A new turn/index rebuild, compaction,
budget/boundary change or replay failure invalidates reuse. Small JSON fragments
remain coalesced; stable producer slices may pass synchronously, at most 4096
bytes each, with no new network buffer. Measure request-stage timings and full
useful-answer delay separately in at least one continuous three-turn group.
No inference of improved acoustic latency from fewer reads or writes alone.

2026-09-26 UX-27 evidence:46/46 ASan/UBSan checks and audio/noaudio builds pass.
App1539808B within1540096B guard; app-only flash and nonapp equality pass.
Repeated selection0–1ms vs earlier389–415ms; live histories differ, so no fixed
network/overall speed percentage. Continuous correction2/3 complete, memory3/3,
first wakes6/6,DMA0. Correction2 has3992ms capture, truncated final ASR and an
unjustified green effect; it FAILS despite matching the intended fixture color.
External correction alignment fails as a group; memory useful answer candidates
11.980/11.601/unknown seconds. Min37016B<48KiB. Firmware64 retained experimental,
fast/capture/listening,reuseon,prefetchoff,LED0,formal0.9.0 unchanged.
Context active bank1019632/1048576B, no deletion; check capacity before more runs.
See docs/VOICE_HISTORY_SEND_REPORT.md. Overall goal active, not accepted.

### UX-28: unfinished spoken correction guard (0.11.65/66, experimental)

An explicit self-correction at a clause boundary followed by an unfinished
negation/change target must not authorize a tool or release a guessed reply.
Detect only this narrow final-ASR pattern in Mandarin/Cantonese; a nonmatch
does not prove full input or repair VAD truncation. Keep the actual transcript.
Cancel/discard fast native or prefetched speech, close/join all owners, and
handoff to one tools-disabled streamed clarification request. No action receipt
may promise execution. Unexpected tool output is a protocol error. Cancellation
and failures cannot be converted into a successful clarification. Reset policy
between turns; ordinary and fully specified corrections retain their routes.

Validate parser boundaries, full vs incomplete corrections, draft discard,
cancel, tools-disabled wire format, unexpected calls, WAL ownership and next-turn
reset. Run one continuous three-turn incomplete-correction replay and one
three-turn complete-correction replay; preserve every failure and unknown
acoustic measurement. Synthetic replay is not human generalization evidence.
Keep the normal app budget1540096B, original partition and200KiB history budget.

66 follow-up: a successful clarification must finish the cue-only stream
normally while discarding all speculative reply PCM. Real cancellation or a
drain failure aborts handoff. The three observed missing cue ends on65 require
one additional three-turn targeted group, not an unbounded retest loop.

2026-09-26 UX-28 evidence:65/66 nine first wakes9/9, exact input3/9. Seven
unfinished transcripts caused no tools, only one final-only clarification;
two complete business inputs set green once each.66 targeted cues3/3 complete
by device events, two externally verified, one acoustically unknown.66 answer
onset candidates unknown/5.547/5.108s, min39152B<48KiB, DMA0. Guards/cue join
improved, but full-input, latency and memory criteria remain failed.46/46 host
checks and audio/noaudio builds pass. App1540080B,16B normal-budget headroom.
App-only backup/readback and nonapp equality pass. Current66 listening,
reuseon,prefetchoff,LED0; context1030232/1048576B active bank, no data deleted.
See docs/VOICE_CLARIFICATION_REPORT.md; formal0.9.0 unchanged, goal active.

### UX-29: bounded fixed-origin HTTP session reuse (0.11.67, experimental)

Use one worker-owned HTTP client slot. DeepSeek and Vocalign fixed origins may
retain a closed SDK handle/session for at most120s; job-only gateway connections
are destroyed at release and public signed URLs are never cached. Origin changes,
credential provisioning, explicit forget and expiry destroy cached state. Failed
or incomplete requests destroy their acquired handle; preflight rejection never
uses the cached connection. Joined paths must start with slash. Keep certificate checks,
redirect rejection, cancellation, no whole-request replay and actual-result policy.
`session_cached` proves selection of an SDK handle, not server acceptance of a
TLS ticket. Closed handles retain some client memory; measure rather than assume
a RAM saving. Do not change2MiB context,200KiB history or the normal1540096B guard.

Test production adapter ownership with SDK mocks, including expiry, cross-origin
credentials, partial writes, cancel, nonpersistent responses and failures. Build
audio/noaudio, freeze source, then app-only flash with verified full backup.
One continuous three-turn memory task measures first-request connection stages,
useful audio, receipts, cue completion, input integrity and memory. Preserve every
failure; this is a finite diagnostic, not randomized A/B or one-second acceptance.

2026-09-26 UX-29 evidence:47/47 ASan/UBSan and audio/noaudio builds pass.
67app1540064B within normal guard, full backup/app readback/nonapp equality pass.
Two continuous3-turn groups: wakes6/6, exact inputs6/6, complete4/6. First group
rounds2/3 fail FULL; all results retained. Existing verified-prefix maintenance
archives1168events to computer, preserves128complete turns byte-for-byte and
memory/summary/source, keeps200KiB budget and2MiB partition, then recovers capacity.
Second group completes3/3; cold first connects907ms and warm47/46ms, external
answer candidates12.894/11.284/10.465s. All6 cues/actual-save notices externally
verified; no human listening claim. Second-group min27192B<48KiB is a resource
failure, DMA0/no observed reset not a margin pass. Current67listening,reuseon,
prefetchoff,LED0,context233680/1048576B. Goal active, accepted:false/formal0.9.0
unchanged. See docs/VOICE_HTTP_SESSION_REPORT.md for all limits and rollback.

### UX-30: capture allocation evidence before handoff concurrency

Before introducing overlapping live model owners, identify the27KiB low seen
in67. Reuse default-off allocation-minimum hooks with fixed512B storage, no
allocation or logging inside hooks. One continuous3-turn memory group, all
failures/skipped callbacks/overwritten rows retained. Diagnostic observations
cannot establish release latency or all outstanding allocations. Use a clearly
labelled diagnostic inside the existing1KiB diagnostic allowance; release keeps
its1540096B budget. Restore normal listening after the bounded investigation.

UX-30 bounded follow-up: diagnostic67 reproduced25976B with a burst of tcpip
allocations (1512/1632/220B), but its overwritten rows do not identify every
outstanding owner or prove a leak. Trial68 changes only TCP send budget11520
to5760B and version; keep TCP_SNDLOWAT1536, receive5760, Wi-Fi TX32, algorithms,
history and storage unchanged. This revisits one parameter from the bundled
2026-09-22 duplex change, not the rejected32-to8 Wi-Fi-buffer experiment.
Normal (hooks-off) audio/noaudio builds must pass original guard. Exactly one
continuous3-turn memory group: retain incomplete inputs, compare upload and
answer stages, require all3 full tasks and a materially higher heap low without
gross transport slowdown before retaining it as an experiment. This is not
randomized A/B or proof of48KiB/1s acceptance. Otherwise restore frozen67.

2026-09-27 UX-30 outcome: diagnostic67 observed capture lows48600/39560/25976B
and first input truncated; keep that failure. Normal68 builds audio/noaudio and
original flash guard pass, app1540048B. Source audit248primary files confirms
only version text and TCP-send configuration change. One continuous normal
group: first wakes3/3, exact inputs/tasks3/3, saved-name tools3/3, DMA0/no observed
reset. Minimum38532B, final-send median1041ms, external-answer median12.128s;
normal67 comparison27192B/916ms/11.284s. History increased between groups;
no overall speedup or causal percentage claimed. Predeclared retention thresholds
pass, retain experimental68;48KiB and1s goals still FAIL. Prefetch remains off.
Final listening/reuseon/LED0, context287588/1048576B,2MiB and200KiB unchanged.
No context reclamation this turn. See docs/VOICE_TCP_QUEUE_REPORT.md.

### UX-31: native receipt download overlapping first DeepSeek request

Previous turn is progress:68retained a measured memory improvement and all3
normal turns complete. Next remove measured2–3s serial receipt-generation wait.
Only final-ASR, one complete future-tense plain receipt, no function/effect and
an idle parser boundary may hand off. Reuse the not-yet-needed engine reply
workspace for8KiB parser,4KiB producer stack and static task metadata; compile
assert fit. No second PCM allocation or context/history reduction.
The engine may prepare/send its first request while that producer drains the
original audio. Before receiving SSE or touching reply bytes, join and delete
the producer, then initialize reply. Also join on cancel, early/transport error
and caller exit. No hardware batch executes before final receipt validation.
Retain the existing serial path when admission/task creation fails. Do not
replay emitted audio or overwrite a live stack. Verify lifecycle on host before
normal/audio-off builds and finite3-turn physical groups. All failures retained;
native pipeline timing and useful-answer timing remain separate metrics.

The live Flash spool's pending write batch also borrows engine.buffer. Before
handoff, move that batch to the receive-only parser's unused outgoing JSON tail
(6144..7263); parser writes remain below6144. No protocol JSON may be transmitted
by the drain task. Seal or stop the producer before releasing this memory.
Producer publishes completion then suspends; owner verifies actual suspension
and deletes the static task synchronously before reusing its stack/TCB.
Finite first trial gates are frozen in ack-overlap-trial-plan-01169-01.json:
3complete memory turns, measured overlap, at least38532B heap/768B receipt stack,
no resets/DMA loss, median planning start<1.5s and external answer<=12.128s.
These are experimental retention gates; overall1s/48KiB acceptance is unchanged.

UX-31 disposition (2026-09-27): candidate69 failed the frozen device gates.
Three complete inputs, only2/3 answers, one NETWORK error; min30508B and producer
stack400/400/368B. Planning-start median691ms does not prove useful1s response.
Keep69implementation/tests only in frozen candidate source; restore default C
sources, build and device to68. Report: docs/VOICE_ACK_OVERLAP_REPORT.md.

### UX-32: bounded receive workspace diagnosis

Previous goal turn is progress:69 changed the next action with measured failure,
not a retained release. Diagnostic70 retains its original4KiB task stack but
moves the1KiB local poll packet into768B of borrowed parser tail. Metadata,
pending NOR batch and packet cannot overlap; parser/cancel/response rules stay.
Measure producer completion separately from join and preserve underlying WS
read errors before closing. One3-turn memory group only, then restore normal68
regardless of outcome. Existing1KiB labelled-diagnostic allowance may be used;
normal1540096B limit and200KiB history/2MiB data remain unchanged. It is not a
speed acceptance run or automatic re-enabling of speculative playback.

UX-32 disposition (2026-09-27): completed its one predeclared3-turn diagnostic.
All3 first wakes/full inputs;2 answers and1NETWORK. Producer stack1744/1728/1696B
now clears the diagnostic768B target, but minheap28828B and receipt underruns
3/1/4 remain. First rawread=-1 with socket/TLS cause0; this does not prove the
specific failing SDK operation. Completion-to-join waits1/1771/142ms measured.
Host47/47 and normal/noaudio builds pass; labelled app1540528B is432B over the
normal guard, not a release. Full backup/app-only restoration, exact17C/header
restoration and both baseline rebuilds completed; default apphash again c9689385.
Device68 listening/reuseon/prefetchoff,200KiB history/2MiB partition unchanged.
Next diagnosis should reproduce framing/timeout failures on host before another
device group; improved stack alone did not eliminate the first-round failure.

### UX-33: preserve WebSocket receive progress before more overlap

Previous turn is progress: the70raw-error and improved-stack evidence narrow
the next action to transport behavior and dual-TLS cost. Compile unmodified
pinned SDK transport_ws.c in an optional host harness, preserving public header
declarations and injecting transport reads. Prove partial-header timeout and
short-control-frame behavior, contrast with the C11 incremental reader, and
cover HTTP-upgrade retained prefixes with static/dynamic SDK buffers.

Candidate71 enables the existing bounded incremental reader after SDK buffer
drain. Keep the SDK for handshake/bootstrap; do not inspect private data on the
device or change its source. Free its upgrade buffer only when consumed using
the documented build option. No reply-loan task or extra receipt/DeepSeek overlap;
the existing final-answer/TTS overlap remains. Keep normal
Flash1540096B,2MiB persistence,200KiB history and448KiB clip. Concise system
wording from69 is independent of its failed task experiment; preserve current
hardware-query and no-unrelated-state contracts. Test all host checks, normal
and audio-off builds before a full-backup/app-only flash.
Predeclared ws-reader-trial-plan-01171-01.json permits memory and greeting
groups,3consecutive turns each, with all6first wakes/full inputs/tasks, no DMA
loss/reset and minheap>=38532B. On failure stop after that group and restore68,
including source/config. This gate does not replace the overall1s/48KiB goal.

UX-33 disposition: SDK static/dynamic cases and49host tests pass. Normal71 app
1539488B and audio-off build pass. Its first memory group has3/3first wakes,
inputs/tasks and no DMA/reset; min38444B misses38532B gate by88B. Stop after that
group, no greeting group/repeat; rollback68 application and4runtime/config files.
All tests/evidence retained. Formal answers external11.780/11.844/17.503s;
third turn includes an invalid-summary repair plus an extra light query. No
speed/naturalness acceptance. Main-task unused stack2308B is measured evidence
for a later bounded stack-budget review, not proof all command paths need less.
Full baseline rebuild succeeds; its70changed bytes are only build time/date,
ELF SHA and derived image checksums. SDK/config and executable payload match.
Actual device is restored from the original frozen68binary. Future build
comparisons must account for CONFIG_APP_COMPILE_TIME_DATE=y explicitly.

### UX-34: summary serialization and handoff round trips

Previous turn is progress: all71inputs completed, but memory missed the frozen
gate and one summary repair plus an unrelated query doubled model requests.
Keep the proven receiver candidate; measure relevant main-loop read-only paths
before reclaiming only512B of the6144B main stack, leaving a measured margin.
Do not reduce the network/other stacks, persistence or selected-history budgets.

Summary allocation must follow actual JSON escaping rather than6xevery UTF-8
byte. Keep source verification, CRC, cancellation and rollback behavior. Add
allocation bounds/failure tests for ASCII, Chinese, four-byte UTF-8, controls and
mixed text at the unchanged1536B limit. Tool feedback must identify summary
byte overflow and legal through_seq/fields without relaxing validation. Target
concise durable user facts in model guidance; omit volatile status/capabilities,
keep unrelated durable facts and allow combined requested actions.
Actual six-turn device gates and USB stack checks must be frozen before flashing.

72plan: memory then greeting, three turns each; all first wakes/input/tasks,
no DMA loss/reset, cumulative minheap>=38532B and main unused>=1024B. A memory
fixture requires durable before/after evidence; a previously saved fact may be
acknowledged without a redundant rewrite. Spoken success alone is insufficient.
This known repeated fact does not test first-ever memory creation. Stop after a
failed group; no retry to replace it. Formal one-second/48KiB goals unchanged.

72result: both groups6/6first wakes/full inputs/tasks, no DMA/reset; min38716B,
main-unused1796B and nine-command post-probe pass. Retain72experiment; normal
app1539984B, noaudio build pass. Known-memory readback verified, no argument
repair, but summary_set repeated2times and summary_get used2times across3turns.
External answer9.432–16.452s for memory and1.207–2.207s for greeting. No stable
one-second/naturalness acceptance or general speed percentage. Prefetchoff,
formal0.9.0 unchanged. Runtime source frozen; separate export-only correction
classifies normal capture_joined as diagnostic, not error. All earlier rows kept.

### UX-35: prove the speculative cancellation failure before changing policy

72is retained progress, not overall completion. Seven saved provider traces have
valid created/done ordering; they do not reproduce59's on-device cancellation
fault. Do not weaken response/input identity or final-coverage checks by guess.
Add an OFF-by-default, numbers-only rejection trace. Record pre/post flags,
response-ID CRCs, input/response revision and PCM byte count; no speech/keys.
Use one labelled73protocol diagnostic, one three-turn correction group, then
restore frozen72 unconditionally. Same normal data layout/budgets, <=1KiB app
diagnostic reserve; this diagnostic cannot pass speed or production acceptance.
Keep every failure and ordinary recovery; do not repeat until a failure appears.

73 result: exactly three physical correction turns, first wakes/full inputs/tasks
3/3, only final green applied; all three required whole-clip recovery. Minimum
heap29472B, DMA0, no observed reset; acoustic alignment failed, so no latency or
cue acceptance. Two transcript rejections are correct premature-success policy
cancellations. A separate ASR-final rejection remains branch-ambiguous; its
earlier empty final independently requires recovery. Do not relax either guard.

Three preregistered host-only calls of that saved clip:200ms at1x/2x each produced
six segments/three empty finals;800ms at1x produced two segments/no empty finals,
but still omitted words. Each condition one call, no retry/action/playback;
results are diagnostic, not aggregate accuracy or device-speed acceptance.
All finals in these three traces arrived in source order. Keep defaults unchanged.
50/50 host tests and diagnostic/normal/noaudio builds pass. Restore frozen72,
diagnosticsOFF/prefetchOFF/reuseON/listening; formal installer unchanged.
Next work must preserve full input while revising drafts, before claiming benefit
from speculative audio. Details:docs/VOICE_PREFETCH_STATE_REPORT.md.

### UX-36: preserve manual ASR while preparing a disposable response

Previous turn made diagnostic progress:200ms VAD fragments the saved correction
clip even at two send speeds. Existing manual-mode traces already contain live
ASR previews before commit. Investigate one-connection manual input: keep all
audio uncommitted, use session.update instructions to request one disposable
preview response, then commit the full original input once and validate its ASR.
This combination is unproven provider behavior, not assumed support. No early
tools/playback/history writes and no deployment before protocol evidence.

Run one bounded host-only pilot using the frozen73correction recording, same
Omni model/voice,512sample chunks,real-time pace and2s trailing zeros. At most
one draft and one final response;25s session bound,no retries/reconnections.
If the provider rejects uncommitted-input drafting, stop this route before
rewriting device state machines. Keep partials/results/failures; don't widen
the objective or count file-clock PCM as acoustic latency. Firmware72 remains
listening with experimentalprefetchOFF. Any subsequent device test is at least
three continuous turns, with frozen criteria, backup/app-only writes androllback.

UX36 result: first pilot stopped on changed precommit ASR ID, withno provider
error; immutablefailure retained. One explicitly bounded follow-up observed
two preview IDs, one complete-inputcommit and matching finalID. Fullcorrection
transcript intact, no reupload; draftPCM1.489s before fileend, but draft differs
from finalinput and finalreply falselyclaims unexecuted greenaction. Neither is
playback/action acceptance. No microphone/playback/flash in thisphase.

C11 now exposes manual session.update with upload continuing and acknowledgement
barrier, plus commit_input without implicit response. Oldcommit contract kept.
50/50sanitizer tests,3offline probe tests andnormal/noaudio builds pass. Normal
1540032B remains64B insideguard; sourceonly foundation is not activated ondevice.
Read-only USB snapshot confirms frozen72listening,reuseON,prefetchOFF. Remaining
work is the bounded candidate/identity state andadapterintegration, then physical
three-turn groups andfulllatency/resource checks. Goal unchanged andincomplete.
### UX-37: device manual draft experiment (0.11.75, not accepted)

Replace the experimental 200 ms server endpoint with one manual input and at
most one speculative response. A preview stable for 120 ms may start a silent
draft on the existing connection. Continue uploading every admitted sample;
after local capture joins, commit once and bind final ASR to its commit ID.
Preview changes permanently revoke that draft. Only an exact final match
(ignoring sentence-edge punctuation/whitespace only), a complete bounded audio
message, and the existing action/receipt policy permit playback. A miss with
valid input restores normal instructions and generates on the same connection;
protocol failures may use the existing one-time full-clip recovery before any
answer/effect. No second TLS client, new PCM allocation, context reduction or
raised application guard. Experimental mode stays opt-in.

Run host ownership/protocol/cancellation tests and normal/no-audio builds, then
freeze and back up before app-only flashing. Fixed physical groups: three
continuous greeting turns and three correction turns. Preserve every failure,
source capture, external recording and measured latency; no retries to replace
failed rows. Revert to frozen 0.11.72 if input/actions/wake/stability regress.
One-second acoustic response and 48 KiB stress heap remain separate unmet goals
until measured; passing protocol tests alone does not satisfy them.

UX37 result:75 three wakes but zero complete inputs, rejected missing optional
echo fields. Restored72, fixed against saved real provider events, froze76.
76 greeting3+correction3 all first wakes/full inputs; green-only correction3/3,
no recovery/DMA/reset observed. Draft hits0/6, ordinary replies semantically
wrong (temporary-transcription persona twice, empty receipt once), one wrong
colour receipt. Minheap28512B; one-second/48KiB not passed. Protocol completion
is not semantic acceptance. 50/50 sanitizer and normal/noaudio builds pass;
1539776B inside unchanged normal guard. Restored72 app-only, verified nonapp
bytes equal, reuseON/prefetchOFF/listening. No further physical retries in this
phase. Source76 kept experimental; goal remains active and incomplete.

### UX-38: isolate the speculative response's source before another deployment

Previous goal turn made progress: six complete physical inputs but zero useful
drafts, changed persona/empty greeting, one wrong parameter receipt; 72 restored.
Saved ASR records show batchwise revisable suffixes, so120ms stability does not
prove semantic completeness. Run exactly three file-only sessions of the saved
73 correction: no draft, audio-only response.create before commit, and a text
preview added to a stable product persona. Same model/voice, real-time512sample
upload, same2s tail, one full commit, at most one draft and one final response,
25s cap per connection; no playback, tools or retries. Draft trigger is after
the original file has uploaded, deliberately a protocol probe, not proof of
before-speech-end latency. Verify whether the model sees uncommitted audio and
whether a discarded answer can be isolated. Preserve every result. Only a
supported, bounded route proceeds to device integration and >=3 continuous
turns; no speculative API fields or replacement of the full objective.

UX38 protocol result: uncommitted audio alone can drive one response. A second
response after commit asked what else was needed, so consumed input must not be
regenerated in the same session. A fourth, separately registered overlap probe
committed200ms after the source file while that response was pending; one full
ASR and one correct future-tense response completed. Host only, no early-user-end
or device-latency claim.

### UX-39: audio-driven draft, source fence and authoritative commit

Candidate0.11.77-audio-draft removes partial ASR from system instructions.
One candidate is nominated after confirmed local speech and240ms classified
quiet, with the voiced source and quiet tail already uploaded. Producer counts
must cover uploaded samples except the current incomplete20ms frame. Future
positive frames beyond that immutable upload boundary revoke the draft once;
they cannot revive it. Classification is a conservative heuristic, not semantic
proof. No tools, playback or persistence before successful local join, one full
commit and authoritative ASR matching the commit acknowledgement. Commit may
overlap the one pending/active response. No draft means ordinary generation
once; rejected consumed drafts close the connection and hand complete ASR to
the existing DeepSeek route. Never generate twice on consumed input. Lamp
receipts containing explicit colours, digits or on/off guesses are rejected.

Unchanged40KiB borrowed cache, one TLS connection, context/history/clip budgets,
partition layout and1540096B normal app guard. No new task or PCM allocation.
50/50 sanitizer tests and normal/noaudio builds must pass before freeze. Fixed
device groups once each: three self-introduction turns and three corrected
blue-to-green turns, prefetchON/reuseOFF. Preserve failed wakes/rows; strengthen
fixture checks for requested introduction, role leakage, green-only effects and
parameter-free speculative receipts. Protocol completion is separately stored.
Record external audio, local ASR, internal counters, source fence, heaps/stacks,
DMA/reset and recovery. Do not call a cue or USB event a one-second reply.
Return to frozen72 if semantic/input/action/stability regressions or no useful
early answers; no repeated physical attempts replacing failed rows.48KiB and
one-second acoustic response remain full-goal criteria, not relaxed by this
finite experiment. Package77 remains experimental until all criteria pass.

UX39 result:50/50 host sanitizer checks and both builds pass. Actual greeting3
and correction3 first wakes/full inputs6/6, green-only effects3/3, no recovery,
DMA loss or observed reset. Candidate requested1/6, accepted0/6; first PCM of
that candidate516ms after localvad_end. External greeting candidates2.556,
3.084,3.164s; final correctionunknown,9.532,10.628s. Stressminheap38776B;
one-second and48KiB remain unmet. Initial content checker falsely rejected two
correct post-ASR green receipts as speculative. Rawfalse rows unchanged;
separate recheck admits correct final-ASR receipts while still rejecting black
and preconfirmed parameters. It does not change latency failure. Restored frozen
72 with complete backup/readback/nonapp verification, listening/reuseON,
prefetchOFF/LED0. No more physical rows in this phase; finite experiment closed,
whole goal still active. Report docs/VOICE_AUDIO_DRAFT_REPORT.md.

### UX-40: lexical intent nomination with final compatibility

Previous turn made progress: direct audio generation+concurrent full commit
works, but240ms source-quiet gate yielded one late request and0/6adoptions.
Replace that experimental nomination gate with bounded ASR intent categories.
The hypothesis is inferred from partial words; it never becomes a system prompt
or a tool command. Model generates one silent candidate from uploaded audio.
Only supported final intent compatibility plus response-specific checks may
release it. Explicit negation/unrelated topic changes revoke incompatible
candidates; incomplete input still asks clarification. Full recording, commit
identity, cancellation and no same-session regeneration remain mandatory.
Unknown questions keep ordinary full-input generation; do not label this a
general semantic classifier. Preserve general Agent fallback and full goal.

Cover partial/final pairs, topic/object changes, Mandarin/Cantonese forms,
revocation, missing/full ASR, cancellation, completed-claim rejection, cache
bounds and ordinary mode in host tests. Normal/noaudio builds and unchanged
1540096B guard before freeze. Then at most one greeting3 and correction3 physical
group, retain every row/external recording, evaluate full valid answers and
receipts separately. No repeated rows to replace failures. Restore frozen72 if
accuracy/stability regress or no useful early answer. Keep goal active unless
fullscope1s/48KiB and other requirements actually verified. No new task/TLS,
context reduction or default-enable before evidence.

UX40 result: host50/50 plus3 focused checks pass; normal/noaudio builds pass.
Finalapp1540080B stays16B inside unchangedguard after shared vocabulary/grammar
refactor. Six physical firstwakes/fullinputs/finaltasks6/6, requests6/6, hits1/6.
Introduction acoustic candidates3.928/10.387/10.271s; two cachefull misses.
Correction drafts explicitlyblue3/3 rejected, executedgreen3/3; acousticunknown
because originalalignmentgates failed. One rejectedargument then successfulgreen
was misgraded as effect; rawfailure retained with explicit derivedrecheck.
Stressminheap25556B, main1796B/network2504B, DMA/recovery/reset0 observed.
1s/48KiB unmet; restoredfrozen72 app-only with fullbackup/readback/nonapp checks,
fast/capture/listening/reuseON/prefetchOFF/LED0. Experimentclosed, overallgoal
active. No further UX40 physical repeats. docs/VOICE_INTENT_DRAFT_REPORT.md.

### UX-41: release validated candidate prefix, then stream

Previous turn is progress: actual adoption1/6, stale-parameter blocking3/3 and
measured wait/cache/heap failures change the next implementation. Preserve full
goal, not just introduction performance. Old provider trace confirms transcript
deltas precede PCM, while transcript.done may wait until all audio is delivered.
After joined capture, commit-bound final ASR and final intent/reply checks,
release a complete admitted sentence's cached prefix and stream remaining PCM
on the same response. Do not wait for the entire audio. Freeze spoken text at
release: non-whitespace later text, extra output/tool, mismatched final result,
network failure or cancellation stop output with no replay/effect/handoff.
This is ordinary bounded streaming after authoritative input, not a guarantee
of arbitrary model factual correctness. Earlier PCM remains silent. Candidate
miss before output retains whole-input fallback; no second generation on the
consumed session. Keep cache/partitions/context/stacks unchanged, no new task.

Host checks must exercise prefix+tail order (including odd cached sample count),
PCM after final ASR before response.done, delayed final input, late text/tool/
status failure, cancel and no replay, plus ordinary and handoff paths. Measure
the actual final-ASR receipt and release timestamp. Read existing heap evidence
before changing allocations; observed lower heap first appears during response
completion while capture is still active, which is not yet a causal diagnosis.
Normal/noaudio builds and1540096B guard before deployment. At most one group
of3 introductions and one group of3 corrections on device for this variant;
retain all results/external recordings and failed alignments. Restore72 if
speed/accuracy/stability regress;1s and48KiB remain full-goal criteria. Do not
default-enable or mark complete based on a narrow passing fixture.

Build-budget implementation: initial stream app1540496B exceedsguard by400B.
All three ESP-HI realtime begin calls useauto_vad=false. Compile that adapter's
unused automatic/segmented protocol support out withAGENT_RT_AUTOVAD=0; portable
default stays1. Unsupported requests rejectbeforeTLS and unexpectedserverVAD
events reject. One host adapter executable compiles the same manual-only backend,
the trace variant retains full backend. No active capture/endpoint feature is
removed. Do not alter guard/partition/context to fit the new stream path.

UX41 result: host08 all50 pass12.07s; normalapp1539648B, noaudio1065792B.
Six physical firstwakes/fullinputs/finaltasks6/6, requests6/6, hits3/6, cachefull0.
Introductions stream30720/30720/15360cached samples to105600/78720/78720total;
playback submission4ms after finalASR. Acoustic candidates2.708/2.701/2.190s,
not1s. Correction3/3 rejects explicitblue drafts and executesgreen; acoustic
unknown under unchanged alignmentgates. Minheap49396B afterintroductions,27640B
aftercorrections; main1796B/network2504B, DMA/recovery/reset0 observed. Restore
frozen72 app-only; fullbackup/deviceverify/readback/nonapp equality pass. Ready
fast/capture/listening,reuseON,prefetchOFF,LED0,COMclosed. No installer promotion,
context/partition reduction or furtherphysical repeats. UX41 is progress, not
completion of overall1s/48KiB goal. docs/VOICE_STREAM_DRAFT_REPORT.md.

### UX-42: identify allocation pressure during early output

UX41 is progress: three actual adoptions, complete prefix/tail playback and
measured remaining final-ASR wait. Current79 source and immutable audit rechecked;
physical72 remains the rollback. Existing allocation hook can identify task/size
near capture minima without allocation/logging in hook. First perform one group
of three continuous correction turns on labelled80-heap-protocol-diag, otherwise
same79 algorithm/config. Keep hook skips/overwritten rows and all trial failures.
Never use diagnostic timing for release latency or claim a size implies a caller.
Use existing1KiB diagnostic-only guard inside unchanged app slot; no expansion.
Back up/verify full Flash, app-only install/readback, keep data partition bytes.
After evidence choose one bounded implementation change; require host tests and
normal/noaudio builds before normal physical validation. Restore72 if no verified
improvement; no diagnostic left installed, no lowered1s/48KiB goal or context.

UX42 diagnosis80: all3 fullinputs/tasks pass, hook minima36212/28600/24848B,
retained low rows include tcpip1632/220B andwifi1700/28/16B. Ring skips23/28/8,
overwrites78/69/75. Lastcapture commit184ms, then1232ms to networkjoin. Existing
vad_end is AFTER uploadjoin, notphysicalmicrophoneend; preserve oldlogs andcorrect
interpretation. This doesnotprove exactbufferownership or leak. Restored72.

Candidate81 tests one scheduling change: manual speculation drains available
downlink across metadata/frame boundaries after each input append, bounded by
16 zero-timeout reads or2ms checked betweenreads. No-progress ends the burst;
ordinary/automatic paths preserve their existingbounds. No queue/context/cache
size changes, extra task or TLS session. Test order/cancellation/error/empty/count
andtime bounds, then normal/noaudio build. Only after those pass, one greeting3
andonecorrection3 normaldevicegroup. Retainallrows; no latencyclaims from80.
Assess input/task continuity, compare heap against79(27640B) and full48KiB gate,
acoustic introductions against79 andretained72. Restore72 iffullresourcegate
stillfails or speed/accuracy regresses; no extra physicalrepeatstoimprovevalues.

UX42 result: diagnostic80 three fulltasks pass, observationsabove retained.
81normal firstwakes6/6 butfullinputs/tasks5/6. Correction1 truncated at"不要",
uploaded64192samples; asksclarification withnoLED effect. No replacementrow.
Introductions adopt3/3, acoustic2.171/3.120/3.421s; correction2/3green, acoustic
9.584/9.865s. Stressminimum25604B, main1796/network2504, DMA/reset0 observed.
No verifiedimprovement: revertexactthree81sourcefiles to79afterhashchecking;
50/50restoredhost checks13.55s, normal1539648B/noaudio1065792B compile. Device
frozen72 restoredapp-only withfullbackup/readback/nonappverification, fast/capture
listening,reuseON,prefetchOFF,LED0,COMreleased.81packageacceptedfalse preserved.
Oldvad_end correctedderivedmetadata: uploadjoinaftercapture, notmicstop. Retain
1s/48KiB fullgoal; nextaction targets input/upload/final-ASR sequencing, not more
receivequota variations. docs/VOICE_DUPLEX_DRAIN_REPORT.md.
### UX-43: reuse a validated speculative session

2026-09-27. UX42 rejected the receive scheduler; source is frozen79 behavior,
device is restored72. Hypothesis: repeated cold handshakes create avoidable
capture upload backlog when speculation disables existing reuse. First run ONE
file-only provider session with three existing fixtures, one early response and
one whole-input commit per turn, then acknowledged clear. Retain every event,
PCM and failure; no playback, tools, new TTS, retry or replacement trial. Validate
distinct committed input/response IDs, final transcripts and late event handling.

Only if that protocol proof passes, allow reuse for a fully played, persisted
ordinary speculative response, with existing three-turn/60s limits. Preview IDs
remain advisory; only the commit binds final input. A miss, tool/delegation,
correction, cancellation, error or change to normal instructions closes the
session. Do not reuse a prediction session under normal configuration. Keep
context, buffers, queue sizes, stacks and app guard unchanged. Host identity,
state-reset and error checks, C replay of actual wire, normal/noaudio builds
precede any device flash. At most one continuous introduction3 and correction3
group, reuse ON, with external recording. Full backup/app-only/readback/nonapp
verification remains mandatory. Restore72 if full resource gate or correctness
fails; retain 1s/48KiB goals and all failure rows, no installer promotion by fiat.

UX43 result: one file-only session, three full ASR/responses, clear acknowledgements
63ms each;123 incoming events/215040PCM samples replay through C successfully.
50/50 host checks11.13s; normal1539760B/noaudio1065792B, no budget changes.
Physical introductions reuse rounds2/3, allthreeadopt; six firstwakes/fullinputs/
tasks6/6, no cachefull/DMA/reset/recovery observed. Lastintro cleanup80ms then85ms
uploadtail, vs79last980ms tail. FinalASR still427/1839/1067ms afteruploadjoin;
acoustic2.217/2.433/1.711s. Corrections rejectallblue drafts, executegreen3/3;
acoustic11.590/9.828/unknown (failedalignment retained). Minheap46500 then27704B,
main1796/network2504. Source82 experiment retained; restore72 app-only with full
backup/readback/nonappverification, fast/capture/listening,reuseON,prefetchOFF,
LED0,COMreleased. No further UX43 physical repeats, no1s/48KiB completion claim.
docs/VOICE_DRAFT_REUSE_REPORT.md. Overall goal remains active.

### UX-44: separate final-ASR wait from local output processing

UX43 is progress: same-session preparation proved, warm upload tail85ms, but
final-ASR wait427/1839/1067ms. Revalidate source82/package/current72 first. Add
diagnostic-only timing at whole-input commit and final-ASR receipt, using existing
WS counters plus received/cached PCM deltas. Counters measure wall time including
task preemption, not exclusive CPU. No protocol/model/endpoint/scheduler change.
One continuous3-introduction group on labelled83-protocol-diag, reuse/prefetch ON;
no diagnostic latency acceptance, no replacement trial. Existing diagnostic-only
1541120B guard, full backup/app-only/readback/nonapp verification, then restore72.
If the instrumented build cannot fit, do not enlarge budgets. Use evidence to
choose one next implementation; preserve complete input, cancellation, context,
one-second useful-response and48KiB stress goals. No default enable or completion.

UX44 instrumentation fit: full timing metadata1541264B exceeded diagnosticguard
by144B. Compact to existing eventtimestamps and totalcachedsamples atfinalASR,
not PCMdelta, yielding1541120B. Intermediate1541152/1541136B builds notflashed.
Normal path/budgets unchanged; all50 hostchecks pass12.24s, finaltwoadapter
variants pass after formatting changes. One3intro diagnostic: allfullinputs/tasks,
final-ASR waits1290/1943/276ms, transportcallwall1108/1723/254ms (86/89/92% of
1291/1944/277ms observationintervals). RXpoll alone896/1474/224ms; otherwall183/
221/23ms includesCPU/callbacks/preemption. CacheatASR46080/53760/0samples. This
supports transportwaiting asdominant, not a proof ofserverorTCPcause. Restore72
guardedbackup231915; no acousticacceptancefromdiagnostic. Source normal82 built
withtimingOFF1539760B. Currentdevice fast/capture/listening,reuseON,prefetchOFF.

Next bounded protocol check: official current3.5Omni documentation permits
16kHz PCM output. Run ONE file-only3-turn manual-draft reuse session requesting
16kHz via audio.output.format, preserving16k input/Tina/model and whole-input
identity gates. Require the matching output-format echo before audio, preserve
rawbytes/PCM/metadata, then independent local ASR/duration checks. If rate cannot
be verified, stop before firmware change. No new TTS, device recording, replay,
tools or retry. This tests a1/3 data reduction without enlarging memory queues.
It is not a subjective quality or device latency pass. Board music remains24k.

UX44 protocol result: one session,3 complete inputs/responses, matching16k PCM
echo before audio; local SenseVoice recognizes all3 output intents. WAV durations
3.84/2.24/2.72s, no full-scale samples. Names contain ASR spelling errors; no
subjective quality claim. Saved output16-protocol-01184-01, no retries/playback.

### UX-45: rate-consistent16k speculative output

Implement16k PCM for the dedicated Omni realtime path only, with mandatory
session-format verification. Keep input16k, music and separate DeepSeek TTS24k.
Update sample-duration limits and hardware clock together; keep the completion
cue's200ms zero lead,180ms sweep and10ms ramps independent of stream rate.
No extra buffers, contexts, sockets, stacks, model or endpoint changes. Remove
only unreachable manual-VAD diagnostic formatting to fit the existing app guard.
Host rate/mismatch/missing echo, live-draft/reuse/cancel/correction cases and the
actual16k wire replay precede normal/noaudio builds. One physical introduction3
and one correction3 group at most, full backup/app-only/readback/nonapp checks.
Preserve all failures. Restore72 if correctness or the full1s/48KiB goals fail;
do not promote release or reinterpret readiness heap as stress acceptance.

UX45 result:50/50 host checks11.17s, actual121events/140800PCM replay, normal
1539568B/noaudio1065792B; no buffer/stack/context/partition increase. Frozen84
748sourceitems. One intro3/correction3, firstwake/fullinput/tasks6/6, no DMA/reset/
recovery. Intro adoption3/3, finalASR425/281/496ms afterjoin, acoustic1.994/1.088/
1.067s. Intro min50040B is only short-group evidence. Correction rejects allblue
drafts, onlygreen set/get eachround; acoustic10.638/9.282/9.865s, cumulative
min30800B, main1796/network2504. Restore72 aspreregistered; source84 retained.
No1s/48KiB fullacceptance, no release promotion. docs/VOICE_PCM_RATE_REPORT.md.

### UX-46: fix completion-asset rate before further routing work

Fresh inspection finds84's locally cached light completion still24k while the
Omni stream is16k. Prior intro/correction groups did not play this asset; their
results do not prove this path. Device remains72, so do not promote84. First
re-export both existing Mandarin/Cantonese assets offline from their verified
post-tempo PCM at16k, preserving duration/pitch and receipts; no TTS generation.
Add a compile-time rate contract and independent FFmpeg decode/sample/duration
checks, including actual adapter PCM counts. Host then normal/noaudio builds.
One continuous3 literal-light group, full backup/app-only/readback/nonapp checks,
external recording matched against the saved asset at its actual rate. No other
latency test repetition. Preserve contexts and full1s/48KiB acceptance gates.
The subsequent routing design must use documented provider events; an arbitrary
text conversation.item.create is not established by the current official docs.

UX46 result: offline16k asset re-export preserves duration within1sample and
original receipts, saves8396B. Independent decode CRCs f885862c/bca453ee; adapter
emits18033/15424samples after existing leading-zero trim. New test reproduced
local-effect-before-old-socket-close; fixed cancellation order.50/50 hostchecks
11.27s, normal1531168B/noaudio1065792B, unchanged budgets/partitions. Frozen85
751sourceitems. One literal-blue3group: firstwake/action/readback3/3, fullinput
2/3, one whole-clip recovery. External independent ASR identifies fullstimulus
and completion all3, but round2devicePCM notretained and original waveform
alignment failed; latency and physical rate verification unknown. Min38448B,
main1796/network6680, DMA0. Do not replacefailedtrial. Restore72 app-only with
fullbackup/readback/nonappchecks, fast/capture/listening,reuseON,prefetchOFF,
LED0,COMclosed.85 remains unaccepted; docs/VOICE_COMPLETION_RATE_REPORT.md.
Overall1s/48KiB/fullinput goals remain active. Next diagnose input loss before
any more aggressive endpoint changes; preserve per-turn evidence.

### UX-47: preserve endpoint evidence for input loss

UX46 is progress: two concrete regressions fixed, one observed incomplete input
changes the next action. Keep85 capture/route/timing semantics and all budgets.
Build86-protocol-diag with only existing AGENT_ENDPOINT_TRACE enabled; no model,
threshold or endpoint changes. One continuous3 literal-blue group only, no
replacement trial. Add read-only per-turn wake/audio snapshots and export the
last committed device PCM after allthree and voiceoff; do not toggle listening
between rounds. Snapshot queries can lengthen gaps and trace adds postcapture
delay, so this group is diagnostic, not rearm/latency acceptance. Verify metadata
CRC and replay exactC endpoint offline, compare external and lastdevice ASR.
If allinputs succeed, retain negative reproduction rather than loop untilfailure.
Full4MiB backup/app-only/readback/nonapp checks, then restore72 listening with
prefetchOFF; normal source remains85 after diagnostic build. No new TTS.

UX47 result: one diagnostic3group fullinput/task3/3, failurenotreproduced. All
metadataCRC/Cparity3/3, localquiet700ms, no ASRforcedend/recovery/DMA. Actual
clips5476/4748/4872ms, min38900B, main1796/network7276. Lastdevice77952samples
and externalinput independentlyASRcomplete. Queries extendgaps625/641ms, trace
4/3/3ms, notspeed/rearmacceptance. Old85fixed2s-prefix alignment plus sweep
score1.0 finds endcue515ms beforeinputactiveend; originalfullfitfailure remains.
Oldfailednoise114/threshold240 matches currentnormalrounds, so nothighernoise
threshold. Exactbadframecauseunknown. Restore72verifiedbackup002212; normal85
binaryunchanged,86diagwithdrawn. docs/VOICE_CAPTURE_LOSS_REPORT.md. Goalactive.

### UX-48: same-connection final-intent tool continuation proof

Existing explicit corrections discard the candidate and take the slow fullAgent
route. Before changing firmware, one host-only session with three existing
synthetic clips (greeting,blue,blue-to-green). Request a preparation tool before
the full input ends. A read-only await_final_input tool returns ONLY the actually
committed finalASR and its verified itemID, then documented function_call_output
plus response.create continues on that same connection. Check finalgreeting and
blue/green tool proposals separately; never execute hardware proposals or invent
success. No arbitrary input_text injection, DeepSeek, audio playback/recording,
newTTS, reconnect/retry, or new model. Preserve raw events and per-phase PCM.
Require negotiated16k output, freshIDs, exactwholeinput and one completed prep
call before continuation. One independent paidreceipt directory; protocolproof
is not C3 speed/resource/cancellation acceptance. Stop on failure, keepdevice72.

UX48 result: oneactualsession, firstintro completeASR but no await_final_input
call; ordinaryanswer emitted, latercases notsent, no continuation/effect/playback.
Earlyrequest1.781s,commit2.125,firstPCM2.265,ASR2.375,done2.828; hostonly, no
speedacceptance. GenericConnectionended22.875s has unknowntransportcause because
oldreceiver swallowed exceptions. Executedscript preserved; observerfixed to
recordcause/stopmissingtool, no rerun. The all-turn mandatoryprep entry failed,
not a test of RGBcontinuation. Source85/device72 unchanged; goalactive.

### UX-49: final-only literal lamp corrections

Extend the existing completely consumed lamp grammar to at most three explicit
self-corrections. Each repair must follow a clause boundary, include an explicit
repair marker and a replacement setter plus named colour. An optional negated
colour must match the preceding colour. Reject cancellation, conditions, questions,
extra actions, unknown suffixes and unfinished repairs. Only the committed final
ASR may authorize this path; previews/cached answers never authorize effects.
Close the speculative socket before using the existing validated tool, WAL and
success-only local confirmation. Do not regenerate these narrow requests through
DeepSeek. No capture thresholds, contexts, buffers, stacks or partitions change.
Host checks cover stale blue proposals, same-route revisions, repeated repairs,
negation, cancellation, failures and ordinary/deferred paths. Then normal/noaudio
builds and freeze87. At most one continuous3 correction group and one existing
introduction3 regression group; no replacement trials. Validate the final green
effect/readback separately from generic success speech, retain all failed inputs.
Full4MiB backup/app-only/readback/nonapp checks. Restore72 after measurement;
no installer promotion. The complete1s/48KiB/input-integrity goals remain open.

UX49 postmeasurement review:87 allowed a missing boundary AFTER a repair marker.
That would confuse "不是改成绿色" (negation) with "不是，改成绿色" (repair).
Require this boundary too; otherwise delegate with no local effect. Freeze88 with
host and normal/noaudio validation only; the two physical groups remain87 evidence,
not88 acceptance, and are not repeated. Keep72 installed. No scope/goal reduction.

UX49 result:87 normal1531600B/noaudio1065792B; host50/50 pass11.36s. Firstwake6/6,
fullinput/task5/6. Correctionrounds1/3 onlygreen, finalASR-to-tool6/7ms; round2
capturetimeout, noeffect/guessedspeech. Acousticcorrection1=2.570s, round3alignment
fails; intro2.643/1.099/1.130s. Min36496B, main1796/network7280,DMA0. Restore72
with fullbackup/app-only/readback/nonappchecks.88 boundaryfix host50/50 pass12.72s,
normal1531616B/noaudio1065792B; frozen758items, NOTflashed/testedondevice. Both
audits preservefailures; docs/VOICE_FINAL_LIGHT_REPORT.md. Overallgoalactive.

### UX-50: overlap final ASR with capture verification

UX49 made progress, but capture tail and final ASR remain serial. In the fast
streamed path, publish an acquisition EOF only after the microphone/VAD stop and
the entire packed tail is flushed and readable. Keep the recording owner active
through CRC/readback, header commit and cleanup. The network may submit the one
manual input commit at this EOF; it must still join the owner and check its final
error before opening a speaker, admitting final intent, persisting or executing
effects. Never upload missing tail samples or retry a consumed commit. Classic
and recovery paths retain their existing contract. No VAD thresholds, buffers,
contexts, stacks, models or partitions change; no early speech/actions.
Host checks cover final short tail, commit ordering, late CRC/readback failure,
cancel, commit send failure, reused sessions and recovery. Existing clip tests
must prove provisional flush readable before header, with final validation still
required. Then normal/noaudio builds and freeze89. At most one existing greeting3
and correction3 group with prefetch/reuse, no replacement trials or new TTS.
Record commit/join/ASR times separately from acoustic reply onset; missing or
failed waveform matches remain unknown. Full backup/app-only/nonapp/readback,
then restore72; no installer promotion. Full objective and1s/48KiB gates unchanged.

UX50 result:89 app1531872B(+256), noaudio1065792B;50hosttests pass12.23s.
Frozen758items/334currentC-H-CMake verified. One intro3 + correction3,firstwake6/6,
fullinput/task5/6. Commit-return-to-join0/0/1/0/1ms; no proven speedup; intro3
uses normal reused recovery session. Intro acousticcandidates2.068/7.256/1.245s,
round2 wholeclip recovery aftermissingverifiedASR. Correction1 truncated60032
samples/final"blue,notright",noeffects/guessedspeech;2/3 onlygreen,final-to-tool6/5ms.
Correctiongroup fails original alignment gates; all acousticlatencies unknown.
Minheap31028B,main1796/network2504,DMA0,noobservedreset. Restore72 verifiedfull
backup/app-only/readback/nonapp equality,readyfast/capture/reuseON/prefetchOFF/LED0.
No additional paidgroups, installerpromotion or full-goal completion. See
docs/VOICE_EARLY_COMMIT_REPORT.md and earlycommit-audit-01189-01.

### UX-51: bound each upload record below the current TCP segment budget

UX50 measured little commit/join overlap. Current512-sample PCM append expands
through base64/JSON/WS to near1440B before TLS; local TCP MSS is1440B. Measure
actual production encoder output with short/maximum event IDs; reserve85B for
TLS1.2 CBC-SHA384 worst-case record overhead and12B TCP timestamp allowance.
Use464-sample29ms fast-upload blocks if the calculated complete budget fits;
keep512-sample microphone/KWS and generic realtime API unchanged. Same block
bound for wholeclip recovery, full final tail, no payload truncation or newheap.
This is a packet-sizing hypothesis, not evidence of negotiated cipher/pathMSS,
actual packet counts, or latency gain. Preserve all end/intent/cancel/CRC gates.
Host byte-exact roundtrips, boundary frame lengths, fault tests, normal/noaudio
builds and freeze90 precede device work. At most one intro3 and correction3,
same fixtures/harness, prefetch/reuseON; no replacement groups/newTTS. Compare
raw input correctness, final timing, acoustic gates and whole stressminheap.
Full4MiB verifiedbackup/app-only/readback/nonapp checks, restore72 afterward;
no installer promotion and no weakening full1s/input/48KiB requirements.

UX51 result: actual encoder+WS lengths for512samples1439..1448B,464samples
1311..1320B; adding85TLS+12TCPoption budget yields1408..1417B for464. No actual
cipher/pathMSS/packetcount measurement. ExactPCM roundtrips incl73tail pass;
50hostchecks13.72s, normal1531872B/noaudio1065792B, no newheap/partitions.
Oneintro3+correction3: firstwake/input/task6/6, greenonly3/3, noDMA/resetobserved.
Intro acoustic2.057/6.569/1.068s; secondwarmdraft missesverifiedASR andrecovers,
thirdnormalreused. Correction2.150/4.321/2.578s,final-to-tool5/6/5ms. Min38264B,
main1796/network6496. Lastcleanup-to-join12msintro/552mscorrection, no causal
speedclaim from small/unrandomizedgroups. Restore72 withfullbackup/app-only/
readback/nonappchecks,readyfast/capture/reuseON/prefetchOFF/LED0. Frozen759items,
334C/H/CMake audited; code90notaccepted/noinstallerpromotion/fullgoalstillactive.

### UX-52: observe final-ASR delivery on a reused manual-draft connection

89 and90's second intro both entered recovery without verified finalASR, but raw
commit-ACK/final wire events were not retained. One host session, three identical
existing greeting fixtures, currentdevice predictionprompt/model/Tina/16koutput,
464sample pacing,400mslead+700mstail, draft after37120samples. No microphones,
playback, tools, DeepSeek, newTTS, reconnect or retry. Preserve executedscript,
fixtures/config/hashes and every wire/traffic/PCM record. Observe up to8s after
each full commit; ifACK andcompletedresponse exist butfinalASRis absent, record
failedturn beforeclear/next; malformedidentities/transporterrorstop immediately.
Missingfinalneverbecomesacceptedinput. Preflightmustnotloadkey/network; decode
and currentC parserreplay afterreceipt. This isolates cloud/protocol fromC3 but
cannot establish realdevice latency, memory, VAD or cancellation acceptance.
Keep90source/device72; no firmware or installationpromotion in this observation.

UX52 result:one34.36sconnection,3full47040sampleuploads/commitACKs/completedreplies,
but0finalASReventsafter8seach. FirstPCM203..219msbeforecommit,422..437msafterrequest;
notbeforeutterance-endproof. All3protocolfailuresretained, no retries/tools/playback.
93rawevents/315sends/3PCMhashesverified. CurrentCprefix34events/52480samplesaccepts
wirebutcorrectlyrejectsnextturnwithoutinput_final; old84positive121events/140800
samplespasses. TwoASanUBSanprotocolchecks0.09s. Hostobservercontinuationisnotproduct
behavior. Missingfinalalsooccursfirstturn; notspecificallyareusebugorC3requirement.
Comparecreate/commitorderingnextasaseparateboundedtest, no causalclaimyet. No
runtimecode/appbuild/flash/installerchanges; source90/device72, fullgoalstillactive.

### UX-53: isolate the draft-request timing factor

One additional host connection, three identical UX52 greeting inputs, identical
executed probe, prompt, model, output format, pacing, lead/tail and limits. Change
only draft_after_samples from37120 to44080 (95blocks; approximately200ms before
the final append/commit). Preflight compares script/config/full-input hashes
against UX52 before execute. No replacement group, retry, newTTS, microphone,
playback, tools or device flashing. Preserve raw events and missing finals as
failures; final text must cover every word. Audit all3, replay actual C parser
with the appropriate successful or missing-final path. The result can identify
a timing-sensitive protocol condition, not certify general timing guarantees,
acoustic latency, memory or correctness on unseen corrections. Keep72 on device
and90 runtime source; choose any implementation only from the observed result.

UX53 result:exactlyone13.078sconnection/3turns, same script/config/input hashes
excepttrigger44080. FullfinalASR3/3,250..266msaftercommit, firstPCM219..328msafter
commit. CurrentC116event/204800sample/3turnreplaypasses. Earlier640mslead group
failed3/3; about200mslead group passed3/3. Sequential smallgroups show timing
sensitivity, not a safe production window or device/one-second acceptance.

### UX-54: observe a bounded post-commit confirmation on the same connection

UX53 does not justify delaying all predictions to a narrow timing window. Test
whether a completed early candidate and acknowledged full commit, still without
final ASR after350ms, can request exactlyone additional response to finish input
confirmation on the same connection. This is another model generation, not free
polling. One connection/three identical UX52 greetings at original37120trigger,
same prompt/config/pacing/input, at most6responses total. Preserve both responses
separately. No duplicate audio upload, newTTS, reset, retry, playback or tools.
Finaltext/identity checks unchanged; missingfinal after8s remainsfailed. Use
host-only probe and raw evidence; no firmware implementation or promotion until
actual final ASR and resource implications are understood. At most one group.

UX54 result:one18.937sconnection/3inputs/4responses. Firstturn triggersoneextra
generation360msaftercommit; bothoutputscompletedbutfinalASRstillmissingafter8s.
Next2turns receivedfinalASR250/265msaftercommitwithoutconfirmation; theirfirstPCM
was188msbeforecommit. Thus PCM-before-commit isnotasufficientfailurecondition;
same-sessionsecondgenerationnotavalidatedrepair. Groupfailed/exit1retained.
131wireevents/316sends/4PCMhashesverified; currentC59events/112640samplesprefix
passesandcorrectlyrefusesreusewithoutfinal. TwoASanUBSanchecks0.08s. Do notadd
timingwindows or second-generationpatch tofirmware. Verify supported separation
of authoritativeASR/prediction andC3budgetsnext; noactualhardwareacceptance,
firmwareupdatesorinstallerpromotion. Fullgoalstillactive.

### UX-55: standalone manual-ASR protocol and static memory feasibility

Verify the documented qwen3-asr-flash-realtime interface on one host connection,
three existing fixtures: greeting, blue-to-green correction, greeting.16kmono,
464sample pacing,400mslead/700mstail, one full commit each; no response.create,
clear, LLM, TTS, playback, microphone, tools or retries. Auto language detection
by omitting language; Mandarin fixtures are not bilingual acceptance. Keep raw
partial/final IDs, samples, times and configuration echo; wait at most8s for each
final, stop on missing/malformed identity, bound session120s. End with documented
session.finish/finished. Preflight no key/network. The test determines whether
partial text is available before full commit and whether finalization can work
without response generation; it does not validate simultaneous connections.
Audit current C3 ELF sizes, borrowed scratch boundaries and TLS buffers before
any dual-connection implementation. No new permanent RAM buffer, application
build/flash or promotion in this phase. Runtime90/device72 unchanged.

UX55 amendment from actual first connection: client rejected an ID-less
in-progress assistant/input_audio announcement beforecommit after9280samples.
That is client schema mismatch, not ASR effectiveness failure. Preserve01raw;
admit only this observed advisory shape beforecommit without binding anID or
authorizingfinaltext. Addoffline identity/content tests; permit exactlyone new
3turn validation group02 after this code change, no further replacement group.

UX55 result:02 also stopped beforecommit at7888samples because actual partial
text lacksitem_id. Two connections/configs succeeded; neither reachedfullinput,
so no ASRperformance/completeness verdict. Do not call this providerfailure.

### UX-56: collect standalone-ASR association evidence without authorizing output

One explicitly observational connection, same3fixtures. MissingIDs inpreviews,
commitACK or final are retainedasunknown, never synthesized. Still require full
upload->commit->ACK->singlefinal order; any presentcontradictingIDs/errors stop.
Finish eachinput byonefinal before next; no generation/tools/audiooutputs.
Record textcompleteness separately fromidentity/protocolacceptance. At mostone
connection/3turns,8sfinal/120sglobal; no retries/replacementgroup. This receipt
informs a future sequence/connection association contract, not deviceauthority.

### UX-57: isolated C11 manual-ASR adapter, offline only

UX56 returned all three complete transcripts but no item IDs, including on
commit acknowledgements and finals. Do not retrospectively change its failed
identity gate. Introduce a distinct one-input-per-connection adapter: validate
the session/model/config, upload once, commit once, receive one final, finish
the session, then close. No reused input session, automatic endpoint, response
generation, tool execution or audio output. Partial text remains advisory;
callers must also join capture integrity before using a successful final.

Borrow 8192B scratch and the caller's existing transcript buffer; no permanent
PCM/metadata allocation. Incremental WS frame handling, 464-sample uploads,
bounded text/input/time, cancellation and fail-closed cleanup are required.
Test observed raw frames plus fragmented UTF-8, correction, sequence failures,
backpressure, sample accounting, truncated/oversized events and cancellation.
Build host tests and an isolated C3 object for size evidence; do not add this
adapter to runtime selection or flash it until an integration design has a
resource budget. No further cloud calls or replacement observation group in
this phase. Keep current normal binary, installed firmware and data unchanged.

### UX-58: isolated ASR on the existing device voice pipeline

Previous turn is progress: UX56 supplied complete-input evidence and UX57
produced a tested C11 adapter. Add an OFF-by-default AGENT_ISOLATED_ASR build
option. Candidate 0.11.91-isolated-asr substitutes this backend for classic
Qianwen ASR only; existing fast Omni, streaming DeepSeek/TTS, local endpoint,
wake models and context stay available. Report the actual ASR model in status.
Use the existing single WS owner and its 8192B capture scratch, 192B session
on the worker stack and a 464-sample block; no second connection/task/cache.
The live source must drain its final short block, close on source errors and
join capture before any LLM work. Keep all input-integrity and cancel gates.

Host-test live-source idle/EOF/tail/failure and recorded input. Build candidate
and no-audio configuration; enforce the existing 1540096B application guard.
Freeze sources/app before flash. Full4MiB verified backup, app-only write,
readback and unchanged non-app bytes are required. Run one existing three-turn
zh/yue/zh wake + greeting group in classic mode, capture actual speaker output
and phase heap/times. No new fixtures/TTS creation, retries or extra replacement
group. This isolates input integration; classic output latency is not an early
reply acceptance result. Inspect any failure, then restore verified72 unless
the candidate has broader acceptance evidence; do not promote firmware/latest.

### UX-59: use the existing fast capture endpoint with isolated ASR

UX58 physical group passed first wake/input/task3/3, but source-to-PCM onset
was about10–11s. Actual ASR connect/session setup consumed about5s and the
classic TEN path stayed active; only then did64k samples drain in about2.2s.
Final ASR itself took305–314ms from commit start. Minimum heap39264B failed
the48KiB gate. Preserve that group and its restored72 state, do not retest91.

Candidate92 keeps the same isolated model/session and classic DeepSeek/TTS
continuation. Select the existing fast confirmation for isolated-ASR capture;
separate capture-end cue ownership from endpoint choice so original Omni
still owns its cue and classic/isolated capture plays theirs. Existing source
EOF, complete clip readback/join and cancellation remain required. Reuse the
same meaningful-partial lexical check solely for fast endpoint activity; it
does not authorize effects or declare the input complete. No new model, cache,
connection, permanent buffer or context reduction.

Repeat host/build/noaudio checks, freeze92, full-backup app-only install and
one fresh three-turn zh/yue/zh greeting group with the same fixtures/gain.
Compare capture overlap, source end, protocol times, acoustics and heap; this
is an endpoint integration comparison, not proof of speculative reply speed.
No same-version replacement groups or extra cloud probes. Restore72 after
measurement unless the candidate meets the broader user acceptance gates.

UX59 result: first wakes3/3 and meaningful previews12/10/10, but all three
recorded to10s and failed beforecommit. Setup1994/1996/1872ms is not an overall
speed pass. Minheap49552B excludes downstream LLM/TTS; no48KiB acceptance.
Restored72 with verified fullbackup/app-only readback/nonapp equality. External
recording aligns all three stimuli; no final playback. No repeated92 group.

### UX-60: bounded host observation of independent-ASR server endpoints

UX59 isolates a real integration failure: manual ASR finals follow local EOF,
while the lightweight local endpoint does not finish the measured inputs.
Before another firmware change, observe the documented ASR server_vad path.
Use the existing synthetic greeting/correction/greeting WAVs, three isolated
connections, at mostone attempt perinput and120s overall. No tools, TTS,
playback, microphone, private recordings, device writes or output authority.
Keep16kPCM/464sample paced chunks, threshold0.2 and700ms server silence; add
700ms silence to the retained700ms tail so natural endpoints can precede
session.finish. Send every original sample, never manualcommit in VADmode.
Collect speech_started/stopped source timestamps, commits, previews, final
segments and session.finished. MissingIDs remain unknown, not fabricated;
aggregate segmentation only for analysis. Record whether timestamps cover the
actual uploaded source and whether correction remains complete. Failures and
undocumented shapes are retained, with no replacement group. This informs a
future capture/ASR protocol design; it cannot prove physical speed, fidelity,
noise resilience or authorize final intent from an early segment.

UX60 preflight: IDF Python lackswebsocket (zero attempts). The configured local
audio Python has websocket1.8.0. First actual connection confirmed correct ASR,
16kPCM and selectedVAD but the client wrongly compared the whole nested object;
provider echoes extra create_response/interrupt_response booleans. No audio was
sent. Preserve both failures. Narrow validation to selected fields plus exact
model/text-only modality; test extra fields and conflicting settings offline.
Allow one corrected3input observation, not a replay of any transmitted input.
If another preflight or input fails, stop; no further replacement group here.

UX60 result: corrected03 observation completed three isolated inputs in18.079s,
no retry/output/effects. Total239600 samples, split58240/123120/58240.
Starts/stops/commits/previews/finals carry consistentitemIDs.
Finalsegments1/3/1; correction remains complete only after aggregating allthree
segments. Offline packet-hash/raw-event/order/source-time audit passed. A single
final segment cannot end deviceintent or authorizean effect. No further UX60
cloud calls or device changes; next C integration must preserve full-turn
aggregation, newer-segment revocation and capture-integrity/cancel barriers.

### UX-61: bounded C aggregation of observed ASR VAD segments

Previous turn made progress: device integration isolated endpoint failure,
then UX60 supplied actual VAD item IDs/timestamps and a three-final correction.
Implement a caller-owned C11 segment collector, maximum8 source-ordered IDs,
2048B aggregate text and160k input samples. Keep final segment text sorted by
source order, allow bounded delayed finals, reject duplicates/mismatched IDs,
overlapping active segments, invalid timestamps, missing/invalid commits,
truncation and overflow. Partial text remains advisory. A new speech segment
revokes a previous endpoint candidate immediately, even before new text.

Add opt-in server_vad to the isolated adapter without changing manual default.
Same single connection and8KiB scratch; collector metadata borrows caller
storage and the existing text buffer. VAD never sends manualcommit; only full
source EOF -> session.finish -> all segments final + session.finished -> close
returns complete text. Capture join/cancellation still gates all effects.
Test real three-input receipts with fragmented bytes, delayed/interleaved
finals, corrupt sequences, new-segment revocation, empty/oversize cases and
manual compatibility. Measure C3 object/structure resources before selecting
runtime mode; no new cloud calls, physical tests or flash for this collector
stage. This is required protocol work toward speculation, not overall goal
acceptance or proof of a safe acoustic endpoint.

UX61 result: collector/opt-in adapter implemented;52ASan/UBSan checks pass,
three actual traces replay20/43/20 receives,1/3/1segments,239600sent samples.
Additional assertions use the actual endpoint guard to verify newstart
revokes oldcandidate. C3 collector592B,session192B; objecttext2253/6464B,
data/bss0,maximum individualstack96/1056B. Normal1536816B andnoaudio1065792B
builds pass; normalbelow1540096B guard,partitionhashunchanged. Runtime still
selects manualisolated mode; no cloud/USB/flash performedinUX61. This stage
does not establish endpoint robustness, one-second voice or48KiB stressheap.

### UX-62: select isolated server VAD on the existing capture path

UX61 made protocol progress without device changes. Candidate93 selects that
tested collector in classic isolated-ASR mode, using592B worker-stack metadata
and the existing2049B transcript/8192B scratch. Keep the same single WS owner,
fast local confirmation, cloud700ms silence and existing local dense/resume
guards. No new connection, model, endpoint relaxation or context reduction.
Add a read-only endpoint snapshot to wake status so a rejected final can be
distinguished from a missing final; do not mistake all currently settled
segments for an irrevocable whole intent. Full source drain, session.finished,
capture integrity/join and cancellation continue to precede any LLM/effect.

Extend live/recorded source tests to both manual and VAD modes, including idle,
short tail, cancellation, corrupt input and empty source. Build host tests,
normal/noaudio, freeze source/app and verify the1540096B guard. After verified
4MiB backup, write application only and verify non-app equality. Run one
three-turn zh/yue/zh greeting group using the retained fixtures and gain0.35.
Only if all three inputs and tasks complete, run one three-turn correction
group to check that blue-to-green repair is fully collected and only green is
applied. No replacement groups or extra cloud probes; retain failures and
restore verified72 unless broader acceptance gates actually pass. These are
endpoint-integration measurements, not speculative-response speed acceptance.

UX62 result:52 sanitizer checks pass12.50s, normal1537152B andnoaudio build
pass. One three-turn group: firstwake3/3, setup1881/1867/1870ms, each final
segment contains the complete greeting, butall3 source captures hit10s and
timeout withoutwhole-input completion/LLM/TTS/effects. Newsegment afterfinal
observed inrounds2/3; lastguard newest2/finalized1/candidate0, oldend6902ms,
dense9940ms/source10000ms. No replacement or conditionalcorrection group.
ASR-only min49520B isnot a48KiB fullstress pass. External sourcealignment3/3;
no outputlatency. Both guardedapp operations independently audited,72 restored
fast/capture/reuseON/prefetchOFF/listening. Noise attribution remains unproven.

### UX-63: bounded ASR upload-filter comparison on retained device PCM

UX62 produced useful negative evidence: actual server finals are late and
local dense/no-new-segment guards block capture completion. Inspect existing
source and retained recordings before changing either guard. Device ASR
currently uploads raw PCM, whereas local decisions use filtered PCM. Evaluate
one fixed existing300Hz Q30 high-pass on three previously committed device
clips:37 greeting,62 correction,86 completeblue command. No gain, normalization,
threshold/model/VAD-silence change, clipping, trimming or learned parameters.
Use the exact production C filter with16B state, preserve every original sample
and add500ms declared zero tail to both paired inputs (still<=10s). Verify
retained PCM hashes, compare spectra and full local-ASR text for raw/filtered.
These three synthetic-source acoustic takes do not establish human generality.

Only if filtered text passes all three fixed input checks and introduces no
PCM saturation, perform one bounded host server-VAD comparison: six isolated
connections in raw/filtered pairs,464samples paced at16k,120s overall, zero
retries, currentqwen3-asr-flash-realtime/threshold0.2/silence700ms. Capture full
send hashes/IDs/source timestamps/whole finals and failures. Report endpoints
before appended zeros separately; do not credit padding as noise reduction.
No new device capture, speaker playback, flash, effect, response generation or
runtime filter adoption inthisphase. A benefit here only motivates a separate
device integration/resource test; it cannot relax input integrity or prove
one-second voice. If a prerequisite fails, stop that dependent experiment and
retain evidence rather than choosing another coefficient or replacement group.

UX63 result: all six local raw/filtered full-text checks pass, no added PCM
saturation; fixed tail-window change -2.54/-6.89/-2.69dB (not verified silence).
One paired cloud observation completes6connections,45.375s,591616samples,
zeroretry. Endpoint change0/-1479/0ms; only filteredcorrection endsbeforeadded
zero tail. Raw/filtered yield the same content, but correction omits请 andblue
substitutes四科for四颗; strict fulltext checks2/6. Filteredgreeting/correction
also produce an empty initial final. Do notadopt/uploadfilter orflash. Auditing
failure receipts separately doesnotturn these quality failures into passes.

### UX-64: preserve empty ASR segments without granting endpoint authority

ActualUX63 showed identified, committed empty finals followedbyusefultext in
the same valid input. CurrentC aborts atthat firstemptyfinal. Permit an exact
empty string as a finalized segment withzero storedbytes, retainitsID/order,
andkeepthenewstartrevocation. Ifthenewestsegmentisempty, settledstatehasno
endtimestamp, so oldertextcannotreceive a newendpoint fromnoise. Whitespace-
only malformedtext, duplicate/unknownIDs, invalidtimes/commits remainerrors.
Anall-emptywholeinputstillfailsatsession.finished; effectsstillwaitforcomplete
sourceupload/capturejoin/cancelchecks. No gain/filter/model/threshold changes.

Host-test empty prefix/tail/delayed finals, preview removal, all-empty failure
andexistingguards. ReuseUX63sixactualreceiptsforfragmentedCparserreplay, keeping
thecloudworderrorsseparatefromprotocolsuccess. Buildnormal/noaudio andmeasure
size; candidate94iscompile-onlyhere. No extra cloud/device trials or flash.

UX64 outcome: 52 sanitizer checks pass in 11.48 s. Six new receipt replays
(20/27/27/35/26/26 events, 1/2/1/2/1/1 segments) and the three original VAD
receipt replays preserve delivered text. The host manifest parser initially
hit the device request limit; the corrected host-only reader is bounded at
256 KiB, while each wire event retains the original device parser and budget.
Normal/noaudio builds pass. C3 metadata remains 592 B, session 192 B, object
data/bss zero, maximum individual frames 96/1056 B. Application 1537168 B,
16 B above 93, with 2928 B guard headroom; noaudio 1065792 B. No device change,
filter adoption or new performance claim; the last verified installed app is72.

### UX-65: apply existing capture radio mitigation to isolated ASR

The fast capture route already temporarily limits Wi-Fi power to 8 quarter-dBm
when RSSI is at least -55 dBm, then restores the exact previous value after
capture joins. The classic/isolated ASR route omitted this lifecycle. The 93
group had a strong link (-38 dBm) and normal power 80. This is a concrete
integration difference, not yet proof of the cause of its endpoint timeouts.

Candidate 95 adds the same guard to that route, including retained restoration
retry and cleanup after every join/error/cancel. Preserve the primary error;
a restoration error fails an otherwise successful capture. No change to PCM,
gain, VAD/ASR thresholds, source-end guard, model, context or speculative action
authority. It also includes 94's previously host-verified empty-segment fix.

Run existing radio fault tests and the host suite, normal/noaudio builds, size
and partition checks. Freeze a new source/app bundle. Only then perform the
verified full-backup/application-only update and one three-turn greeting group
(Mandarin/Cantonese/Mandarin wakes). Only if all three inputs and tasks finish,
run one three-turn correction group. No replacement groups or coefficient scan.
Record radio apply/restore, source endpoint, final text, first real speech,
heap, DMA, stacks and errors. Missing measurements stay missing. Restore 72
unless all broader existing acceptance gates pass; do not promote the installer.

UX65 outcome: one actual greeting group3/3 complete, one correction group2/3
complete input/core action (one ASR server error, not timeout). First wakes6/6;
temporary8/restored80 power6/6. Greeting acoustic onset candidates7.598/11.154/
6.774s; correction timing alignment failed, latency unknown. Minheap43032 then
40992B, DMA0, no observed reset. Normal tool_done timing markers exposed a
host classifier bug;7 tests pass after fixing classification, without relaxing
colour confirmation. Original failure reports retained; strict correction
content recheck1/3, correct green effects2/3. No acceptance or further group.
Both flash operations verify complete backup/app/nonapp equality;72 restored
and USB-confirmed listening/reuseON/prefetchOFF. Context and installer unchanged.

### UX-66: explicit text input for independently prepared fast replies

UX65 is progress, not acceptance. Classic voice still serializes two large
DeepSeek requests for a simple introduction. The previous same-Omni speculative
audio route sometimes returned a reply without final ASR. Verify a text-input
candidate path which can later be driven by the isolated ASR's advisory preview.
Official Model Studio realtime overview documents conversation.item.create with
input_text; retain qwen3.5-omni-flash-realtime, Tina, PCM16 mono16k, manual mode,
tools/search disabled and max96 output tokens. Do not switch models silently.

First add a bounded C11 text-item operation with exact item acknowledgement,
explicit response request, cancellation and no mixed audio input or automatic
tools. The caller retains immutable text; acceptance of that item is not final
ASR or playback authority. Existing audio behavior and budgets remain intact.
Host fault/fragment tests precede one group of three separate text sessions:
Mandarin self-introduction, parameter-free light acknowledgement, Cantonese
self-introduction. No microphone, device effect, playback, audio upload or
regeneration. Preserve actual request/response/PCM and all failures. Offline
preflight does not load credentials; execute uses a fresh immutable output
directory. Bound sessions to25s each/90s group; stop on protocol failure.

Replay original wire bytes through C at fragmented boundaries; measure output
duration/cache size and request-to-first-PCM separately from full connection
setup. This is protocol evidence, not before-endpoint or device latency proof.
Build audio/noaudio and measure static C3 state. No flash or second device TLS
connection until a separate ownership and peak-memory design is checked; do not
reduce context capacity or endpoint/final-intent requirements to fit the route.

UX66 observed contract amendment before any generation: cloud-01 omitted the
empty tool list; cloud-02 echoed exact user text but assigned its own item ID,
ignoring the client's. Both sessions closed before response.create (zero PCM).
Use a fresh connection with one pending text, bind a nonempty server ID <=64B
only after completed user-message echo with exactly the same content. Do not
send a client item ID or claim it was acknowledged. Reject duplicate/changed
text, unfinished status, mixed input and responses before the unique item ack.

UX66 outcome: cloud-03 completed exactly three responses; request-to-first PCM
453/453/500ms, full host setup-to-first PCM844/828/906ms. Original wire decoded
by production C under fragmented delivery:24/22/26 events,44800/34560/47360
exact PCM samples. All fit the actual24KiB IMA cache without overflow; lossy
IMA readback is not bit-equivalent PCM.52 host tests and4 Python tests pass;
normal/noaudio builds pass. No ASR overlap, playback, device test or acceptance.
C3 realtime state1064B(+8), dual core state2272B. Proposed static allocation
48368B leaves6048B in54416B engine scratch, excluding second TLS/SDK/socket,
task/stack, coordinator and unproved prebuffer lifetimes. Normal image1537920B
(+528B), soft headroom2176B; new text API is not called by firmware, so full
integration cost is unknown. Noaudio1065792B unchanged. Preserve context.
Next gate: explicit connection owners and measured dual-TLS peak, bounded text
configuration echo, then revision/final-input guarded scheduling. Do not infer
device latency or resource acceptance from host protocol or static layout.

### UX-67: independent ASR/reply transport ownership and resource gate

UX66 made progress with an implemented and verified text primitive. Before
joining it to advisory ASR, remove speech_ws's shared live connection: classic
TTS and Omni retain one slot; the isolated ASR gets its own slot when enabled.
Each slot owns TLS/WS handles, incremental reader, closed ticket and counters.
One task owns each slot; global expiration/config changes require both owners
joined under the work lock. No automatic reconnect or replay. Closing/cancelling
or failing one slot must leave the other usable; legacy same-slot BUSY remains.

Compile the real adapter with deterministic host transport stubs for allocation,
handshake/config faults, interleaved reads/writes, ping/backpressure, cancellation,
idempotent close and per-owner ticket expiry. Normal and noaudio builds must
pass. Preserve context and existing buffers. If the compile and slot tests pass,
freeze a labelled diagnostic app and measure three bounded two-session opens
on C3, with no input audio, response.create, tool call or playback. Each cycle
closes both; no automatic retry after failure. Record before/one/both/after
heap and minimum, maximum block and worker stack. This is only TLS admission
evidence, not complete speech workload or one-second acceptance. No reduced
heap criteria. Every device update/restore retains full-backup and data checks.

UX67 outcome: independent transport state implemented;54 host checks pass in
11.52s, normal/noaudio/labelled diagnostic builds pass. One device invocation
completed3 dual-session cycles, no audio input/response.create/output. Alternated
close order and survivor poll succeed. Local minimum61164B, ready largest65536B,
worker stack9028B, DMA0. Closed heaps95488/95484/95496B; after expiring tickets
96744B vs97892B before,1148B residual unclassified. Do not claim zero leak or
full48KiB/latency acceptance: capture, candidate task and downlink absent.
Normal1537904B, diagnostic1539904B, both under original1540096B normal guard.
Install/restore verify complete backups/app/nonapp; full Flash restored byte
identical before reenabling listening.72 USB-confirmed fast/capture/listening,
reuseON,prefetchOFF,LEDoff.2MiB context/204800B history unchanged. Next integrate
bounded candidate ownership/revisions only after checking actual capture arena
overlap and task/stack cost; preserve final-input gate and failure records.

### UX-68: advisory ASR to independent text candidate

Add default-OFF AGENT_TEXT_PREFETCH, requiring isolated ASR and audio. Start
the independent candidate worker only after ASR's handshake. Borrow the actual
43224-byte capture prefix: state, 24 KiB IMA, 8 KiB protocol scratch and static
6 KiB task stack/TCB must fit with compile-time and runtime checks. Keep the
live source metadata and ASR tail untouched. No heap PCM or reduced context.

One candidate per utterance, driven by the existing narrow self/light/memory
grammar. A revision or changed intent revokes it; the worker only caches audio.
Use qwen3.5-omni-flash-realtime/Tina/16k, no tools/search, max96 tokens and exact
configuration echo. Complete response, final whole-input agreement, capture
validation and worker join are mandatory before playback or workspace reuse.
Self introductions may finish directly; future-tense task receipts hand the
complete input to the ordinary engine. No guessed GPIO effects or replay.

Test protocol/config faults, revision/cancellation and ownership before builds.
Then freeze and run one three-dialog greeting group, with a correction group
only if resources/input integrity permit. Retain all outcomes; acoustic1s and
full-workload48KiB gates remain. Failed acceptance restores72 with complete
backup/app/nonapp verification. No installer or Git publication.

UX68 outcome:56 host checks and normal/noaudio builds pass. Static C3 candidate
41008B plus at most15B alignment fits43224B; worker stack free2344B. After
removing the superseded prediction branch only in this opt-in build, app1538400B
fits the unchanged1540096B guard. One3-dialog group completed all first wakes,
full Mandarin inputs and fallback answers. All3 candidates missed: only10240
samples arrived before500ms final-input wait expired. Nomination was124–209ms
before VAD, not proven before actual speech end. Minimum30092B, main worker
stack1096B, DMA0; acoustic fallback7.724–9.187s. No correction group after failed
resource gate. Backups/app/data checks pass;72 restored listening/reuseON and
prefetchOFF. Keep objective active and candidate unaccepted; no data reduction.

### UX-69: explicit engine workspace ownership

Prerequisite for reclaiming the unused candidate cache during TLS: separate
the 54416-byte C3 message/reply/SSE/request workspace from persistent engine
state. Keep every buffer capacity, history limit and partition unchanged.
The caller owns storage. Bind/detach must update the context serialization
alias, reject invalid alignment/size and refuse active answer/progress readers.
A detached engine must fail before network, persistence or tools.

This first refactor retains a static runtime backing store, so it does not yet
reduce heap pressure or change the candidate playback policy. Validate repeated
workspace relocation through real host history/HTTP scenarios, guard bytes,
detached/error paths and all existing ownership tests; build audio and noaudio.
Do not flash a storage-ownership-only refactor or claim latency/heap acceptance.
The next integration must replace the static backing with phase-owned storage,
skip TEN allocation for the already selected fast path, preserve ASR/metadata
lifetimes and join all owners before reclaiming. Measure actual full-workload
minimum after that implementation; the 48KiB and useful1s criteria remain.

UX69 outcome:57 ASan/UBSan checks pass (14.65s), audio1538736B and noaudio
builds pass.798-source freeze candidate-0.11.99 retained, not flashed. All
sizes/lifetime invariants remain; no speed or heap improvement claimed.

### UX-70: compact capture arena and deferred candidate PCM allocation

In the existing opt-in text-candidate build only, replace the static full
engine workspace with caller-owned heap storage. At the admitted fast capture
boundary, close obsolete owners, detach the context scratch alias and replace
54416B with28216B:17024B candidate control/task/parser prefix,3000B immutable
source metadata and8192B ASR tail. The fast endpoint is selected before TEN
construction, with the same700ms/source/ASR end rules as UX68. Normal mode and
non-candidate captures retain the full workspace and existing VAD selection.

Allocate the same24KiB IMA cache only after TLS/config acknowledgement and an
actual nomination. No request on cache-allocation failure. Capture/ASR and the
candidate worker must join before freeing the compact arena, cache or closed
TLS tickets. Restore the full engine/context alias before persistence or the
fallback LLM. Failures must return memory errors with no stale pointer use.
Keep complete-response/final-input checks and500ms candidate wait unchanged.

Run the ownership/protocol suite with allocation/restore failure and arena
sentinels, audio/noaudio builds and the unchanged normal image-size guard. If
those pass, freeze and run one3-dialog physical group, preserving all outcomes.
Keep true48KiB minimum and useful acoustic1s criteria; restore72 on failure.
This phase changes ownership/lifetimes, not input or context capacities.

UX70 outcome:57 tests pass13.45s; audio1539200B/noaudio1066032B.798-file freeze
retained. One3-dialog group passes first wake/full input/fallback answer3/3;
all candidate deadlines miss (10240/15360/10240 samples), no speculative output.
First candidate-stage SDK minimum57680B vs UX68's37152B; whole SDK minimum23888B
and acoustic onset7.923–8.325s remain unaccepted. SDK sums regional lowmarks at
possibly different times; no instantaneous full-workload minimum was measured.
Do not relabel as passing from sparse samples. Candidate stack2344B, network
stack1116B, DMA0. Backups/app/nonapp verified both ways;72 restored listening,
reuseON,prefetchOFF,context2MiB/history204800B. No extra group or promotion.

### UX-71: input-priority candidate connection and retained tickets

Keep the UX70 phase-owned storage and all final-input/response/deadline gates.
Give only the speculative WSS opener an explicit no-promotion policy: its
priority2 worker must not become priority4 ahead of the priority3 ASR uploader.
ASR, ordinary Omni and classic TTS retain their existing handshake policy.
The speculative opener shares the primary slot and cannot bypass exclusion.

Remove the two unconditional ticket-cache expirations added with the compact
workspace. Tickets are independent fixed-origin heap objects; preserve their
existing60s TTL, authentication-change invalidation and actual owner close.
Do not claim a server resumed TLS merely from a retained object. Workspace
restore must still fail safely if fragmentation prevents its full allocation.

Verify normal/diagnostic adapter tests at priorities2/3/5, failures, cancel,
slot exclusion and ticket lifetime; run existing host checks and both builds.
Then one frozen three-dialog normal group, no changed500ms wait or early
playback policy. This is a combined connection-policy experiment; report the
combined result, not an isolated causal percentage. Preserve48KiB/1s criteria.
On failure restore72; additional physical work needs a new evidence-based
change, not a repeat of the same group.

UX71 outcome:57 tests pass14.44s, normal audio1539232B/noaudio1066032B,
799-source freeze. Three first-wake/full-input/fallback successes; zero
candidate requests/PCM/playbacks. Ready arrives after final ASR455/462ms in
rounds2/3, first round not ready. Acoustic onset7.222–7.413s; SDK min23888B,
DMA0, no observed reset. Verified backup/app/nonapp restore72 and retained
445events/517728B/context2MiB/history204800B. No promotion or repeated group.

### UX-72: overlap connection setup before ASR is ready

The UX71 candidate started only on asr_started and could not submit in time.
Start it on asr_connect instead, after candidate arena admission but before
the independent ASR owner's synchronous open. Keep candidate priority2,
ASR admission4/upload3, separate owner slots, unchanged nomination/final-input
validation, complete-response gate and500ms post-ASR wait. No generation occurs
without a nomination. This moves setup only; it does not invent an early
intent or execute speculative GPIO. Idle warm/listener timing remains unchanged.

Concurrent TLS peaks have not been measured; require existing host ownership
tests, normal-size audio/noaudio builds, a frozen image and one3-dialog physical
group. Compare with71 while recording all failure/cancel paths. Do not claim
server resumption,1s response or48KiB memory from first PCM/sparse samples.
Restore72 on failed acceptance; no correction/long repeat after a failed group.

UX72 outcome:57 tests14.42s and both builds pass (audio1539232B/noaudio1066032B).
One3-dialog group: first wakes/full ASR3/3, completed spoken answers0/3.
Candidates cancelled with0PCM; requests in rounds1/2 only, after VAD671/662ms.
All fallback TTS runs fail allocating16749B; first playback actual free55528B
but largest block16384B. SDK whole minimum35108B, DMA0, no observed reset.
Restore72/full backup/app/nonapp verified; context452events/521256B/generation17/
next3550;2MiB/history204800B unchanged. This is a failed timing experiment with
a playback allocation regression, not an improvement. No extra physical group.

Acoustic analyzer regression: punctuation-only local ASR and error-interrupted
turns must not yield an answer-latency claim. Keep raw measurements/transcripts
and the superseded original analysis; write corrected v2 and separate clips.
Five focused tests pass; v2 marks all three rows unknown. Complete successful
turns still need the existing waveform and transcript gates; no subjective proof.

### UX-73: reclaim closed tickets at the capture-to-engine boundary

UX72 exposed a real16749B TLS allocation failure with only16384B contiguous
free space. After every capture/candidate/ASR borrower is joined, expire closed
WS ticket objects before freeing the compact arena and allocating the full
engine workspace. Preserve ordinary idle cache TTL and do not close an active
connection. This is a fragmentation mitigation to verify, not a claim that
tickets alone caused every failure. Budgets and partition layout stay unchanged.

Keep candidate priority2, concurrent connection start, nomination/correction,
complete-response/final-input and500ms rules. Add two post-join statistics:
actual candidate worker entry and valid session.created receipt timestamps.
No extra USB events in capture; these distinguish dispatch delay from setup
without claiming CPU time or TLS-only duration. Verify owner cleanup and event
ordering on host, both builds/normal size guard, then one three-dialog group.
Retain all failures and restore72 unless full acceptance is demonstrated.

UX73 outcome:57 tests14.47s, audio1539328B/noaudio1066032B,801-source freeze.
One three-dialog group completes first wake/full input/fallback3/3, no TLS
allocation failures. All candidates cancelled, noPCM; third revoked on a new
ASR segment. Entry delay2/3/3ms; entry-to-session.created4387/5177ms in first
two, none in third. Acoustic fallback7.285–7.696s; SDK min23888B still fails
full resource gate. Verified restore72,459events/525892B/next3557, capacities
unchanged. Reclamation mitigation observed on this group, not long-run proof.

### UX-74: share candidate CPU with ASR upload

UX73 proves the candidate thread actually started promptly; it did not become
ready promptly. Change only worker priority2->3, matching input upload while
remaining below capture and VAD4. Keep the no-promotion opener and all UX73
reclamation/nomination/revision/final-input/complete-response/500ms gates.
No priority4 speculative TLS as in old70. This tests fair scheduling; it does
not assume the4387/5177ms are pure CPU or prove that they are all starvation.

Existing protocol/ownership tests must include caller priority3, both builds
must retain normal size guard; freeze and run one normal3-dialog group. Record
ASR startup/upload as well as candidate stages, to catch latency shifted onto
the input. Keep48KiB and useful1s criteria. Restore72 on acceptance failure;
only a measured improvement justifies dependent correction/longer validation.

UX74 outcome:57 checks14.56s and both builds pass;1539328B audio,1066032B
noaudio,801-source104 freeze. One3-dialog group first wake/full ASR/complete
fallback3/3; allocation regression remains absent. Candidates allcancelled,
0PCM; entry-to-created4250/4388/4334ms, ASR connect-to-ready1799/1838/1726ms.
Acoustic fallback7.869219/6.823375/7.153719s; SDK min23876B, networkstack1084B,
DMA0, no observed reset. Neither1s nor48KiB acceptance. Verified restore72,
467events/532412B/generation17/next3565, capacity budgets retained. No further
priority trial or expanded group; next evidence needed is completed WSS open
separate from model session.created. Installer/Git unchanged.

### UX-75: separate transport-open completion from model readiness

UX74 cannot distinguish successful WSS open from session.created receipt.
Wrap only the candidate opener with a timestamp after successful return.
Copy its existing operations/owner unchanged into worker-owned storage; no
extra connection, read, credential path, retry or USB event during capture.
Record opened_ms alongside started/created/ready only after worker joins.
Failed/unattempted opens must remain0 rather than a previous turn's value.
This separates DNS/TCP/TLS/HTTP upgrade as a group from subsequent protocol
receipt/initialization; it is not a TLS-only, network-only or CPU measurement.

Keep104 behavior and all input/response/deadline/resource gates. Host checks
must verify failure and ordering with the real protocol adapter; build both
configurations under normal image budget. Freeze then one three-dialog group
to decide which setup phase to change. No additional priority experiment or
latency/48KiB success claim from instrumentation alone. Restore72 afterward
unless the entire existing acceptance is independently proven.

UX75 outcome:57 checks14.58s, audio1539408B, noaudio build passed,801-file
freeze. All3 first wakes/full-ASR/fallback responses succeed; candidates all
cancelled, samples0/5120/0. Entry-to-open4365/4301/4282ms versus open-to-created
5/4/5ms localizes delay to transport establishment/HTTP upgrade as a group.
Acoustic fallback8.120031/7.254719/7.037375s; SDK min22060B, networkstack1036B,
DMA0. Verified restore72;474events/537040B/generation17/next3572 retained.
No1s/48KiB claim or installer promotion. Next split TLS connect from upgrade.

### UX-76: time the nested TLS connection without replacing its transport

Use a default-OFF AGENT_WS_CONNECT_TRACE and GNU ld --wrap for the SDK's
esp_transport_connect. Register only the candidate's live TLS handle; forward
every original call exactly once with identical arguments/return. Record
entry/return clocks, without I/O, extra connection/handle/heap or SDK edits.
The handle comparison is atomic because ASR connects concurrently. Only the
candidate owner accesses its clock pair, and unregisters before destruction.
No credential or payload logging. The default build contains no wrapper.

Attach clocks to the existing post-join stats only on successful WSS open;
failed/unattempted opens remain zero. DNS/TCP/TLS wall time is still a group,
not TLS CPU time. HTTP upgrade elapsed includes scheduling/server/network.
Host checks must cover unchanged forwarding, independent owner, failure,
unregister/re-register and diagnostic JSON; verify final SDK call relocation.
Both builds, normal image guard, source freeze and one three-dialog group;
retain failed rows, restore72 and preserve all acceptance/capacity budgets.

UX76 outcome:60 checks12.84s, audio1539648B/noaudio1066032B,804-file106
freeze. Final ELF verifies SDK nested call enters wrapper. TLS-parent connect
4183/4311/4221ms; remaining upgrade72/64/61ms; created9/5/5ms later. NoPCM
or candidate playback. First wakes/full input3/3 but fallback answers1/3;
two16749B allocation failures recur. Only first successful row has acoustic
latency7.939719s; failed rows remain unknown. SDK min36000B, networkstack1028B,
DMA0. Verified restore72 preserves481events/540096B/generation17/next3579.
Ticket reclamation was a mitigation, not a general fragmentation solution.
Next fix fallback contiguous-allocation reliability before retaining more live
connections. Do not alter context budgets, certificate verification or gates.

### UX-77: forget prior HTTP cache at candidate capture admission

The candidate path currently closes but retains a completed prior DeepSeek
HTTP handle before shrinking its54KiB workspace. Its ticket/config allocations
may pin holes through capture and restoration; this is a hypothesis, not a
proven cause of UX76. At the existing reserved-turn/work-lock boundary, fully
forget that idle HTTP owner before unbinding/freeing the workspace. Preserve
normal non-candidate HTTP reuse and every active-call ownership constraint.
Keep106 diagnostic clocks, priorities and request/input/playback decisions.

Use existing HTTP cleanup/isolation and workspace ownership tests, both builds
and normal image guard. Freeze then exactly one three-dialog group; preserve
all errors and restore72 if overall acceptance remains unmet. Do not expand
to more cases merely if this group succeeds; inspect largest-block and cold
connection cost. No context/partition budget reduction or automatic replay.

UX77 outcome:60 checks14.82s; audio1539632B/noaudio1066032B;804-file107
freeze. First wakes/full input/complete fallback3/3, no16749B allocation
errors in this group, noDMA loss/reset observed. Candidates all cancelled,
noPCM. TLS-parent4230/4188/4171ms, upgrade68/64/65ms; acoustic fallback
7.827719/8.541719/8.160031s. SDK min23880B/networkstack1028B still insufficient.
Verified restore72:488events/544748B/generation17/next3586; all budgets intact.
Do not claim general fragmentation repair or causal speed improvement.
Potential next step: after full-input agreement for a simple answer, promote
the matching candidate to an authoritative fast response instead of cancelling
at the speculative500ms cutoff and invoking large-history DeepSeek. This needs
an explicit bounded policy and tests; it is not implemented or accepted here.

### UX-78: promote an agreed simple candidate to the final fast answer

After successful capture and complete ASR, the existing final-intent gate must
agree with the same immutable SELF nomination, language and segment revision.
Only this branch may wait up to1800ms from final-input admission, instead of
the500ms speculative receipt budget. Report candidate_final at admission.
This is an explicit scheduling change, not a new definition of1s success.
Task receipts keep500ms; changed/cancelled/uncertain inputs get no promotion.

Keep a single generation and complete-response/type/text/audio validation,
same worker-owned transport/cache and join-before-play/free policy. Global
cancel remains checked while waiting; malformed, stalled, OOM or expired
generation falls back as before. Persist the actual full input after accepted
playback; no speculative tool execution or enlarged memory/context budget.
Host delayed-response tests cover late SELF acceptance, unchanged task timeout,
cancel during promoted wait and failed capture. Run existing tests/builds,
freeze then one3-dialog physical group; distinguish promotion, received PCM,
actual playback and skipped DeepSeek. A speed/resource improvement is needed
before a dependent correction/task group; retain failure and restore72 if
the overall acceptance is unproven. Do not expand the narrow gate's scope.

UX78 outcome:60 checks20.45s;1539712B app. Three first wakes and complete
answers, but only1/3 candidate hit; other two cancelled/full24KiB cache.
Acoustic9.286531/9.316719/3.672719s; firstPCM after finalASR638/608/624ms.
SDK minimum23880B, stack1028B, noDMA loss. Restored72,495events/549360B,
next3593. No full-workload48KiB, one-second or subjective acceptance.

### UX-79: stream the confirmed candidate sentence

Keep one speculative request, existing final-input/revision/language/capture
gates and SELF-only promotion. Extract output-readiness from same-session
ASR authority: a complete validated sentence plus audio may be released only
after the independent full-input gate grants permission. No invented commit
or transcript flags. Release at a completed WS message, outside JSON callbacks.

Reuse the joined ASR's disjoint8192B tail as the playback ring. Candidate worker
alone owns its parser, compressed prefix and PCM writes. Main publishes the
ring and permission with release/acquire atomics, then joins the worker and
speaker before freeing either workspace. Task receipts keep their existing
complete-response500ms policy and sealed spool. No new persistent allocation.

Before playback retain1800ms from full input. After release allow the single
stream to finish, bounded by10000ms from full input and the existing14000ms
session cap. Cancellation remains independent. Validate final response type,
identity, completion and transcript; a late error after release is handled as
partial failure, never automatic replay/DeepSeek fallback or successful turn
persistence. candidate_stats.samples now counts received PCM, including its
direct tail, instead of only the compressed prefix. No changed model/prompt.

Tests cover split input/output authority, deferred permission, cache bypass
beyond24KiB, delayed audio tail, cancelled/changed/failed input, late transcript
mutation/mismatch, sink failure and workspace lifetime. Disable optional
TLS-connect diagnostics in this normal build; keep the normal1540096B guard.
Run one three-dialog group only after builds/tests/freeze; retain failed rows,
use external acoustic timing separately from PCM arrival or cues. Do not
expand scope or claim general anticipatory conversation from a SELF branch.

UX79 outcome:109 normal app1540048B/noaudio1066032B,804-file freeze. Full60
checks23.32s plus final candidate-only two checks10.75s. One greeting group
3/3 live candidate successes with no DeepSeek, acoustic2.748375/2.836719/
2.782375s; final-to-play812/659/749ms is NOT acoustic latency. One long tail
exceeds old cache capacity. No reported underrun/DMA loss, SDK minimum29004B.
Dependent correction group3/3 full inputs and final-green-only actions, but
3IDLE watchdog alerts: runtime stability FAIL. Acoustic7.777375/7.847375/
unknown; third source alignment failed. All original outputs preserved.
Restored72/listening:510events/557340B/next3608, budgets unchanged. No1s or
48KiB acceptance, no subjective pass or promotion. Next work must diagnose
the repeatable scheduling failure before further early-connection trials.

### UX-80: protect idle scheduling during speculative TLS

Lower only the independent candidate's synchronous connect call to idle task
priority; restore the exact calling priority on every return. ASR/classic
priority, TLS verification, cancellation semantics, response generation,
streaming and full-input gates stay unchanged. No WDT timeout/reset/disable,
SDK modification, extra task or allocation. Time slicing is enabled in the
pinned SDK; idle_should_yield is0, so measure the latency cost rather than
assuming the scheduling change is free. This is a mitigation to verify, not
proof that all watchdog causes or earlier speed requirements are resolved.

Host adapter checks both owners and priorities0/2/3/5, success/error/cancel,
including diagnostic wrapping. Add explicit runtime-health evidence to future
physical reports: a completed action with a watchdog/panic/reboot is not an
overall successful trial. Do not alter previous raw reports to turn failures
into passes. One correction group directly targets the known failure; if it
stays responsive and all three flows complete, one greeting group checks the
latency tradeoff. Restore72 if overall acceptance remains unproven.

UX80 outcome:60checks24.36s plus8health/content checks;1540048B audio and
1066032B noaudio,804-file110 freeze. Two3-dialog groups firstwake/fullinput/
function6/6, no observed watchdog/panic/reboot/DMA loss, no final underruns.
Correction acoustic9.140031/8.050875/8.461031s; greeting9.624375/3.526375/
8.645375s. Candidate hits1/3 versus109's3/3; WSS setup5534/5197/5345ms.
Idle priority alone regresses latency and is NOT the final policy. SDKmin23888B,
networkstack1340B, no full48KiB or1s proof. Restored72,527events/568028B/next3625,
all capacities retained. Future work must preserve idle responsiveness without
discarding fast candidates or hiding delay with a larger timeout.

### UX-81: cooperate with idle during candidate setup

Retain candidate handshake priority0 and register one permanent, normally
inactive C3 idle callback. An atomic phase flag grants only nonblocking
taskYIELD while the candidate opener is active; the callback owns no pointer
to the candidate task, capture arena or TLS data. It returns normal idle
behavior after the phase ends, including an end occurring while it yielded.
Register failure fails admission; no network generation begins in that case.
Success/error/cancel must clear the phase flag. No WDT feed, disable, timeout
change or SDK modification. Other idle hooks remain active.

Returning false while handshaking avoids losing a time slice to idle sleep;
it can spin when the candidate is waiting for network I/O. Record this bounded
power/CPU tradeoff explicitly, rather than describing it as a final energy
optimization. Only the setup phase is enabled, not listening or playback.
Move session.created diagnostic timestamp behind existing CONNECT_TRACE to
keep normal app budget; protocol validation of session.created is unchanged.
Test hook registration/failure, yielding/normal return, flag cleanup on all
paths and both connection owners. Existing all60 and8health checks plus both
builds precede freeze. Start with the3-dialog correction failure fixture, then
one greeting group if appropriate; do not accept slower fallback as success.

UX81 outcome: final app1540032B/noaudio1066032B;60initial hostchecks24.53s,
8healthchecks plus5affected finalchecks10.75s. Two3-dialog groups completed,
firstwake/fullinput6/6, observed watchdog/panic/reboot/DMA0, finalunderruns0.
Greeting candidates3/3; external2.651719/3.578719/2.777719s, setup4314/5275/
4409ms. Generation still626–892ms afterVAD; no pre-end response readiness.
Correction onlyfinalgreen3/3, candidatecancelled/no generation; fixed acoustic
alignment failed, all3latenciesunknown. SDKmin23888B/workerstack1356B, full
48KiB and1s still unproven. Restore72 verified,543events/576800B/next3641,
all data capacities retained. No installer promotion; limited evidence only.

### UX-82: distinguish TLS preparation and crypto costs

Optional TLS_PHASE_TRACE requires existing WS_CONNECT_TRACE and a labelled
protocol diagnostic app. Bracket unchanged pinned IDF TLS config/handshake
entrypoints and PSA key-generation/agreement/verification calls. Only the
candidate owner's task records counters; every argument, output and result
is forwarded once, unchanged. No keys, certificates, transcripts or credentials
are inspected/logged. Wall time includes scheduling and network waits; sums
are not CPU time. Other tasks bypass counters. Normal build excludes this
module and all wrappers. Clear watch on connect return; report phases only
for successful openings to avoid stale data after early precondition failure.
No TLS/SDK settings, cipher preferences or timeout changes. One three-dialog
physical group after host/build/backup gates, then restore72. Measurements
must determine the next optimization; this is not response-speed acceptance.
candidate_tls is a fixed10-integer JSON tuple: configbegin/end, handshakebegin/
end, generation/agreement/verification wallmilliseconds, then those3callcounts.
Static clock view is read after workerjoin and before anothercandidate starts;
no per-worker copy or additional allocation is needed.

UX82 measured:3firstwake/fullinput/candidate/completion, runtimehealth passes.
Preconfig67/50/48ms, config0/1/1ms, handshake4312/4323/4315ms, agreement245/
244/238ms, verification2167/2317/2230ms with3calls each; remaining1900/1762/
1847ms unattributed. Not CPU or cipher/curve attribution. Three valid external
recordings2.851/2.852/2.833s, diagnostic only. SDKmin28908B, nofull48KiBproof.
112diag1541072B, normal1540032B/noaudio1066032B;812-filefreeze. Restored72,
549events/579452B/next3647, allcapacity retained; no ordinary-release promotion.

### UX-83: reserve Flash for faster TLS compilation

The firmware has no TLS server endpoint. Select the SDK's client-only TLS role
without changing peer verification, cipher/group lists, buffer sizes, tickets,
wire protocols, timeouts or scheduler policy. First build client-only at-Os to
measure code savings; then the same client-only selection at supported-O2.
Verify actual SDK choices and compile commands, normal app budget, disabled
diagnostic wrappers and noaudio build. Freeze only a budget-compliant result.
If the full performance profile does not fit, retain measured artifacts and
do not raise the guard or remove data/context capacity. No claim of faster
response until physical3-dialog groups measure it; check both simple answers
and corrected tool intent, including real DeepSeek handoff.

Measured fullclient-O2=1558480B, overnormalbudget18384B: notflashed. Client-only
-Os=1532608B, saves7424B from111 and leaves7488B normalbudget. Use explicit
AGENT_TLS_ASYM_PERF for only pinnedbuiltin bignum.c,bignum_core.c,rsa.c at-O2;
otherlibrary files remain-Os. Require clientonly/globalSIZE, missingfiles fail
configuration. Verify actual per-source finalflags and SDK tree cleanliness.
114 is this experimental profile; no fresh cloud model or persistent buffer.

UX83 measured:114=1536400B/noaudio1062400B,812-filefreeze. Greeting firstwake/
fullinput/candidate/answer3/3, acoustic2.451719/2.397375/2.491719s, setup4008/
4038/4081ms. Requests still527/583/758ms afterVAD; no pre-end response readiness.
Correction onlyfinalgreen3/3 and no candidate request/PCM/play, but round3
missed initial polite prefix: input_complete=false, groupcomplete=false/exit1.
Acoustic6.699031/5.710375/6.791031s; final value is a failed-input observation.
Runtimehealth6/6, observed watchdog/panic/reboot/DMA0, finalunderruns0. SDKwhole
min22204B/networkstack1356B, neither full48KiB nor1s proven. No causalpercent
from unpaired groups. Verifiedrestore72,565events/588216B/gen17/next3663,
allcapacity unchanged; normalinstaller not promoted. Preserve failedgroup.

### UX-84: bounded idle preparation with an owned workspace

Extend experimental text candidates to the existing volatile `preconnect idle`
option. Once after enabling or completing a turn, release the unused engine
workspace, allocate the existing compact capture layout, and start its existing
candidate worker. Prepare the text-only session but send no user item, response
request or audio before a real ASR nomination. Idle lifetime is30s, one attempt
per idle period, with no reconnect loop. Capture may claim an in-progress warm
worker without waiting for TLS; ASR retains its own worker/connection. Claim
starts the existing14s active bound. Source metadata/tail remain disjoint.

USB work, background sync, option/credential changes, cancellation and expiry
must cancel/join the idle worker before restoring or reusing its memory. Main
cancel only publishes a lifetime-independent atomic flag; no borrowed pointer
is read from the command thread. Idle state is an atomic public status. Cold
capture remains available and unchanged when idle preparation is disabled.
No new stack, large permanent buffer, model, weaker TLS or changed endpoint
threshold. Test quiet idle, ready and in-progress claim, cancelled/expired warm
connections, failure cleanup, metadata preservation and unchanged cold paths.
Build/size/backup gates precede at most two three-dialog physical groups,
including rapid re-wake and changed final intent; retain all failures and
restore72 unless broad acceptance is actually proved.

UX84 measured:115 normal1538096B/2000B guardmargin, noaudio1062400B;
61hostchecks plus final2workerchecks pass. Quiet lifecycle sends no input or
generation, three prepared sessions, cancellation ACK203ms/cleanup406ms for
ready session,30s expiry, no idle retry. This is not worst-case TLS-open cancel.
Greeting fullinput/candidate/answer3/3; request140/173/247ms before deviceVAD,
firstPCM80/96/26ms afterfinalASR. Valid acoustic1.840719/1.772375s; thirdsource
alignmentfails and latency staysunknown. These clocks do not prove generation
before actual speech ended or a1s useful acoustic answer.
Correction complete2/3, onlyfinalgreen; thirdhits10s localcapturecap, nofinal
input/action/answer. Prior greenreadback is not a third successful action.
Allthree speculative generations revoked withzero candidateplay. Directtotal
heap samples31976B/34680B, below48KiB: fullmemorycriterion fails, independent
of cumulativeSDKmin14544B. No observedwatchdog/panic/reset/DMA/underrun inthese
six replaytrials; fiveanswers andonecapturefailure. Rawfailure retained,
no repeattochoosebetterresults. Verifiedrestore72/fast/capture/listening,
reuseON/prefetchOFF/LEDoff;578events/595212B/gen17/next3676. Allcapacities and
nonappFlash preserved. 115acceptedfalse, noinstallerpromotion. Nextbounded
diagnosis targets speculativecache overlap andlocalendpoint; do not make a
partialtranscript authoritative or count aninternal PCM/cue asusefulresponse.

### UX-85: allocate only stored speculative audio

Keep the same24KiB IMA capacity and input/revision/playback rules. Add an
optional caller-owned1024B-page store to the existing C11 draft codec. The
codec never allocates; the candidate adapter lazily allocates required pages,
with no realloc/copy peak. Preserve byte-identical IMA output across random
write/read chunk boundaries and atomic rejection when a required page is
missing. Release pages after admitted prefix drain or cancelled/failed worker
closure; retain a successfully completed, unadmitted reply until final input.
No page may outlive its owner or be read after release. Allocation failure or
full cache remains a rejected candidate, never clipped playback. Record peak
cache bytes, keeping idle allocation zero. Do not change VAD thresholds to
conceal115's timeout; inspect its saved uncommitted clip separately and label
the absence of an uploadCRC. Tests/build/ordinarysize/backup precede atmost two
three-dialog groups, including corrected intent; allfailedruns remainvisible.

UX85 measured:116normal1538464B/1632B guardmargin,noaudio1062400B,813-entry
freeze. 62C11hostchecks23.47s, final2workerchecks11.67s,4acousticcuechecks pass.
Physicalsixfirstwakes/fullinputs/tasks pass; nocapturefailure/watchdog/reset/
DMA/underrun observed. Candidatecache peaks3/5/3KiB greeting,5/10/8KiB correction,
same24KiB capacity. C3borrowedstruct16576B remainsinside17024B prefix. Candidate
heap samples89696/88272/85284 and59236/49332/56136B, notcontinuousminimumproof.
Wholeworkloadstillfails48KiB: direct41264B atllm_final_headers. SDKmin23980B
isaseparatecumulativefigure. Threecorrections revokeactualPCM, onlyfinalgreen.
Analyzerfoundendpointcuecontamination:originalgreeting0.585/.988/1.010s are
invalidasspeechlatencies. Preserveoriginalandderiveoffline-analysis-cue-review;
fixed180ms sweep rule rejectsambiguous/missingcue inindependentcandidatepath.
Correctedgreeting1.515031/1.828031/1.999719s; correction6.823875/unknown/5.986375s,
secondsourcealignmentfailed. No1susefulresponseclaim. NoVADthresholdchange or
newcausalclaimfromnonrecurrenceof115timeout. Verifiedrestore72,593events/
603192B/gen17/next3691,unchangedpartitionsandhistorybudget. Goalactive,
116unaccepted; nextresourceinspectionmusttargetfullengine/DeepSeekstage.

### UX-86: preserve parameter-free receipts across colour corrections

The speculative LIGHT request contains no colour or value, and its output is
already restricted to a fully consumed preparation sentence. Preserve that
one immutable nomination across ASR sentence IDs and colour repair markers.
Admission still requires a verified complete capture, an explicit whole final
lamp request accepted by the existing strict grammar, and matching language.
Cancellation or a positively identified different intent irreversibly revokes
the candidate. SELF/REMEMBER revision rules stay unchanged. No speculative tool
execution, parameter-bearing speech, extra generation or captured-prefix action.
Final DeepSeek planning still receives the complete corrected request and owns
the actual action and result. Keep all memory, context and Flash budgets.

Require host coverage for Mandarin/Cantonese repairs, cancellation, changed
intent/language, incomplete or conditional/multiple requests, and model output
that leaks a colour or falsely claims completion. Then normal/no-audio builds,
hash/freeze and verified full backup before app-only flashing. At most two
three-dialog physical groups: corrected lamp request and SELF regression, with
first-wake, full-input, generic receipt, only-final-green tool/readback, acoustic
cue exclusion, final spoken reply and runtime health reported separately.
The known full-workload heap failure is not resolved by this gate change.

UX86 first physical group (117): three first wakes, only1/3 complete inputs;
first two ended at an unfinished repair, third was complete but the generic
receipt still failed admission. The existing literal grammar incorrectly
treated ASR's final newline as an additional instruction.118 accepts only
trailing whitespace, retains unknown-suffix rejection, and routes an unfinished
candidate fallback through the existing tools-disabled clarification phase.
Native receipt joins emit progress_end for independent acoustic separation.
Replace the planned second SELF group with one three-dialog correction group
after these fixes; preserve the failed117 group and do not repeat beyond six
trials. VAD and the full48KiB requirement remain unchanged and unresolved.

UX86 final:118 host62/62 pass23.42s; acoustic5/5; normal1538592B/noaudio1062400B,
1504B normal-margin,816-entry frozen source. Second physical group3/3 first wakes,
complete inputs, generic receipts and only-final-green effects/readback. Original
117 group stays1/3 complete, not superseded by118. Both repeated-source acoustic
fits failed; latency remains unknown. Separate local-ASR diagnostic crops identify
three generic receipts and green-result answers, without timing/subjective claims.
118 candidate heap samples47308/50000/53988B; whole event minimum41592B at final
HTTP headers fails48KiB. No observed WDT/panic/reset/DMA/underruns in six trials.
No VAD change, no promotion. Verified app-only restore72,fast/capture/listening,
reuseON/prefetchOFF/LEDoff,617events618376B/gen17/next3715.2MiB context,204800B
history,448KiB clip remain unchanged. Audit a8a3aac6fb2aeb072e256ffa2004efb0f8aa4d6d19ed870a34f2ddc8479575ff.

### UX-87: separate TTS handshake from local final-response parsing

The119 experiment starts TTS only after the complete LLM request body is sent,
then waits at most15s for TTS startup before the HTTP owner reads/parses the
response. The remote LLM request is already in flight; model generation still
overlaps TTS setup. Text and PCM remain streamed after startup, with no extra
arena or change to history/input/answer capacities. Existing songs/plans defer
the TTS worker asynchronously, preserving the prior speaker-ownership path.
Cancellation/startup failure/timeout must join all partially created resources
before any borrowed scratch is reused. Do not weaken TLS or change SDK sources.

This is a measured scheduling hypothesis, not proof of48KiB or1s. Cover readiness,
delayed startup, failure, cancel,15s bound and pre-existing speaker ownership in
host tests; retain all engine streaming tests. After ordinary/no-audio builds
and backup, at most two three-dialog groups cover corrected lamp input and
existing memory fact, with actual effects, speech and resource outcomes separate.
Any failed acoustic alignment remains unknown; no repeated runs to select success.

UX87 final:62/62 host checks23.62s; normal1539040B (1056B margin), noaudio1062400B;
817-entry frozen source and clean pinned SDK. Six startup-before-header orders
confirmed. Header free heap60784..70152B, but candidate direct sample48456B still
fails48KiB. SDK cumulative23960B remains separate. Correction2/3 complete: the
first truncated at unfinished repair and executed zero tools; other two only
green. Memory3/3 complete/readback, candidate2/3; redundant summary writes remain.
All6 first wakes, no observed reset/WDT/panic/DMA/underrun/USB loss. Network stack
free1356B, TTS3244B. Correction acoustic alignment rejected, latency unknown.
Memory preparation energy candidates2.032/1.788s, final9.712/8.813/10.628s; no
phoneme/human or1s acceptance.119 unaccepted; verified app-only restore72,
fast/capture/listening, reuseON/prefetchOFF/LEDoff.637events642724B/gen17/next3735
preserved;2MiB context/204800B history/448KiB clip unchanged. Audit SHA256
42c77e286a6517b6f44b6a3426f567422015d3a3cda182330725c0583390c94a.

### UX-88: diagnose current partial self-correction from source frames

The119 group retained one truncated repair and therefore does not establish
capture reliability.120 is a labelled endpoint diagnostic of unchanged119
production logic. Enable the existing post-capture150ms metadata export only;
do not change VAD thresholds, prefetch routing, audio preprocessing or budgets.
Run one three-dialog correction group, retaining every attempt, per-turn status,
all frame metadata and the final board clip. Metadata must pass CRC and exact
production-C state replay before any classification inference. The read-only
queries/trace disqualify speed and rapid-rearm acceptance. Do not infer missing
earlier PCM or repeat until a failure/success appears. If a cause emerges, any
fix needs counterexample/negative evidence as well as the failing input. Normal
cap1540096B, explicitly labelled diagnostic cap1541120B remain fixed. Verify
full backup/app-only readback and restore72 after this bounded investigation.

UX88 final: one3-turn diagnostic group completed all inputs/only-green effects,
first wakes and generic candidates. No early-cut reproduction, so119 failure
remains. Endpoint source times6520/6600/8780ms; all3 CRCs and exact C state replay
match. Last140928-sample clip replays440 frame energies exactly. Fixed6.4-8.4s
interval:83 spectral/34 strong frames, ASR raw'I.'/filtered'.'; supports noisy
continuation but is not a proof of no quiet speech or a causal radio diagnosis.
No thresholds changed. No speed/rapid-rearm acceptance. Candidate sample47788B
still below48KiB; SDK cumulative23960B separately retained. Diagnostic1540288B
is within1541120B, explicitly labelled, archived818 files; normal1539040B image
and runtime source exactly restored to119 after removing the temporary version
branch. First unlabelled flash command failed before device access; existing
diagnostic flag subsequently passed unchanged limits. Full4MiB backups and
non-app parity verified twice. Device restored72 fast/capture/listening,
reuseON/prefetchOFF/LEDoff,646events648048B/gen17/next3744, unchanged capacities.
Audit6fdf79129f83f6b9ea6e96e7176b56ac86f3fdc656773de17a895bf14cd8df41.

### UX-89: retain incomplete-input PCM before the next capture

Add an opt-in host diagnostic that exports the actual failed turn's committed
clip before another wake can overwrite it. It must verify capture identity,
pause wake only after an idle terminal turn, leave voice/provider configuration
unchanged, bound export to30s, restore USB timeout and re-enable wake only on a
verified export. Errors keep evidence protected and abort the diagnostic group.
No prior/stale clip may be attributed to a new failed capture. Normal continuous
tests remain unchanged and the diagnostic never counts as speed/rearm acceptance.
Use the already frozen120 firmware, one3-dialog correction group at input gain
0.25 (2.92dB below0.35); wake gain unchanged. This is a specified quiet-input
condition, not a claimed exact reproduction of119's0.35 failure. No retry until
failure. Retain all results; any firmware decision change needs input evidence,
negative regressions and a separate ordinary3-dialog verification. Keep budgets
and backup/app-only restore unchanged.

UX89 final: one3-turn gain0.25 diagnostic group, first wakes3/3 but complete
input/final-green task0/3. Retained all three failed clips before overwrite,
identity/config verified across wake-only pause/resume. Endpoint source times
3240/2880/3960ms;52352/46464/63872 samples,163/145/199 frame energy and state
parity exact. External ASR contains the complete correction; device PCM does
not. Round2 really set blue from truncated input; rounds1/3 had no setters.
Two offline continuation prototypes replayed12 existing traces each. Both
preserve weak prefixes but extend known noisy tails, rejected without firmware
changes or physical retry. No claim of complete future capture from a prefix.
Added exclusive C replay file output with exact row/clock validation after an
observed28672B stdout truncation.6 retention+2 output host tests pass. SDK
cumulative min23972B still fails48KiB; no observed reset/DMA loss/underrun.
Existing119 normal application remains byte-identical1539040B. Diagnostic120
was restored to72 with full4MiB backup/app-only/non-app parity verification.
Restored fast/capture/listening,reuseON,prefetchOFF,LEDoff. Context unchanged
across restore:653events651484B/gen17/next3751; original capacities preserved.
Authoritative audit02 SHA7057b0ced82036a9a347d8c9b1dbaf63f3334836d0968f4c632ec0822f764fe1.
The first audit included its growing stdout log; retained and superseded by02.
No speed/rapid-rearm acceptance, no firmware promotion, overall goal active.

### UX-90: retain the missing continuation under concurrent ASR

UX89 changed the next action: metadata-only weak-support changes cannot separate
the observed short syllables from noise. Build one labelled diagnostic121 from
119: for fast captures only, keep original endpoint state/metadata but retain
raw acquisition through8000ms after an accepted local endpoint. Preserve cancel,
errors, no-speech rejection and11.5s wall timeout; bypass producer/server early
stop only within this diagnostic. No new audio buffer, partition or threshold.
Require endpoint trace and mark the fixed capture floor in its header. Original
endpoint is observational during the additional capture, never presented as the
actual capture duration. One3-dialog gain0.25 correction group, same ASR/prefetch
configuration as UX89, retaining every current clip before overwrite. All outcomes
are diagnostic, not latency/rapid-rearm/production acceptance. Verify full PCM,
source-frame parity, final input and resource health; do not retry until success.
Restore72 and exact prior production sources after freezing121 and collecting
evidence. Maintain fixed app budgets and4MiB backup/app-only verification.

UX90 final: one3-dialog group, first-wake/full-input/only-green/candidate-hit3/3.
Proposed local endpoints3420/6580/3240ms, retained8024/8040/8024ms,401/402/401
frames with exact C state/PCM energy parity. First/third later dense evidence
4000/3940ms; independent ASR segments retain errors as well as recovered repair.
Two cloud finals arrived after EOF; cloud-only endpoint is not established.
The fixed8s capture is diagnostic only, not an accepted VAD or speed change.
ASan/UBSan capture bounds plus6 retention tests passed.1211540384B within the
labelled1541120B cap,821-file source frozen. Normal source and1539040B119 app
restored exactly. Candidate sampled min48736/50832/48172B, SDK min23952B still
fail48KiB. No observed reset/DMA loss/underrun.4MiB backups143055/143807 and
non-app parity verified. Restored72 fast/capture/listening,reuseON,prefetchOFF,
LEDoff;665events659132B/gen17/next3763 unchanged across restore. Audit SHA
da07d2bbf5773aa7dec4fc90d80eac9c5e3f9e931901759d114a8392827b1a14.
No latency/rapid-rearm/human-generalization acceptance; overall goal active.

### UX91: bounded unfinished-intent continuation (2026-09-27)

Replay one fixed hypothesis before firmware changes: recognized incomplete lamp
arguments or explicit unfinished repairs may defer one tentative local endpoint
by at most1000ms per capture. No threshold change, automatic budget refill,
terminal revival, action authorization, or extension of the10s/11.5s limits.
Unknown text is not a declaration of completeness. Use UX90 full metadata and
actual partial timing, plus UX89 retained prefixes and UX88/86/62 counterexamples.
Require complete correction content and bounded noise before integration; cloud
stop and producer bounds must share the proof. If replay fails, preserve and
reject the hypothesis without another threshold sweep or device trial. Otherwise
validate host invariants and normal app budget before one3-dialog physical group.

UX91 offline gate: the fixed hypothesis retains complete final-green content in
UX90 full PCM at6680/7420/6480ms (independent ASR errors retained). All3 ordinary
blue inputs keep their original endpoints. Some older repair tails defer while
their latest partial is incomplete; each is bounded by the same one-shot1000ms
source deadline, not accepted as noise-free or latency improvement. Implemented
as a shared atomic hint/deadline for local and eligible cloud proposals; classic
has no hold. Fast producer acquisition bound gains at most1000ms of conservative
slack so it cannot starve the held consumer;10s/no-speech limits remain intact.
122 normal app1539552B,63 host tests pass, no new dynamic allocation. Require
one3-dialog weak correction group, retain current PCM, then restore72. No second
group/threshold change on failure; inspect evidence instead.

UX91 final: reject122. One normal-firmware3-dialog group yielded3/3 first wakes,
2/3 complete final-green tasks. First input ended4920ms and retained4984ms,
omitting final green; no wrong-blue effect, lights stayed off and reply clarified.
Other endpoints5920/6520ms, retained5948/6564ms, bothgreen. Independent PCM ASR
agrees. Candidate hit1/3; no speed/rapid-rearm/generalization acceptance. Exact
initial hold deadline was not logged in this normal app, so its timing is not
claimed measured. SDK min23952B fails48KiB, network stack1340B, no observed reset,
DMA loss or reported underrun. Frozen122 has822source files and1539552B app.
15 production files restored exactly; normal1539040B119 app restored bytewise.
Both4MiB backups150914/151603 and non-app parity passed. Device72 restored
fast/capture/listening,reuseON,prefetchOFF,LEDoff; persisted677events666776B/gen17/
next3775 plus2MiB context,204800B history and448KiB clip unchanged across restore.
Audit168645c24fcbcf6bb287776b400c6d699383efde01c24bde08a4baa31d097539.
No extra physical group or threshold sweep; overall goal remains active.

### UX92: replay saved PCM through the exact C3 vendor classifier

Do not infer UX91's missing spectral metadata with a substitute host VAD. Build
a separate USB-only diagnostic app using the same pinned vendor archive and
production C filters, without microphone/speaker/network or writable storage.
Use indexed320-sample frames with per-frame and whole-stream CRC, a500-frame
limit, explicit fresh VAD state, abort and idle timeout. No production behavior
or resource budget changes. Keep current partitions and guarded app-only updates.

First prove all1204 metadata frames from the three full UX90 captures match,
including spectral decisions. On mismatch stop attribution. If exact, reconstruct
the three retained UX91 inputs, replay frozen122 plus original partial timestamps,
and locate the first hold deadline and subsequent ending proposal. Reconstructed
timing remains counterfactual unless it matches actual endpoint state and ordering.
Do not claim unknown audio after the truncated clip. No cloud requests or new
acoustic input group is required. Restore72 and verify data after the diagnostic.

UX92 final: USB-only123 app131760B, frozen828-file source.4 host checks and12
device protocol checks passed. All1204 UX90 frames exactly match; fresh vendor
replay reconstructs249/297/328 UX91 frames with explicit partial tails discarded.
Frozen122 terminal state/end/speech/quiet/resume/transcribed values match all3
observations, using original ASR timestamps without fitted offset. Reconstructed
hold deadlines4720/4040/4300ms and held240/240/620ms explain the first spent
window; these are reconstruction, not newly discovered original telemetry.
Diagnostic per-frame maximum2094us and333300B post-clip heap are isolated-probe
measurements, not production concurrent resource acceptance.

One offline alternative charges only actually deferred20ms source frames, with
the same total50-frame budget and unchanged grammar/thresholds.18 retained
traces and5 invariant fixtures complete under ASan/UBSan. Ordinary blue3/3
keep5400/4720/4820ms. Full UX90 ends7100/7420/6780ms, adding420/0/300ms versus
the rejected fixed window. UX91 first/second stay active at retainedEOF4980/
5940ms; missing audio is unknown, not successful complete input. Third ends
6520ms unchanged. No production integration or new physical/audio/cloud group.
Before adoption, require shared cloud/local accounting with unique source-frame
charging, immutable terminal decisions, producer bound proof and latency review.

Two4MiB backups153606/154101 pass app and non-app readback; the diagnostic made
no Flash changes, and the full restored image equals its starting image. Device72
fast/capture/listening,reuseON,prefetchOFF,LEDoff verified;677events666776B/gen17/
next3775,2MiB context,204800B history and448KiB clip preserved. Normal119 source
and1539040B application unchanged. Relative output-path failure recovered from
exact401-frame serial/CRC evidence without repeating it; initial build and two
offline harness failures retained. Audit8e03434848be6ea816f545af190849895c5f4a75ccaf0220a0923148e3317d8e.
One-second/48KiB/generalization acceptance still open; overall goal active.

### UX93: one source-frame owner for cumulative continuation

Previous goal turn was progress: exact-device reconstruction explains wasted
fixed-window time. Implement the one UX92 accounting hypothesis without changing
classification or phrase coverage. Only the fast endpoint owner spends up to50
deferred20ms frames. Network hints and eligible cloud-end proposals are atomic
inputs; neither caller decides a fast capture's terminal state or spends quota.
Revisions revoke cloud proposals. Classic behavior stays unchanged. Repeated
partial text cannot refill quota; cancel, no-speech and hard limits remain.
Provide conservative producer slack, expose used hold time, prove equivalence
on the18 saved traces and exercise local/cloud order, retraction and bounds.
No new PCM allocation or context shrink. Require host/normal/no-audio build
gates and1540096B before one3-dialog normal-device group; retain each input and
restore72 on failed capture/runtime gates. No repeated group or threshold sweep.

UX93 final: reject124. Single source-frame owner and revocable cloud source
proposals implemented; actual deferral consumes the50-frame budget once per
frame.63 ASan/UBSan checks,18 traces/5790-frame parity with fixed UX92 prototype,
normal1539936B and audio-disabled build passed.830-file source frozen. One normal
device group: first wakes3/3, main-path full input/green task2/3, candidate hits2/3.
First ended2920ms/retained2952ms, held0, final onlyblue and actual wrongblue.
Others ended6900/7360ms, retained6920/7384ms, held1000 each and actualgreen.
Independent ASR agrees with the first truncation/secondgreen but calls the third
colour black; retain disagreement, not subjective or unanimous confirmation.

First live ASR activity admitted at1720 source ms, confirmation1740/1769 wall;
Thank partial1764 wall then empty settled text2364, Thanks2642/2660. First请把
hint3536 arrives after actual stop. Admission remains latched after withdrawal,
but missing spectral trace prevents claiming it as sole cause. Next inspect
saved PCM/admission ordering before further delay or lexical changes. No extra
physical group. SDK min23932B fails48KiB; network stack1352B, no observed reset,
DMA loss or reported underrun. No speed/rapid-rearm/generalization acceptance.

Both4MiB backups161254/161922 (UTC) pass app/non-app checks. Device72 restored
fast/capture/listening,reuseON,prefetchOFF,LEDoff;690events675124B/gen17/next3788
and2MiB context/204800B history/448KiB clip unchanged across restore.15 source
files restored exactly;1539040B119 build hash9a56676b... restored. Audit
6724d830b30846400beb96fa9f586a47066959904cc2c6b798eedb8554cd9519.
Overall goal active, release package unchanged.

### UX94: diagnose withdrawn ASR admission from retained PCM

Previous turn was progress: normal124 demonstrated no continuation grant before
the earliest lost correction, with ASR-only admission and subsequently withdrawn
text. Reuse immutable123 USB-only vendor replay, first check one401-frame known
reference then reconstruct all3 UX93 retained clips. Verify the same app/filter/
vendor hashes. No new acoustic group, cloud call, threshold or production change.
Compare frozen124 with observed ASR admission against strict-local-only admission
and inspect independent seven-vote onset and withdrawal timing. Distinguish
source-clock facts, receipt-time reconstruction and unknown audio beyond EOF.
Use a bounded offline counterfactual only after exact observed terminal parity.
Guarded app-only updates and restoration of72/current persisted data required.

UX94 final: reference401 frames exact; all3 retained inputs rebuilt through the
same C3/vendor path. Frozen124 observed terminal parity3/3. Strict-local-only
admission staysWAIT through first retained2940ms, rejects the other two at4000ms;
maximum strict votes6/6/7, first independent onset for third4060ms only. This
proves ASR dependence, not that weak human speech was silence. One revoke-only
offline hypothesis passes5 invariants but first still ends2920ms: withdrawn
admission is reactivated by later partial at2660ms. Reject it; no further trials.

Official ASR schema distinguishes confirmed text from revisable stash. Current
merge loses that provenance; historical merged previews cannot reveal which
field supplied the early English draft. Next retain provenance and test prefix
promotion, withdrawal, segment ordering and isolation before deciding admission
policy. Speculative preparation is not authority to end input, play or act.
No new cloud calls/acquisition or production change. Two4MiB backups162912 and
163233 verify full-image restoration. Device72 ready;690events675124B/gen17/
next3788,2MiB context/204800B history/448KiB clip unchanged. Normal119 unchanged.
Audit dc140988de7d98eaec64945b2078868c2fdd5e82a815b60cdc5fe1331cd903b0.
One-second,48KiB and full dialogue gates remain open; overall goal active.

### UX95: preserve confirmed-prefix provenance for speculative input

Previous turn was progress: exact replay disproved revoke-only admission and
identified information lost when text and stash are merged. Add a byte boundary
to accepted isolated-ASR updates, without a second transcript or allocation.
Expose a synchronous borrowed asr_prefix event immediately before partial and
segment notifications. It contains only the contiguous confirmed prefix; a
missing earlier segment must not turn a later final into a leading prefix.
Promotion of a draft into confirmed text must notify even if preview is equal.
Keep candidate preparation on the complete preview and current admission policy
unchanged until provenance evidence establishes a justified change. No new
threshold, endpoint timer, GPIO authority or output release condition.

Test draft-only, unchanged-preview promotion, withdrawal, UTF-8 boundaries,
source-ordered/missing finals, new segment, failure and new-session isolation.
Replay existing provider receipts through the actual C parser and compare
prefixes to an independent interpretation. Audit source audio identities and
keep old failed transcripts. Only after these checks and normal/no-audio builds
within1540096B consider a new device observation. No repeated acoustic group or
new cloud session is required for this protocol stage. Preserve72 on the device.

UX95 final:125 adds confirmed_bytes and borrowed asr_prefix before complete
preview notifications. No second transcript/allocation; missing earlier finals
bound the contiguous prefix, unchanged-preview promotion still notifies. Current
admission/output/effect policy unchanged.63 ASan/UBSan checks and normal/no-audio
builds pass. Normal1539216B (+176B versus119,880B below1540096); no-audio1062400B.
Nine retained provider sessions/356 notifications match independent interpretation
with raw receipt and PCM-chunk identity checks, no new cloud or acoustic trials.

Meaningful confirmed text trails first meaningful draft1390..5750ms;7/9 first
appear only as segment final,4/9 arrive after4seconds. Thus prefix-only admission
is not adopted. Next examine whether revoked ASR admission is reusing acoustic
evidence preceding withdrawal; do not assume waiting for finals fixes low latency.
Source/build125 kept, device remains72 confirmed by read-only USB:fast/capture/
listening,reuseON,prefetchOFF,Wi-Fi,690events675124B. Existing budgets/data retained.
No candidate promotion or overall completion; one-second/48KiB/full dialogue open.

### UX96: fresh local evidence after explicit ASR withdrawal

Previous turn was progress. Test one bounded acoustic-evidence hypothesis before
production integration: an explicit empty accepted preview/final can revoke only
ASR-assisted onset before terminal state; a subsequent draft must have120ms of
new local-positive frames after that withdrawal. Preserve clocks, vote history,
independent strict onset, hold spending, producer limits and saved PCM. A short
nonempty partial such as one Chinese character is not an explicit withdrawal.

Compare explicit-empty revoke alone against the same rule with fresh evidence,
holding original first ASR admission at its measured source time. This differs
from UX94's broader not-meaningful revocation and must not hide that distinction.
Use original partial/final events including empty strings, reconstructing their
receipt times without fitted offsets. Check retained full inputs, successes and
truncated prefixes plus fixed synthetic invariants; unknown audio after saved
EOF remains unknown. No automatic threshold sweep or repeated acoustic group.
Only integrate if the counterexample improves without losing saved successes.

UX96 final: first observed baseline and explicit-empty-only control still end
2920ms; requiring120ms fresh positive frames instead staysWAIT at saved2940ms
EOF. Withdrawal floor460ms, later total520ms means only60ms new evidence.
Second/third successful inputs remain6900/7360ms. All3 original paths match
previous exact-device reconstruction.18 more traces/5790 frames unchanged; they
contain no empty updates and establish only ordinary-path compatibility. Total
21 traces/6652 frames and8 fixed synthetic invariants underASan/UBSan. First
fixture expected820ms omitted existing1000ms pending hold; corrected expectation
only to1820ms, original failure retained. No invented post-EOF audio or live pass.

Current atomic boolean can coalesce withdrawal followed by new draft. Integration
must publish a withdrawal generation with current flags, preserve sole source
ownership and update current confirmation; test cancellation, late withdrawal,
cloud order, producer bounds and new-turn isolation. Keep125 prefix telemetry
when merging124 accounting. No code/firmware change or new cloud/acoustic group
inUX96. Device72 fast/capture/listening,reuseON,prefetchOFF;690events675124B and
all storage budgets unchanged. Audit8fa5cb92cd7de4076406003b8bb74252ac29db6a762f7524f7cc534c9bd9c222.
Next integrate this fixed hypothesis, then one3-turn group if resource/build
gates pass. Overall goal remains active; one-second and full acceptance open.

### UX97: live source owner consumes persistent withdrawal notices

Merge the reviewed124 cumulative endpoint plumbing into125 without losing ASR
prefix provenance. Atomically publish the current text flags, a persistent
empty-update generation and revocable cloud proposal. Only the source-frame
worker changes admission, fresh-evidence floor, continuation spending or end.
An empty result followed by a new draft between frames must remain observable.
After ASR-only admission, require120ms fresh local positives after withdrawal;
strict independent onset, clocks, spent quota and hard limits remain unchanged.

Check coalescing, late withdrawal, terminal/new-turn isolation, independent
onset, cloud ordering and producer bound; compare actual production logic with
the21 saved traces. Require normal/no-audio builds and1540096B application cap
without reducing context/history/clip. If these pass, freeze126 and run one
three-turn retained-input device group. Preserve failures and restore72 if the
candidate fails. No threshold sweep or repeated cloud/acoustic group.

UX97 final:126 implements coherent notices/current confirmation and preserves
125 provenance.65 sanitizer tests pass;21 saved traces6652frames plus8 synthetic
fixtures match the fixed UX96 rule frame-by-frame. Normal1540096B exactly at cap,
no-audio1062400B. One3-turn weak correction group:complete input and green-only
effects3/3,independent retained-PCM ASR agrees,first wakes3/3,zero observed reset/
DMA loss. All3 had independent strict onset and no ASR-only withdrawal, so this
is ordinary-path compatibility rather than3 live proofs of the withdrawal fix.
Prepared PCM arrived3076/4302/4065ms before ASR final; no acoustic1second claim.
Minimum heap23912B fails49152B; no candidate adoption. Source/build126 retained,
device72 restored fast/capture/listening,reuseON,prefetchOFF,Wi-Fi,LEDoff.700events/
681268B and all storage budgets preserved. Next address overlap peak memory;
no further threshold/acoustic sweep authorized by this phase. Goal remains active.

### UX98: whole-turn allocation evidence before memory changes

Extend the existing bounded allocation hook from legacy capture to every voice
job, including isolated ASR/candidate, workspace restoration, final response and
cleanup. Keep16 rows/512B; record current total free, requesting task, allocation
size and timestamp at descending minima. Explicit skipped/overwritten counters;
no allocation, blocking lock, largest-block walk or output inside the hook.
Idle prewarm and skipped hooks remain outside the evidence. SDK sum of per-region
historical minima stays separately reported; do not reinterpret it as a
simultaneous total or declare48KiB passed by changing the metric.

Diagnostic127 uses the existing labelled1541120B limit only. Normal126 remains
1540096B and unchanged functional behavior. One fixed three-turn correction
group atgain0.25,prefetchON,reuseOFF,retained inputs. No threshold sweep, repeat
cloud group or capacity reduction. Locate a concrete peak owner before changing
memory lifetimes; restore72 after the diagnostic. Goal remains active.

UX98 final:127 diagnostic1541024B, normal126 rebuilt1540096B, no-audio1062400B;
65 sanitizer tests pass. Single three-turn group all complete/green-only and
first wake; observed current-free minima38164/38088/37720B all between TTS connect
and connected. SDK historical per-region sums24896/22868/22868B kept separately.
16-row hook recorded138/120/122 downward minima; skipped47/39/38, overwrote122/
104/106. Candidate polling50040/45268/48560B shows its own remaining pressure.
No48KiB/one-second/rapid-rewake/generalization acceptance. No repeated cloud group.

CurrentELF frame evidence:engine_turn4784B includes4608B tool-local arrays,
network2608B,voice_run928B,speak_task416B; these are not whole-call-graph stack
requirements. Next limit temporary tool array lifetime before measuring safe
stack changes; no task stack or TLS policy changed yet. Device72 restored and
verified fast/capture/listening,reuseON,prefetchOFF,Wi-Fi,LEDoff;712events688944B,
generation17,next_record3810.2MiB context/204800B history/448KiB clip unchanged.
Normal source/build126 retained with diagnostic-only hook scope extension.
Overall goal remains active; this phase is progress, not completion.

### UX99: shorten tool-result stack lifetime before reducing task reservation

UX98 was progress: whole-turn hooks identified actual37720..38164B lows during
TTS connection and compiler evidence identified4608B tool-only arrays retained
throughout engine_turn. Extract that scope without changing validation, tool
effects, call IDs, cancellation, final-batch fallback, WAL ordering or capacities.
Prevent LTO from reinlining the large arrays; verify actual C3 prologues, not just
source braces. Preserve original15360B network and8192B TTS stacks initially.

Run existing65 sanitizer tests, normal1540096B/noaudio gates; only then one labelled
diagnostic with3 consecutive fixed correction dialogues, no per-success capture
export gap. Save failed capture if needed, count that diagnostic gap explicitly.
Measure network/TTS watermarks before selecting any reduced stack reservation;
retain at least the prior observed safety margin and do not equate a three-turn
sample to exhaustive stack coverage. A subsequent distinct reduced-stack build
gets one3-turn regression if evidence and size gates support it. No threshold
sweep, context/buffer reduction or weakerTLS. Report remaining48KiB/one-second
failures; preserve rollback and current app-only flash/backup guards.

UX99 pre-device refinement:128 extracts the batch but still needs4736B while
executing tools. Source ownership review shows assistant() has already copied
planning text into messages before execution; nextcomplete() resets the reply.
129 therefore borrows the idle8193B reply text for disjoint2048B tool output and
2560B event JSON. No alias with call IDs/arguments, context scratch or messages;
all results are emitted/persisted/copied before reuse for the next tool. Preserve
capacity checks and refuse overflow. No additional allocation or smaller budget.
Add four large distinct results in one batch, next-request/WAL original-ID checks,
preserved planning text, final answer reset, and post-effect consumer failure.
128 remains a frozen compile-only intermediate, not flashed. First device group
uses129 with both original stack reservations and allocation diagnostics, then
make a separately measured decision about task reservations.

UX99 unreduced129 group completed with allthree firstwakes and finalTTS paths,
but input completeness1/3:3340ms stop after不对;3300ms stop after蓝色 caused blue;
third6840ms fullinput andgreen. Both failed clips retained; export gaps~7.9s,
so no rapid-rewake claim. Noextra cloudattempt to hide failure. Allthree network
stack minimum5804B; TTS3320/3320/3324B. Actualheap38276/37912/37756B. This proves stack scope benefit, not globalheap improvement,
because reservations were deliberately unchanged.

130 reduces network15360→11264 andTTS8192→7168:5120B less reservation, predicted
sample margins1708/2296B. Preserve at least1536/2048B in this test. Changes to
ASR/VAD/thresholds/buffers prohibited in this comparison; functional failures
remain failures independently of resource improvement. Next one3-turn reduced
group, retain failedinputs, measure true allocation lows and both task margins;
restore72 if any release gate remains unproven. No assumption of48KiB success.

UX99 completed: source130 retains idle reply-text scratch reuse (2048B result +
2560B event JSON) and measured task reservations11264B network/7168B TTS.
Reduced group three complete corrected inputs, green-only actions, first wakes;
network minimum1692B, TTS2208B. Current-free allocation minima43136/43248/42976B,
not48KiB. Candidate sample minimum49156B leaves only4B above that threshold.
Original129 failures two ofthree remain in report, including mistaken blue;
no causal VAD improvement claimed. Status-query/export gaps invalidate rapid
rewake and acoustic latency acceptance; internal VAD-to-preparation playback
499/514/497ms is separate from end-of-human-speech latency. No repeat sweep.
65 sanitizer checks plus the changed stack lifecycle/cancel contract pass;
normal1540016B/noaudio1062352B/diagnostic1540944B. Only source-level change to
system wording merges duplicate hardware-status instructions to fit Flash.
Three full-backup/app-only readback/non-app parity operations pass. Restored72
fast/capture/listening,reuseON,prefetchOFF,Wi-Fi,lightoff;733events701764B,
generation17,next_record3831.2MiB context/204800B history/448KiB clip preserved.
Frozen130accepted=false; formalinstaller unchanged. Overall goal active.

### UX100: separate network branch lifetimes before a measured stack change

UX99 was verified progress, not full completion. Current130normal1540016B,
device72 restored. Inspect live worktree first. The network entry frame2608B
contains command-only buffers and inlined ASR/session branches. Split nonvoice
command job, live-ASR and idle-warm calls with noinline boundaries, preserving
behavior, error/lock/order/cancellation/streaming. Do not shrink any payload,
history, capture or audio capacity. Preserve11264B network and7168B TTS stacks
for the first measurement. Check actual LTO prologues, host checks, normal and
noaudio builds before one labelled three-turn diagnostic with stack/heap data.
Retain any failed capture and actual gaps. A further distinct stack reduction
requires measured margin >=1536B network and its own three-turn check. No blind
threshold sweep, cloud repetition or success claim from compiler frames alone.
All flash operations fullbackup/app-only/readback/nonapp parity, ordinary flash
cap1540096B and labelled diagnostic cap1541120B remain. Restore72 after bounded
diagnostic if goals remain unverified. Current overall goal remains active.

UX100 pre-device refinement: isolate context_command and upload_song_job
individually, instead of a wrapper around ordinary text chats. The latter
would retain a2208B frame during nonvoice TLS and undermine a voice-only
watermark reduction. Both intermediate builds remain compile-only. Keep
original11264B task stack until voice and USB command paths are measured.

UX100 first131 group complete: all3 full correction and green-only effects;
network spare3900B includes preceding USB search. TTS2160/2212/2260B;
allocation minima43288/43160/48096B, below48KiB. USB10 command checks passed,
but harness wrongly aborted an already committed upload, so its report stays
failed. Earlier limit4 search failed as expected for actual max3; harness fixed.
Volume restored and context unchanged by USB checks. Device stopped cleanly:
743events707884B,gen17,next3841; no voice reboot/DMA loss. Diagnostic queries
made1063/1047ms gaps, not250ms/acoustic acceptance. Reduce network11264->9216
in distinct132, predicted1852B spare >=1536B; keepTTS7168 and run corrected USB
checks plus exactly one distinct three-turn diagnostic before conclusions.

UX100 outcome: normal1321540096B, diagnostic1541056B, noaudio1062352B.
Network task11264->9216B saves2048B; measured spare3900->1852B, TTS unchanged
7168B with remaining2296/2240/2208B. All10 corrected USB checks pass and leave
context unchanged. Unreduced131 full correction3/3; reduced132 only1/3. Other
two captures end at4040/3820ms, ask clarification and issue0tools; retained
device PCM4116/3848ms independently lacks finalgreen phrase. Third turn has1
playback underrun. All6 firstwakes, noDMA/WDT/panic/reset observed. Minima132
45660/45264/44912B, stillbelow48KiB. No acceptance from diagnostic gaps or
internalVAD timestamps. PreparedPCM ahead of finalASR3.2-4.0s in131,3.263s in
132complete turn; true acoustic<=1s unresolved. Original failures preserved.
65core/protocol sanitizer tests do not directly cover full runtime; board
commands and voice exercise changed orchestration. Normal/noaudio buildpass;
noaudio deliberately uses fallbackversion0.6.3-context, not voice132 label.
Frozen132accepted=false. Source/build132 retained; fullbackup/app-only readback
and allnonapp parity verified for3flashes; device72fast/capture/listening,
reuseON,prefetchOFF,WiFi,lightoff,COMclosed.751events712268B,gen17,next3849;
2MiBcontext/204800Bhistory/448KiBclip unchanged. Overallgoal remainsactive.

### UX101: diagnose retained weak-input termination before changing thresholds

UX100 is verified progress: task reservation saves2KiB, but132 weak correction
only1/3 complete. Keep goals intact. Inspect current132 sources and preserved
failed PCM first. Failure2 used full1000ms pending quota with1740ms quiet;
failure3 ASR-only admission3800ms immediately ended3820ms using1620ms old quiet.
Neither failure authorized tools. Replay one known reference plus these two
failed clips and one complete131 clip through the already verified C3 vendor
probe, requiring reference byte parity and all CRCs. No new microphone capture
or cloud requests for this diagnosis. Compare actual strong/weak spectral and
filtered-energy evidence, reconstruct current termination without fitted event
offsets, preserve asynchronous limitations. Only a single evidence-backed rule
may proceed to finite offline checks; no blind threshold/pending-budget sweep.
Keep132 normal image and all capacities. Every flash fullbackup/app-only/
readback/nonapp parity; restore72 listening after diagnostic. No acceptance
from a retained prefix that cannot prove later speech. Overallgoal active.

UX101 outcome: sameC3 probe1139frames; reference401 byte-identical and allCRC
valid. Three UX100 endpoints reconstruct8/8fields exactly with initialASR
admission pinned to observed source time and otherevents unadjusted. Reduced
r2 expiredquota at4040ms despite strongrun starting4020ms; reducedr3 admitsASR
3800ms andends3820ms usingold1620ms quiet. Only firstissue corrected in133:
afterpendingquota exhausted, contiguous strongbits1/3/7 may bridge at most
threeframes to unchanged strict4vote continuation. Gap/weak/revokedpending
ends; noquota refill, no newstate/memory, hardbounds unchanged. r3 unresolved.
32fixedcases9640frames (21old recordings+8fixtures+3newretained);30 unchanged,
failedr2 continues toEOF4100, previouslycompleter3 continues toEOF6820.
Missingtails areunknown, not completecapture/noextradelay proof. Integrated
factoredexpression matchesprototypeeveryframe.65ASanUBSan pass, then5targeted
checks afterfactoring; boundednoise/cancel/terminal/hardlimit/producercoverage.
Normalbuild01 over64B,02over16B; shorten3same-meaningUSBhelp phrases only,
03fits1540096B. Noaudio02 1062352B. NoLLMpromptorcapacitychange.133frozen
accepted=false, not installed fornewvoiceacceptance. AppSHA256
b1b04c031bf0fd5eaaa848bc807ef52ca750ba66cbde68809182019f0a7bb55f.
Twofullbackup/app-only operations probe123/restore72 verified; probeentireFlash
unchanged, restored4MiBidenticaltopreprobe. Device72fast/capture/listening,
WiFi,reuseON,prefetchOFF,lightoff,COMclosed.751events712268B,gen17,next3849;
2MiBcontext/204800Bhistory/448KiBclip unchanged. No newrecording/cloudcalls.
Goalremainsactive: lateASRadmission, weakcapture reliability,48KiBmemory and
stable1sactualresponse remainunverified. SPEC/README/VOICE_LATE_RESUME_REPORT
and immutableevidence preservefailures andtradeoffs; no newmultiday sweep.

### UX102: bounded observation after late ASR-only admission

UX101 leaves a reproduced late-admission failure: first accepted ASR at3800ms,
old silence1620ms, capture ends3820ms. Test one fixed rule: fast ASR-only
admission may spend the SAME existing1000ms continuation quota until end_ms
has elapsed since admission. Keep quiet clock and thresholds unchanged;
repeat text never re-arms; withdrawal still requires120ms fresh evidence.
Independent strict onset or a guarded cloud proposal bypass this additional
wait. Four-second no-speech and ten-second hard caps remain. Prove producer
coverage with the new admission boundary before integration; extend only
its conservative acquisition bound if necessary. No new buffers or fields.
Compare frozen133 across the same32 retained cases, explicitly report added
waiting, censored tails and scheduling limitations. Host sanitizer checks,
normal/noaudio builds within original caps before at most one3-turn physical
weak-correction group. Every flash fullbackup/app-only/readback/nonapp parity;
restore72 afterward unless a complete new acceptance supports promotion.
Keep2MiBcontext,204800Bhistory,448KiBclip and old frozenrollback. Do not claim
stable1s actual reply,48KiB floor or complete intent from retained prefixes.

UX102 outcome:134 adds bounded observation after late ASR-only admission,
spending the existing1000ms shared quota; strict onset/guarded cloud end bypass,
no silence reset/quota refill/new fields. Fast producer energy-stop floor5060ms
with>=6possible frames covers latest admission+quota+strong-run grace; silence
and classic remain4s.776admission/window and1940producer boundary combinations
plus existing invariants pass;65ASan/UBSan tests23.63s.32fixedcases9640frames,
30unchanged; synthetic terminal+40ms, oldUX100r3 now open at EOF3840, missing
continuation remainsunknown. First admission pinned; no fitted clocks.
USB help9calls concatenated, literal bytes unchanged1188/659; board9lines match.
Normal1540096B,noaudio1062288B; all originalcaps/capacities retained.134normal
SHA c3f521f8b9c8b24eb2eccef3593c7ba84dec18b28af3a5ba1528ad28a72c658c,
frozenaccepted=false. One3-turn physical group gain.25,prefetchON,reuseOFF,idle:
first fullgreen, second truncatedBLUEeffect, third truncatedclarification with
ZEROnewsetting. All strict local onset/transcribed0; newASR-only branch NOT
exercised on board. Firstwake3/3,DMA0,underrun0,no detectedpanic/WDT/reboot;
query/exportgaps1047/8797ms invalidate rapid-rewake acceptance. Firstcandidate
PCM ready3927ms beforefinalASR, completecache1332msbefore; VAD event to internal
speaker361ms, ASR to speaker172ms. Not acoustic human-end1s. Finalspeech after
VAD7249/5130/4246ms. Othercandidatescancelled; summary02 distinguishes worker
completion from successfulcache, supersedes misleading fieldname in01.
RetainedfailedWAV3800/3832ms, localASR doesnot recovergreen; possible negation
ASRmiss onr2 kept asuncertainty. Thirdshortpartial may clearpending before
remaining420ms spent; needs exact reconstruction, not proven/fixed thisphase.
Network/TTSminimumstack2116/2220B; SDKhistoricalregionminsum30056B isnot
simultaneousheap.48KiB and reliabletrue1s remainopen. Noadditionalphysicalgroup.
Twofullbackup/app-only/readback/nonappparity passes:UTC20260927-204826/205305.
Device restored72fast/capture/listening,WiFi,reuseON,prefetchOFF,LEDoff,COMclosed.
Context751/712268B to761/718184B due10newtestevents5916B; unchanged byrestore,
next3859,gen17,2MiBpartition,204800history,448KiBclip. Formalpackage unchanged.
Evidence checked=true,accepted=false; auditSHA256
29f3811bcdc85c43abc2a48a9ed0e4b9bd666bf32703bec1a9cadb5fc1b68fdd.
Goal remains active. Source134 retained; no claim of complete experience.

### UX103: preserve a pending argument across a short ASR revision

UX102 is verified progress, not acceptance:134 fixes a lateASR boundary, but
physical1/3full and one wrongblue action. Diagnose the two saved failedclips
on the sameC3 vendor probe plus one byte-identical reference. No new capture
or provider request for reconstruction. Keep134 fixed baseline and existing
32case corpus. Test one semantic rule only after attribution: a nonempty
insufficient draft may preserve an already pending argument, never create
pending by itself, replenish the1000ms quota or authorize effects. Explicit
empty withdrawal and meaningful completed/cancelled/newintent text retain
current semantics. Keep no-speech/producer/hard bounds, all resource caps.
No change to colour grammar/energy threshold/timeout budgets. If host and
fixed replay gates support integration, normal/noaudio builds and one bounded
3-turn physical group may follow. Everyflash full4MiBbackup/app-only/readback/
nonapp parity; restore72 listening, keep contexts and package unchanged.
Missing captured tails and scheduling limitations cannot count as success.

UX103 outcome:135 preserves only an existing pending bit across nonempty
insufficient ASR revisions. No new pending/onset/proposal, no quota refill;
explicitempty and meaningfulnewtext semantics retained. Source fields/buffers,
threshold/colourgrammar/4s/10s/producerbound unchanged. SameC3 probe782frames,
401referencebyteparity and CRCs pass,max2164us. Oldr2 endpoint8fields match;
oldr3 unadjusted receipt replay staysactiveatEOF3820 vsactualDONE3800. Mismatch
retained, no fitted clocks or claimofunique attribution.34fixedcases10021frames
unchanged byprototype/integratedrule. Ownerboundary tests51spentquota positions,
publicationflags, withdrawal/cancel/terminal/no-speech included.65ASanUBSan
pass24.75s; afterequivalentalgebra5relatedtests and fixedframeparity pass.
Normal01over32B,02/03over16B; maskpacking plusshorter normalversion/bootprompt
makesnormal04 fit1540096B. Noaudio02 1062272B. Normalversion0.11.135, SHA
f9c2cfdd82e42ce1f9fb2e3d4f776d06338f109984e24b4e54c23403f1d47e21;
frozenaccepted=false. NoLLMprompt,stack,partition or capacitychange.
One3-turn weakcorrectiongroup:2fullgreen,1truncatedclarification/noeffect.
Endpoints6100/8740/3660ms; thirdquiet1140,hold380,ASRadmission2000. Partial
complete-lookingblue receipt3654ms, endpoint3660, actualfinalwithnegation4677;
clockdomains differ. Next investigate partial/final distinction rather than
interpreting a complete-looking partial argument as final intent. Noadditional
physicalgroup. Firstwake3/3,DMA0,underrun0,no detectedpanic/WDT/reboot. Diagnostic
gaps1046/1047ms; no rapid250msacceptance. ASRfirstadmission0/2320/2000 doesnot
prove whichfreshness/subsequentstrict branches fired. No causalrate claim2/3vs1/3.
SuccessfulcandidatePCM aheadASR3457/5365ms, completecache846/2856ms; VADinternal
marker to preparationstart492/232ms. Lead includescapturetail, not whollyhidden
underuserspeech. Finalreply delays7420/6645/4391ms; trueacoustic1s unmet.
FailedWAV3688ms, independentlocalASR sameblue/negation, nogreen. Network/TTS
minstack2116/2212B. SDKhistoricalregionminsum30056B not simultaneousfreeheap;
48KiB acceptance unresolved. NewPCM/externalrecordings retained locally.
Threefullbackup/app-only/readback/nonappguardpasses, UTCbackup suffixes
20260927-210709/211856/212348; USBprobe Flashcompletelyunchanged. Device72
fast/capture/listening,WiFi,reuseON,prefetchOFF,LEDoff,COMclosed. History761events
718184B to771events724204B due10newtestevents6020B, unchangedbyrestore;
next3869,gen17,2MiBctx/204800history/448KiBclip intact. Packageunchanged.
Auditchecked=true,accepted=false SHA
b49db7b6f0f4c0439113fe502dc008f0aeb6cb4f83e92f5323c5f99c71be4582.
Fullgoal staysactive, no completion claim. Source135 retained; device72.

### UX104: distinguish provisional ASR from settled text when releasing pending

UX103 made progress but2/3 complete remains insufficient. Latestfailure got
complete-looking partialblue at3654ms and stopped source3660ms beforefinal
negation4677ms. Do not wait indefinitely for cloudfinal:11of12recent finals
arriveafterVADupload-end. Preserve ONLY already pending intent across nonempty
unsettled previews, within the SAME1000ms capture quota; no new blanket wait
for greeting/complete firstdraft, no quota reset or buffers. Semantic pending
may still originate from existing grammar; emptywithdrawal/terminal/cancel,
local quiet/4s/10s/producerbounds stay. Settled meaningful text can replace
pending as before, but settled is not independently a user-turn endpoint.
Carry stage through isolatedASR and legacyOmni delta adapters; include
asr_segment_pending. Check exact stage mapping, legacybehavior, bounded lost
final, cancellation, shortcommands and preservation of corrections. Compare
fixed recordedhints with stages taken from actualevent records, never inferred
from punctuation. Report any unknownstage/clock limitations without fitting.
One finite implementation, host/build gates, then at most one3-turn diagnostic
weak-correction group. Keep normal1540096B budget, stacks, partitions,2MiBctx,
204800history,448KiBclip. Everyflash full4MiBbackup/app-only/readback/nonapp
parity; restore72 after experimental work. No stable1s/48KiB claim without
actualscope-matched evidence. Formalpackage remainsunchanged.

2026-09-28T05:52:49.315823+08:00

### UX104 build and fixed replay gates

Normal136 app1540080B (16B below normal cap), SHA256
c1869f6a58963a452b58e88f6547191ab6335a186d66f2d8c6adcc83ca432a99;
noaudio1062272B. No new buffers, endpoint fields, stack reservation or data
capacity. Normal heap hooksOFF. Initial65-test run64passed, new pending test
expected held10 but four-vote resumed syllable correctly spends three more
frames; corrected expectation13 and five affected tests passed. Original
failure log retained. Frozen candidate136 accepted=false, source included.
Fixed comparison study-03:34cases10021frames; actual ASR stages matched original
receipt times and text (outer-whitespace normalization only),22 previously
ignored pending-segment previews added to candidate adapter, baseline ignores
them. Baseline135 reproduced every8-field result.31cases unchanged; UX90-r2
end7420->7580ms, shared hold840->1000ms. UX91-r3 and UX102-r2 stay active at old
retained EOF; no claim of recovering missing suffix. No timing offsets fitted.
Two study setup failures preserved: outer whitespace in prior TSV and older
report lacking wake_after, resolved using original TSV text and archived
record_at source. These were harness provenance fixes, no endpoint tuning.
Next exactly one diagnostic3-turn group, then restore72; no promotion yet.

2026-09-28T05:59:54.272006+08:00

### UX104 finite device group and restoration

Single3-turn group asr-stage-ux104-weak-correction-01 completed, no retest;
source136, same5.895s correction fixture gain0.25, zh/yue/zh first wakes3/3.
All3 inputs complete, exactly onegreen effect perturn, no blueeffect, no
rejectedcalls, DMA0/underruns0/runtimehealthpassed. Sourceends6840/6920/6880ms,
all pending1000ms, quiet1680/1720/1660ms: bounded completeness improvement in
this group comes with remaining enddelay. Not a controlled A/B or human test.
CandidatefirstPCM leads finalASR4037/4084/4102ms, cachedcomplete leads1212/
1000/1736ms. VADuploadmarker->preparationstart625/389/490ms; finalspeaker7800/
6944/6563ms. Those are internal clocks, include endtail and do not prove
speechend->acoustic1s. Diagnostic postturn gaps1062/1047ms; rapidrewake
acceptance false. No failedclip to retain or re-ASR; external WAV saved local,
SHA b89b3504e746755789630087bd7a3416485c68baab8add96596ad0efb3fe7f30.
Network/TTS minstack2108/2192B; actualcandidate freeheap sample45836B below
48KiB. SDK per-region historicalminimum sum29904B separately, not concurrent.
Candidate136 and rollback72 full4MiBbackup/app-only/readback/nonapp parity,
UTCbackup suffix20260927-215121/215654. Restored72-summary fast/capture,
listening/WiFi,reuseON,prefetchOFF,LEDoff,COMreleased.2MiBctx/204800history/
448KiBclip intact. Test added12events7696B:771/724204 to783/731900; restoration
preserved history,generation17,next_record3881. Formalpackageunchanged.
Auditchecked=true,accepted=false SHA
070be11a962ddc25797c0d76770c8e96afa62da7e6faa732e575d3a1997d41db.
Report docs/VOICE_ASR_STAGE_REPORT.md, source136 and failure logs retained.
Fullgoal remainsactive: stable1s,48KiB and rapidcontinuousacceptance unmet.

2026-09-28T06:05:25.177338+08:00

### UX105: bounded isolated-ASR receive drain

Prior goal turn classified PROGRESS: UX104 changed stage propagation and one
3-turn group completed all corrections, but source quiet tail1.66-1.72s,
late interim green, finalspeech delay and actualsample45836B still fail goals.
Current source136, device72 restored. Verify current baseline before edits.
Source review: isolatedASR feed sends464samples then one512B nonblocking read.
A fragmented or burst transcript may remain queued across later uploads.
This is a scheduling hypothesis, not yet the proven cause of device latency.
UX81 tried draining Omni audio and failed benefit/onecapture; it was reverted.
Do not claim that older attempt was successful. This changes only isolatedASR
text receive, keeping endpoint136, sample counts, thresholds, models and all
buffers/stacks/partitions. Add bounded zero-time poll batch,8reads/2ms checks
between reads, return on empty. Nonzero-time waits stay single-read. An individual
transport/parse call is not forcibly preempted at2ms. No extra task or allocation.
Test burst/fragment order, empty, time/count caps, failure/cancel, PCM tail,
existing protocol invariants. Consider equivalent fixed4-byte base64 writes
only if useful for packet construction/code budget, validate everybyte.
Normal1540096B and noaudio builds. Then at most one3-turn correction group;
only if complete and healthy, one3-turn short self-introduction group without
diagnostic postturn queries for rapid-rewake/recording analysis. Retain all
outcomes, no fitted retests or blanket VAD shortening. Everyflash full4MiBbackup,
app-only/readback/nonapp parity; restore72 after experiment. Preserve2MiBctx,
204800history,448KiBclip; formalpackage unchanged until acceptance.

2026-09-28T06:17:19.345597+08:00

### UX105 implementation gates

IsolatedASR zero-time poll now drains at most8 reads /2ms checks between calls,
returns on empty and preserves all protocol cancellation/errors and uploadbytes.
No change to endpoint136, models, triggers, buffers, stacks or data partition.
65ASan/UBSan tests pass24.72s. Added deterministic burst10message ordering,
byte fragments, empty,2ms/timeoverrun,8reads, networkloss/cancel incl emptyread,
32bit clockwrap. For footprint, shared two duplicate PCM/base64 upload encoders
in plugins/speech/pcm_json.c/.h; all0..512sample lengths decoded byteexact,
little-endian goldenNBI=, padding, null/limit failures verified. No sent format
or packet size change. Eight affected tests then finalisolatedASR check pass.
Earlier selective regex missed isolated_realtime_asr, covered in following
8tests; do not claim the3selected unrelated ASR tests covered encoder changes.
Normalbuilds1540272/1540240/1540112/1540112B overbudget; finite equivalent
encoding and loop cleanup yields1540048B, no budget waiver. NormalSHA
92894a8f072c6adae3dc916e3e38a85d16da40cf2e4aa32db29c55b7b87f43cd.
Noaudio1062272B. Frozen137 acceptedfalse. Network9216/TTS7168 unchanged,
heap hooksOFF. Normal app is32B below136 despite boundeddrain. Fullgoal active.

2026-09-28T06:31:18.4792709+08:00

### UX105 finite result and rollback

One3-turn correction group completed: firstwake3/3, complete2/3, effects
green/green/BLUE. Thirdsourceend2900ms retainedWAV2920ms/46720samples;
localSenseVoice and cloud both onlyblue. Wrong tool effect fails acceptance.
No contingent greeting group or fitted retest. DMA0/underruns0/runtimehealthy.
Successcandidate PCM precedes finalASR3995/3693ms, cachecomplete1149/597ms;
internal VADupload->preparationspeaker375/353ms, final6073/5736ms. Third
candidatecancelled, wrongfinal5303ms is notspeed success. Sourceends6820/
7280/2900ms, quiet1680/1180/700, pending1000/1000/0. Validlightpartial only
arrives afterthirdcaptureend. No proof of networkqueue cause or realacoustic1s.
Network/TTS stackminimum2124/2196B; candidate partialsample heaps60404/54468/
95240B do notprove whole48KiB; SDKregionhistoricalsum29924B separately.
Diagnosticgaps1047ms mean rapidrewake notaccepted. All evidence retained.
Rollback receivefunction/header to136; remove experimentalburst tests only
fromcurrent source, preserve137 sourcezip and experiment-source snapshots.
Keep byte-equivalent sharedPCMencoder, now138;8relevant tests pass4.68s.
Normal1539952B within1540096B (128B below136), noaudio1062272B, noheaphooks,
allstacks/buffers/partitions intact.138 SHA
e042e8616a078618f0d2f5a5358ea8bd33899577495733eb8309aa0e23a41c0c.
Frozen138 acceptedfalse, notdevice-tested.137 preservedfailed, formalpackage
unchanged. Restore72 verifiedlistening fast/capture/WiFi,reuseON,prefetchOFF,
LEDoff,COMreleased. Bothfull4MiBbackup/app-only/readback/nonappparity passed;
UTCsuffix20260927-221624/222113. History783/731900 ->793/737900 (10events6000B),
generation17,next_record3891; restorepreservedhistory.2MiBctx,204800history,
448KiBclip unchanged. Auditfirstfailed on stdout-only directoryfield compared
to storedmanifest, correctedcomparison matches everypersistedfield; failure
retained. Auditcheckedtrue acceptedfalse SHA
248312fb733697f56bf3b96ef67a6520976e6da8b80b29713eeae88f3e254bfd.
Report docs/VOICE_ASR_DRAIN_REPORT.md. Fullgoal remainsactive; nextbounded
diagnosis should examine localending beforemeaningfulASR, notrepeat thisgroup.

2026-09-28T06:37:29.219219+08:00

### UX106: local premature endpoint evidence

Previous goal turn PROGRESS: UX105 retained a new failed capture, rejected and
rolled back ASR draining, preserved equivalent encoder cleanup138 and restored72.
Begin with exact saved PCM replay on the same C3 via frozen USB-only123 probe:
one known parity reference and UX105 third failed recording. No fresh microphone
capture/cloud request in this diagnostic. Verify all PCM/metadata CRC, source
filter/vendor identity and fullFlash backups, app-only writes/nonapp parity;
restore72/listening. Reconstruct138 endpoint against measured terminal fields,
use measured ASR receipt/stages without fitting. Noise119 makes both high/low
energy thresholds240: investigate evidence before modifying threshold or vote
rules. Any candidate must retain strict onset, bounded noise tail, cancellation,
all capacities and normal1540096B budget; compare saved failures/noise and real
complete inputs, not just one clipped prefix. No claim beyond retained EOF.
After host/build gates, at most one fresh3-turn physical group for a candidate;
retain failures and acoustic/rapid-rewake limitations. Full goal remains active.

2026-09-28T06:54:02.165347+08:00

### UX106 bounded coupled counterfactual

Exact8-field reconstruction ofUX105 thirdcapture succeeds. Three strong20ms
frames2460-2500ms miss four-vote continuation; nearby adequate-energy frames
fail MODE_3 spectrum. High/low floors both240, but lowering energy alone cannot
recover these spectral negatives. Fixed8-input MODE_3/MODE_2 C3 replay completed.
MODE_2 alone rejected for580ms additional endpoint delay on completeUX90-r1.
Its last strict speech remains5980ms: addeddelay comes from unspent1000ms
pending quota after all provisional complete drafts, not later speech votes.
No changes adopted into normalfirmware. One further offline coupled hypothesis:
MODE_2 with135-style pending replacement on meaningful complete provisional
text (short drafts still preserve pending; empty withdraws), keeping136 stage
API for identity and all existing source/ASR limits.135+MODE_3 previously failed;
do not erase those failures or claim this combination inherently reliable.
Use same8 fixed inputs; reject new noise onset or addedtail>200ms, check any
earlier complete-input cut with independent local ASR and source words. Missing
retained continuation remains unknown. No threshold sweep or physical retest
until these gates, code tests and unchanged Flash/RAM/capacity gates pass.
2026-09-28T07:03:52.0597701+08:00
### UX106 combined integration gates

Mode2-only failed the predefined delay gate (+580msUX90-r1), remains rejected.
The one coupled counterfactual passes finite8-input gates: UX105 failedprefix
stillcapturing at2920ms; completeUX104 cut6880->6540ms; UX90 complete7100->7160ms;
two shortquestions and two negativecounterfactuals unchanged. Rawnegative
background uses declared119noise, not originalmeasurednoise. No success inferred
past a saved prefix. Four full/cut localSenseVoice text pairs agree after only
punctuation normalization; UX104 BOTH read finalcolorblack instead ofsourcegreen,
retained as ASR limitation. Code140 uses mode2 only in fastconfigure beforeany
sourceframes, releases initialmode3 detector first, no secondliveclassifier;
classic/newlistening retains mode3. Meaningful provisional intent releases
pending; incomplete shortdrafts and explicitwithdrawal retain oldboundedrules.
No argument execution beforefinalvalidation.65ASan/UBSan tests pass24.76s;
5probeprotocol hosttests pass.1986integratedframes byte-identical to prototype.
Normal1540000B within1540096, noaudio1062272B, C11/noheaphooks confirmed.
AppSHA186c4f014338859e65a1c77bfbde094d22bd3cbca27ab46435aece9eb4276619.
Frozen140 acceptedfalse; unchangedwakeweights, energy/vote/silence thresholds,
2MiBcontext,204800history,448KiBclip,buffer andtaskstack budgets.
Next exactlyone3-turn weakcorrection group withsamegain0.25 andzh/yue/zhwakes,
prefetchON/reuseOFF/idle. Preservefailedrecording andstackevidence, so thisgroup
is diagnostic, notrapidrewake oracoustic1s acceptance. Restore72 aftergroup.
2026-09-28T07:15:23.4763819+08:00
### UX106 physical outcome and restoration

One3-turn local-end-ux106-weak-correction-01 completed; allfirstwakes andcomplete
inputs3/3, exactlyonegreen setter andgreen readback perturn, noblueeffect.
Sourceends6780/7280/6480ms, quiet980/1360/700, held1000/820/0. Firstturn ASR-only
admission2320ms, otherstrictlocal. Backendlocal-spectral-vad2 confirmed. NoDMA
loss orWDT/panic/reboot, butthirdturn playbackunderruns1 (firsttwo0): nothealthy
playback acceptance even thoughdialoguecomplete=true. No fitted repeatgroup.
CandidatePCM leadsfinalASR3529/3914/3841ms, cachecomplete1111/1577/1060ms.
VADuploadmarker->preparation385/391/543ms; final6932/7091/6188ms. Internalclocks,
not realwordend/acoustic onset; diagnosticgaps1062/1046ms invalidate rapidrewake
acceptance. Network/TTS minstack2108/2296B. Candidateactualheap samples57032/
59232/56696B; insufficient toprove whole48KiB. SDKregionhistoricalsum29904B
separate. No subjective listening. ExternalWAV SHA
c2852f9fcf4a105fa284405d79a0c7d18bc9f601ec863ff6a9c397378356da02.
Probe123/139, candidate140 andrestore72 allfull4MiBbackups/app-only/readback/
nonappparity. Probeusage madezero persistentwrites. UTCsuffix223749/224755/
230236/230823 on20260927.72-summary restoredfast/capture/listening/WiFi,
reuseON,prefetchOFF,LEDoff,COMreleased. History793/737900 ->805/745616,
12events7716B,generation17,next_record3903. Restorepreservedallhistory,2MiBctx,
204800history,448KiBclip.140 frozenexperimental, formalpackageunchanged.
Auditchecked=true accepted=false SHA
f411067274caae8fe0280ccd28861e1014387841a7216060ed92d2dadbdc9ff3.
Reportdocs/VOICE_LOCAL_END_REPORT.md. Fullgoal remainsactive. Nextconcrete
latency path: voice_candidate.c finishdirectlyhandles onlySELF; LIGHTcandidate
onlypreparesack andthenfallsinto fullengine. Consider verifiedfinal-intent
simpleaction route reusingexistingtoolpolicy; do notexecute provisional args
or relabelunexecuted progressspeech ascompletedanswer. Thisroute notchangedhere.

2026-09-28T07:22:37.207705+08:00

### UX107: final-intent local execution on the text-candidate route

Previous turn PROGRESS: UX106 exactly reconstructed premature termination,
integrated and tested one VAD/semantic combination; all three corrected inputs
completed, but a playback underrun and 6.2-7.1 s final-response delay remain.
Current source140, device restored72. No stale process is being awaited.
Reuse the existing strict final lamp grammar, original tool validation/policy,
context messages and success-only cached confirmation in the new text-candidate
path. Capture/ASR must be complete; never use a guessed RGB value. Cancel and
join speculative workers before effects or engine memory reuse. Candidate
failure must not authorize fallback after any local effect or spoken response.
Ambiguous, negative, compound and unfinished requests retain the full Agent.
No new persistent audio buffer/task or partition/history/capture reduction.
Keep normal1540096B application cap. Test cancellation, hardware/playback/
persistence failures, worker ownership and exactly-once routing; preserve old
fast behavior. Only after host/build/size gates, one fresh continuous three-turn
correction group, Mandarin/Cantonese/Mandarin wake. Retain failures and current
content/readback criteria; measure actual external audio separately from cues
and internal timestamps. At most one additional three-turn group if new source
changes or an independent required regression justify it. Guard every flash
with full backup, app readback and non-app parity; restore72 after diagnosis.
This does not shrink the full goal or claim one-second acoustic success.

2026-09-28T07:37:19.295264+08:00

### UX107 build gates and finite physical group

141 initial local route passed four focused host checks, but1540480B exceeded
normal cap by384B; frozen as candidate-0.11.141-overbudget, never flashed.
142 shares the two equivalent voice history writers (same full-input records,
error precedence, route/effect/partial flags), saving240B. First142 app1540240B
still over budget; retained142-before-prompt-compact.bin. Compact equivalent
candidate instructions (same SELF/REMEMBER/LIGHT contract and14-character cap)
save a further144B. Final1421540096B fits normal cap exactly; no headroom.
SHA34a3892fd3a8c14eb9bba1499d32402db9eb29fd0519bce9817694becbc34a6a.
65/65 ASan/UBSan tests pass,24.02s; final prompt change also passes both worker
lifetime/clock tests10.68s. Noaudio1062272B. All three changed production C units
compile asC11 with allocation hooks off. No capacities, tasks or stack changes.
Host tests exercise joined owner+restored arena, complete corrections, discarded
bad/late cached responses, cancellation, policy round limit, hardware failure,
audio write/drain failures, storage failures and busy owner preservation.
Run one same-input three-turn correction group at gain0.25, prefetchON reuseOFF
idle preconnect, same diagnostic retention/stack evidence asUX106. Retain every
failure; no rapid-rearm/acoustic1s claim from internal markers. Restore72 after
bounded work. Full goal remainsactive. Exact application margin0B is recorded.

2026-09-28T07:40:40.316279+08:00

### UX107 independent greeting regression scope

The sole correction group has produced three complete green effects and no
reported underruns; its driver still owns cleanup/clip export, so do not start
another serial owner until the live handle terminates. One further three-turn
SELF group is justified by the shared history writer and compact candidate
prompt, and exercises rapid re-wake without diagnostic per-turn status queries.
Same142 binary, same Qwen ASR/idle preconnect/prefetchON/reuseOFF, fixed saved
'greeting' at gain0.25. No threshold/input acceptance changes. All failures stay
in the report. No third group or parameter search this turn; then restore72.
External audio correlation+independent local ASR are candidate timings, not
human listening/first-phoneme certification. Full goal remains active.

2026-09-28T07:54:46.625850+08:00

### UX107 physical outcome, language regression and restoration

142 one3-turn correction group: full inputs, first wakes and exactly one green
setter+readback3/3; no DeepSeek request stages, DMA loss, underruns or detected
reset. FinalASR->effect8/8/8ms, ->speaker169/170/170ms; upload-end->speaker
399/374/504ms. These are internal markers, not physical end-of-speech latency.
Network stack min3164B. Candidate heap samples59204/55732/54836B; SDK historical
region minsum46296B is not simultaneous freeheap or a48KiB pass. Diagnostic gaps
1031/1047ms. Exact C-rendered completion reference independently ASR-decoded all
three external crops as'灯光设置好了'; waveform completion gates failed, so
all physical onset latencies remainunknown. General speed analyzer's refusal
of diagnostic run was retained, not bypassed or used to claim acceptance.
Independent greeting regression: first wakes and completeASR3/3, actual re-wake
gaps265/265ms. Internal upload->speaker437/455/663ms. External original analyzer
candidates1.609/2.639/1.668s; no first-phoneme/human listening or<=1s pass.
All three Mandarin inputs got Cantonese replies. Preserve rawcomplete=true but
mark language acceptancefalse; old fixture only checked self-role/name. Added
strict observed-regression check for this exact Mandarin fixture;9Python tests
pass after correcting a misplaced test insertion (initial failure kept).
143 makes default Mandarin explicit; otherwise same application logic as142.
Both candidate host checks pass10.71s; normal1540096B/noaudio1062272B, no spare
normal budget.143SHA2860f4f195c32471ea0cf365a3d0ae3a659d9de6100c15a90cf79f29735cd8ad.
Not flashed or cloud-language-tested, so142 results are not attributed to143.
No third physical group. Initial greeting-01 stopped before playback/request,
zero trials, because correction-only manifest lackedgreeting; preserved. Real
three-turn run uses existing voice-cloud/prompts-01 manifest in greeting-02.
Guarded142 install+72restore backups20260927-233721/234535:4MiB verified, app-only,
readback and all non-app parity.72 restoredfast/capture/listening, WiFi/reuseON,
prefetchOFF, lightoff; minheap71240B is restart snapshot. COM/recorders closed.
Context805events/745616B ->817/751904B, +12events/6288B; generation17,next3915,
restored context identical to142 aftertests. Allcapacities unchanged.
Source143 retainedexperimental;142/141frozenfailures retained; fullgoalactive.
Next evidence needed:143 requestedlanguage and actual end-of-speech latency,
without losing fullinput/complexhandoff/continuouswake/resource requirements.
Reportdocs/VOICE_LOCAL_ROUTE_REPORT.md.

2026-09-28T07:59:06.444656+08:00

UX107 evidence audit passes:600 C/H/Python files equal frozen143; only candidate
prompt/version and fixture language check differ from tested142. Six actual
turns, six first wakes, three green effects, false language/latency acceptance,
fullFlash hashes, app bytes, non-app parity and retained context verified.
AuditSHA014c0dc8ec10fc02c2b00dfc1ccc6942e4d5ad2aae3f94eb533e6654151b9f2f.
First audit assumptions about noaudio label/config were wrong: noaudio retains
0.6.3-context; actualcompile confirms-DAGENT_ENABLE_AUDIO=0 and noKWS definition.
Both failed audit logs retained; corrected check uses authoritative commands.
No source or physical data changed to satisfy audit. Normal1431540096B,
noaudio1062272B. Currentdevice72-listening. Goalactive; nocompletion claim.

2026-09-28T08:05:57.764179+08:00

### UX108: validate language and isolate remaining response tail

Source/frozen143 is ready but not physically verified; device starts restored72.
Use existing fixed inputs, preserve context/history/clip/partitions and normal
1540096B app cap. First run one143 three-turn Mandarin SELF group with read-only
per-turn capture evidence to separate local endpoint, uploaded EOF, ASR final
and first speaker. Such diagnostic timing is not rapid-rewake/physical1s
acceptance. Independently verify an explicit Cantonese request using existing
material if available. Inspect capture/upload ownership before any endpoint
change; no weakening of final-ASR/tool/cancel gates or input-completeness checks.
At most one focused improvement and one further three-turn physical comparison
in this phase. Record all failures; backup+app-only guard each flash; restore72
after bounded tests. Full goal remains active, no <=1s completion claim.

2026-09-28T08:20:35.244082+08:00

### UX108 connection tail findings and bounded candidate144

143 Mandarin SELF3/3: first wake/full input/content all pass; generated replies
are Mandarin. ASR handshake+setup starts on wake, while only candidate is warm.
Local capture end->uploadEOF456/601/711ms; ASR transport open1306/1293/1329ms.
Candidate request begins405/384/474ms after local capture end, first PCM
145/42/31ms after finalASR. Stable prediction is therefore still late.
Keep endpoint/draft/final/tool rules unchanged.144 adds idle ASR transport
setup to the existing candidate worker before warming reply transport. Its
untouched socket transfers once via release/acquire to the ASR owner; only
that owner reads/configures/uploads. Open failure permits one fresh connection
before any audio, not a turn replay; cancellation/timeout/expiry join and
release unused handles. Same tasks, borrowed arena, buffers and stack sizes.
Two control fields reside in the small coordinator; initial insertion in the
borrowed work struct exceeded the x86 trace-build static assertion and was
reworked without expanding the arena. Test counter reset was initially put
in workspace_restore; moved to test init, retaining failed logs. Five focused
checks pass11.19s; all65 ASan/UBSan pass24.30s. Normal first two images exceed
budget352/400B, never flashed. Compact hardware prompt removes redundant
wording while retaining every IO/display ownership/time/evidence rule; third
build1540080B is16B below1540096B. No capacity/partition/threshold changes.
One further three-turn same greeting comparison with identical diagnostic
settings, no third physical group this phase. Explicit Yue output may be
checked separately by bounded host protocol probe, not as device/ASR proof.

2026-09-28T08:38:01+08:00

### UX108 acceptance state

The two-group limit is complete:143 and144 each passed three physical first-wake,
complete-input and Mandarin-content checks, no observed DMA loss/underrun/reset.
Idle ASR transport adoption removes wake-time handshake wait (1306/1293/1329ms
to0/0/1ms); local-end->upload becomes108/177/134ms. Keep final-input/admission,
correction/cancellation/tool policy and all capacity limits unchanged.
Six external onset candidates remain diagnostic-only:143:1.564/1.733/1.874s,
144:1.206/1.131/1.167s. Original eligibilityfalse and within_one_secondnull;
stable physical <=1s, rapid250ms re-wake, subjective experience and48KiB full-flow
heap are NOT accepted. Separate host-only language3/3 is not Cantonese ASR proof.
144 normal1540080B/noaudio1061856B,65 sanitized host and5 focused tests pass;
16B app budget margin, existing16576B arena/tasks/stacks/partitions unchanged.
Current C/H/Python sources match frozen144 (600files); only eight code files
differ from143. Three full verified backups/app-only writes preserve non-app
bytes. Restored72 retains829events/757284B,generation17,next3927,history204800B.
Current device72fast/capture/listening with reuseON/prefetchOFF; source144 remains
experimental and installation package unchanged. No third physical group.
Audit4289ee00778887277cfb08a6b7cfef13a3dca4ef3c886396ccb891ed0f98cb44 and
all failures retained in artifacts/voice-fast/overlap-tail-ux108/.
Report docs/VOICE_ASR_PRECONNECT_REPORT.md. Overall goal remains active.


2026-09-28T08:42:14.055533+08:00

### UX109: verify full handoff after idle ASR preconnect

Previous turn PROGRESS:144 reduces handshake/upload tail, six physical turns
verified, but full goal remains open. Start from live72 and frozen144. At most
two physical groups of three turns. First existing remember fixture uses real
250ms re-wake gaps without added per-turn diagnostics, exercises both bilingual
wake and DeepSeek handoff/persistence. Use the next group only to resolve a
concrete finding or correction regression; retain all failures, no cherry-pick.
Preserve complete input, final effects, cancellation, heap/capacity/app budgets.
Back up and verify fullFlash before app-only updates. Restore72 after bounded
work unless a fully justified replacement is verified. No <=1s or broad
reliability claim from internal markers or diagnostic acoustic candidates.
Evidence artifacts/voice-fast/handoff-tail-ux109/plan.json.


2026-09-28T08:48:01.349637+08:00

UX109 first three-turn144 group FAILS completeness: firstASR only记住, later
object omitted; turns2/3 complete, first wakes3/3, re-wake266/265ms. First
incomplete input nevertheless reached summary get/set; existing fact readback
is NOT proof of requested input capture. Third candidate missed500ms wait
deadline; later DeepSeek answer remained slow. SDK region minsum29904B.
Root code gap: pending-argument hints cover lamp/repair but omit bare memory
verbs, and final clarification guard covers only unfinished corrections.
145 will include bare Mandarin/Cantonese memory verbs in the existing shared
1000ms pending quota, with no new buffer/state or global silence change. Final
incomplete arguments must enter tools-disabled clarification if no continuation
arrives. Preserve successful full-input route. Validate host semantic/bounded
time/cancel guards, compile within same cap, then exactly one3-turn same-input
comparison. Slow complex handoff and whole-flow heap remain open, not accepted.


2026-09-28T09:05:37.818421+08:00

### UX109 results: progress, full goal not accepted

145 adds bare memory verbs to existing pending-argument quota and uses the
same final guard for tool-disabled clarification. No VAD threshold, buffer,
task, history capacity or partition change.65 ASan/UBSan checks pass24.33s.
First app1540128B exceeds budget32B; frozen-overbudget, never flashed. Shorter
equivalent clarification prompt gives1540096B; noaudio1061808B. Follow-up regex
selected onlypending_endpoint; separately ran actualvoice_final_stream0.57s.
No false count of missing tests. Final source/app frozen as145.
144/145 each exactly3 physical turns, first wakes6/6; observed re-wake gaps
266/265 and282/266ms after@done. No DMA loss/underrun/USB drop/detected reset.
144 first input only记住 and wrongly reached summaryget/set;145 all three
receive named-lamp content, but first two omit请. Strict completeness1442/3,
1451/3; both overallreportsfalse, gates unchanged. No broad reliability pass.
Candidates144miss/hit/miss,145hit/hit/miss;145 third revoked on ASR sentence
revision before request.144 third exceeded500ms wait. Formal DeepSeek reply
remains slow:145 external candidates7.301/7.932/5.614s; acknowledgement1.261/
1.292s in first two, none third. First two also fail full-input gate. These
are local-ASR/energy candidates, not human phoneme/subjective or<=1s passes.
145 request bodies83-100KB, firstconnection732-745ms, send580-674ms, final
firstbyte1075-1979ms. Preserve full200KiB budget; no causal speed percentage.
Network/candidate stackmin2124/2320B; SDK regionminsum29904B is not simultaneous
minimum. Eventminimum54904B does not prove48KiB whole-flow acceptance.
Guarded144/145/72 backups20260928-004230/005300/005745 verify4MiB, appreadback and
all nonapp parity. Context829/757284B ->842/782304B ->851/795188B, +22/+37904B;
restored durable fields match, generation17,next3949. Three RAM prompt/request
counters correctly reset.2MiB context/204800B history/448KiB clip retained.
72 fast/capture/listening restored, WiFi/reuseON,prefetchOFF, LEDsOFF, COMclosed.
Audit verifies600 sourcefiles, seven changes, hashes/rawreports/Flash/status;
SHAa7477392787acac5590061b3c2ff2cb231ba9ad833e79647bb3f2a437f487ea1.
Legacy stage parser failed on nonJSON advisory; kept failure, reused only its
unchanged llm_requests helper. Initial audit compared volatile counters, fixed
to explicit RAM-reset expectations after source review; rawdata unchanged.
Reportdocs/VOICE_MEMORY_CONTINUATION_REPORT.md. No third physical group;145
experimental only, installer unchanged. Whole goal remainsactive.


2026-09-28T09:12:02.072634+08:00

### UX110: retain applicable preparation across ASR sentences

Previous turn PROGRESS:145 fixes missing-memory-argument handling, but reveals
a parameter-free memory receipt revoked on ordinary ASR sentence revision.
146 will whitelist LIGHT and REMEMBER receipts for sentence continuity; SELF
answers stay bound. Actual complete final intent/language and validated output
still gate playback; explicit memory repairs/cancels/kind changes still revoke.
Question-form memory text should not admit a preparation promise. No effects
execute speculatively. Preserve all thresholds, quotas, cache/workspace/tasks,
capacities and normal1540096B cap. At most two physical groups of3: existing
remember and correction; no repeated optimization loop. Retain strict input
failures, real gap values and acoustic uncertainty. Backups/app-only guard;
restore72 after finite experiment. No overall completion claim.
Evidence artifacts/voice-fast/receipt-continuity-ux110/plan.json.


2026-09-28T09:34:52.806197+08:00

### UX110 results: preserve preparation across ASR sentences; not accepted

146 retains parameter-free LIGHT/REMEMBER preparation across ASR sentence revisions;
SELF remains bound. Complete final intent/language/output, explicit cancellation,
repair and task-change checks remain. Question-form memory requests are rejected.
65 ASan/UBSan tests pass24.17s; no VAD/quota/wait/cache/arena/task/stack capacity change.
First app1540128B exceeds cap32B; frozen-overbudget, not flashed. Only USB help wording
shortened afterward: final1540080B, noaudio1061792B, same1540096B cap. Seven changed
code/test files; all600 C/H/Python files match frozen146, appSHA95c03791e31172caed09b2705d18e3d63d3cb12bf925a12f62f58561fc3c64a1.
Exactly two groups of3 using continuous_rewake.py, prefetchON/reuseOFF/idlepreconnect,
original remember and correction fixtures/gain.25. First wakes6/6; no detected reset,
DMA loss, underrun or USB drop. Strict input complete1/6; both reportcomplete=false.
Remember: omit请 / omit请记住 / complete; candidatehit/miss/miss. Third receives no
candidatePCM within existing500ms finish wait. This group did not reproduce ordinary
split-memory sentences, so it does not prove the host-verified continuity improvement
in hardware. Re-wake265/282ms. Existing memory readback is not proof of a new full input.
Correction: first two ASR only把灯调成蓝色; blue readback, wrong result. Third includes
full correction but omits请; green readback, content-only check passes. Local actions
follow ASR final in all3, never speculative; truncated final text can still be wrong.
Re-wake265/265ms with light queries. No threshold/gate relaxation and no third group.
Remember acoustic diagnostic: ack1.722s only first; answer7.182/6.721/6.293s. Internal
uploadEOF->ack793ms; answer6257/6215/5892ms. Preparation/feedback are not useful answers;
allwithin_one_second null and subjectivefalse. Correction repeated-source alignment
fails (only third copy passes), acousticunknown retained. No premature attribution
to device VAD vs capture vs ASR vs host playback; investigate actual retained evidence.
Candidate/network/main stackmin2320/2124/1792B. SDK region historicalminsum29904B is
not simultaneous minimum; sampled63312/68336B also do not prove48KiB whole-flow budget.
Guarded deploy146 and restore72 backups20260928-011628/012640 verify full4MiB, installed
app and nonapp parity. Context851/795188B->866/811472B (+15events/+16284B), restored
durable fields identical, gen17,next3964. Three RAM prompt/request counters reset.
2MiB context/204800B history/448KiB clip retained. Final72fast/capture/listening,
WiFi/reuseON,prefetchOFF, LEDsOFF, COMclosed. Installer unchanged;146 experimental only.
Evidence artifacts/voice-fast/receipt-continuity-ux110/{plan.json,audit.py,audit-01.json};
separate remember/correction146 directories retain rawreports/recordings/offlineanalysis.
AuditSHA5142b5f99314647bd6fd512076901e22977df7c7c758e5b83942e05c86f84f10 checkedtrue/acceptedfalse.
Report docs/VOICE_RECEIPT_CONTINUITY_REPORT.md. Overall goal remainsactive: complete input,
stable <=1s useful reply, natural handoff and whole-flow memory are not accepted.


2026-09-28T09:37:08.388469+08:00

### UX111: attribute lost correction before further timing changes

Previous turn PROGRESS:146 adds receipt continuity, but UX110 correction loses
its second clause in two of three turns and the common acoustic fit fails.
First compare immutable prompt prefix/suffix and all three external copies,
with source-clock device events. Do not assume VAD, cloud ASR or host playback
causality. Preserve original acceptance results. At most one new diagnostic
group of3 with retained per-turn clips/endpoint metadata, conditional on an
evidence-backed hypothesis. No blind threshold sweep or capacity reduction.
If flashing needed, same backup/app-only guard and restore72. Overallactive.
Evidence artifacts/voice-fast/input-attribution-ux111/plan.json.


2026-09-28T09:47:11.951142+08:00

UX111 setup findings: immutable UX110 recording prefix aligns; endpoint cues precede
input active end by2.605/1.765s in failed turns, while local ASR of external audio
contains both the continuing correction and board reply. Third cue is1.035s later.
This supports premature capture completion, not cloud-latency attribution. Original
whole-source fit remainsfalse. No response-latency acceptance from prefix matching.
Normal146 rejects endpoint-trace (@error argument) before any playback/recording;
zero trials, process exited, cleanup rejection retained. Prepared147-protocol-diag
with existing trace enabled, no VAD/stream/route changes. Fixed diagnostic labelling
for TEXT_PREFETCH+ENDPOINT_TRACE. First build1541344B exceeds diagnostic1541120B
cap224B, frozen-overbudget and neverflashed. Diagnostic-only shortened help reduces
app to1540304B; normal command help/parser unchanged. Normal cap not raised.
Same planned one3-turn diagnostic group remains; record all clips and post-capture
frame metadata. Added setup builds are not additional acoustic tests.


2026-09-28T10:03:55.558875+08:00

UX111 bounded diagnosis completed3 live turns on147: first2 full and green;
third ends2880ms and setsblue. All clips retained (6.264/6.524/2.924s). Offline
SenseVoice agrees that third raw clip lacks correction. All785 frame metadata
CRC passes; current C replay matches8 endpoint fields in all3. Failed turn last
strict speech2180ms;2 strong/3 weak votes2400-2600ms do not reset silence. New
provisional complete blue text clears pending before2880; holdspent0 despite
1000ms existing quota. Failure is actual early recording end, not just ASR.
One artifact-only recent-draft/shared-quota hypothesis tested11 fixedinputs;
it crosses failureEOF, but adds520ms on UX90 completeinput and extends another
complete clip beyond recordedEOF. Rejected, productionendpoint unchanged.
Next finite offline-on-C3 experiment uses the sameMODE_2 with existing clean
energy-filtered PCM as classifier input. No new live sound/network requests.
Eight fixed clips:3 current, one earlier full correction,2 short greetings,
backgroundtail and digitalzero.148USB-only probe and host tool declare/check
input_filter identity; require both original/clean energy fields byte-identical
to baseline, allowing only spectral bits to differ. Six protocol host tests pass.
No silent adoption or completion claim; compare endpoints on same ASR clocks.
Actual diagnostic setup had one early stop-prefetch call while recorder still
ownedCOM5; port-open failed before commands, retainederrorlog. Waited terminal
17400exit1 before successful snapshot. No duplicate test/process start.


2026-09-28T10:22:18.742801+08:00

### UX111 results: source capture truncation attributed; no endpoint change adopted

Exactly one live group of3 on147-protocol-diag, prefetchON/reuseOFF/idlepreconnect.
First wakes3/3; strict full input2/3; green/green/blue. Third raw clip2.924s lacks
correction in local SenseVoice too. All785 frameCRC passes; original C replay
matches8 final endpoint fields each turn. Last strict speech2180ms, sparse weak
syllable support expires; provisional blue clears pending and capture ends2880ms.
This is actual premature recording end, not only cloud transcript omission.
Per-turn clip export suspends wake: latency/rearm acceptance remainsineligible.
UX110 prefix-only acoustic diagnosis finds cues2.605/1.765s before continuing
input ends; original full alignment failure and unknown answer latency preserved.
Recent-draft shared-quota artifact adds520ms to a complete clip: rejected.
Existing clean filter as vendorVAD input tested on sameC3:8savedPCM/2088frames,
original+clean energyexact,CRCexact,max2157us. Failed endpointstill2880ms; another
completeclip+140ms. Rejected; no live capture/cloud calls during this probe.
Pending-intent plus two short strong frames crosses failureEOF in artifact only;
another complete input crossesEOF too. Missing continuation remainsunknown,
not a passing full-capture result. Productionendpoint.c/h,source_stream.c unchanged.
Only normal runtime diagnostic label/help condition and four probe/tool/test files
changed this phase. Normal146app1540080B, traceOFF;147diag1540304B; probe148131808B;
noaudio1061792B. Initial1471541344B overdiagcap224B preserved, neverflashed.
Normal/diagcaps1540096/1541120B unchanged. Eight related ASan/UBSan tests pass11.82s;
six hostprotocoltests pass. All609C/H/Python/CMake files match148sourcefreeze.
SDK historical minimum44588B and event sampledminimum63772B are not simultaneous
whole-flow minimum proof;48KiB remainsunaccepted. No observed reset/DMA/USBdrop/
underrun in3live turns; no generalization or one-second acceptance claimed.
Five guarded app-onlywrites verify4MiB backups, applicationreadback, nonapp parity.
USB probeafterimage exactly matches finalrestore beforeimage: no Flash mutation.
Backups UTCsuffix013957/014606/015424/020257/020701. Final72fast/capture/listening,
WiFi/reuseON,prefetchOFF, LEDsOFF,COMclosed. Context866/811472B->872/814988B
(+6events/+3516B), fullcontextstats equal afterrollback, gen17,next3970.
2MiBcontext/204800Bhistory/448KiBclip intact. Installer unchanged.
Initialauditfailed on normalizedLF prototypehash vs physicalCRLF; originalfailure
log kept, bothhashes separatelyverified, second invocation passes. No data rewritten.
Audit artifacts/voice-fast/input-attribution-ux111/audit-01.json checkedtrue,
acceptedfalse,SHAfb5d57410b86af50ac9d7f440ea905d23c7e2169548c11a7ad7f4a321e8152e6.
Report docs/VOICE_INPUT_ATTRIBUTION_REPORT.md; README updated. Overallgoalactive.
This bounded phase ends without additional live groups or firmware promotion.


2026-09-28T10:25:08.473440+08:00

### UX112: bounded ASR-corroborated short-syllable continuation

Previous turn PROGRESS: UX111 exactly reproduced actual capture truncation and
rejected recent-draft waiting and clean-input VAD hypotheses. Start from146
production,72installed. Test the saved one-condition prototype against all8fixed
inputs and explicit onset/noise/withdrawal/cancel/deadline bounds; no fabricated
post-EOF continuation. Only after source/host/build gates, one group of3ordinary
consecutive correction turns on149 with original fixtures/gain and short re-wake
gap. No per-turn clip export that would confound this re-wake measurement.
Same700ms/1000ms/320ms, appcap, memory and partitions. Guarded app-only flash and
restore72 if not accepted. No broad model/timing sweep or overallgoal completion.
Evidence artifacts/voice-fast/pending-syllable-ux112/plan.json.


2026-09-28T10:45:43.043942+08:00

### UX112 results: bounded short-syllable support; three corrections preserved

149 adds admitted pending-ASR corroboration for two CURRENT strong frames within
existing320ms after strict continuation. No new onset, weak-only reset, renewal,
state/buffer/stack, silence or cumulativequota. Classicfeed explicitly unchanged.
65ASan/UBSan tests pass23.50s including boundary/noise/withdrawal/cancel checks.
Eight fixed real inputs, sameASR clocks:6terminalresults unchanged;2crossoldEOF,
futureunknown. No fabricated continuation or offline full-capture acceptance.
Exactly one ordinary3-turn group, original correction/gain.25, prefetchON,reuseOFF,
idlepreconnect, no per-turn export. Firstwakes3/3, actualgaps266/265ms. Allthree
contain blue->green repair and execute exactlyone green effect afterfinalASR.
Strictinputfalse/false/true because firsttwo omit请; reportcomplete remainsfalse.
No threshold/check relaxation or A/B improvement-percentage claim. No observed
reset/DMA/USBdrop/underrun. Lastclip exported only after3turns:107648samples6.728s;
firsttwo rawclips notretained, so omitted请 source versusASR remainsunattributed.
All3external sourcefits pass; localASR confirms 灯光设置好了/好啦. Acoustic onset
candidates.8776875/1.973375/1.084375s, no subjectivefirstphoneme or one-secondpass.
UploadEOF->finalASR325/682/247ms; final->candidatejoin7/12/3ms. Localroutealready
skips candidate500ms wait; its missreason final_local_light is intentional.
Localreply->speaker161/162/161ms, source retains200ms PDMwarmup. This is a specific
future overlap/latency investigation, not proof the warmup can be safely deleted.
Normal149app1540096B (+16), exactsamecap; noaudio1061792B samehash asUX111.
AppSHA67bfa7b2f17c4b509120ad2585a03bc6df408893566121701d49342adb78e3cc.
SourceZIPa47012c4d57644a0397b7e121569b97b1602b194ad522f914e80dcb5d437e41c;
609C/H/Python/CMakefiles matchfreeze,4fileschanged. Candidate/network/mainstack
min2288/3132/1792B. SDKregionhistoricalminsum43248B, eventsampledmin54924B,
candidateownsamplemin48024B: no simultaneouswhole-flow48KiB acceptance.
Guardeddeploy149/restore72 UTCbackups023034/023528 verifyfull4MiB, appreadback,
nonapp parity. Context872/814988B->878/818572B (+6/+3584B), fullcontextstats equal
onrestore; gen17,next3976. 2MiBcontext/204800Bhistory/448KiBclip unchanged.
Final72fast/capture/listening,WiFi/reuseON,prefetchOFF,LEDsOFF,COMclosed.
149source/frozenapp retainedexperimental, installerunchanged. Allhandles terminal.
Audit artifacts/voice-fast/pending-syllable-ux112/audit-01.json checkedtrue,
acceptedfalse,SHA71b5ecefb0b46a3a09027d3801aed9fe05b5eaeb7f4b2718b5c58d2dc4a6fdce.
Speedtable latency-149.csv SHA24e29a17beb710643e13ca957341170725d7d5f47c251e5f36f1e96d005d9392.
Report docs/VOICE_SHORT_SYLLABLE_REPORT.md; README updated. PhasePROGRESS,
overallgoalactive: stableone-second/completeinput/naturalhandoff/whole-flowheap
remainunproven. No additional live groups thisphase.


2026-09-28T10:54:02.360452+08:00

### UX113: output handoff after the capture completion cue

Previous turn PROGRESS:149 preserves3corrections, but measured localreply->speaker
161/162/161ms and finalASR wait325/682/247ms remain. Inspect actual hardware owner:
wake_cue closes PDM/PA and nextstream repeats verified200ms cold settling. Do not
remove cold settling. Test a bounded audio-owner handoff at the same sample rate,
digitalzero only, <=2000ms unclaimed, release on cancel/end/error before nextwake.
At most correction3+self3+cancel-diagnostic3; no automatic repeats or newmodel.
Existing guards, capacities and1540096Bcap retained. Fullbackup/app-only guard;
restore72 if not accepted. Evidence artifacts/voice-fast/output-handoff-ux113.


2026-09-28T11:20:59.833088+08:00
UX113 revision1:150six ordinary turns preserve3/3final-green effects and3/3self-introductions,
all6first wakes; localreply->speaker still161/161/160ms, output_handoffs0.
Actual production realtime.h/progress.h use16kHz; cue was24kHz, so the rate guard
correctly reopened cold. Earlier assumption of24kHz replies was wrong.
Three instrumented cancellations observed held=true, then released in63/63/62ms;
not ordinary latency/rearm acceptance. Original9trial bound reached; for this
specific evidenced correction extend once by exactly2groups/6turns, no more.
151moves isolated-ASR cues to negotiated16kHz, preserving160/180ms pitch/duration
and40ms tail; music24kHz unchanged. PureC renderer receives host duration,
pitch/sweep/fragmentation/volume/zero-capacity tests, all65checks pass24.00s.
Failed budget builds are immutable and never flashed:1501540688/1540224B,
1511540144B. Compact experimental USB help retains command names; final151
1540080B fits1540096Bcap, audio-off1061792B unchanged. No capacities reduced.
151guarded deployment complete; correction3+self3are the last groups thisphase.


2026-09-28T11:35:32.178878+08:00
UX113 completed finite experiment; LOCAL_GAIN_WITH_FAILED_INPUT_GATES, not promoted.
151corrected-rate output_handoffs5over6normal turns; correct green turn local
reply->speaker11ms, failedblue turn7ms (excluded from success).150was161/161/160ms.
All12ordinaryfirst wakes and3cancel-diagnosticfirst wakes hit; no reset/DMA/USB
loss/underrun observed.150corrections3/3semantic,0/3strict due missingplease;
151corrections1/3semantic: firsttruncated at“不对”and clarified, thirdonlyblue
and incorrectlysetblue. Bothselfgroups3/3fullinput/content, PCMfirstarrived
AFTERfinalASR in all6; no claim of ready-before-user-finish on these shortinputs.
151self externalonset candidates1.082/1.680/1.882s; no stable1s acceptance.
151correction fullgroup sourcealignmentfailed; acousticlatenciesUNKNOWN,
fixed gates retained. Lastdeviceclip exported beforeoverwrite:60288samples,
3.768s,WAVSHA3a6f1391e10b70d39c4ff2e610c3306a575b6b246fd4078637af60d8facc630b.
LocalofflineASR“请把灯调成蓝色不对”differsfromcloudfinal; missinggreen retained
forfutureattribution, no assumption that cue-rate is the sole cause.
150held-outputcancel releases63/63/62ms, no tools. No additional livegroups.
65ASan/UBSanhostchecks passed24.00s inclcuepitch/duration/chunk/volume;
normal1511540080B<=1540096B; noaudio1061792B SAME SIZE, different currenthash
145cdd4c4040275d4263e4a6c8e46a7b106c4d91707fa08aa184410acc7733ce.
Audit correction: removed unsupported equality-to-prior-noaudio-hash assertion;
current binary/hash and equal size are verified, no old hash claim retained.
609C/H/Python/CMake files match frozen151; five sourcefiles changed. SDK I2S
sources verified unchanged atfff9895c82d744c7237be8847347bdd1b07c6643.
Eventsampledheap62024B, candidatesampled54424B; SDKregionhistoricalminsum
34716/29900B, not proof of simultaneous48KiBfloor. Candidatecachelimit1kept.
Candidate/network/mainstackminimum2288/2104/1792B;151TTS2296B.
All3guardedflashes backed/verified4MiB and nonappbytes unchanged; backupUTC
030402/031832/032428. Context878/818572B->902/830704B (+24events/+12132B),
persistentstats unchanged afterrestore; volatilepromptstats reset separately.
2MiBctx/204800Bhistory/448KiBclip/gen17 retained. Verified72fast/capture/listening,
WiFi/reuseON,prefetchOFF,LEDsOFF,COMclosed, PCrecording/testprocessesended.
Report docs/VOICE_OUTPUT_HANDOFF_REPORT.md; README updated. Source/frozen151
remain experimental; stable1s/completeinput/naturalhandoff/heap/subjectivelistening
remain unaccepted. Goalactive, no newlivegroup thisphase.
Audit audit-01.json SHA0a4617c19bd31733166b18b0eed205bf07d8a4fe8f98eee2d6c0cf49f5a2636d; checkedtrue,acceptedfalse.
CSV latency-150-151.csv SHAd5df7ef770f218b46c53d0bfe1c30a2d000f0328409aa66505ee3485c0bbcc67.


2026-09-28T11:41:35.189486+08:00
UX114: previous turn PROGRESS (verified warm-output reuse and exposed incomplete captures).
Begin with3fixed savedPCM clips on frozen139mode2USBprobe, including exact known
reference parity; production filter sources match frozenprobe. No new recording
or cloud in diagnosis. Reconstruct151failed and149full lastturn with unchanged
ASR clocks; missing post-EOF remains unknown. One focused edit only if evidence
supports it; at most2ordinarygroups of3thereafter. Retain all capacities/guards.
Evidence artifacts/voice-fast/capture-integrity-ux114; restore72if not accepted.


2026-09-28T12:07:04.946179+08:00
UX114 complete: DIAGNOSTIC_PROGRESS_NO_LIVE_GAIN, not accepted/promoted, goalactive.152ordinary correction3+self3, firstwakes6/6. Correction core[false,true,false], strictinput0/3; finalbluewronglyexecuted inround3. Selfstrictinput/content3/3. Gaps265..282ms, allterminals@done, noobservedreset/watchdog/DMA/underrun/USBdrop. Outputhandoffs5/released, candidatelimits0. Selfexternalonset1.26571875/1.79471875/1.854375s; candidatePCMlateafterfinal432/855/1060ms, uploadEOFtospeaker708/1092/1300ms. Correction acoustic alignment failed: UNKNOWN, no gates relaxed. Validgreenlocalhandoff13ms; wrongblue6ms excludedfromsuccess.
Lastfailedcapture exported beforeoverwrite63104samples/3.944s, WAV338d118c154c7b8eb345ce105957c9403eab233a3c5e406ff8b0c03191cd8efd; offlineASR contains不对不, no绿色; cloudonlyblue. Eventsampleheapmin61396B,candidatesample54060B,stack2344B. SDKregionhistoricalminimumsum29900B, no wholeflow48KiBclaim.
3guardflashes probe139/152/restore72:4MiBbackupverified/appreadback/nonappunchanged, UTC034138/035435/035926. Context902/830704B->914/836572B, persistentstats identicalafterrestore, volatilepromptfields reset.2MiBctx/204800Bhistory/448KiBclip/gen17retained. Final72WiFifast/capturelistening,reuseON,prefetchOFF,LEDsOFF,COMclosed,0taskrecording/testhelpers. Source152/frozen152remainexperimental; regularpackageunchanged.
Audit609codefiles, only4expectedchanged; audit-01.json checkedtrue/acceptedfalse SHAa92ce31a03418993b5b0b33a85d9421e605b4d8bbbf560dfde468242ba239113. CSVlatency-152.csv SHA5e7072579223838c8b966715579f5c65f59f20b986818e7601b1f6cc98273ead. Report docs/VOICE_CAPTURE_INTEGRITY_REPORT.md, README/SPEC/ACTIONLOGupdated. No additional livegroups thisphase. Remaining captureintegrity and candidateconnection/request timing are distinct unresolved gates; no stable1s or subjectiveaudioacceptance.


2026-09-28T12:13:26.659133+08:00
UX115: previous phase PROGRESS in diagnosis, no live gain.152failed capture gates; candidate started->open includes serial ASR warming and repeated candidate handshake. Begin one bounded actual text-only session/3consecutive turns, then identity-preserving C11 protocol primitive if provider receipts support it. No new device recording/playback/flash at this protocol gate; no retry/model switch. Device owner/arena lifetime and app/RAM budget must be validated before future integration. Preserve152and72, all capacities and full goal. Evidence artifacts/voice-fast/text-session-reuse-ux115.


2026-09-28T12:36:46.3917092+08:00
UX115 completed: PROTOCOL_PROGRESS_NO_DEVICE_GAIN_CLAIM; full goal remains active.
One actual verified provider WSS session completed three text turns with no retry,
mic, speaker or device actions. First PCM after response request:406/484/484ms
on the host; these are not C3 end-of-speech response measurements. Provider-01
failed before credentials/network due to missing websocket; provider-02 is the
only connected run. Raw receipts, executed scripts and failure evidence retained.
C11 next_text_turn preserves response/input identities and absolute deadline,
rejects stale events and unauthorized between-turn input/generation, maximum3.
68 real events replayed with random byte splits match126720 PCM samples exactly.
65 ASan/UBSan checks passed23.24s; actual C3 compiler measures protocol object
1064B with feature disabled/enabled. This is not a live TLS RAM measurement.
Board feature remains compile-disabled. Normal build1540096B fits unchanged cap;
SHAf68f24bc69fbbb0dfa1acb706c034535a05fcc01234bbcde8c6681d7d32117f1.
Frozen candidate-0.11.152-text-preflight is not accepted/promoted; sourceZIP
SHAa8609f5f0e3708ae0a3023b3d6ee453dee5afddbd8705b802ed24daba21d023a.
613 frozen source files checked, only four existing code files and the new probe
changed relative to frozen152. No no-audio rebuild claimed in this phase.
Read-only device check confirms72 WiFi fast/capture/listening;914events/836572B,
2MiB partition and204800B history unchanged. Zero flashes/new acoustic groups.
Report docs/VOICE_TEXT_REUSE_PROTOCOL_REPORT.md; reproducible saved-evidence
check artifacts/voice-fast/text-session-reuse-ux115/audit.py and audit-01.json.
Next required work: unique connection ownership and bounded state rebinding
across capture/Agent arena turnover, then app/RAM checks and at least3 real
continuous turns. Do not retain borrowed pointers from reclaimed capture memory.
Capture integrity, stable1s, complete model handoff and whole-flow RAM remain
unaccepted; no new provider/device group is added at this protocol-only gate.


2026-09-28T12:40:56.4030483+08:00
UX116: previous phase PROGRESS. Implement bounded primary text-session ownership across arena turnover. Preserve identities; clear/rebind borrowed pointers; only accepted complete direct replies may retain, at most3turns and30s idle. Ordinary task/TTS, changed intent, cancel, expiry and errors close. Validate actual adapter with poisoned reclaimed memory and faults, then unchanged build budget; at most2livegroups of3 after guarded flash. Evidence artifacts/voice-fast/text-reuse-owner-ux116; full goal active, no thresholds/capacity changes.


2026-09-28T13:13:46.2805305+08:00
UX116 completed: LIVE_REUSE_GAIN_WITH_REMAINING_HANDOFF_AND_INPUT_GATES; not accepted/promoted, full goal active.
153 uses value-only1080B retained protocol state, clears/rebinds borrowed pointers,
keeps input/response identities and TX nonce, joins workers/speaker before transfer.
At most3turns; bounded idle ownership; cancel/expiry/config/ordinary task/change/error
close. Local allocation failure releases TLS before one allocation retry, no replay.
65 ASan/UBSan checks passed24.17s. Freed/different-arena three-turn adapter tests,
old IDs/PCM, nonce, cancel/expiry/fallback/restore/persist/thread failures covered.
First host check failed because the mock cache peak was not reset for a new turn;
fixed test bookkeeping, retained failure logs; nonce restart found and corrected.
Initial app1541232B and rejected-Oz1541744B were not flashed. Compiler flags restored.
Use pinned IDF common roots38certs/15905B instead of full66984B. Exact generated
bundle verified against host TLS-only DeepSeek/Qwen/Vocalign with hostname/cert
validation and no system root fallback. Actual C3 Qwen/DeepSeek paths succeeded;
Vocalign board speech not retested. New verifier tools/voice/verify_public_roots.py.
Normal1531490192B<=1540096B; audio-off1010720B. No capacity/partition/VAD changes.
AppSHAe5437d9fdb5d39075e1087fd429d4fbf7c6b185ea112ccb41adffdf0af7746d8;
sourceZIPdfa0f7fe47dbfc6bf893e8080722a981ef30690aef6c8b87e1200732c841ceee.
Exactly two acoustic groups,3turns each; first wakes6/6 zh/yue/zh per group,
all@done, observed no WDT/reset/DMA/underrun/USB loss. Selfstrict3/3; connection
reused[false,true,true]. UploadEOF->speaker109/497/464ms; acoustic candidates
1.22103125/1.09071875/1.05371875s (152was1.26571875/1.79471875/1.854375).
No randomized causal or stable1s claim. Candidate PCM vs finalASR -30/+180/+215ms.
Remember core3/3, strictinput2/3 (third lacks请); generated ack hits2/3. Ack acoustic
candidates1.40171875/1.330375s; finalanswers6.75171875/9.220375/8.285375s.
Third candidate late/cancelled. No extra groups, no threshold/check relaxation.
DeepSeek/TTS needs primary so memory-task candidates cannot retain that slot;
formalhandoff/repeatedconnection and fullworkspace turnover remain next work.
Candidate/worker/main stack minima2328/2120/1792B; candidate sampledheap62208B.
SDK historicalheap28860B is retained, not wholeflow48KiB acceptance or leak proof.
Two guarded flashes: full4MiB backups verified/appreadback/nonapp identical.
Initial rollback path was nonexistent, failed before USB; corrected immutable log
restore-72-02 records real restore. Context914/836572B->929/852220B (+15/+15648B),
persistent fields match afterrestore.2MiBctx/204800Bhistory/448KiBclip/gen17 intact.
Final72WiFi fast/capture/listening,reuseON,prefetchOFF,lightsOFF,COMclosed.
Report docs/VOICE_TEXT_REUSE_OWNER_REPORT.md; README updated. Evidence
artifacts/voice-fast/text-reuse-owner-ux116 incllatency-153.csv,rawaudio/source,
collect.py and measurements.json;619source/config files match frozen153.
Stable1s, general quick answers, natural full-model handoff, complete input and
wholeflow RAM remain unaccepted. This phase is actual reuse progress, not completion.

UX117: a complete ANSWER: response with no tool calls and no prior tool round may be voiced once without a second LLM request. Stream fragments stay silent until successful completion; strip marker before output/persistence. Tool-bearing or post-tool planning keeps existing final request. No buffer/capacity reduction. Preserve cancellation, errors, progress join and history lifetime. Validate host then two bounded3turn hardware groups; whole goal not complete.

UX117 refinement before acoustic trials: allow the complete ANSWER response after previous successful tool batches too. Prior actions must have finished their existing validation/execution/persistence path; current response must be complete with zero calls. No tools are omitted/replayed.155 replaces untested154 for the same two planned groups.

UX117 outcome: completed155 direct-answer path verified on host and one real no-tool answer; post-tool shortcut remains host-only evidence. Two3turn groups completed with business checks6/6 but input3/6,ack1/3memory and no stable1s. No resource/experience acceptance.155 frozen;72 restored with all data; whole goal active. Details docs/VOICE_DIRECT_ANSWER_REPORT.md.

2026-09-28T13:49:52.5394762+08:00
UX118 started: stream admitted complete task sentences before response.done, using existing Flash spool and unchanged500ms first-PCM/10s tail bounds. Worker joins and spool seals before arena turnover. Bounded host/build/2x3real-turn plan at artifacts/voice-fast/receipt-stream-ux118/plan.txt. No new resource buffers or wider input authority. Goal active.

UX118 outcome:156 streams validated task receipts into existing live Flash spool after final ASR;500ms admission and10s tail unchanged. Worker joins/seals before arena reuse, cancellation pointer belongs to stable turn.65checks pass;2x3actualturns completed,firstwake6/6,strictinput5/6;memoryack2/3,notstable1s.1490784B app,no capacity reductions. Experimental156 frozen,72restored with data. Fullgoal active; evidence docs/VOICE_RECEIPT_STREAM_REPORT.md.

2026-09-28T14:11:09.0458871+08:00
UX119 started: overlap existing primary TTS connection with first DeepSeek planning response, no synthesis before real TTS begin;once-per-turn bounded ownership and cleanup, no new scratch/task/capacity reduction. Evidence156 formalhandshake952/1203/959ms. Priorprogress verified. Bounded plan artifacts/voice-fast/tts-preconnect-ux119/plan.txt; fullgoal active.

UX119 outcome:157 prepares the existing primary TTS WSS once after first planning request body, overlapping remote generation; no run-task/text/PCM before formal begin. Sole ownership,10s local adoption deadline (not timer-based release), no borrowed arena, unused cleanup on all exits. Ordinary TTS may connect after a preconnect miss; no uncertain task replay. Model prompts/input authority/context unchanged.
65/65 ASan/UBSan passed28.74s; normal1491328B (+544 vs156),48768B under normalcap1540096; audio-off1011360B. No new task or scratch buffer; candidate arena16576B unchanged.
Two bounded3turn groups remember/recall_name completed: firstwake6/6,all@done,core6/6,strictinput5/6. All6warm adopted0-1ms,formal run-task startup66-74ms. Remember formalexternal7.468/5.277/7.932s;acks1.958/1.477/miss. Recall formal4.801/unknown/4.682s;no candidate ack. Unknown retained on failed acoustic alignment. No stable1s, general-intent or subjective acceptance and no causaloverallgain claim.
Candidate sampledheap60564B,stack2272B;eventheap53060B;network2124/main1792;SDK historicalmin28912B remains below wholeflow48KiB gate. Context956/874272B->969/883244B;2MiB/204800Bhistory/448KiBclip/gen17 preserved. Guardeddeploy/restore verified full4MiB,appreadback,nonappidentical.72 restored WiFi fast/capture/listening,reuseON,prefetchOFF,lightsOFF;COM/recorders closed. Frozen157 not promoted. Report docs/VOICE_TTS_PRECONNECT_REPORT.md; artifacts/voice-fast/tts-preconnect-ux119/measurements.json and latency-157.csv. Fullgoal active; phase closed without further trials.

2026-09-28T14:31:20.1412497+08:00
UX120 started: general source-bound contextual thinking receipt for ASR previews; existing three-category coverage misses ordinary questions. Plan artifacts/voice-fast/contextual-receipt-ux120/plan.txt. PreviousUX119 verifiedprogress; fullgoalactive; boundedtwo3turngroups and unchanged capacities/guards.


UX120 outcome: generic contextual thinking receipts implemented experimentally; live experience FAILED, fullgoal active.159 marks raw ASR previews待续: and grounds topic characters in their original order, while requiring complete final-ASR prefix/language and no cancel/repair. This is output grounding, not semantic correctness or authority to act. Full Agent still receives final input; no tool before final. Reused65B audio-ID slot inside existing candidate, marker9B/rawUTF8<=55B; actualC3arena16576B unchanged. Original prefix/cache/stack/input/history/time limits retained.
158 and159 each65/65 ASan/UBSan pass31.81/31.68s; app1492736/1492944B below1540096,finalheadroom47152B;audiooff1011360B unchanged. Exactlytwo3turnhardwaregroups:158recall3/3strict,ack0/3(cancel/protocol/cancel);159sky0/3strict,ack1/3,firstreceiptawkward天空是. Firstwake6/6,all@done,noobservedWDT/panic/reset. Corequestion6/6 does not override strictinput3/6. Single host-only provider connection3textturns diagnosed wrongaction interpretation and topic word deletion; no mic/playback/device/retry. Firstgroup not rerun.
Sky receiptPCM99ms before finalASR,speaker65ms after finalASR,not after verified user-speech end. Other two cancelled beforePCM. Sky externalalignment failed:alllatenciesunknown. Independent localASR of retained55744sample/3.484s capture has only firstquestion;5.275s source has following请简单解释一下. Capture truncation established for lastturn,not merelycloudtranscription. Fixed26s externalASRwindows confirm receipt/explanation content only,nottiming/human quality.
Final159frozen acceptedfalse;72restoredWiFi/fast/capture/listening/reuseON/prefetchOFF/lightOFF. Threeguardedflashes full4MiBbackup/appreadback/nonappverified. Context969/883244->981/889160 (+12/+5916),2MiBctx/204800Bhistory/448KiBclip/gen17 preserved. SDKmin28912B remains below wholeflow48KiB,nostabilityleakclaim. Report docs/VOICE_CONTEXTUAL_RECEIPT_REPORT.md; speedCSV/artifacts at contextual-receipt-ux120. Next priority capturecontinuation and meaningful topicselection; no more livegroups thisphase.


2026-09-28T15:15:48.523422+08:00
UX121 started: retained159 sky capture lacks the second sentence. Offline source has approximately920ms sentence gap, versus700ms local silence. Bounded plan artifacts/voice-fast/phrase-continuation-ux121/plan.txt: verify139producer parity, at most one3clip USB replay batch, one evidence-driven candidate and at most2x3continuous live groups. No repeat-for-score, no thresholds/capacities/alignment-gates relaxed. Speculative preparation continues; complete input still gates playback/actions. Goal active.


2026-09-28T15:27:56.730744+08:00
UX121 candidate160: explain-question pause hint zh/yue, at most400ms shared with existing1000ms per-turn hold; no new endpoint fields/tasks/buffers. Known short identity/direct lamp commands unaffected. Changed intent/empty withdrawal clears hint; no onset or action authority. Repeated text and gaps cannot refill allowance; classic path unchanged. Original159 retained capture reproduced3440ms end exactly; synthetic full source now5200ms instead2740, actual missing tail remains unknown. Ten other fixed inputs unchanged frame-for-frame except internal notice encoding.65/65 host checks pass31.89s. Normal build succeeded; hardware acceptance pending.


2026-09-28T15:39:13.977251+08:00
UX121 closed: BOUNDED_IMPLEMENTATION_AND_FAILURE_ATTRIBUTION_PROGRESS; goal active, not accepted/promoted.
160 explanatory-question hint zh/yue adds at most400ms within existing1000ms shared hold. Revocable coherent flag, no new endpoint fields/tasks/buffers; classic/onset/energy/capacities unchanged.65/65 ASan/UBSan31.89s; ten old fixed inputs unchanged. Frozen139 USB one3clip batch:401frame known parity exact; retained159174frames reproduce3440ms stop/dense2740/quiet700/speech740 exactly. Digital source before2740/after5200, NOT acoustic continuation proof. Low0.292 prefix correlation excluded from timing.
Normal1493136B (+192),46960B under1540096cap; noaudio1011360B SHA8a10ac2e81271c42b8a294381b32b2dc360aa9c2014e25fa958565d14869d613. Frozen866files,381codeverified,tenexpectedchanges vs159. AppSHA6ec64c8b7ff26d67e2a088001abd67860074503a44861b9ed1258b02fbcf48c4; sourceZIPcbcaf49111c556c2e0c268378ec87e34120f4dfc4e6cac85dd24d72f3dcdedaa. Initial replay helper missed cJSON include; corrected before executable replay. Native Git plain whitespace check flags existing CRLF widely; explicit cr-at-eol check clean, no global config or mass normalization.
Exactly2x3 continuous groups,zh/yue/zhwake,zhbusiness,gain0.25,~250msrewake; firstwake6/6,all@done,noobservedWDT/panic/reset/DMA. Sky strict0/3,stillmissingsecondclause,ack1/3awkward; external waveform gate failed and all acoustic latencies UNKNOWN. Last capture61312samples3.832s; actual400msheld/1100quiet/source3800; localASR onlyfirstquestion while played5.275s WAV hasfullinput. WAVSHA8efb25691ca8e26f605e325fb3be60edded8fe36e653fc7363ceae4f5998af95. No extra window increase or repeat-for-score.
Greeting strict2/3; firstadds请用语音 and uses genericthinkingreceipt. Its receipt0.920s/formal5.100s is NOT successful1sanswer. Correct rounds2/3 direct replies1.696375/1.034719s, noDeepSeek; lastconnectionreused. Totalrequestcounts2/1/1,1/0/0; zero tools. Sky nominations only45/37/1348ms beforefinalASR; lastPCM244msafterfinal. Sourceplanning/ASRarrival/connectiondelay remain distinct next issues.
Candidatearena16576,17024prefix,24KiBcache unchanged; candidateheap85196B/stack2328B,eventheap58888B,worker2124/main1792;SDKhistoricmin28912,no wholeflow48KiB or leak/subjectiveclaim. Context981/889160B->993/894972B,gen17,2MiBctx/204800Bhistory/448KiBclip intact; persistentstats identicalafterrestore. Three guardedflashesUTC071606/072840/073426 full4MiBverified backups/appreadback/nonappidentical.72restoredWiFi/fast/capture/listening/reuseON/prefetchOFF/LEDsOFF. Allhandlesreaped. collect.py checkedtrue/acceptedfalse; measurements.json,latency-160.csv,report docs/VOICE_PHRASE_CONTINUATION_REPORT.md. Normalpackage unchanged, experimental160retained.


2026-09-28T16:00:15.308549+08:00

UX122 / experimental161, not accepted: short general-knowledge question previews
may nominate a real Qwen answer rather than a thinking receipt. Conservative
why/definition question forms, source<=55 UTF8 bytes without truncation, closing
question mark, no hardware/history/realtime references. Incomplete recognized
questions wait; this is a lexical routing heuristic, not semantic understanding.
The immutable source must equal complete final input after OUTSIDE whitespace
and terminal-punctuation trimming; added constraints, repairs and changed words
discard silently and pass full input to Agent. No tools from previews.
Existing65B slot/24KiB paged cache/6144B stack retained. Actual response<=96B,
one sentence, final capture before playback, complete status/text before WAL.
Late text or protocol error stops with no full-turn replay. Reuse max3inputs.
One provider WSS/3text turns finished with real zh/yue short answers and self
reply, firstPCM469/468/375ms; these are host timings, not C3/user latency.
Provider transcript.done arrives with audio.done: first prototype waited for it,
which can overflow existing3.072s cache (actual3.2/4s replies). Before flashing,
kept existing single-sentence streaming rule plus end-marker validation instead.
Initial host run63/65 due new test incorrectly expecting one WAL event instead
of user+assistant; corrected expectation, not persistence implementation.
All audio/capture/endpoint/context/partition budgets unchanged. Finite plan in
artifacts/voice-fast/quick-answer-ux122/plan.txt; hardware acceptance pending.


2026-09-28T16:11:10.702177+08:00

UX122 closed: genuine source-bound short-answer preparation implemented and
validated on device; whole goal active, experimental161 not promoted.
Exact2x3livegroups: all6firstwakes, all6strictinput, all6fastformalanswers, zero
DeepSeekrequests/tools. New single-question sky source, NOT repair of prior
two-clausecapture failure and NOT matchedA/Bagainst72. Acoustic alignmentpass;
sky2.318719/2.609375/2.283719s, greeting0.977531/0.882719/1.073063s. Sourcelead
is compared to finalASR only: sky2PCM1473msbeforefinal and playsubmit25msafter;
other skyPCM586/661mslate. GreetingPCM-120/+210/+29ms. No cue counted asanswer.
ExternallocalASRconfirmsreplycontent,no analysedfullscale samples; nohumanquality
or bilingual-business acceptance. zh/yue/zhwake, zhsyntheticbusiness,gain0.25.
65/65finalASan/UBSan35.90s. Initial63/65newtesterror corrected (WALtwoevents),
next65/65thenfinalrequiredrerunafterprovider-drivenendmarkerstrategychange.
OneproviderWSS3turnsclosed,firstPCM469/468/375ms hostonly. No repeatedlivegroups.
Application1495072B (+1936vs160),45024B below1540096cap; noaudio1011360B unchanged.
AppSHAc60cff8e84cec9b60140f4191af4d3ff874be3c81b5256ec2365e811cc4f9be4;
sourceZIPeb2a5b3518d8be62516b8336514702fd7b9f54bdb30efb9207548bb6503233ed.
866frozenfiles,381code/buildfilescurrentequal,12expectedchangedfiles vs160.
Candidatearena16576B,stackfree2328B,heaplow68348B; eventheaplow83552B. SDKmin
46324B still FAILS48KiBwholeflow; noleakclaim. Afterheap74336/74328B (6samples
cannot establish stability); networkstack3116,main1792. NoobservedDMA/WDT/reset.
2guardedflashesbackups kws-voice-flow-20260928-080121/-080600, full4MiBverified,
appreadback/nonappsame. Context993/894972->1005/900432 (+12/+5460),gen17,
2MiBctx/204800Bhistory/448KiBclip unchanged. Restoredpersistentfieldsexact.
72restoredWiFi/fast/capture/listening,reuseON,prefetchOFF,LEDsOFF. Candidate161
acceptedfalse, defaultinstallunchanged. Source/runtime/report retained; no new
cloud sweep or endpoint delay adjustment. Remaining: finalinputwaiting/earlyASR,
longpausecompletecapture, general semantic revision, wholeflowRAM and broader
human/complex-task experience. Report docs/VOICE_QUICK_ANSWER_REPORT.md; evidence
artifacts/voice-fast/quick-answer-ux122/measurements.json and latency-161.csv.


2026-09-28T16:15:57.958943+08:00
UX123 started: bounded six ASR observations (three existing sources at700/400ms), threshold0.2 unchanged. Split provisional ASR segmentation from capture ownership; no physical capture/playback/device mutation in this diagnostic. Plan artifacts/voice-fast/early-asr-segments-ux123/plan.txt. UX122 retained source still161, device72 ready. No model sweep or retries.


2026-09-28T16:26:48.613720+08:00

UX123 closed: VERIFIED_EXCLUSION_DIAGNOSIS_PROGRESS; full goal still active.
One bounded6callASRcomparison700vs400ms, threshold0.2/model/rate unchanged,
threeexistingPCM sources eachonce/config, identicalPCMhashes, no retry/reply/
microphone/speaker/devicewrites. All6transcriptscovertheiractualfilecontent.
Firstfinaladvances328/327/1218ms; fullquestionpreviewadvances16/-47/-15ms,
therefore no consistent earlier speculative request. Two-clause source retains
bothsentences, lastfinal390msearlier.400actualcapturehasextraemptytailsegment.
CurrentCsegmentcollectorASan/UBSanreplays127actualevents,6/6exactaggregate,
max2segments, emptynoisefinalpreservestext. EveryuploadedPCMblock and raw event
order verified. Retained160capturecannot prove recovery of its missingsecond
clause. Host timings are not device acoustic acceleration or human acceptance.
Onlydiagnostichelper explicitboundedfixturelist added;6Pythoncheckspass0.061s.
InitialPowerShell->WSLloop lostarguments, emptyfile retained; explicitPython
subprocessargs repaired localreplay. No renewedproviderrequest. No firmware
parameterchange/build/flash: earlierfinalalone doesnotmove partial-based
candidate request or safelyreplace fullcapture join.381C/buildfiles and normal
appSHA match frozen161. Runtime72WiFi/fast/capture/listening/reuseON/prefetchOFF,
LEDsOFF; context1005events/900432B/gen17/last29836/next4103 unchanged.
UX122 externalendcue offsets1.245/2.275/1.190s forsky and0.330/0.335/0.340s for
greeting show nonuniformlocalendpointwaiting; serialreceipttimes retained as
diagnostic only. No cue counted asanswer. No stable1second, longcapture or
wholeflow48KiBacceptance claim. Next work must address actual capture/end
confirmation rather than another unproven ASRsegmentation parameter sweep.
Report docs/VOICE_EARLY_ASR_SEGMENT_REPORT.md; evidence and checks under
artifacts/voice-fast/early-asr-segments-ux123/ including asr-timing.csv,
protocol-measurements.json, c-replay-results.json, final-checks.json.

UX124 started: exact source-frame ASR mailbox trace, diagnostic-only 512 B rows plus metadata; production classification and latency thresholds unchanged. One planned three-turn full-sky group, exact C replay, then restore72. Plan artifacts/voice-fast/endpoint-notice-ux124/plan.txt.


UX124 closed: VERIFIED_SOURCE_CLOCK_DIAGNOSIS_PROGRESS; full goal remains active.
2026-09-28: added AGENT_ENDPOINT_TRACE-only bounded64 notice transitions,520B
static state, exact consumer source clocks, independent CRC, explicit overflow.
No production threshold/routing/partition change. Replay supports --notices,
validates source order and rejects incomplete trace; legacy traces retained.
65/65 ASan/UBSan pass36.38s; decoder3/3; C recorder overflow/dedup and105-frame
clock replay pass. Build normal161=1495072B, diag162=1496624B, regular budget.
Exactly one3-turn full-sky acoustic group: first wake3/3,@done3/3, strictinput0/3,
no tool calls, DeepSeek0/1/0. CRC and production C state parity3/3 over586frames.
No consumed cloud-end proposals. Tail spectrum/energy disagreement and spent
400ms phrase quota localize truncation; weak frames are not proven phonemes.
Last actual clip61824samples3.864s; local ASR only first clause, fullsource hasboth.
Second candidate audio rejected final_input_changed; premature partial input
never authorizes tools. No response-speed acceptance from diagnostic outputs.
Whole-run SDK minheap28356B FAILS48KiB; no observed reset/WDT/DMA loss. Final
ADC record clipping0,startcue1483 separately. Do not promote experimental build.
Full backup/app-only/readback/non-app checks passed for 20260928-083850 and084255.
Restored72 WiFi/fast/listening/reuseON/prefetchOFF/lightOFF. Context1005/900432 to
1011/903200 (+6events/+2768B), generation17; after/restore equal, history204800,
ctx2097152 and clip458752 unchanged. Ordinary trace flag returnedOFF.
Evidence: artifacts/voice-fast/endpoint-notice-ux124/measurements.json,
endpoint-notice-ux124-full_sky-162-01/endpoint-analysis/summary.json.
Report docs/VOICE_ENDPOINT_NOTICE_REPORT.md. Two collector assertion failures
(field location and C string pooling assumption) retained and fixed; no repeat
acoustic group. Next bounded work: active-utterance low-energy continuation
against saved negative controls, not blind global threshold/silence changes.

UX125 started: one private modulated-continuation C prototype against exact UX124 source notices and existing negative/full-capture controls. No production threshold change or board test until finite offline gate passes. Plan artifacts/voice-fast/continuation-energy-ux125/plan.txt. UX124 is diagnostic progress, not a wait/blocker.


UX125 private energy prototype rejected: stale-transcript RF18 background extends2600->4220ms; cannot enter firmware. Plan amended to one3-turn fixed8s shadow-capture diagnostic163, retaining all real PCM and source-clock notices beyond the original endpoint. No relaxed production classification. See plan-amendment-shadow.txt.

UX125 closed: COMPLETE_TAIL_CAPTURE_AND_ALLOCATION_DIAGNOSIS_PROGRESS; goal active.
2026-09-28: one private continuation-energy prototype, 17 fixed inputs, rejected
before firmware integration: stale-transcript noise end2600->4220ms (+1620ms).
One amended three-turn fixed8s diagnostic163 captured all actual tails, 400
frames each, exact source-notice CRC/C parity3/3. Original ends3800/3860/3820ms,
next strict continuation4240/4300/4260ms. Local independent full/prefix/tail ASR
confirms missing second clause. Full-input3/3, first-wake3/3, business2/3;
no latency/rearm/human-generalization acceptance. Rejected prototype extends
only one of three complete captures, still not integrated. No score retries.
Round1 candidate discarded for final input change, then @error memory:
free144228B, largest45056B, actual C3 ELF engine workspace54416B. Restoration
uses one contiguous allocation; exact fragmentation owner remains unproven.
Diagnostic minheap25500B fails48KiB; no observed reset/WDT/DMA loss.
65/65 ASan/UBSan pass36.30s, shadow/decoder/clock checks pass. Normal161
1495072B, noaudio1011360B, frozen diag1631496736B. Both diagnostic flags OFF
in normal builds. 379 frozen C/build files match. App-only flashes with full
backup/readback/non-app equality: 20260928-090907 and20260928-091446.
Restored72 WiFi/fast/listening/reuseON/prefetchOFF/lightOFF. Context1011/903200
to1015/905212 (+4events/+2012B), after/restore equal; history204800,
ctx2097152, clip458752 and default installation unchanged.
Report docs/VOICE_SHADOW_CAPTURE_REPORT.md; measurements/workspace-size.json
and original failures in artifacts/voice-fast/continuation-energy-ux125/.
Next targeted work: source/ASR end coordination using complete captured tails,
and restore workspace without requiring a54KiB contiguous block. Do not
promote fixed8s collection or globally lower energy thresholds.

UX126 started: split the54416B engine restore allocation while preserving capacities. Previous UX125 is diagnostic progress, no external blocker. Finite plan and source snapshots: artifacts/voice-fast/split-workspace-ux126/.

UX126 closed: SPLIT_ENGINE_ALLOCATION_FIX_AND_REGRESSION_EVIDENCE_PROGRESS.
2026-09-28: normal engine restore now allocates29836B state and24577B request
buffer separately instead of54416B. Caller pointer metadata outside borrowed
bytes; checked split/full binding and platform-owned rollback. Legacy TEN/
classic capture explicitly retains full contiguous storage. Capacities unchanged.
66/66 ASan/UBSan pass34.54s; allocation cap45056 and each allocation failure
tested in actual platform helper. Separate ASan-redzone stream/history/tool/
cancel/persistence test passes0.99s; related3 checks0.65s. App1641495264B (+192),
diag1651496928B, noaudio1011296B (-64), all within1540096B regular budget.
Both diagnostic optionsOFF in final normal builds. See manifests for hashes.
One3-turn fixed8s same full-sky group: first-wake/full-input/business3/3,
all candidates discarded final_input_changed, no memory errors, exact1200frame
CRC/current-C parity3/3. Minheap23412B fails48KiB. No speed/rearm acceptance.
One normal3-turn correction group: first-wake3/3, full-input/green-result0/3,
terminal done/argument/done, blue readback. No memory error/reset/WDT/DMA loss.
Second model response has parsed tool notifications then argument error;
exact parameter cause remains unproven. Candidate connection never ready.
Last true clip52864samples/3.304s lacks green clause by independent local ASR;
full5.895s source has it. External acoustic alignment failed fixed gates;
speed remains unknown, wrong effect never counts as correct quick response.
Cancel capture/playback acknowledgement203/203ms; rearm2266/1125ms, idle609ms.
No cancellation context writes. Three full verified backups/app-only flashes/
non-app equality, restored72 WiFi/fast/capture/listening/reuseON/prefetchOFF/
lightOFF. Context1015/905212->1029/912644 (+14events/+7432B), after/restore
equal, gen17/next4127, history204800/ctx2097152/clip458752 unchanged.
Default installer not promoted. Current allocation fix retained; broad goal
active, not accepted. Prior diagnosis is progress, no external blocker.
Report docs/VOICE_SPLIT_WORKSPACE_REPORT.md. Evidence/measurements/failed
attempts: artifacts/voice-fast/split-workspace-ux126/. Diagnostic speed analyzer
correctly refused shadow capture; no bypass. Next: source/ASR end coordination
before committing actions, then late candidate connection and argument error.

UX127 started: remote-authoritative end for candidate/isolated capture, retaining strict local admission and hard cap; unchanged classic path. Previous UX126 = verified allocation progress, normal corrections still0/3. Plan/source snapshots artifacts/voice-fast/remote-end-ux127/.

UX127 closed: REMOTE_ONLY_END_REJECTED_AND_WITHDRAWN_WITH_EVIDENCE.
2026-09-28: finite remote-authoritative endpoint experiment166; strict local
admission, revocable timed final,160ms coverage/observation, bounded pending
hold and10s source cap. No source/ASR capacity, energy/model/volume, history or
partition changes. Source acquisition's earlier local-energy bound disabled
only for this mode. Previous classic path unchanged. Host66/66 ASan/UBSan
passed34.43s; archived trace decoder3/3. Six retained400-frame captures replay
old behavior6/6; new policy ends one7720ms, five unknown at retained8sEOF;
no future audio invented and no latency acceptance from shadow captures.
One normal3-turn correction group: first wakes3/3, exact full inputs0/3,
correct green1/3; outcomes clarification(lights off),timeout,green(0,255,0).
No retries for score. Third raw clip126592samples7.912s, local ASR disagrees
with cloud; duration alone is not source fidelity. One normal3-turn greeting
group:first wakes3/3, all timeout, no final input/answer accepted. First two
ASR final texts at4727/4510ms after capture; timeout10371/10351/10417ms.
Exact failing timed/source/end guard still unproven (no per-frame trace).
Greeting candidate PCM received but discarded after timeout; no stale speech
or predicted GPIO effect. Correction external alignment failed fixed gates;
greeting no answer onset. No one-second claim. No observed reboot/WDT/panic/
DMA loss; boot minheap33828B still fails48KiB, includes both groups.
1661495392B (+128) within1540096 budget. On failed board gate, preserved
complete source/app and changes.patch, checked frozen hashes, withdrew only
13 experiment files. Current382C/header/build files match164; rebuild app
1495264B SHA589be96d57c24807d2bb2e8406515630d22d5b6be192f884c0a632a5b0e86a76
exactly matches UX126. Related4 restored checks pass0.14s. Noaudio1011296B
unchanged; diagnostic flagsOFF. No default installer promotion. Both verified
full backups/app readbacks/non-app equality:20260928-102143 and102806.
Restored72 WiFi/fast/capture/listening/reuseON/prefetchOFF/lightOFF. Context
1029/912644 ->1034/915332, gen17/next4132, after/restore equal. History204800,
ctx2097152, clip458752 unchanged. Recorder/USB/flashing processes closed.
Report docs/VOICE_REMOTE_END_REPORT.md; artifacts/voice-fast/remote-end-ux127/.
Goal remains active: this is a rejected hypothesis with a verified rollback,
not an accepted UX improvement or an external blocker. Next isolate the
already-finalized text's rejected end guard and source quality; candidate
connection still late. Do not replace diagnosis with fixed waiting or broad
energy-threshold relaxation, and do not repeat failed groups for score.

UX128 started: bounded final-end guard and TLS clock diagnosis; previous UX127 is rejected-policy evidence with verified rollback. One3-turn diagnostic, no threshold/volume/capacity changes; plan artifacts/voice-fast/end-guard-ux128/.

UX128 closed, 2026-09-28: finite diagnostic completed, UX acceptance still open.
One3-turn diagnostic: first wakes3/3, full inputs/business2/3; exact production
guard replay3/3. The failed turn finalized segment1 then received segment2
26ms later, revoking its endpoint; segment2 never finalized before10s cap.
Do not ignore later segments or label all weak input as noise. TLS signatures
took4080/4072ms wall across3calls per completed handshake; CPU/key-size cause
unproven. Existing speculative reply preparation must remain revocable until
complete input validation; no predicted GPIO action, no speed claim from this
trace build. Host67/67 passed; minheap45288B fails48KiB. Diagnostic source
archived/withdrawn, normal164 binary hash identical and4trace flagsOFF;
device72 restored fast/capture/listening/reuseON/prefetchOFF/lightOFF.
Data partitions/history unchanged. Report docs/VOICE_END_GUARD_REPORT.md;
evidence artifacts/voice-fast/end-guard-ux128/measurements.json. Next bounded
work: attribute actual TLS verification work and resolve pending-segment end
without dropping real continuation. Goal remains active, no external blocker.

UX129 started: finite per-verification/key-size/scheduled-runtime attribution; prior UX128 was exact causal endpoint evidence and verified rollback. Plan and source snapshots artifacts/voice-fast/tls-verify-ux129/. No TLS validation or data-capacity changes.

UX129 closed,2026-09-28: public-key optimization retained in170 source;
whole voice objective remains active. Fixed bignum source/hash, public API
dispatch preserves<=3072bit hardware and secret software path; larger public
moduli use upstream public-exponent path. No custom arithmetic, vendor edit,
TLS trust/padding change, priority/endpoint/model/volume/capacity change.
One cold verification comparison:RSA4096 task runtime332312->221993us,
wall2019->1903ms; not a general latency guarantee. Host66checks and140
independent32-bit-limb math/RSA positive/negative cases pass. Three groups
of3turns (baseline diag, optimized diag, normal): first wakes/input/answers9/9,
no observed reset/WDT/panic/DMA. Normal actual-answer onset estimates
1.955/1.734/1.594s, minheap46840B: one-second/48KiB still fail. Long-input
and correction faults were not changed or reaccepted. Source170 retained;
normal app1495408B (+144), noaudio1011440B (+144), normal diagnostic flagsOFF.
Device72 restored fast/capture/listening/reuseON/prefetchOFF/lightOFF; same
2MiBcontext/204800history/448KiBclip. Evidence docs/VOICE_PUBLIC_MPI_REPORT.md
and artifacts/voice-fast/tls-verify-ux129/. Next: early-ASR/candidate trigger
and remaining scheduler wait, while preserving complete input and revocation.

UX130 implemented: speculative text pipeline and request-scaffolding gate.
2026-09-28. Validated tool-free text sessions send item/create consecutively;
exact text/new-ID acknowledgement is still mandatory before response/PCM.
Original final-input/action/cancellation/identity guards preserved. Existing
ASR may deliver useful words near/after VAD; this is not a general1s fix.
171thirdgreeting nominated THINK on 请用一句话介, then revoked on 介绍.
172 excludes leading politeness/format scaffolding from generic information
count. Same13real previews through old/new actualC: old kind4 at50610 revoked,
new kind1 at51143 final_match. No predictedSELF from generic prefixes.
Host66/66 ASan/UBSan34.42s; one verifiedWSS3turn pipeline with78ms-before-ack
requests,407/468/468ms firstPCM. Actual73events/151040PCM C replay exact.
Initial host diagnostic ABI rejected two clocks in borrowed arena; fixed by
existingrequest clock+one4B coordinator ack clock, arena16576 unchanged.
No resource budgets, tasks, capacities, model, endpoint, TLS validation change.
Two distinct source normal3turngreeting groups:1713/3fullinput/wake,2/3
candidatehit, onsets1.864719/1.625031/6.350719s;1723/3all,1.868719/1.456031/
1.251719s. Fixed alignment accepted, localASR nonempty actualanswers; not
randomized comparison or universal speed guarantee.172warm nomination to
request18/20ms, ack74/73ms later. One3turn correction: wakes3/3,fullinput1/3,
green2/3. First missingrepairtail clarified/no effect; second missedleading请;
third complete. First2raw boardclips retained before overwrite; diagnostic
export gaps exclude thisgroup from latency/rapidrearm acceptance. Initial
correctioncommand usedwrongfixture manifest:zero playback/modelturns,finally
voiceoff; kept failure, then ran originalcorrectionfixture onlyonce.
Nine realturns no observed WDT/panic/reset/DMA; minheap17145244,172greeting46936,
172whole43980B fails48KiB. Fullgoal remains active and unaccepted. Apps:
1721495760B(+352vs170), SHA7ec29a9935ef88ab6cf8099f03439720464878444e8ca6c24e7c5debf7f69fbe;
noaudio1011440B SHA970d8160cc9dd9c20f0d02dbc868fa98d09451aeca041a2a2015a60409227430 unchanged.
All4diagnostic flagsOFF, PUBLIC_PERFON. Frozen888files sourceSHA
b63ed79d85430f8f395e9f8d33131c4fcd1b1168c348d97593f569e86bc4bb7f.
Context1056/925216->1074/933884,gen17,next4172,lastlocal31244; capacities
2MiBcontext/204800history/448KiBclip unchanged. Defaultinstaller unchanged.
Bounded plan/amendment, raw receipts, C replay, build/test/failure/flash logs,
latency CSV and final audit artifacts/voice-fast/candidate-pipeline-ux130/;
report docs/VOICE_CANDIDATE_PIPELINE_REPORT.md. USB/audio resources released
at end; device72restore verified separately below. No retry loop for scores.

UX130 closed 2026-09-28T12:20:15.000016+00:00: SOURCE172_PIPELINE_AND_SCAFFOLDING_FIX_RETAINED_GOAL_UNACCEPTED. 628 code/build/Python files match frozen172. Verified full4MiB backups, app-only reads and non-app equality: kws-voice-flow-20260928-120129, kws-voice-flow-20260928-120906, kws-voice-flow-20260928-121703. Restored72 WiFi/fast/capture/listening/reuseON/prefetchOFF/lightOFF, context1074/933884/gen17/next4172 equal after workload. No default installer promotion. USB and recorder test owners joined. CSV latency-171-172.csv and measurements.json preserve all9realturns; zero-play fixture failure separately retained.

UX131: bounded retained-input diagnosis supports the user's asynchronous
intent/candidate preparation request. A candidate remains advisory until full
input agreement; source-frame endpoint evidence and wall-clock ASR events must
not be conflated. One907-frame fixed-PCM USB batch:401-frame historical control
exact,173/333-frame UX130 captures with valid input/output CRCs. No new capture,
speaker playback, provider call, model change, threshold sweep or normal build.
Frozen139 filter parity is exact except an inactive capture-probe block; normal
macro0 preprocessed source is identical. First preflight hash refusal retained.
CurrentC/receipt-time replay matches8/8 full-capture terminal fields; truncated
capture does NOT match. A separately labelled inferred pending-clear ordering
before source3440 reproduces8/8 but is not a measured frame-mailbox trace.
Independent localASR confirms firstclip ends after不对; secondclip contains full
repair including绿色. No future PCM invented. No firmware policy change justified
by this limited clock reconstruction; source172/defaultinstaller unchanged.
139probe and72restore both full4MiBverified/app-only/readback/nonapp equality;
flash remained identical during USB replay. Context1074/933884/gen17/next4172,
2MiBcontext/204800Bhistory/448KiBclip intact. Restored72fast/capture/listening,
WiFi/reuseON/prefetchOFF/lightOFF. Evidence correction-notice-ux131; report extends
docs/VOICE_CANDIDATE_PIPELINE_REPORT.md. Goal active, no one-second acceptance.

UX132: asynchronous intent preparation requires complete-input authority.
Partial ASR may nominate one tool-free candidate and cache speech while input
continues. A candidate cannot stop capture, execute GPIO, commit history or
play before final validation. Repairs/negation/added constraints revoke stale
predictions. Existing implementation retained; revocable capture-end handoff
is a future design item, not an implemented or accepted capability.
One fixed8s shadow group173: first wakes3/3, green task3/3, exact input1/3;
not latency/rapid-rearm acceptance. Actual1200source frames and consumed
notices reproduce11terminal fields per turn. First/third pending clear at
3240/3260ms followed by stop3260/3280ms; real future continuation4020/3940ms.
One offline periodicity prototype on2102fixed frames rejected: still loses
both tails and adds220ms to complete input. No threshold grid or policy merge.
Seven relevant ASan/UBSan checks and3trace-parser checks pass. Diagnostic
minheap47164B fails48KiB. Normal172 rebuild1495760B byte-identical to frozen
172; four diagnostic flagsOFF, PUBLIC_PERFON. Two guarded app-only flashes
with verified4MiB backups/readback/nonapp equality. Restored72 WiFi/fast/
capture/listening/reuseON/prefetchOFF/lightOFF. Context1080/937472,gen17,
next4178 equal across restore;2MiBpartition/204800history/448KiBclip unchanged.
Report docs/VOICE_FRAME_ORDER_REPORT.md and frame-order-ux132/closure.json.
Goal active: no subjective/full-response/one-second acceptance claimed.

UX133: model preparation must not determine input completeness. One offline
1000ms cumulative pending-output grace studied on2102fixed sourceframes.
Always-pending output recovered source continuation but ready output preserved
both truncations; complete input/RF18 waits grew1020ms. Policy rejected, no
producer integration/firmware change. Actual cold firstPCM wall clocks are
only a labelled projection, not source-consumption or latency acceptance.
Separate bounded amendment:3retained8srecordings+700ms identical silence,
464sample paced blocks, currentQwen3ASR vs existingFunASR,3calls/model,no retry.
900blocks/model exactly equal. Fun lamp firsttext later984/1312ms, earlier406ms;
repair earlier1048/671ms in rounds1and3 but misrecognized in2; exact final1/3 eachmodel.
Fixed order/recorded synthetic source, no population or device speed claim.
CurrentC parses all208events/6sessions, exact final text; Fun old adapter emits
only1partial callback per session despite8/9/7nonempty previews. No model swap.
Device72 read-only verified WiFi/fast/capture/listening/reuseON/prefetchOFF,
context1080/937472/gen17/next4178 unchanged. New evidence and failed wrappers
retained in provisional-end-ux133; report VOICE_PROVISIONAL_END_REPORT.md.
Full goal remains active; no new firmware, memory or acoustic acceptance.

UX134: reject clean-only threshold calibration as the truncation fix. Three
fixed ablations over2102frames leave both UX132 early ends unchanged3260/3280.
Existing floor240 and no-clean arms delay complete input/RF18; no threshold grid.
Exact C filter levels and baseline state parity verified. A separate bounded
174 diagnostic adds only CAPTURE_PROBE-gated256x8B prewake records plus count,
source-owner timestamps/CRC and a read-only clock. Existing150ms USB budget;
no normal runtime allocation or input-policy changes. Three correction turns
with2500ms prewake idle markers are ineligible for latency/rapid-rearm acceptance.
Room-marker noise124/128/102 vs frozen179/131/163;654 reconstructible estimates
match the actual32-frame third-lowest estimator. Not a general language effect.
Same-notice projections3220->6660,8000active->8000active,3440->3440: partial cause
only, no production threshold change. First wakes3/3, green task2/3, exact input0/3,
one8s forcedLIMIT. Only valid clips1and3 retained; round2 metadata kept without
substituting oldPCM. LocalASR full/prefix/tail disagreement explicitly preserved.
Whole diagnostic minheap44600B fails48KiB; no observed WDT/panic/reset/DMA loss.
Host7/7, trace parser3/3, ring boundary checks and new shadow replay3/3 passed.
Fixed host-only8s forcedLIMIT omission; initial failedparity retained, corrected
33/33 terminal fields match. No repeat hardware run to replace the timeout.
Guarded flash/restore with verified4MiB backups and nonapp equality. Restored72
WiFi/fast/capture/listening/reuseON/prefetchOFF/lightOFF. Context1084/939864/gen17/
next4182 exact acrossrestore;2MiBpartition/204800history/448KiBclip unchanged.
Normal172-label rebuild1495776B(+16B vs frozen172), distinct SHA d63f16677a5edfc0
1eebebbd326ae2a286adf71182303887513713e3aa6ed927, not installed/promoted. All four
diagnostic flagsOFF, PUBLIC_PERFON. Report VOICE_NOISE_SCALE_REPORT.md and
noise-scale-ux134/closure.json. Full goal active; input/end/one-second still open.

UX135: reject direct SDK NS integration at the existing RAM budget. USB-only
REPLAY_NS is defaultOFF, baseline original-filter MODE2 and a separate NS/VAD
stream. Pin vendor3473d612da471232b8f5ce69470d3ef73aa706d4c4b2372ff626164c9d619d79.
Header advertises10/20/30ms but175 ns_create20 asserts frame_length_ms==10 and
reboots. Preserve failure; one explicit API amendment creates10ms in176 and
processes two160-sample blocks for each unchanged20ms VAD frame. No mode sweep.
32digital-silence frames complete: NS41552B > predeclared8192B; eachVAD760B;
NS median3037us/max3134us per20ms at160MHz, not speech/concurrent worst case.
Single-teardown heap delta88B unclassified. Stop on RAM ceiling: planned8clips
executed0, device12protocol checks not run, no suppression/recognition claim.
Existing host protocol6/6; diagnosticON/OFF builds, three guardedfull4MiB
backup/appwrite/readback/nonapp checks. 175assert is one observed diagnostic
reset, not hidden. RestoredwholeFlash equals original. 72 WiFi/fast/capture/
listening/reuseON/prefetchOFF/lightOFF. Context1084/939864/gen17/next4182 exact;
2MiBcontext/204800history/448KiBclip preserved. REPLAY_NSOFF/noNSsymbols; normal
fourdiagnosticflagsOFF, production sources and172-labelapp unchanged. No cloud,
recording or live-dialog trials. VOICE_NS_FEASIBILITY_REPORT.md, ns-ux135/closure.
No production promotion; fullgoal/inputintegrity/one-second/minheap remain open.

UX136: keep an independent three-of-eight boundary repair, not the noise-floor
prototype. Existing60ms grace may wait for a fourth vote within160ms after a
short gap. No new quota, quiet reset, threshold, state, buffer or classic change.
Regression fails old740ms terminal and passes fourth-vote/no-vote/cancel cases.
12related hostchecks pass; original10clip/3302frame baseline unchanged. Source
producer remains conservative. Normal1771495792B(+16), SHA
f07afd7d584a412d85d8c5aa345f6ec2c911c154afc25813a85980daa1ad9ff3.

Prior finite noise prototype128frame/rankceil(3N/32)/floor32:40000 sorted-reference
checks,10savedclips/3302frames, original baseline exact. OnlyUX134 has prewake
samples; no history fabricated for others. Protected noise126/132/110 vs179/131/
163, joint ends6660/3980/6620. Boundary fix removes3980 cut but leaves8s active.
Backgroundtail stillactive; no noise rejection or speech generalization pass.
Do not integrate the longer noise window/floor32 from this evidence alone.

Two normal3turn groups, no trace/capture-export/initial-ready or interturn waits,
existinggain0.25, wakeszh/yue/zh, businessMandarin. Firstwakes6/6; correction
input/task0/3, blue readback3; greeting3/3. Gap266/281 and281/265ms. No observed
WDT/reset/DMA loss. Same-boot historicalminheap43732 fails49152, not independent
pergroup peak. Networkstack2112; candidatestack2328/2376/2376. Greeting external
waveform gates pass; onset2.250/1.513/1.428s, not1s; localASR saysxiaoyan asxiaoyan
homophone, retained rather than subjective pass. Internal end->play847/499/554ms.
Warm nominations only60/24ms beforelocalend, request->PCM526/440ms; coldprimary
open4148ms and request->PCM438ms. ASR connection1208/1160/1127ms, start1325/1315/
1228, firstpartial2583/2662/2592. Device event time is not phoneme/source timing.
Next investigate ASR connection/upload/first usable draft overlap without
weakening complete-input admission, resource budget or known noise controls.

Two guarded4MiB backups/appwrite/readback/nonapp passes, restored72 fast/capture/
listening,reuseON,prefetchOFF,lightOFF. Context1084/939864->1097/946204/gen17/
next4195, persistent fields equal acrossrestore; last-request RAM counters reset
to0. First closure incorrectly compared those3ephemeral fields; corrected after
source inspection, initial failedlog kept.2MiBcontext/204800history/448KiBclip
unchanged. AllfourdiagnosticflagsOFF; normal177 source retained, defaultinstaller
unchanged. VOICE_THREE_VOTE_REPORT.md and relative-noise-ux136/closure.json.
Goalactive: input integrity, stable1s, memory and whole voice acceptance open.

## UX137 — 2026-09-28 predictive readiness, bounded configuration trial

Existing177 firmware unchanged. Idle preconnection + prefetchON/reuseON:
3 greeting and3 correction turns, gain0.25, zh/yue/zh wakes, no ready wait or
extra interturn delays. Firstwakes6/6; gaps265/266ms each group. Greeting3/3,
external reply onset candidates0.955719/1.066375/0.964719s. Historical same177
capture comparator2.250375/1.513375/1.428063s; not randomized, no fixedspeedclaim.
ASRopen1205/1157/1124->0/0/4ms (warm handoff, not TLSduration). FinalASR->play
144/176/163ms. FirstPCM still82..158ms afterfinalASR; do not claim full response
prepared before speech completion. vad_end means upload-loop end, not micend.
Correction input/task0/3: first/third trailing不对 candidates rejected, middle
lost repair and executedblue. No falsegreen/pass; unresolvedinputintegrity.
No observed reset/WDT/DMA. SDK bootmin62140 aftergreeting,43940 aftercorrection;
48KiB overall gate open, historymin not an independentgrouppeak. No new buffers.
App1495792B SHA f07afd7d584a412d85d8c5aa345f6ec2c911c154afc25813a85980daa1ad9ff3.
2 guardedfullFlashbackup/readback/nonapp-equality passes; restore72-summary,
fast/capture/listening,reuseON,prefetchOFF,lightOFF. Context1097/946204->1109/
951772B/gen17/next4207; preserve2MiB/204800Bhistory/448KiBclip. Source/default
installer unchanged. Report docs/VOICE_PREDICTIVE_READY_REPORT.md;
evidence artifacts/voice-fast/predictive-ready-ux137. Goal remainsactive.

## UX138 — 2026-09-28/29 rejected noise-history integration

Normal178 integrated fixedUX136 128frame lowpercentile/32floor for fast mode,
classic32frame thirdminimum and240 unchanged. Shared128uint16 history,136B
logicalstaticincrease, app+256B. Producer bound updated with consumer floor.
40000 independentpercentile/classicparity and1000producerconsumer cases;
11ASan/UBSanchecks pass,3302savedframes and3prewakehistories matchprototype.
Two normal3turngroups idle/prefetchON/reuseON,gain0.25,no extra waits/trace.
Firstwakes6/6,gaps281/281ms correction,266/265ms greeting. Correction input/task
1/3 green;othersblue. Thirddeviceclip2.936s vs5.895s source retainedaftergroup.
Greetingcore3/3 butstrictinput1/3; diagnosticexternal7.229/1.146/0.900s,
firsttwo ineligiblefor complete-inputspeedacceptance. No retryforgoodscore.
NoobservedWDT/reset/DMA. SDKsamebootmin43920->41768B;48KiBgatenotmet.
Reject/noiseintegration rolledback9source/configfiles to177; experimenttest
archived,178frozen. Routecounterexample explainsanother slowpath: partial
你用一句话介绍 consumesTHINK becauseleading你 nothandled; actualC replay
reproduces finalprefixmatch butunusabletopicreply, whereas请variantselectsSELF.
Routefix NOT mixed into this trial. Next repair prelude parsing with complete
finalobject/negation/extra-action validation; not another blanketthresholdcut.
178testedapp1496048B b067b21d9f25994e13b630af51d7cf191df79e2066315a584f9c942ffd9547be.
sizewithout --no-ccache triggeredrebuild;untested178hash1284f095ed2dd49c7a5153a3f93fdf9ddbdb43a2300f5b8e454728fa76222f98,
only68buildmetadata/checksumbytes differ. Restored177build1495792B
13d600cc6662d31653612c287ca3cd0e94f19769b95aa6e5d100398a7bc54001; only69
buildmetadata/checksumbytes differfromfrozen177; coherentELF/bin, notoldhash.
Two guardedfullFlashbackup/write/readback/nonapp passes, restore72 listening,
fast/capture,reuseON,prefetchOFF,lightOFF. Context1122/958456B/gen17/next4220,
2MiB/204800history/448KiBclip preserved. VOICE_NOISE_HISTORY_TRIAL_REPORT.md,
noise-history-ux138 evidence. Goalactive, no productionpromotion.

## UX139 — 2026-09-29 second-person introduction prefix

Retain narrow C11 introduction grammar for optional你/您, excluding these lead-ins
from generic-topic counting; preserve oldidentityquestions, exactfinalobject,
capture/language/output checks and cancellation/revision/extra-action rejection.
ActualUX138 draft sequence fails old candidate+intent tests;12 targetedASan/UBSan
checks pass afterfix. No new audio buffers/tasks or VAD/noisepolicy changes.
Normal179 app1495872B (+80), SHA38757c9c1c9b7e98a94bdda777a5b07b62795f2eae447e295191e8a37e133bc8.
One3turn syntheticgroup, localHuihui second-personfixture, gain0.25,
idle/prefetchON/reuseON;firstwake3/3,gaps266/265ms. Fullinput0/3; firstASR
动物进化介绍 gavewrongtopic; later2lost你 butintroducedself. Modifiedliteral
branch0/3 actuallyreceived; no claimedhardware proof of its latency benefit.
Externalalignment0/3 failedfixedgates; latencyunknown, no relaxedgates/retries.
InternalfinalASR->play6754/350/564ms only; prepareafteruploadend198/58/191ms.
Lastclip2.968s retainedaftergroup, source3.055s; independentASR alsoomits你
onboardclip, butthisaloneisnotproofwhichsampleslost. Originalfailuresretained.
No observedWDT/reset/DMA;bootmin41608B<49152,networkstack2112. FourdiagnosticsOFF.
Two fullFlashguard/application-only/readback/nonapp passes;72 restoredWifi/
fast/capture/listening/reuseON/prefetchOFF/lightOFF. Context1128/961460B,
gen17/next4226;2MiB/204800history/448KiBclip preserved. Defaultpackage unchanged.
Report docs/VOICE_REQUEST_PREFIX_REPORT.md, request-prefix-ux139/results-02.json.
Currentcode179 narrowfix retained; experimentnotpromoted, goalremainsactive.

## UX140 — 2026-09-29 actual source attribution and zero-padding fix

Three manual7s boardcaptures on72, identicalHuihui gain0.25,500ms warm sampling,
outputclock ABA(off/on/off). Eachsimultaneously recorded outputloopback/RAW/
DirectShow/board. All12 content/template checks passed; noDMA/ADCclipping.
Not wake/VAD/cloud or latencyacceptance; no supportfor outputhandle loss.
One normal3turn dialoguegroup onunchanged179, idle/prefetchON/reuseON,
sameprompt/gain,wakeszh/yue/zh,gaps266/265ms. Wake3/3,selfintro3/3,fullinput0/3.
CloudASR lacks你twice andthirdsays动计划. NativeRAW44.05s complete, no packet
gaps/errors/clipping. Three outputpromptspansinternallycontinuous,match>.9,
localASRcomplete; RAWtimestampboundedcropsalsofull. Wholeloopback5positiongaps
remainsFAILED; no padding/reclassification. Acousticanchor.53<.8 remainsunknown.
Lastboardclip48064samples/3.004s alsoomits你offline; missingASRword isnotproof
ofsampleloss. WrongdraftTHINK correctlyrevoked afterASRrevision; preserveguards.
Fix align_repeated toexcludeonlyexactzeroedgepadding with10ms margins;
allnonzerosincluding1LSB retained, offsetsreturnedtooriginalplaybackclock.
Test-first loudcue-in-zero-tail counterexample;15 targetedPythonchecks pass.
139/140 newpaddingawareanalyses stillunknown (1/3 copies each); no speedclaim.
One fixedexistingCfilter gain1 on2wakeclips+1manualcontrol nothelpful; notdeployed.
Firmware/app unchanged1495872B/SHA38757c9c1c9b7e98a94bdda777a5b07b62795f2eae447e295191e8a37e133bc8.
Bootmin45072<49152;networkstack2112;noobservedWDT/reset/DMA. Two fullguarded
appflashes/nonapp-equality pass;72 restoredWifi/fast/capture/listening/reuseON/
prefetchOFF/lightOFF. Context1134/964164/gen17/next4232; persistentrestoreequal,
2MiB/204800history/448KiBclip kept. ReportVOICE_INPUT_CLOCK_REPORT.md.
Currentgoalactive; nextboardinputquality diagnosis, no new broadthresholdgrid.

## UX141 — 2026-09-29 capture output lifecycle

Experimental180 defaults capture_output_hold=false: release PDM/PA after the
wake cue inside the existing ADC restart boundary; reopen normally for output.
Keep the explicit hold override, current VAD, partial/final intent guards,
streaming providers, context and storage budgets. No added allocation or task.
Saved PCM and one matched three-turn179 off trial support this change: last
capture prefix-window levels lower by4.48dB at1-2kHz,6.84dB at2-3.5kHz; not a
population SNR/accuracy claim. Strong speech level remains similar.
Introduction candidates used3/3, strict full input1/3, actual response content3/3;
acoustic onset candidates0.706/1.087/0.666s, not stable1s acceptance.
One default180 three-turn correction group confirms output_hold=false without
a command override: green2/3, first premature endpoint asks for color with no
effect. Full input1/3; correction acoustic alignment unknown. Six first wakes
hit, no observed WDT/reset/DMA. Boot min heap62152/43600B;48KiB gate incomplete.
Retain180 source/app1495888B, staticDRAM155860B, no installer promotion.
Four targeted ASan/UBSan checks pass. Three full backup/app-only/nonapp guards
pass; device restored72 fast/capture/listening, context1146/970128B/gen17/4244.
Report docs/VOICE_CAPTURE_OUTPUT_REPORT.md; input-quality-ux141 evidence.
Next address the remaining weak continuation, not another output toggle sweep.

## UX142 — 2026-09-29 continuation evidence and rejected credit

Keep asynchronous partial-ASR candidate generation, fixed audio cache and final
input/capture/language checks; no early GPIO effects. Long-input retention is
still required before a speculative answer may be trusted.
One181-protocol-diag group retains8s/128000samples per turn plus exact source
metadata and consumed ASR notices. First wakes3/3, green corrections3/3,
strict full input1/3; latency and rearm acceptance explicitly excluded.
Current C reproduces all11 shadow terminal fields on1200frames. R2/R3 stop
3300/3240ms despite a late three-frame strict run; pending quota still unused.
One private C hypothesis charges the quiet interval against the same1000ms
quota when that run has local onset and current text. Across13 saved traces,
4502frames, short controls unchanged and digital silence rejected; new r2
still ends3980ms before the repair finishes. Reject the hypothesis. No normal
board trial, source-bound edit or production endpoint change. Known background
false admission remains a failure; offline notice scheduling is counterfactual.
Normal180 rebuilt with all four diagnostic flagsOFF, identical1495888B/hash
28f6cd732d7ff1cd2fe7df1900a9cc058f2c2ed7dbbf3a3e92a77ac658567d78.
181 app1498272B, accepted=false. SDK diagnostic bootmin38512B fails48KiB.
Two full4MiB backup/app-only/nonapp guards pass; restored72 listening with
reuseON/prefetchOFF. Context1152/973712B/gen17/next4250, restore equal.
Preserve partition/history/clip/default installer. Report
docs/VOICE_CONTINUATION_EVIDENCE_REPORT.md; continuation-ux142 evidence.
Goal remains active; no stable1s or complete-input acceptance claim.

## UX143 — 2026-09-29 experimental late-band continuation

Experimental182 adds a fixed HP1600/Q0.707 Q30 feature on the existing clean
stream. Initial nonspectral median calibration requires8 frames, uses at most32,
skips the two settling frames, and freezes at the first original strict four-vote
event or32 calibration frames. No added capture wait. The feature requires2x
band amplitude, floor32, spectral speech and expiry of the ORIGINAL320ms strict
support window. Consumer still requires local onset and current ASR text.
It neither admits onset nor authorizes a candidate reply/tool action.
Six-byte A7 metadata carries the extra bit; legacy A6 remains readable. PCM,
vendor classifier input, raw/clean energy, current700ms endpoint and quotas stay
unchanged. Producer includes every possible band vote in its conservative bound.
An independent old boundary bug is test-first reproduced: at a320ms weak reset,
producer6000ms could precede consumer6020ms. Shared700/60ms constants now make
the fast bound include the weak window; deterministic fixed bound6080ms.
Normal build1496848B, staticDRAM155972B (+960/+112B), no heap/task/audio-buffer
addition. Six sanitizer checks pass; ten saved captures3298 produced frames
match the frozen prototype's11 endpoint fields and original signal metadata.
Original broad feature delayed RF18 by220ms and was rejected. Restricting it to
late continuation restores both short-control timings. New full captures end
6600/6060/6480ms with local ASR content matching their full recordings, including
retained ASR mistakes. Old UX132r1 remains truncated; r3 endslate7820ms. No general
accuracy or latency claim. Board validation pending; default installer unchanged.

UX143 closure — Reject182 after exactly one normal3turn board group.
First wakes3/3; complete final repair/green effect1/3. First two captures3944/
3992ms end before green; third6644ms complete. No early blue action observed.
Candidate PCM precedes VAD by1135/1366/3631ms, but all candidates rejected.
First two replies falsely call the off lamp green; keep this separate failure.
No observed WDT/panic/reset/DMA; SDK bootmin37552B fails48KiB. Acoustic alignment
fails fixed gates: latency unknown, no1s claim. No repeat playback group.
Withdraw band/A7 source against frozen182 and exact before-source snapshots.
Retain only independent weak-support source coverage fix; version183. Six
ASan/UBSan checks pass0.28s, includes1000 asynchronous sequences and old6000/
6020ms counterexample, fixed6080ms. Normal1495920B/DRAM155860, no addedRAM;
noaudio1011440B builds.183 not board-tested as a whole and not promoted.
Two fullFlash guards pass; restore72 listening/fast/capture/reuseON/prefetchOFF.
Context1158/976756B/gen17/next4256; persistent values equal across restore.
Report docs/VOICE_CONTINUATION_BAND_REPORT.md; goal incomplete.

## UX144 — 2026-09-29 live light state before model response (in progress)

183's verified source-bound fix retained. Do not repeat rejected ASR drain105.
184 adds the light driver's RGB snapshot after history, alongside existing LCD
snapshot, before current user input. Failure is explicit unavailable, no old
RGB reused; later actual tool results override this initial state. Read only,
no new heap/task/buffer and no context/history budget change. Snapshot is not
persisted or replayed as history. Intended benefit: correct state and allow
simple state answers without another model/tool/model round. Measure it.
Test-first missing-snapshot assertion reproduced. Updated request/WAL tests
cover off/green/blue, get failure and absent callback, voice/USB, retry, tools,
long historical bodies and cache ordering. Three ASan/UBSan suites pass1.62s.
Normal and noaudio build pass. One fixed three-turn voice group planned.

UX144 closure — One group of3state questions, first wakes3/3. Core question
recognized each time, but all omit final brief-answer clause: exact input0/3,
rawcompletefalse andexit1 retained. Current off-state statements3/3 by literal
text review; first reply still adds unsupported historical causal narrative.
Read-only light_get calls1/1/0; DeepSeek requests3/2/1. Onlythird uses snapshot
without an extra tool; no baseline-controlled speed-improvement claim.
Narrow content checker corrected to separate truth/tool-free metrics; oldreport
retained. It still conservatively rejects first answer's historical blue mention:
automatic2/3, current-state textreview3/3, toolfree1/3. No extra playback.
Keep184 source:1496224B (+304), DRAM155860 unchanged. Threehost suitespass,
normal/noaudio builds pass. SDK bootmin43340 fails48KiB; acoustic latencyunknown
because fixed alignment gate fails. Candidate receipts hit3, but formalmodel
starts only after workercompletion/arena restore; this serial handoff is the
next concrete lifecycle question, not an invitation to alias two live owners.
Guarded72restore/listening verified, persistentcontext1166/981228/gen17/4264
matches before restore. Fullgoal incomplete; reportVOICE_LIGHT_SNAPSHOT_REPORT.md.

## UX145 — 2026-09-29 overlap only TLS preparation (in progress)

184 live-state fix retained. Existing69/70 full-request overlap evidence includes
network failure and low memory; do not repeat that ownership design unchanged.
185 adds one connect-only DeepSeek preparation on the existing main network
worker while an admitted task receipt streams. No HTTP headers/body, generation
request or effects during warming; formal request still waits for safe candidate
join. Skip direct answers, rejected input, local effects, Gateway and cancel.
Bound preparation to1000ms and one attempt/turn. Reuse only the same fixed-origin
SDK client; failed/expired preparation cleans up. Ordinary TLS verification and
final request credential refresh remain. No task/buffer/context capacity change.
Public IDF open sends headers, so a normalized-source-SHA-pinned translation-unit
shim exposes async connect only, restores is_async on every step, and fails
configuration if vendor implementation changes. Installed SDK stays untouched.
Four host sanitizer suites pass23.28s. Plan: normal/noaudio builds and one3turn
normal device group matching144, without repeated playback for a better score.

UX145 closure — Retain185 experimental connect-only path, not promoted.
One normal3turn group: warm used rounds1/3,716/917ms; both finished730/1395ms
before candidate completion. Formalfirstconnect4/625/3ms vs184628/633/632.
Only connection overlap proven, no controlled end-to-end speedup. Firstwakes3,
currentoffstatements3, no effects; readonlyget1/0/0, modelrequests2/1/1.
All3omitbriefsuffix: exactinput0/3, rawcompletefalse/exit1. Acousticunknown
because original fixedalignment gate fails. SDKmin39876 fails48KiB (prior43340),
no claimed memory improvement; networkstack1952/candidate2328, noWDT/reset/DMA/
underruns observed. ThreeSDKread0errno11warnings retained, no turnerrors.
Fourhost suites and CMake exacthash/change-rejection checks pass; normal1497088B,
DRAM155860 unchanged, noaudio1011824B. Guarded72restore ready; persistentcontext
1173/984876/gen17/next4271 unchanged across restore, original partitions/budgets.
Report docs/VOICE_HANDSHAKE_OVERLAP_REPORT.md. Full objective remains active.

## UX146 — 2026-09-29 bounded continuation for what-questions (in progress)

Saved source contains both clauses with880ms diagnostic low-energy gap;
saved144/145 device clips and independent local ASR contain only the first.
Existing700ms end and question pause classifier exclude what/咩, unlike why/how.
Extend only vocabulary under unchanged400ms cumulative phrase allowance,
1000ms shared pending quota, source bound and final-input action gate.
Known short self requests stay fast. No new model/task/buffer/threshold.
This is a finite completeness experiment, not a semantic end or1s guarantee.
One identical3turn state-question group; retain only if3/3 full input with no
new functional error, otherwise restore185 source. All failures preserved.
Plan/evidence artifacts/voice-fast/input-boundary-ux146/plan.txt.

UX146 closure — Reject186 after the sole fixed3turn group: fullinput0/3,
coreinput2/3, firstwakes3/3, protocol@done3. Firstwhat partial relative toVAD
+245/-1543/+105ms: two hints arrive after irreversible local stop. Thirdclip
2168ms, localASR also lacks color/suffix; heldquota0. Do not infer all other
rounds' internal quota from unavailable frame traces. Late-keyword dependence
and weak continuation remain; no more word/threshold sweep this stage.
CandidatePCM round2 precedesVAD461ms; round3 follows714ms. These are receipts,
not useful answers or acoustic1s. Currentofftext3, tools0, modelrequests1/2/1.
SDKmin38604 fails48KiB; worker2016/candidate>=2288stack. NoWDT/panic/reset/DMA/
underrun observed. Fixed externalalignmentunknown; no subjective listening.
186+32B/DRAMunchanged, normal/noaudio pass. Fourhost suitespass11.35s; first
new timing assertion corrected2480->2520, failure kept; rollback4pass11.32s.
Restore3changedCfiles exactly against saved185,382C/H and rebuilt185appmatch.
Guarded72restore/listening verified; persistentcontext1179/987692/gen17/4277
identical across restore. All budgets/default installer preserved, USBfree.
Report docs/VOICE_INPUT_BOUNDARY_REPORT.md; candidate186 rejected and archived.
Full goal remains active, not complete.

## UX147 — 2026-09-29 saved-input neural continuation reference (in progress)

146 late-keyword failure is new evidence, not a reason to repeat ADC/filter/
noise-floor experiments. Compile existing pinned project TEN C backend with
its original fixed DSP definitions for a finite seven-clip native diagnostic.
Use three full PDM-off captures and four old speech/noise controls, unchanged
PCM, no padding/gain/resampling. Existing0.4 probability threshold only.
No new training/model download/USB/firmware/cloud or acoustic test. Native
classification is not C3 parity/timing or a deployment proposal; old23.428ms
CPU peak per16ms hop is a known realtime constraint. Candidate endpoint use
requires independent evidence and resource proof. Plan/evidence under
artifacts/voice-fast/neural-continuation-ux147. Goal remains active.

UX147 closure — Existing pinned neural reference is not a supported direct
continuation replacement. Seven unchanged savedWAVs/2627full16msframes,
ASan/UBSan clean; partial tails128/192samples retained but not padded/scored.
Critical3000..3240ms regions positive at0.4:7/15,1/15,0/15; each7..8s tail0/63.
Third weakmax10034Q15 < oldRFtailmedian10393Q15, no scalar threshold search.
Regions are diagnostics, not human labels; no accuracy/F1 or universal claim.
Native allocated40772B, unchanged inference allocation/zero onclose; notC3RAM
or arithmetic parity. HistoricalC3NNpeak23.428ms/hop16ms prevents blinddeploy.
Conditional endpoint candidate not pursued; no training/firmware/toolchain
change. Source185, device72. Initialread encounteredqueued unsolicited voice
capture timeout; failure retained, secondread afterpreserving/drainingqueue
confirmsWiFi/fast/capture/listening/reuseON/prefetchOFF, context1179/987692/
gen17/4277 unchanged. Scriptsinitiatednoaudio/cloud tasks; do not claimthe
backgrounddevice made none. ReportVOICE_NEURAL_CONTINUATION_REPORT.md.
Goalunfinished; next inspectdirectcaptureI/O evidence, notrepeatPDM/RFreduction.

## UX148 — 2026-09-29 finite Flash/capture attribution
Diagnostic-only 4/80/4 sector preparation with bounded erase timestamps.
One fixed8s three-turn ABA; unchanged endpoint/RF/PDM/data layout. Not a wake
latency or real-time acceptance run. No autosuspend activation or threshold
sweep. Restore normal185 and guarded72 after evidence. Plan: artifacts/voice-fast/flash-capture-ux148/plan.txt.


UX148 closure — Reject erase suppression as a supported latency fix. One fixed
4/80/4-sector ABA,3captures/128000samples each. In-capture erases19/0/19;
tail RMS -38.30/-37.99/-37.18dBFS, middle+0.31/-0.81dB vs outer. No consistent
reduction; shadow endpoints6500/3080/3260ms, all11 replay fields match1200frames.
Preparation272.5/3956.4/269.5ms, not a speed improvement. Fullinput2/3, final
green3, firstwakes3, no DMA/clipping/reset/WDT. SDKmin44204 fails48KiB; fixed8s
and perturn exports exclude latency/rearm acceptance. FirstCformat build
failure retained/fixed; valid/invalid diagnostic command preflight passed.
Restore3files,397code/CMake match185 and rebuiltapp e23ba1df... byteequal.
Guarded72 restoredready, context1185/991288/gen17/4283 preserved. Report
docs/VOICE_FLASH_CAPTURE_REPORT.md. Goal remains active, no installer promotion.

## UX149 — 2026-09-29 candidate send stack
Preserve protocol/input capacities while framing text candidate payloads in
existing scratch with8B extraheadroom; eliminate2056B sender stackcopy. Tests
coverwireequivalence,mutablepayloadrestore,busy/cancel/partialwrite. First3turn
watermark gate>=3072 permits onefixed1536B stack/prefix reduction, followedby
one3turn validation>=1536. Fullgoal/input/latency checks remain independent.
Plan artifacts/voice-fast/candidate-send-ux149/plan.txt; guardedrestore72.

UX149 closure — One3turn greeting group passed fullinput/firstwake/candidate
reuse; acoustic source alignment accepted, answer-onset candidates0.876/1.096/
1.088s, not exactphoneme or stable1s acceptance. Request only252/266/7ms before
VAD; firstPCM191/525/483ms afterVAD. Sender frame2096->48 did not satisfy
wholeworker gate:2360/2432/2432<3072. No189/secondgroup/stackshrink. Reject
addedAPI/16B arena/336B app cost without releasableRAM. Restore14files and397
C/H/CMake byteequal185, rebuildsameSHAe23ba1df. Host16suites andnormal/noaudio
builds pass candidateandrollback. Device72ready/context1191/994024/gen17/4289
preserved byguardedapp-onlyrestore. ReportVOICE_CANDIDATE_SEND_REPORT.md;
goalopen, no defaultinstallerpromotion or controlledspeedupclaim.

## UX150 — 2026-09-29 streaming ASR provider feasibility
Currentincrementalsettings reviewed; no repeatof81/95/137drain/prefix trials.
Atmost6read-onlyASRsessions on3existingfull8sUX132clips: newofficial3.1streaming
then currentQwen controls, oncefile/model, identicalPCM700mspad/464blocks.
No thresholdsweep/newaudio/tool/firmwarechange. Measure partialcorrection/full
text andretainallerrors; no bilingual/device1sclaim. Newprovidercompatibility,
accuracy andlatency unproven. Planartifacts/voice-fast/asr-provider-ux150/plan.txt.

UX150 closure — Exactly6sessions complete/no retries. Identical900chunks/model.
New3.1firstlamp1047/1375/1093ms later; repair1001/absent/704earlier; green1219/
-187/641earlier. Strictfulltext current1/3,new2/3, includingmissingrepairinnew
round2. Rejectdirectproviderreplacement, no parametergrid/C3deployment claim.
207realreceipts replaythroughproductionC underASan/UBSan,all6match. Legacy
binaryadapter emits1partial perturnvs23/48/26providerpartials; currentisolated
pathunchanged. Source185/device72,context1191/994024/gen17/4289 preserved;
no builds/flashes/newaudio/tools. ReportVOICE_ASR_PROVIDER_REPORT.md,goalopen.

## UX151 — 2026-09-29 concurrent request memory gate
Before restoring receipt/request concurrency, one diagnostic3-turn group
reserves the unchanged full engine workspace while an admitted receipt and
preparedHTTP connection are live. <49152B free or allocation failure rejects
direct overlap; a passing lower bound does not prove a real concurrent request.
No body/tools or capacity changes in the probe. All original input/health gates
remain. Restore exact185 source and guarded72 after the group; no repeat/sweep.
Plan artifacts/voice-fast/request-overlap-ux151/plan.txt; goal remains open.

UX151 closure — One3-turn group, only1 live probe. Successful allocations left
41364 B (<49152 B), so reject direct full-workspace concurrency. No real body
overlap measured; other2 rounds had late candidates. Full input0/3 remains
failed.16 host suites and normal/noaudio builds pass. Temporary2source changes
removed;397 code/CMake match185, restored app SHA e23ba1df... exact. Guarded72
ready, context1198/997624/gen17/4296 preserved, all original capacities unchanged.
Report VOICE_REQUEST_OVERLAP_MEMORY_REPORT.md. First-request streaming with
deferred reply-state allocation requires host wire/lifetime proof before any
future deployment; no goal or installer promotion from this feasibility gate.


## UX152 — 2026-09-29 请求流式构造的前置实现
UX151 的并行完整工作区实测不足 48 KiB。本阶段先实现无额外大块缓冲的
JSON/消息写出、统一上下文前缀和直接用户文本持久化。改动前后的实际 HTTP
请求必须逐字节一致，并验证最大输入、历史、错误及取消；保留自定义适配器。
不在本阶段启用设备并行或声称速度改善。无云调用、音频试验或烧录，设备72
保持可用。通过主机与普通/无音频构建后冻结190，默认安装包不变。
计划：artifacts/voice-fast/request-stream-ux152/plan.txt；完整目标仍未完成。


UX152 closure — 已实现同步 JSON/消息写出、统一上下文前缀、直接原生用户
事件落盘；自定义 append 兼容保留。改动前后12份 HTTP 请求1808879字节
完全相同，最大219929字节。204125字节前缀+历史+完整输入无消息工作区
构造检查通过，保留200KiB历史预算、16KiB消息和2KiB输入限制。
67项既有测试通过；新增套件在修正空Flash初始化及既有取消语义假设后通过。
没有修改生产取消语义或放宽容量。普通190为1497632B，无音频1012496B，
均构建成功。源码/构建190保留，设备72只读核对、1198/997624/gen17/4296
不变；无云调用、音频或烧录。尚未启用设备并行/证明内存与速度改善。
报告docs/VOICE_REQUEST_STREAM_REPORT.md；完整目标和默认安装包状态不变。


## UX153 — 2026-09-29 首请求发送与接话任务交接
在190的流式序列化上接入延迟回复工作区：完整ASR确认后发送正式请求，
接话生产者退出后才分配messages/reply。兼容旧适配器；失败/取消必须join，
不重放已发送轮次。实时快照对实际发送冻结，保持2MiB上下文和200KiB历史预算。
先主机字节一致性、故障和普通/无音频构建，再最多一组三轮实机门槛测试。
计划 artifacts/voice-fast/request-handoff-ux153/plan.txt；完整目标仍未完成。


### UX153 结果 — 候选191拒绝推广
69项ASan/UBSan测试及普通/无音频构建通过；20请求3605423B逐字节一致。
实机唯一三轮组中两轮确有正式请求发送与候选重叠，但候选timeout/cancelled，
SDK最低堆36332B，网络栈1852B，未达到资源/有效回答门槛。3/3首唤醒、无复位
及DMA丢失不能替代业务通过。源码恢复190，设备72，历史保留；完整目标未完成。
见 docs/VOICE_REQUEST_HANDOFF_REPORT.md 与阶段 closure.json。


## UX154 — 2026-09-29 捕获区直接交接与并行发送资源修复
基于191受测协议，区分“扬声器归属”和“缓存排空可交接”；TLS预热先于请求
缓冲分配。捕获分配同时满足后续状态区大小，join后直接接管同一块，保持逻辑
捕获、2KiB输入和204800B历史预算。增加有界接收/写音频终止时钟，不能延长
截止时间掩盖超时。主机/双构建通过后最多一组三轮实机，再按证据决定保留。
计划 artifacts/voice-fast/capture-adopt-ux154/plan.txt；完整语音目标仍未完成。


### UX154 结果 — 并行分支未覆盖，192不推广
69/69主机测试及普通/无音频构建通过，捕获区直接接管、排空后的就绪屏障、
TLS先于请求分配已实现。唯一实机三轮首唤醒3/3，但完整输入/任务均0/3，
实际延迟请求重叠0次，最低堆43256B，不能证明该分支提速或资源达标。
源码192保留实验态、设备恢复72、默认安装包不变；2MiB上下文与204800B历史
预算保留。下一步分离输入完整性和交接诊断，不追加同类回放碰运气。
见 docs/VOICE_CAPTURE_ADOPTION_REPORT.md 与 capture-adopt-ux154/closure.json。

## UX155 — 2026-09-29 固定输入隔离并行交接
诊断开关默认关闭，使用实际候选/请求/接管链及原期限；在确认边界和请求发送
边界加入受控等待，分别测成功、改口、取消及接收终止失败，不计作声学提速。
先主机及普通/无音频/诊断构建，再最多五个固定输入设备作业，恢复72及保留历史。
计划 artifacts/voice-fast/handoff-isolation-ux155/plan.txt；完整语音目标不缩减。

### UX155 结果 — 固定输入发现候选文本拦截问题
69项主机、普通/无音频/诊断构建通过。唯一五作业矩阵仅改口场景准备了候选，
无旧候选播放，正式回答完成；其余因文本校验/缓存限制退出，0次请求并行。
未知白色状态被正确拒绝；“灯的颜色”因新增“的”误拒绝，下一步限定中性连接词
修复再验证。SDK最低47228B，不能声称资源或1秒目标达标。设备恢复72，
代码193含默认关闭诊断入口；见 docs/VOICE_HANDOFF_ISOLATION_REPORT.md。

## UX156 — 2026-09-29 接话中的中性连接词
按193实录允许话题内一个“的/嘅”，其余至少两个实词字符仍按原输入顺序匹配；
长度、句式、最终输入和GPIO授权不变。未知状态/过长主题仍拒绝。先主机及
双常规/诊断构建，再最多一组相同五作业矩阵；不作为声学提速或并发达标。

### UX156 结果 — 连接词修复保留，正常并行回答仍未通过
69项主机及普通/无音频/诊断构建通过，193固定五条候选由1条可用增到3条，
仅指校验样本。194唯一五作业中3次进入延迟路径，2次完整请求体发送，3次
原捕获块接管成功；取消约406ms、注入失败正常退出，正常pass仍到原期限取消。
SDK最低46984B、网络栈1688B，未完成完整语音及资源门槛。设备恢复72，
实验源码194保留；见 docs/VOICE_RECEIPT_CONNECTOR_REPORT.md。

## UX157 — 2026-09-29 候选接收与正式请求并行对照
仅诊断构建新增接收计数及warm模式：保持真正HTTP预热，但候选join后才发正式
请求；与pass交错各三次，同一固定输入。原超时/字节限额/校验不变。不把人工
屏障和无ASR负载当作提速验收。六作业完成即停止，备份后恢复72。

### UX157 结果 — 接收停顿已定位到传输就绪层，原因未定
69项ASan/UBSan、普通/诊断/无音频构建通过。唯一六作业warm/pass/pass/warm/
warm/pass为done/limit/cancelled/done/limit/cancelled。只有第一轮warm覆盖预热后
接收；第四轮已收齐。两次完整正式请求并行均在TLS/WS最后数据后约8秒停止
进展并到原期限取消；原捕获块接管均成功。不可将2次warm完成当2次接收并行
成功。诊断源码195保留，设备恢复72，完整目标不变。详见
docs/VOICE_RECEIVE_OVERLAP_REPORT.md。

## UX158 — 2026-09-29 正式HTTP头接收诊断
仅诊断模式在sent后有界读头（单次100ms、循环预算500ms），再执行原候选
join；不提前向引擎feed，不丢SDK保留的正文，原候选期限不变。只做三作业，
按是否读到头及候选当时是否未完区分结果，之后恢复72，不视为声学验收。

### UX158 结果 — 已读到HTTP头仍未恢复候选接收
69项主机、三构建通过。一次三作业为limit/cancelled/limit，仅中间轮准入；
完整53781B请求后148ms读到HTTP头，但候选最后WS数据后8078ms仍未结束，
原期限取消，原捕获块接管成功。两轮cache limit不算并行，目标未通过。
诊断源码196默认关闭，恢复72；见docs/VOICE_HTTP_HEADER_HANDOFF_REPORT.md。

## UX159 — 2026-09-29 暂停时长与缓冲分配对照
诊断delay/reserve均在预热后暂停候选650ms，再join；只有reserve在暂停期间
分配真实请求缓冲，之后保留并正常接管。正式请求都在join后发送，原期限/
取消和输入校验不变。一次六作业、不重试，结束恢复72；不能替代声学验收。

### UX159 结果 — 额外暂停有成功样本，分配原因未排除
69项主机/三构建通过。一次六作业5 done/1 cancelled，但有效“额外650ms后
继续接收”仅delay两轮；reserve两轮预热超时未分配、一轮已收齐，无有效
分配并发样本。第6轮尚未分配/发送也原期限取消，且末期仍收数据，不能当作
同根因的8秒断流。四次接管、0请求体重叠。恢复72，197只保留诊断；目标
未完，下一步先移除人工屏障验证正常工作流。见docs/VOICE_HOLD_CONTROL_REPORT.md。

## UX160 — 2026-09-29 无人工接收屏障的正常调度验证
诊断flow使用固定输入、真实候选生成与最终确认，仅移除prepared/drained的
人为等待，准备事件只记一次。先三作业且全部正常，再烧普通198做一组三轮
连续灯状态语音；否则保留失败并跳过新增声学组。原目标和验收范围不变。

### UX160 结果 — 无人工屏障仍有停收
69项/三构建通过；唯一三flow作业1成功2原期限取消，两次真实请求体重叠及
capture_arena接管。两次最后数据到退出8347/8329ms，无读错误；SDK最低45088B，
3次underrun。未达声学前置条件，跳过普通198声学组。恢复72/数据保留，
完整目标未通过。见docs/VOICE_UNHELD_FLOW_REPORT.md。

## UX161 — 2026-09-29 TCP乱序保留单参数实验
基于本地固定IDF的RX饥饿说明，将每连接乱序pbuf上限4改1，总RX8/静态4/
TCP窗口5760不变，应用算法与超时不变。三构建、单参数审计后只跑三flow；
全正常才进入唯一三轮声学组，失败回退配置。69项主机证据沿用UX160，明确
不冒充本阶段重跑；设备结束恢复72，不修改默认安装包或清理任何上下文。

### UX161 结果 — 2026-09-29 未覆盖并行分支，回退参数
单参数审计通过：应用仅版本198→199，两个sdkconfig仅乱序上限4→1；普通/
诊断/无音频构建通过，主机69项沿用UX160而非重跑。唯一三flow为done/limit/
protocol：首轮无句号收齐后播放、第二轮无句号缓存满、第三轮8字话题被6字
约束拒绝，实际请求体重叠0。不能认定参数解决停收，也不能用异路径堆值
计算收益。最低46988B/网络栈2388B/underrun0，23.25秒实录。未达前置门槛，
不跑普通199声学组，不追加参数扫描。回退工作源码/配置到198，构建产物199
单独保留；guard恢复72，新增2事件928B保留为1248事件/1019768B。下一步先
修候选完整性对末尾标点的依赖。详见docs/VOICE_RX_RETENTION_REPORT.md。

## UX162 — 2026-09-29 接话完整边界与流式释放
THINK固定收尾完整时不再仅等句号，仍须最终输入/来源/语言校验；播放后只允许
为无标点THINK补一个句末符号及空白，新词/重复符号仍拒绝。话题收敛为2–8字、
24B以内，保留来源顺序及单个连接词。缓存/超时/上下文/网络配置不扩大。
主机分片、改口、取消和长音频测试、三构建后只跑三flow，全部正常才进入一组
普通版三轮声学；结束恢复72，不能以接话代替一秒有效回答。

### UX162 结果 — 2026-09-29 接话提前释放修复，网络仍失败
THINK完整收尾可无句号准备；补一个终止符/空白可接受，新词/重复符号仍拒绝，
最终输入/来源/语言/取消不放宽。话题2–8字且24B以内。初测68/69，新增测试
发现终止符后换行使准备状态回退；修复后69/69 ASan/UBSan 44.62秒。普通/
诊断/无音频1501968/1504816/1014096B。唯一三flow都在5120样本准备、缓存
3072B、真实请求体重叠，随后最后数据至退出8307/8502/8513ms，全为cancelled。
无limit/protocol并不代表业务成功；SDK最低42180B，网络栈1700B，播放有超时/
取消/underrun。原期限不变，没有重试；不满足前置门槛，跳过普通200声学。
guard恢复72，新增6事件2556B保留为1254事件/1022324B；预算/默认安装版未改。
源码200保留实验态，下一步同样接话边界下只复核OOO上限1，避免上一阶段0次
实际重叠的无效对照。见docs/VOICE_RECEIPT_BOUNDARY_REPORT.md。

## UX163 — 2026-09-29 TCP乱序保留单参数实验
基于本地固定IDF的RX饥饿说明，将每连接乱序pbuf上限4改1，总RX8/静态4/
TCP窗口5760不变，应用算法与超时不变。三构建、单参数审计后只跑三flow；
全正常才进入唯一三轮声学组，失败回退配置。69项主机证据沿用UX162，明确
不冒充本阶段重跑；设备结束恢复72，不修改默认安装包或清理任何上下文。

### UX163 结果 — 2026-09-29 并行路径下OOO上限1仍失败
保留200接话边界，单参数4→1；399代码仅版本200→201，两个sdkconfig只改
OOO上限。三构建通过，主机69项沿用UX162。唯一三flow都真实请求体重叠、
缓存3072B，仍全cancelled；最后WS到退出8316/8142/8435ms，读错误0。SDK
最低39980B/网络栈1700B，播放有underrun/timeout/cancelled，不做性能收益
归因。42秒原始实录保留；没有增加重复测试，跳过未达前置条件的声学组。
回退工作代码/配置到200，201构建只保留实验记录；guard恢复72，新增6事件
2556B保留为1260事件/1024880B，分区/历史预算/默认安装包未变。保留UX162
接话修复；下一步直接检查停收的接收队列及最后协议状态，不再扫同类参数。
详见docs/VOICE_RX_OVERLAP_REPORT.md。完整语音目标和一秒有效回答仍未完成。

### UX164 — 2026-09-29 停收时TCP与协议快照
默认关闭诊断，仅记录最后协议事件及400/1500/4000ms无WS数据时的双方TCP窗口、队列与FIONREAD；tcpip同步回调内只读PCB，不消费响应、不调网络参数。主机与三构建后最多三次flow，保留所有失败；全部成功才跑一组普通三轮声学。结束恢复72并保留上下文。

### UX164 结果 — 2026-09-29
70主机检查与三构建通过。三flow均失败；TCP快照首次显示WS乱序pbuf与HTTP邮箱对象合计下界8，序号持续不动，末事件audio.delta。FIONREAD不支持记-1，不当0。未满足普通声学门槛。已guard恢复72并保留日志；下一步验证L2拷贝释放Wi-Fi RX缓冲及资源代价。详见docs/VOICE_TCP_SNAPSHOT_REPORT.md；整体目标未完成。

### UX165 — 2026-09-29 提前释放L2接收缓冲
基于UX164队列实测，只开启SDK L2_TO_L3_COPY并更新203版本；固定堆pbuf接收并尽快释放Wi-Fi缓冲。RX/OOSEQ/窗口/超时/上下文均不增加。70主机证据沿用UX164，三构建后只跑三flow；全部成功才跑一组相同灯状态输入的普通三轮声学。目标资源48KiB与1秒有效回答不放宽；失败保留，最后恢复72。

### UX165 结果 — 2026-09-29
开启L2_TO_L3_COPY，407代码仅版本变化，SDK仅该开关及旧名别名。三诊断从0/3完成变3/3完成，无欠载；保留配置。普通三轮首唤醒3/3，完整输入0/3，首轮候选等待期限取消、后两轮真实并行完成；有效回复首PCM仍3.374/5.790/6.315秒，接话693/459ms不能替代回答。最低43828B低于48KiB；外录固定对齐失败保留unknown。末轮3.32秒板上录音本地ASR也缺尾句。已guard恢复72、保留1279事件/1033668B。源码/配置保留203、默认安装包不变；目标active。详见docs/VOICE_L2_RELEASE_REPORT.md。

### UX166 — 2026-09-29 提前备答的完整输入检查
保留203并行接收修复及已有单候选生成、静默缓存、最终输入授权。只重放三份已有PCM（已知一致控制、完整问题源、末轮设备片段）核对MODE2分类与结束点；不发新云请求。最多一项有证据的结束策略改动，不扫描阈值、不以提示音计有效回答；所有分区和上下文保留，最终guard恢复72。

### UX166 结果 — 2026-09-29
同一C3重放三份已有PCM共808帧，控制401帧逐字节一致。完整问题源含约930ms句间停顿；生产端点2580ms结束，反事实问句提示可到4800ms。只扩展什么/乜嘢等问句，保持700ms基础静音和400ms累计额度，204正常应用增加32B；70项ASan/UBSan通过45.75秒，正常及无音频构建通过。唯一连续三轮首次唤醒3/3、完整输入仍0/3；两次候选首PCM提前上传结束464/685ms，正式首PCM仍5007/4211/6109ms。末轮确认用满400ms额度，3.688秒本地录音也缺尾句；未追加扫描。SDK最低43968B，实录对齐失败，声学延迟unknown。按条件撤回三文件，407代码及重建应用逐字节恢复203，保留L2修复。所有烧录均guard验证，设备恢复72联网监听；1285事件/1036356B和序号保留，活动区余12220B，分区/预算未变。见docs/VOICE_PREDICTIVE_INPUT_REPORT.md；预测机制及整体目标继续开放，204不推广。

### UX168 — 2026-09-29 只读灯状态提前备答
基于203，复用单候选与静默缓存提前合成实际RGB答案；完整查询、语言、回复文字及播放前读回全部一致才播放。状态改变或额外指令回退完整Agent；不提前执行动作，不增加大缓冲或全局等待。主机与有/无音频构建后仅一组三轮实机，不扫描参数、不更改既有验收门槛。UX167仅检查六轮已有录音的固定短窗：支持前段稳定对齐，不能证明全句或采样率异常；原unknown保持。

UX168结果：70/70主机测试及正常/无音频构建通过。205实机仅一组三轮，首次唤醒3/3、@done3/3，候选实际都生成灯控准备语，严格文字检查拒绝，快答0/3；完整输入仍0/3。首候选PCM相对上传结束-324/+581/-494ms，不等于有效回答；回退Agent首PCM4104/4836/3093ms，声学对齐失败保持unknown。SDK最低48780B仍低于48KiB。206互斥提示词只在电脑建立两次会话，说明文本泄漏和PCM超预算后退出，未构建未烧录，未再跑硬件组。十处实验代码撤回，407代码文件及正常/无音频应用均校验与203一致。设备guard恢复72 fast/capture/listening、reuse on、prefetch off、灯关闭；两次完整备份/验证及非应用一致。上下文1291条/1039044B，恢复前后持久字段一致，未删除数据。详见docs/VOICE_READ_ONLY_PREDICTION_REPORT.md和artifacts/voice-fast/light-answer-ux168/closure.json。整体目标继续，未标记完成。

### UX169 — 2026-09-29 原厂VAD模式对照
上轮为有结果的进展，候选文字不可靠明确了退出条件。本轮保持生产203不变，新增独立USB探针207，同帧配对原厂MODE2与MODE0；八份既存PCM覆盖三份完整改口、旧参考、短句、背景及数字静音。必须先逐帧重现MODE2；不调整能量/结束时限、不采新音、不发云请求。只在完整性与拖尾均有根据时进入一组三轮正常测试，不扫描模式；保留所有历史和分区。


### UX169 结果 — 2026-09-29
同一C3配对MODE2/MODE0，八份保存PCM共2503帧；当前模式逐字节复现，能量保持。12项协议检查通过，两句柄各760B且全部释放。实际C端点16次重放，三个完整改口结束点仍6220/3300/3240ms，未修复；旧参考延后620ms，两个短句保持，背景EOF继续收音记unknown。停止模式试验，不进入正常三轮组，不新增云调用或录音。独立207诊断132256B，正常源码407文件及有/无音频构建仍与203一致，保留L2修复。两次guard全备份、应用核对、非应用一致；探针运行期间及恢复前后整个Flash一致。设备已恢复72联网fast/capture/listening，reuse on、prefetch off，1291事件/1039044B和序号完全保留。详见docs/VOICE_VAD_MODE_REPORT.md及artifacts/voice-fast/vad-modes-ux169/closure.json；目标继续，未声称一秒通过。


### UX170 — 2026-09-29 使用ASR当前声段活动提示续听
UX169排除了模式放宽，属于有证据的进展。旧“保留pending直到final”曾因拖尾撤回，本轮不重复。拟从经过身份/源时间校验的最新未停止声段中提取有意义的当前文字，借用既有1000ms共享额度；speech_stopped即撤销这份活动提示，不等final、不重置额度。先完成协议重放、主机和构建，再至多一组三轮正常改口；通过且存储足够才增加三轮短句。保持203其余机制、分区、上下文和所有用户数据，最终guard恢复72，未完成目标不标记通过。


### UX170 结果 — 2026-09-29
208独立ASR活动位借用既有1000ms额度，70/70主机检查44.50秒、94事件离线协议重放及有/无音频构建通过；正常1502080B(+224)。唯一连续三轮首次唤醒3/3、@done3/3，完整转写1/3，绿色效果2/3；首轮3.912秒输入丢掉绿色要求，未误执行蓝色而追问。候选PCM提前VAD1103/4463/1687ms，均未播放；设备回答起声相对VAD3315/323/279ms，不是声学端到端。最低48792B仍低于48KiB，DMA/欠载/复位0，整段外录对齐失败保持unknown。保留失败，不追加短句或重试；16实验文件撤回，407代码及有/无音频应用恢复203且保留L2修复。guard恢复72联网监听，reuse on、prefetch off、灯关闭。1297事件/1042352B及序号保留，活动区余6224B，分区/预算不变。见docs/VOICE_ASR_ACTIVITY_REPORT.md及speech-activity-ux170/closure.json；208不推广，整体目标未完成。


### UX171 — 2026-09-29 新文字辅助续说的固定输入上界检查
上一轮有明确实验结果，属于进展。本轮先离线检查一个比拟议“仅新文字320ms支持”更宽松的上界：当前TEXT可在旧严格窗口外辅助两个当前强帧，保持所有门槛和累计额度。仅使用UX169固定八份输入，必须逐项复现原端点；若三份完整改口仍有截断，不开展实机。此上界不能直接部署，也不能绕过生产者采样边界证明。不新增云调用、录音、烧录或数据删除，源码203/设备72保持。


### UX171 结果 — 2026-09-29
固定八份C3元数据2503帧，实际C端点基线逐项复现。宽松TEXT辅助两强帧越过旧320ms窗口后，三个完整片段结束6220/3300/3240→6600/6540/6480ms；后两段裁剪本地ASR保留完整参考的改口尾句（第二段完整参考也识别为黑色，不能算绿灯通过）。两个短句不变，背景EOF仍未知。新文字仅320ms的有界原型结果6220/3760/6480ms，第二段仍截断，未扫描时限；事件墙钟到源帧映射为近似，不冒充精确设备时序。生成头文件隔离后重新编译，逐帧输出一致；不增加样本数。已明确改变两票支持时必须同时修复生产者四票上界证明。未新增云请求、录音或烧录，407代码及有/无音频应用仍是203；只读核对设备72联网监听、1297事件/1042352B和全部上下文统计保持。见docs/VOICE_TEXT_SUPPORT_REPORT.md与text-support-ux171/closure.json；目标继续，未达成一秒验收。


### UX172 — 2026-09-29 当前ASR声段辅助本地续说
UX171给出两段可复查的截断修复上界。本轮只让经过校验的当前OPEN声段有效文字辅助两个当前强帧，停止/新空段/短修订即撤销；活动不增加pending等待，不改变起声、能量、静音、累计额度和10秒上限。同时将fast生产者边界证明对应到两票宽松能量事件，classic保持。先主机边界/源覆盖和固定94事件协议对照，有依据才一组连续三轮改口；查余量、不重试刷分、不删除数据，最终guard恢复72。


### UX172 有界修正 — 2026-09-29
仅两强帧版本在固定94事件重放中未改善首尾两段；中段虽延长1820ms，裁剪转写未增加内容，按预定门槛不烧录。逐帧数据确认ASR活动已经到达，但音节只有原弱阈值下的连续语音。保留该版本源码与失败，比较唯一另一条件：当前有效声段可辅助四个弱帧，强起声、能量阈值、700ms静音和1000ms额度均不变；停止或撤回即失效。已有fast两票宽松能量边界仍须覆盖每次重置。先重放与噪声边界，未改善不进硬件；不扫描阈值或时长。


### UX172 结果 — 2026-09-29
按硬件前退出条件结束两版ASR当前声段辅助续说实验。固定94事件配对实际C端点与C3元数据；两强帧版结束2940/7700/3240ms，未修复首尾截断。四弱帧支持版3720/7760/6760ms，第三段裁剪本地ASR保留完整改口，第一段仍只有“请把灯调成蓝色，不对”，第二段多等1880ms但转写未增加。另五段真实元数据在TEXT滞留活动的保守压力条件下，短问候增加460ms；不是声称服务端现场给出了该活动位，背景EOF保持未知。原能量门槛、700ms静音、1000ms累计额度及10秒上限未调。两候选相关7/7检查通过，但行为不符合部署条件；没有IDF编译209、烧录、云请求或新增录音。源码覆盖层与所有失败保留，18实验文件已逐字节恢复；407代码与203冻结包相同，正常/无音频二进制SHA未变。恢复后70/70主机检查通过43.83秒。只读设备仍72联网监听、reuse on/prefetch off、DMA0、1297事件/1042352B全部上下文统计不变，余6224B，COM5释放。见docs/VOICE_SEGMENT_SUPPORT_REPORT.md和segment-support-ux172/closure.json。提前缓存接话不算有效回答，目标及一秒指标仍未完成；不为提高分数追加重试。


### UX173 计划 — 2026-09-29
UX172产生可用否决证据，属于进展；没有活跃进程等待或外部阻塞。核对历史后不重做能量阈值、VAD模式、周期性或时限扫描。先用固定Silero VAD v6.2.3（官方提交5cd7945676eb32225748052e2e6a0580e4686a08，MIT）的电脑端ONNX作为独立离线参照，11份既有录音、原始幅度、16kHz/512样本、0.5/0.35判据及700ms静音保持固定。比较弱尾句与短句/噪声，保存逐块概率和真实EOF边界；不把电脑模型性能称为C3可部署，不上传录音、不训练、不新增云调用或实录、不烧录。正常203源码与72设备保持，2MiB上下文及记录不动；先确认输入是否具备可分辨证据，再决定后续端侧实现。


### UX173 配对说明 — 2026-09-29
原始录音参照仍分句/拖尾，原始播放素材则没有中断。源码核对确认ASR输入为原始PCM（旧UX172字段filtered_input_pcm_sha256命名不准确，其哈希实际是原始PCM）。追加唯一固定对照：用现有C agent_voice_filter与提示音陷波，不改系数、幅度、阈值或时间；11份滤波能量先与实机元数据逐帧核对。保留首轮失败和原素材路径误选导致的前置哈希拒绝，不调用云端、不烧录。


### UX173 结果 — 2026-09-29
固定Silero v6.2.3官方模型在电脑对11份既有录音逐32ms块分析，无补造末尾，每条预处理各2313块。原始UX132三段均在3808/3904ms中断，随后4000ms再次起声；现有C滤波配对不再中途中断，但两段到8秒仍在语音，短问候从5088延到6176ms。不支持直接移植或训练学生模型。11份C滤波共3703帧能量与原C3元数据完全一致，ASan/UBSan通过。实际播放源模型不中途结束，中间数字零145/275.125/290ms；原门槛对齐4/6通过，另两份保持未知。核对出旧UX172的filtered_input_pcm_sha256实际是原始PCM哈希，当前报告明确更正名称含义，旧冻结证据不覆盖。一次原素材路径/哈希混淆在推理前被拒，改用实际测试prompt后保留独立结果。生产407代码及双构建SHA未变，设备72联网监听、reuse开/prefetch关，1297事件/1042352B全部上下文统计不变，COM5释放。零云推理、零录音、零烧录；参考模型只是电脑诊断，不是端侧改进/一秒验收。下一项限量验证可比较原始与现有滤波的ASR修正词报告时机，先取得真实协议证据。详见docs/VOICE_OFFLINE_VAD_REPORT.md和offline-vad-ux173/closure.json。


### UX174 计划 — 2026-09-29
UX173为进展，当前无运行中的进程或外部阻塞。按其结果进行一次有限ASR原始/现有滤波配对：UX132三份真实保存8秒录音，每份raw/filter各一次，顺序1R/1F/2F/2R/3R/3F；qwen3-asr-flash-realtime、16kHz、464样本块、server_vad0.2/700ms均固定。复用已有经过验证的观察器，每连接附加明确标记的700ms协议静音后session.finish，最多6连接/120秒，无重试；不生成回复、不播放、不写设备。原始/滤波及当前407代码哈希先固定，只用原8秒元数据与新真实事件运行当前C解析器和端点，分别记录修正词、完整输入和结束候选时机。电脑时序不是C3延迟。只有完整修正与提示时机确有改善才考虑后续固件；不删除记录、不减少2MiB上下文或历史预算，本轮先不烧录。


### UX174 结果 — 2026-09-29
三份UX132真实8秒录音raw/filter各一次，六个当前ASR连接59.421秒完成、无重试；每份附加明确700ms协议静音。1800上传块校验，190真实事件经当前C分段器及端点重放，观察器6/6与ASan/UBSan通过。完整上传均含绿色改口且字面解析正确，严格全文两组各1/3（其余缺“请”），不是硬件动作验收。滤波修正词相对原始提前313、晚1422、提前719ms，不稳定。当前C结束投影2940/5880/3240→3140/5880/6300ms，裁剪本地识别首段仍只有蓝色、第三段滤波保留绿色；早到Thank/Yep不计有效意图。电脑投影不冒充C3时序，不再请求、不构建或烧录。407生产代码及有/无音频SHA仍为203；只读设备72联网fast/capture/listening、reuse开/prefetch关。相比UX173自主监听期间多2事件848B，查询前异步USB包含一轮结束，当前1299/1043200B，活动区余5376B；不当受控试验、不声称记录未变，全部保留。分区/历史预算/默认安装包未改，COM5释放。详见docs/VOICE_ASR_FILTER_REPORT.md及asr-filter-ux174/closure.json。单候选预取已有实现，但输入截断仍影响最终确认；完整目标与一秒有效回答继续开放。


### UX175 计划 — 2026-09-29
UX174形成滤波否决证据，属进展，无在运行的实验。本轮先离线检验硬件域轻量语音分类是否值得实现：复用76份已保存6秒设备采集及原TTS声音train/validation划分，关键词负例也按人声处理。只使用旧对齐中相关度>=0.5的测量值，不拿估算时延标注；不可靠样本记录为未评分并报告覆盖率。源32ms能量给出有48ms边界余量的回放存在代理标签，源前200ms/源后300ms以外为背景代理，不冒充真人逐帧真值。当前C定点40频带特征、256样本步长、相邻两帧→16ReLU→1logit，1313参数；固定种子、512步AdamW、0.5判据，TRAIN归一化，验证集不选轮次。未通过验证正帧90%/背景误接纳5%筛查不做设备集成；即使通过仍要六份改口及短句/噪声端点独立检查。一次CPU小试，无云请求、录音、烧录、数据删除，保持407生产代码/203和设备72及分区容量。


### UX175 结果 — 2026-09-29
完成一次1313参数、512步CPU语音/背景小试，无追加训练。76份旧KWS设备回放按原声音隔离，61份有相关度>=0.5的对齐（train39/val22）；15份粤语明确未评分。标签仅为源时间/能量推导的回放存在代理，不是人声真值。验证正帧873/1029=84.8%、背景345/5344=6.5%，未达预设90%/5%，不集成。冻结后独立三频段对齐仅27/61通过且均在原偏移20ms内，其余位置未能确认；未改标签/权重，246来源身份无跨集复用。当前C定点前端22875训练素材帧、4628独立诊断帧及三个正弦标尺通过ASan/UBSan；448尾部样本不足16ms明确未评分。六份诊断弱区/尾部得分仍混杂，不做端点试装；9448B只是主机完整KWS容器，不称C3预算通过。407生产代码和有/无音频SHA保持203；只读72联网监听、reuse开/prefetch关、DMA0，1299事件/1043200B与UX174全部上下文统计相同、活动区余5376B。首次只读查询的音频Python缺serial、USB未接触，换既有IDF环境成功；无云请求、新录音、烧录或清理，COM5释放。报告docs/VOICE_VAD_PILOT_REPORT.md，证据vad-pilot-ux175/closure.json。下一步先用至多三份当前并发收音条件的双同步声标定获得可信时间标签，诊断须在回复/工具/历史写入前退出；不扩大训练或将标定算正常三轮验收。整体目标保持未完成。


### UX176 计划 — 2026-09-29
先用三份带前后同步扫频的既有改口素材标定当前采集/ASR并发路径，固定8秒影子端点诊断；最多三次唤醒/三次ASR，无失败重试，不生成回复或执行工具/写历史。临时210诊断默认关闭，保留原影子状态，独立标明8秒采集完成；取消/真实错误仍失败。前后同步声测偏移与时钟差，预定相关度、竞争峰和三频段一致性门槛；同步声及±200ms排除于人声/背景标签。不能将带标定声音的端点或暂停导出当正常对话、快速重唤醒或1秒验收。不训练、不调门槛、不清理上下文，2MiB分区及204800B历史预算不变，最后恢复203源码和72设备。完整备份/应用烧录/非应用区一致性仍由flash_guard执行。


### UX176 结果 — 2026-09-30
至多三份当前并发链路的双标记时间标定完成3/3。每份完整8秒128000样本、400帧CRC正确、同步标记三频段预定门槛通过；真实ASR三次都读出蓝改绿。原影子端点3360/3400/3120ms，按固定声源能量规则估计语音仍至约5720/5770/5730ms，证实早停漏改口。本诊断没有生成回复、执行工具或写历史，不能算正常连续对话或一秒首响。上下文本轮前后1187事件/845012B；两次guard非应用区相同，设备恢复72且DMA0。407源码字节与203快照一致，正常/无音频二进制SHA亦一致。完整证据见docs/VOICE_INPUT_CALIBRATION_REPORT.md和input-calibration-ux176/closure.json。完整目标未完成，下一步以该校准录音有界验证续说依据，保留最终意图核对与改口撤销。


### UX177 计划 — 2026-09-30
UX176完成当前并发采集的双标记时间对齐，原端点在完整改口前约2.4秒结束。本轮不再录音或烧录：以标记测得的采样偏移，沿用冻结的1313参数C前端VAD小试及固定0.5判据，在三份新8秒板上录音上一次性评分。源稳定32ms能量给人声存在代理标签，双扫频及±200ms不入标签，源后背景保持300ms余量；代理不是真人人工真值。预设每份都有正/负帧，总体正帧≥90%、背景接纳≤5%才可进入进一步方案审查；单三份即使过线也不直接集成。保存逐帧分数与统计，不重训、不扫门槛、不调端点、不进行云请求/烧录，保持源码、二进制、Flash、上下文不变。


### UX177 结果 — 2026-09-30
冻结1313参数小模型在UX176三份当前域已标定录音上一次推理，语音代理297/407=73.0%，尾部背景代理误接纳120/204=58.8%，未达事先90%/5%门槛，拒绝集成，不重训/扫阈值。原终点后四强帧分别于+520/+540/+760ms到达，说明当前原声学元数据仍有可用续说证据，但没有测试新的C端点策略，不宣称800ms修复或1秒达标。407生产源码及两构建SHA未变；无新USB、云、训练、音频或烧录。见docs/VOICE_CALIBRATED_VAD_REPORT.md及calibrated-vad-ux177/closure.json。目标保持未完成。


### UX178 计划 — 2026-09-30
UX177拒绝冻结VAD模型；当前3份硬件元数据在旧端点之后520/540/760ms仍形成四强帧。先核对快速采样的独立生产者上界：把UX176三份400帧原数据送入同一C source_bound，另按预先固定的扫频前后200ms掩蔽两个测试同步声，分别记录实际与无标记反事实停采样本。不修改阈值或声学分类，不将掩蔽版说成实机采集；若生产者本就提前停，则不能只改消费者状态。此阶段无云、训练、录音、烧录、上下文写入。


### UX178 结果 — 2026-09-30
三份UX176实录原20ms元数据送入同一C采样上界；原端点3360/3400/3120ms，实际元数据的上界推算8840/8820/8780ms。掩蔽同步扫频反事实修正后上界8240/7860/7900ms，仍覆盖后半句。8秒之后只用固定噪声补算，明确不是实测。首轮掩蔽起点无符号下溢，旧replay.json保留；修正后55/57/56帧才入结论。六次C重放ASan/UBSan无错，407源码与两二进制不变，无设备/云/训练。单纯给每句增加800ms会伤短句低延迟，未改端点或烧录。见docs/VOICE_PROVISIONAL_BOUND_REPORT.md和provisional-bound-ux178/closure.json；整体目标仍未完成。


### UX179 计划 — 2026-09-30
UX178验证C采样生产上界足以覆盖三份后续改口，单纯延长所有短句会妨碍一秒回答。主机仅检验一个预定选择条件：本轮ASR曾出现过不完整参数PENDING才有一次额外800ms可撤销结束候选；四个160ms内的严格强帧才能取消候选，随后恢复原700ms，文本本身不续时。对三份当前标定改口、两份短问候和历史改口/背景/静音固定元数据与通知各重放一次，基线必须对齐实测；当前三份至少覆盖到5800ms且在10秒内结束，短问候额外≤100ms，无语音不得接纳，历史不能退化。失败不集成，不调整参数重复刷分；本轮无烧录、云、训练、录音或上下文写入。


### UX179 预先补充内容门槛 — 2026-09-30
主机端点时间通过只是必要条件；在运行裁剪识别之前，追加更严格的门槛：同一冻结本地SenseVoice对三份新精确PCM裁剪都必须保留最终绿色改口，并同时报告完整8秒及旧结束点；任何一份失败则不进固件。该识别是独立离线辅助证据，不替代真云和真人验收。


### UX180 计划 — 2026-09-30
UX179主机一组选择性暂定结束方案已在三份当前标定录音取回绿色改口，两个短问候时间不变；冻结本地识别精确裁剪3/3保留绿色。将规则加入纯C端点，默认关闭、仅快速ASR候选路由启用：本轮曾有不完整参数PENDING才把首次700ms静音暂定，最多800ms等四强帧；续说则恢复700ms并且不再重授，文本自身不能重置。先备份5个生产文件，复用精确C代码主机重放/取消/采样覆盖、70项主机检查与三种构建；通过才guard备份并只烧录应用，最多一组三轮正常声学。失败保留证据，恢复72设备/203源码与完整上下文；不清理记录、不改分区、默认安装包。


### UX179–UX180 结果 — 2026-09-30
13份固定元数据26次生产C配对重放完全匹配原型，三份当前录音的精确新裁剪本地识别3/3保留绿色，两个短句原结束点不变；1000组生产者覆盖和边界测试通过。70/70主机检查、带开关C3应用及无音频构建通过，实验应用1502048B。一组三轮真实连续测试首次唤醒3/3，但只有第一轮识别完整绿色改口并正确设绿；该轮仍漏“请”，严格全文0/3。第二轮ASR空且协议错误，第三轮录音3.768秒，在绿色改口前停止并追问。启动以来最低空闲堆48184B，小于49152B验收门槛。故否决该开关，不纳入默认固件，不宣称一秒有效回答。完整备份/只更新应用/数据不变校验后恢复72设备；407生产源码及正常203二进制SHA逐一恢复，恢复后70/70。上下文1187/845012B→1191/847076B，新增对话保留。详见docs/VOICE_SELECTIVE_PROVISIONAL_REPORT.md及provisional-firmware-ux180/closure.json。预测候选必须最终核对，后续先诊断实机输入漏续说与内存余量。


### UX181 计划 — 2026-09-30
仅用UX180保存的播放源、旁路麦克风、第三轮USB原录音和事件时间，做一次三频带对齐及语音间隔核对。预先固定两频带相关≥0.35、起点相差≤80ms才能称音频对齐；不通过只报告直接可见的长度与能量。弄清后半句是否已在端点结束前抵达设备麦克风，避免再次调结束时限来掩盖声学或播放问题。本轮没有新唤醒、录音、云、烧录、训练或生产代码变更。结果不能单独验收真人和一秒有效回复。


### UX181 结果 — 2026-09-30
冻结第三轮设备录音与原提示三频带相关0.828/0.971/0.704、起点相差10ms，对齐通过。设备3768ms录音截止于源约3.038s，源5.09s仍有声音，未录入约2.05s。旁路麦克风三轮均对齐源开头和结尾，证实电脑完整播放。源有声段0.12–1.82、2.19–2.54、3.05–3.83、4.28–5.09s；设备截止与第三段起点在约10ms格点内重合。第二段已在设备录到17帧，但相同C滤波后的清洁能量中位216、最高328，均低于强门槛352，严格强能量0帧。在线“不对”临时文字在VAD结束335ms后才出现，无法作为预停录提示；本地源裁剪/板上录音转写分别支持截断前只有蓝色和“不对”，后段才有绿色。没有板上逐帧频谱、第二轮板上录音或真人数据，不能归因于单一硬件/算法故障。407生产源码和正常203固件哈希保持，无新USB/网络。见docs/VOICE_INPUT_ATTRIBUTION_REPORT.md与input-attribution-ux181/closure.json。整体语音目标未完成。


### UX182 计划 — 2026-09-30
仅把UX180第三轮保存的60288样本原录音送入已冻结的C3原厂VAD双模式USB探针，补足UX181缺失的逐帧频谱位，不再次录音或运行对话。原生产滤波能量逐帧字节校验，输入/输出CRC与序号完整才可归因；MODE2为当前量产判定，MODE0只作配对参照，不由一段材料决定换模式。先核探针/录音哈希，guard全备份仅应用烧录；一次重放后guard恢复72，联网监听和原上下文核对。生产203源码、分区、默认安装包不动，结果仍非三轮/一秒验收。


### UX182 结果 — 2026-09-30
单份188帧C3探针重放完成，输入CRC690d8b45、MODE2/0元数据CRC e62edb6a/694f1bcf，能量与原生产C逐字节一致。UX181映射弱句完整17帧中，MODE2频谱阳性6、MODE0阳性11，但两路强能量0、弱能量5；现有强判据不能让该句重置静音。无需凭单段结果切换模式或降低门槛，UX169/172反例仍在。两次guard整片前后SHA完全相同、非应用分区不变；上下文1197事件/849824B前后相同。设备恢复72，在线fast/capture/listening、reuse开/prefetch关、灯灭、DMA0。未改生产源、未验三轮和有效一秒答复。详情docs/VOICE_VENDOR_TAIL_REPORT.md及vendor-tail-ux182/closure.json。


### UX183 计划 — 2026-09-30
先主机检验单次弱语音桥接：沿用UX179真实13份元数据/通知、不改强起声及能量门槛；ASR本轮曾PENDING才有700ms后的800ms暂定区间。该区间内当前频谱和双路弱能量通过、160ms内四票可续接一次，恢复700ms且永久用尽暂定资格；不循环弱重置。三份当前改口须保留至5800ms且10s内结束，两短句额外≤100ms，历史改口不得早于旧选择规则，无语音不准入。新第三轮只有已保存前缀，PENDING-at-zero为明确合成通知，仅检查声学机会，不补造尾音或冒充完整对话。失败立即否决，不扫参数，不构建/烧录；通过还须裁剪内容、采样覆盖和堆预算门槛。生产源/固件/设备数据保持，方案及固定清单在weak-bridge-ux183。


### UX183 结果 — 2026-09-30
13份冻结真实通知/元数据39次C配对，原基线及选择版精确重现；新四弱票单次桥接保持当前三段6420/6460/5940ms、两个短句2600/3040ms，历史不提前。最新正常第三段的PENDING-only合成通知缺转写准入，初次夹具失败原样保留；按实机摘要2060ms准入预先修正为明确重建通知，旧端点3700/speech460/quiet1500重现，但新桥接仍未触发。17帧弱句中频谱与双路弱能量同帧交集仅157–159三帧，四票必要条件不满足，否决候选，不扫三票或进入固件。只读堆事件定位下降出现于第三轮TTS区间，不能推断分配栈或盲减任务栈。407生产源及203应用SHA一致，无新设备/云/训练。docs/VOICE_WEAK_BRIDGE_REPORT.md、weak-bridge-ux183/closure.json；目标未完成。


### UX184 计划 — 2026-09-30
UX183同帧弱桥接因频谱/能量交集三帧失败；保留四票、160ms窗口，检验两个独立通道的时间佐证。只有实际曾PENDING的单次暂定区間，当前频谱阳性、8帧频谱≥4、双路弱能量≥4且最近能量阳性≤现有60ms，才续接一次，随后700ms并永久花掉资格。13真实对比4版52次，先重现前三版，再过当前5800ms/10s、两短句≤100ms、历史不提前、无语音和背景未知门槛；第三轮前缀沿用明确重建通知，先实测摘要对齐再检查桥接，不能算尾句完整。通过后才同冻结本地ASR裁剪核内容、生产者覆盖和边界；失败不扫参数或构建。方案、源哈希与固定清单在temporal-bridge-ux184，生产/设备不变。


### UX184 结果 — 2026-09-30
13真实对比52次、实际纯C模块与原型一致，状态12B；两短句时间不变，三当前段6420/6460/5940ms精确裁剪与UX179相同，复用原冻结识别3/3绿色，没有新推理。最新第三轮重建通知仅证明3220ms桥接/3760msEOF仍活跃，实际通知和缺失尾音未知。边界/1000人工生产者模式、63便携+8固定SDK=71项检查通过；首次缺IDF配置的63项与补足8项单列。默认203应用SHA不变，406原代码一致，仅新增模块及主机注册，尚未入设备，无新USB/云/录音。仅准入一次有界实机试验，不标默认/一秒通过。docs/VOICE_TEMPORAL_BRIDGE_REPORT.md、temporal-bridge-ux184/closure.json。


### UX185 计划 — 2026-09-30
UX184纯C时间佐证已过71主机检查和固定裁剪/生产者门槛，接入默认OFF实验开关，仅fast+isolatedASR+prefetch路径；原20ms元数据/强弱阈值/32ms采样/10s上限不变。只增加原子桥接时间状态，无逐帧USB导出或强制长录音。备份5生产文件，先构建带开关/默认/无音频；guard全Flash备份验证后只更应用。最多一组三轮普通话/粤语/普通话蓝改绿，首次唤醒、全文/最终意图、协议、DMA/复位和48KiB堆分别记录，1秒另测。失败不补轮或扫阈值，结束恢复72与原生产源，新增对话保留，纯C主机模块保留。证据temporal-firmware-ux185/plan.json。


### UX185 结果 — 2026-09-30
一次实验三轮完成，首次唤醒3/3，完整最终绿色功能2/3、严格全文1/3；第一轮截断在‘不要’，第三轮缺‘请’。实际候选PCM提前2299/7703/3105ms、完整候选提前937/5855/732ms相对vad_end已准备，最终输入校验拒绝第一轮，没有蓝灯误执行；后两轮走final_local_light，不把准备语音当有效回答。最后桥接3960ms，DMA0、无协议/复位，min_heap48740低于49152B412B，否决默认。39.65s外部录音固定对齐失败，声学响应unknown，不降门槛。带开关1502272B/默认/无音频构建通过；两次guard全备份只应用读回，非应用不变，已恢复72联网fast监听/reuse开/prefetch关/灯灭、COM5释放。5文件回滚、406原代码SHA及203默认重建SHA一致；上下文1197/849824→1203/853104B新增保留，全部持久化统计相同；首轮闭环发现三项请求运行统计重启归零，已记录差异，不称所有字段相同。不补轮、扫门槛或清理。docs/VOICE_TEMPORAL_FIRMWARE_REPORT.md、temporal-firmware-ux185/closure.json。整体目标未完成。


### UX186 计划 — 2026-09-30
上一轮三份灯光候选均生成但未播放，后两轮走更快本地执行。先主机验证可选本地优先预取：保留完整final灯控语法，不把RGB或权限给临时文本；只提供纯形状识别，简单/缺参数/未完成改口不提名无用灯光语音，明确复杂灯控仍按原有话题/最终一致性准备过程语句。旧API与默认行为不变，新adapter开关默认OFF。主机覆盖中粤语/连续partial/修正/否定/取消、复杂多动作、工具与缓存生命周期；全套/ASan通过才进入另有界实机阶段。不改VAD门槛/堆栈/上下文，本阶段无USB/云/录音。local-first-ux186/plan.json。


### UX186 结果 — 2026-09-30
新增默认OFF本地优先候选入口与构建开关，原候选API保持、最终灯控纯语法复用，不给preview RGB/执行权限。简单中粤语连续partial不发response/create或分配PCM，未知复合灯控仍可提名源话题过程应答；取消/输入错误/完整final工具门槛保留。73完整主机检查(含8固定SDK与2新增ASan/UBSan)通过。共享fixture main重命名缺return0、新fixture复杂ack未join两次失败已保留并修复fixture，未降低门槛。无新增内存/任务或VAD/上下文变化，本阶段无USB/云/录音；只准入有界实机，不称省堆/一秒通过。docs/VOICE_LOCAL_FIRST_PREFETCH_REPORT.md、local-first-ux186/closure.json。


### UX187 计划 — 2026-09-30
UX186本地优先73主机检查通过。保持UX185同一次时间佐证端点/强弱门槛/10s，只增加本地优先候选，213实验默认OFF，复用同prompt/gain0.25/preconnect idle/reuse on/prefetch on一组三轮。先默认/无音频/带开关构建及预算，guard全备份只应用；核每轮首唤醒、严格全文/最终绿色、无蓝灯误操作、候选response/PCM页、DMA/复位/协议/48KiB堆。声学首响另测不冒充。失败不补轮或扫门槛；结束恢复72与5临时文件，保留默认OFF本地优先源码及新增历史，归档完整实验变更源。local-first-firmware-ux187/plan.json。


### UX187 结果 — 2026-09-30
固定一组三轮首唤醒3/3、完整绿色作用/读回3/3、严格全文2/3(第三缺请)，候选request/PCM/页0，空闲线程取消不代表对话取消，三轮均@done。min_heap64024B对比UX18548740多15284B，超过48KiB；DMA0、无协议/复位。端点消费者/header/模块SHA同185，状态成员仅混合换行差异，包首查拒绝后确认整文件标准换行仅9行诊断，不改音频处理。应用1502624B、三构建/73主机通过；418源配置/缓存归档。第三原录音96640样本三频带0.854/0.980/0.742同0.71s，独立首句含请，倾向识别省略但不补写；独立整段误读黑色保留。外部声学有效应答候选未知/1.479/0.585s，未过稳定1秒/严格3轮门槛，不默认、不追加轮。两guard非应用不变，恢复72联网监听/reuse开/prefetch关/灯灭、COM5释放；5临时文件回滚、407本轮前SHA一致，默认OFF本地优先保留，默认重建SHAb227ad406c979dfed1869f55fa8c5790aaca60eb50ec395a8ee9599f0c789dd0。上下文1203/853104→1209/856696B新增保留且恢复统计全部相同。预试默认hash保留因已被实验版覆盖而拒绝，不称二进制前后同。docs/VOICE_LOCAL_FIRST_DEVICE_REPORT.md、local-first-firmware-ux187/closure.json。整体目标仍未完成，转查串行收尾/复杂路径。


### UX188 计划 — 2026-09-30
固定原录音离线核查本地灯控声学分析未排除结束咻音的遗漏；先主机复现并补同一固定检测规则，再输出独立命名分析，原报告/音频/旧分析不覆盖。缺少或含糊提示音保持unknown，不降低来源匹配/ASR/能量门槛，不把提示音当有效答复。只用实际遥测拆收尾，缺失采集结束时刻保持缺失。无USB/云/新录音/固件/阈值改动。tail-attribution-ux188/plan.json。


### UX188 结果 — 2026-09-30
发现声学分支遗漏：final_local_light未排除结束咻音，旧0.585/1.479s撤回为提示音。同一固定扫频/歧义门槛补到本地回退，旧证据不覆盖，新分析未知/1.749/0.855s候选；来源弱/缺失或歧义咻音保持unknown。33离线检查通过，固定cue检测SHA不变；原report/音频/旧analysis SHA一致、407固件代码及默认appSHA不变，无USB/云/新录音。收尾审计只接受最后组后身份/样本匹配快照；第一二轮采集时钟保持缺失。最终ASR到首PCM25/60/43ms；第二第三源结束到咻音1.47/.58s，咻音后答复候选约.10s。第三采集结束到上传183ms、收尾523ms，既有ASR并行已生效，不重复认领。下阶段一次性补采集/提交/提示音逐轮时钟，再拆咻音前等待，不扫VAD。整体目标未完成。docs/VOICE_TAIL_ATTRIBUTION_REPORT.md、tail-attribution-ux188/closure.json。


### UX189 计划 — 2026-09-30
复现UX187桥接/本地优先，仅加24B时钟，收音中无新增USB输出，join后复用已释放ASR scratch发一个版本化tuple。逐轮记录收音停止/确认/EOF/提交/提示音/收尾，先构建再guard全备份只应用，一组中粤中三轮，不补轮扫阈值；结束恢复72及六临时文件，保留新增历史和精确试验源。声学固定排除咻音，不将诊断当提速。capture-tail-firmware-ux189/plan.json。


### UX189 结果 — 2026-09-30
一组三轮诊断首唤醒/最终绿色3/3、严格全文2/3(第二缺请)，三轮均@done；24B时钟/一个join后tuple，端点不变。提交169/179/175ms、咻音流程360/361/360ms、结束至收尾530/540/536ms；ASR已重叠，未认领提速。min_heap62212B、DMA0、无协议/复位。声学共同来源仅一轮过门槛，三轮起点unknown，不降门槛/补轮；遥测审计保留null声学。9时钟+6提示音检查和正常/诊断/无音频构建通过，应用1503184B在原预算。两guard全备份只应用非应用相同，恢复72联网fast/capture监听/reuse开/prefetch关/灯灭/COM5释放。六文件恢复407基线一致；默认重建SHA05fcd2b04adfa7c38a1ab428b5b20da180a46e7332e7ad1c999e0a12b8f9b0ed，不称二进制前后同。历史1209/856696→1215/860288B新增保留，恢复全部context统计相同。下一有限候选为完整校验与静音预热重叠，保留咻音/最低预热/成功join；本阶段尚未实现。docs/VOICE_CAPTURE_TAIL_CLOCK_REPORT.md、capture-tail-firmware-ux189/closure.json。整体目标仍未完成。


### UX190 计划 — 2026-09-30
依据UX189提交约170ms后才预热的串行等待，有限尝试收音停止/EOF后先开数字静音PDM，再完整CRC回读提交；扣除已真实经过的预热时间，剩余零样本补足180ms，保留完整咻音/成功join/工具门槛。驱动分配更早，实测瞬时堆，不称零成本。先主机计时/分片不变及全套检查、三构建、guard全备份只应用，一组同条件中粤中三轮；不补轮扫阈值。v2只在join后发时钟。结束恢复72与九文件，保存精确实现/失败/新增历史，整体目标不因局部提速标完成。cue-preheat-firmware-ux190/plan.json。

### UX190 结果 — 2026-09-30
一组三轮首次唤醒3/3、正确绿色及严格全文2/3；第三绿误识别为黑并实际黑色，@done3/3不代表业务通过。完整提交175/179/214ms，咻音函数184/181/175ms，本地收尾361/362/391ms，对上一组530/540/536ms观察差169/178/145ms，非统计或声学因果效果。预热实际175/179/214ms补零5/1/0ms，最低180ms与完整CRC/咻音保留。前两轮固定声学答复候选1.067/1.389s，第三输入匹配失败unknown；最终ASR至PCM24/23/6018ms，稳定1秒未过。73主机/10时钟检查、三构建/恢复构建通过；应用1503536B原预算内。min_heap49636B(余门槛484B)、DMA0、无协议/复位；第三云回退不同，不把堆差归为预热。两guard全备份只应用非应用不变，恢复72联网监听/reuse开/prefetch关/灯灭/COM5释放，九文件与407基线恢复。历史1215/860288→1222/864436B新增保留，持久统计恢复相同，三项prompt/request运行统计归零单列。默认重建SHA同UX189，422源冻结/分析另存。否决默认，不补组不扫阈值；先离线分辨第三输入/ASR/回退。docs/VOICE_CUE_PREHEAT_REPORT.md、cue-preheat-firmware-ux190/closure.json；目标未完成。


### UX191 计划 — 2026-09-30
离线核UX190末轮绿误黑及6018ms云回退。冻结原报告/外录/源/guard备份及407代码SHA；用同一生产C读取已提交packed-v3录音，另核Python解码/头与编码CRC，坏头/正文/截断拒绝。固定既有2s前缀诊断只用于输入裁剪，不提升原完整来源失败为延迟通过；本地ASR限source/完整板录/首2s/末3s及既有三外录输入。读取原prefix/ASR/LLM/TTS阶段、请求上下文；无USB/云/新声录/烧录/阈值改动，不强制绿色或补组。input-fallback-ux191/plan.json，整体目标保留。

### UX191 结果 — 2026-10-01
只读离线诊断关闭：生产C与独立Python解码126592样本精确一致，头/正文CRC通过、满幅0；6项分块/损坏ASan检查通过，上传无PCM校验不能宣称字节一致。源/三外录ASR绿色，板录完整/尾段黑色，缩向输入路径但原因未定，原第三声学unknown不变。临时ASR在2.2–2.34s已有灯意图，已转发预测器；确认前缀晚到不等于临时文字晚到。66视图纯C分类非并发重放，实际请求/PCM0保留。候选miss清job.fast绕过缓存进度提示，确认实现缺口尚未修复；最终ASR至PCM6018ms、实际黑色失败保留。407固件源/默认app不变，无USB云烧录新音频；docs/VOICE_INPUT_FALLBACK_REPORT.md与input-fallback-ux191/closure.json。用户误唤醒反馈转UX192优先，整体目标未完成。


### UX192 计划 — 2026-10-01
用户报告频繁误唤醒。已读取72状态并临时voice off，累计11唤醒不直接计误报。先保存末录，固定740/850两门槛单次离线旧开发分数比较；同板各18源(中粤各4正+10负)及45s环境观察，不开云对话、不补组扫参。850仅在离线误报下降且同板双语有效命中无退步时保留，再持久化原应用设置并一组中粤中连续回归。环境未核实、短观察不折算生产每小时误报，历史失败保留。false-wake-ux192/plan.json；语音提速主目标保留。


### UX192 结果 — 2026-10-01
同板各18条：740普通话/粤语有效4/4、4/4、负例2/10；850为2/4、4/4、负例0/10，因普通话明显回退否决850并恢复740，监听暂关。各45s未观察触发不等于日常误报为零；模型工作区/声源/源文件相同，无DMA或资源错误。旧开发负例5/636→3/636但中45→44、粤33→31，不能仅宣称改善。保存末录CRC/WAV和一次本地ASR，仅后触发录音非唤醒音节证据；用户反馈计数11不能全算误报。离线误触发高分连续64–96ms，下一阶段只试740+128ms连续确认，不启动训练/参数扫描。false-wake-ux192/closure.json，整体目标未完成。


### UX193 计划 — 2026-10-01
否决850后，仅测试740+连续4块128ms确认；保留legacy2/3可选，1字节复用detector padding，无新PCM缓冲/模型/任务。冻结13旧源，主机短脉冲/暖机/间断/冷却/重置/legacy及实际C旧分数检查、hash绑定静音seed重生，当前工作区有声/无声构建。guard全备份只应用后同18条与45s观察；负例减少且中粤4/4保留再一组中粤中完整语音，失败回滚72保留记录。不再追加候选/训练/补轮/扫阈值，真人和日常小时误报不称通过。wake-confirm-ux193/plan.json，语音流畅目标保留。


### UX193 结果 — 2026-10-01
128ms连续确认实际C主机5项/双构建通过，detector24B、融合12632B、静音6816B不增RAM；实验app1501968B/SHAced2fafa3982e10377b79cea05a2528781b6f164a7f8b7ec5f4fdb39e5899429。旧开发误报5/636→0但中44/45、粤27/33有损。实机同18条中1/4、粤4/4、误报2/10仍存在，否决候选；45s0事件非长期验收。因唤醒前置条件失败不追加三轮云回归/训练/候选。两guard全备份只应用且非应用不变，已恢复72/SHA22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07，740/gain1，fast/capture；自动语音及唤醒暂关以抑制非预期回复，串口释放，持久context统计全部相同。可选实验默认OFF保留精确源码/配置压缩包，正常build显式OFF保证回滚选择；未声称近似词或现场根因已修复。docs/WAKE_FALSE_TRIGGER_REPORT.md、wake-confirm-ux193/closure.json，整体目标未完成。


### UX194 计划 — 2026-10-01
上一轮有进展：排除850及128ms两个失败修复并完整回滚72。沿最新误唤醒反馈，先一组中粤中连续baseline语音保留失败；再关自动语音，以固定TRAIN声源24条(每语言6正6负)、6s设备录音采集实际声学输入，不看分数选源，不替换失败。原录保留，固定C/整数740与实际seed128零帧，核对源/板录分数、对齐、ADC/PCM截顶；够资格只追加TRAIN，原validation/test/norm完全不动。本阶段不训练/烧录/扫参/追加组，不把机器回放等同真人。wake-input-ux194/plan.json；完整目标保留。

### UX194 结果 — 2026-10-01
一组中粤中连续原72对话，首次唤醒/输入/业务3/3、minimum heap49984B、复位/看门狗0。固定24 TRAIN麦克风捕获全部通过旧audit门槛，ADC/PCM满幅0；固定C/740板录中粤各6/6，负中0/6粤1/6（小燕尾后62ms，E/L均高）。原源0/6与4/6、重建实际提交PCM0/6与5/6，两个源域负例0；重建非声卡字节证明、标签非真人音素真值。只追加168 TRAIN示例，15455旧数组精确前缀，val/test/norm字节不动及无held-out来源交叉；6工具检查通过。前置原clip导出失败原样保存，旧槽已在193完整备份，不称新增导出。72/740保留、自动voice/wake off、context新增三轮保留、USB释放；无训练或烧录，verification.json及docs/WAKE_FALSE_TRIGGER_REPORT.md；主目标未完成。

### UX195 计划 — 2026-10-01
仅一候选从L微调2000步/batch32/CPU4/seed202610012/lr5e-5，新旧684个device-extra TRAIN强调0.5、device_fraction0.5、negative_peak_weight0.35；E/前端/740/legacy2of3/等权smooth3/RAM不动，val/test/norm冻结。TRAIN512校准、静态INT8/Python/C数值一致性及sanitizers后，实际C一次验证必须旧负触发<5/636、中>=45/45粤>=33/33、compact中>=15/16、device/半词/提前不回退；24已录TRAIN板输入正12/12、负0/12只是in-sample前置门槛。不通过即停止不扫参。全部通过才基于72快照仅换模型/seed/version，guard全备份只应用；同18源+45s必须中粤各4/4负0/10及无DMA/复位、推理<32ms；再一组中粤中完整语音3/3及heap>=48KiB。任一失败回滚72关监听，保留上下文/全部失败。不补轮/改门槛/反复训练/看test，非真人泛化或小时误报证明；wake-repair-ux195/plan.json，完整语音提速目标保留。

### UX195 结果 — 2026-10-01
唯一2000步训练约27s，按预定val loss checkpoint950；TRAIN512校准、4096帧1085440整数值Python/Torch/C parity与5 sanitizer检查通过，融合RAM12632B。固定740旧validation负5/636→4/636，但中45→44、粤33→32，compact15/16；24原TRAIN板录中粤各6/6、粤小燕误触发1/6仍在。原门槛失败，候选否决，不烧录/不追加实机回放/语音/扫参/再训练/test选择。第一次cmake前PATH失败保留，补已有local/bin后唯一完整核；没有重跑训练。原72/740保持，voice/wake off，context原样。wake-repair-ux195/offline.json与closure.json、docs/WAKE_FALSE_TRIGGER_REPORT.md；模型修复未完成，完整目标仍未完成。

### UX196 计划 — 2026-10-01
UX195排除一候选是进展，误唤醒未解决，完整目标保留。修复已实证candidate miss略过cached提示：在candidate/fast汇合处，仅无ack text/pending/spoken、非deferred/clarify的已结束完整输入播放一次既有缓存提示；取消/失败begin/工具/最终答复/终端均沿旧join，无新模型/缓冲/任务/配置。主机完整检查+有声无声构建，固定E/L740及stableOFF，guard应用试验；仅一组既有greeting中粤中连续fast/capture/reuseon/prefetchoff保留失败，核输入/业务/健康/heap48KiB及真实cached回退分支时钟。旧外录离线波形/内容分析，不把cached当生成快模型或任务完成，不作跨72/不同输入百分比因果。结束不论结果均guard恢复72保留新增上下文并关监听；不改门槛/训练/补组。现有keywordPCM骨架依赖已移除模块/旧后端计分，不能直接启用为C3录音证据；实际触发前输入另需实现。candidate-miss-progress-ux196/plan.json。


### UX196 结果 — 2026-10-01
共享candidate/fast汇合处的缓存提示准入已实现，65主机检查与有声/无声构建通过，无新任务/大缓冲。唯一中粤中组首唤醒3/3、完整对话2/3，第三录音约2.97s返回limit/40368上传未完成；根因未定，不称10s上限。最低堆54664B、无复位。prefetch off走普通native fast，candidate miss/新增fallback均未观察，不能认领修复实机通过。外录来源对齐通过，前两回答起始候选1.414/1.066s，第三unknown不提升。最终app1502000B/SHAa49eea5a7910f0b9149eb424e8b3fec39c6cd4151e200aa80e8d30886bdf764e；中间缺inc包与编码辅助错误保留，完整冻结匹配烧录。两guard全备份只应用/非应用不变，恢复72/740/gain1与voice/wake off，新context1242/873624B/next4469保留，USB释放。不补组；docs/VOICE_CANDIDATE_FALLBACK_REPORT.md及candidate-miss-progress-ux196/closure.json，完整目标与误唤醒未完成。


### UX197 计划 — 2026-10-01
用户误唤醒仍优先。增加仅诊断构建的C3真实关键词输入USB观察：借空闲engine arena作有界SPSC队列，512 PCM推理前复制，推理后附E/L/融合分数、sample clock、事件/序号/CRC；复用主任务USB，无常驻录音大块/新任务/Flash写入。先sanitizer所有权/队满/取消检查、有声诊断与无声构建；同模型740/gain1/旧2of3。guard备份只应用后唯一60s未标注环境流+24既有TRAIN源各6.016s/1s前置/gain.35/rms.14，来源不重选不补失败；资源/DMA/传输失败停批。完整CRC/连续帧及实际C从固定prime重放逐分数/事件一致，标签须对齐；非独立泛化或小时误报。结束一律guard恢复72关监听保留数据，不训练/扫参/额外组。wake-listener-input-ux197/plan.json，整体目标未完成。


### UX197 结果 — 2026-10-01
真实关键词输入观察已完成：66主机/5KWS检查及有声诊断无声构建通过；唯一60s环境+24固定TRAIN源共6387帧连续CRC，E/L/融合/事件与实际C精确prime逐帧相同，24来源对齐/满幅0。中粤正各6/6，负中0/6粤1/6(小燕)；环境15.072s一次未执行检测，离线ASR前置对白，无人工标签不计小时误报。最长推理8032us/复制33us、最低堆69176B，无DMA/复位，非云对话资源通过。43x1064槽借45752B既有arena；相对UX196应用+3472B/静态+72B/新任务0，仅诊断。app1505472B/SHA b74c15d163dc4d7deb7368cc52592fb507b8903bcbbf28f7f9a08ca5ca23ac6d，最终237源包匹配，前置漏diag/cJSON包保留未用。两guard备份核验只应用，全观察非应用亦字节不变，恢复72/740/gain1/voicewake off/context1242及USB释放。不训练/扫参/补组；输入路径本次一致不等于识别已修复。docs/WAKE_FALSE_TRIGGER_REPORT.md与wake-listener-input-ux197/closure.json，完整目标未完成。


### UX198 计划 — 2026-10-01
依据UX197实际关键词PCM/分数逐帧证明，唯一L微调改用24对齐真实输入域；未标注60s环境不训练，E/740/gain1/结构/old2of3/RAM冻结。原train前缀精确、val/test/norm字节不变；2000步/seed202610013/lr3e-5/negpeak1/focus仅device-extra-listener-ux197-0.5/device0.5，TRAIN512校准及C parity。预先采用实验级门槛：旧负<=3/636、旧中>=44/45粤>=32/33(每语言至多1退步如实报告)、compact中>=15/16及device/partial/提前不增；实际12正全命中/12已知负0只是in-sample准入。全部通过才独立72源仅换L/精确zero seed/version，同正常1540096B预算，guard试刷；唯一18源+45s要求中粤各4/4负0/10，然后唯一中粤中连续输入业务3/3/heap48KiB/无DMA复位。任前置失败不刷不补，任实机失败guard恢复72关监听保留数据，不重训扫参看test。wake-listener-repair-ux198/plan.json，原语音目标保留。


### UX198 结果 — 2026-10-01
实际监听域24TRAIN补充168例，15623旧数组精确前缀/valtestnorm字节不变；未标注环境不训练。adapter缺gain首次失败保留，修正真实.35后新目录导出，未重启训练。唯一2000步约22.2s、预定val loss选350，TRAIN512校准及4096帧整数parity/5检查过/RAM12632。旧中45->43、粤33->28、负5/636->4、compact15->13，实际12正保留但负1/12未消除；超过事前每语言至多1退步门槛，否决不烧录不补组不扫参看test。未标注环境事件1->0未用门槛不冒充小时误报改善。原72/740/gain1/voicewake off/context原样/USB释放，全部失败源码模型记录保留；docs/WAKE_FALSE_TRIGGER_REPORT.md、wake-listener-repair-ux198/closure.json。完整目标未完成，先检查表征可分性/量化饱和再决定下一改动。


### UX199 计划 — 2026-10-01
UX198否决后只做原L表征/量化诊断：6387实际512帧一次提取40输入/11隐藏层/输出，固定head公式必须与全部原L分数一致，统计INT8 rails。只24标注TRAIN源作保守约束：正例原命中两最高投票块>=268+8，负例源区间+800ms三块mean<=268-8；环境不入约束。一次最小L1输出层可分LP，24权重[-128,127]/偏置q8±65536、原scale/其他层不变，现有WindowsSciPy/HiGHS30s。若可行只舍入一次并实际C detector重放，分析头不导出模型不刷板，不拿val/test优化、不扫参/新训练/声音/USB/云。原72关闭自动监听、数据保留；不可行只解释本次保守约束。wake-head-diagnosis-ux199/plan.json，整体目标未完成。


### UX199 结果 — 2026-10-01
6387实际帧原L输出公式全部精确，原C detector事件全部吻合。输入rail占0.0207%，隐藏层高rail为0；未把此统计冒充饱和因果。唯一642约束最小L1 head LP可行，舍入一次后TRAIN正12/12、近似负0/12，未标注环境事件消失但不计验收。24系数与偏置改变幅度较大，尚未验证原独立对照，不导出/烧录、不标记误唤醒已解决；原72设备未动。wake-head-diagnosis-ux199/closure.json保留约束/表征/求解器/整数重放，下一步仅一次原冻结validation准入。


### UX200 计划 — 2026-10-01
UX199冻结LP输出头只导出一次，原L前11层/scale/E/740/3块平滑/结构不变；5主机检查和4096帧整数parity，再一次原validation旧中>=44/45粤>=32/33负<=3/636、compact>=15/16及device/partial/premature不退步，实际TRAIN输入12正/12负准入。失败不重新求解/舍入/训练/看test/扫阈值。全过才由原72源码独立构建，仅L/seed/version变，1540096B预算，guard app-only，固定18实机和45秒环境，再3连续会话/minheap48KiB门槛；失败还原72保留历史。wake-head-candidate-ux200/plan.json，整体任务未完成。


### UX200 结果 — 2026-10-01
冻结输出头5主机检查及4096帧整数parity通过，无RAM增加；TRAIN实际12正/0负表现保留，但原validation中45->42、粤33->26、负5->5、compact15->14，premature增加。只适配24输入的LP泛化不足，否决不烧录、不补解/舍入/训练/换源/看test。原72/关闭自动监听设备未动。wake-head-candidate-ux200/closure.json保留失败，任务仍未完成。UX199 close首次Windows默认GBK读取UTF8失败，补-X utf8后成功，不涉及数据/模型变化。


### UX201 计划 — 2026-10-01
UX200仅24输入过拟合，改为一次广TRAIN保留可行性诊断：15791TRAIN冻结E/L整数表征，float64批量卷积先8例逐层对齐C，val/test不进入表征/求解。所有原TRAIN有效正例保留2票(旧+2与276取小)，所有负例warm后3块均值<=max(旧,260)，再加原642实际约束；唯一30s最小L1 LP/原scale/INT8界，失败停止。若可行仅一次舍入/导出/原validation准入；要求中45/45粤33/33全部保持、负不超过原5/636、compact>=15/16及device/partial/premature不退步、实际12正12负正确。目标是实物近似词改善并保留宽语音识别，不冒充val/hours误报全面减少。全过才延用UX200原72独立源码/1540096B/guard/18实测与3连续门槛；不重解/扫参/造音/看test。wake-head-preserve-ux201/plan.json。


### UX201 结果 — 2026-10-01
15791TRAIN共4042496帧，8例1085440内部整数值与C逐层精确，全部L头重构精确，提取203.4s。唯一630076约束LP(6097原有效正/9496负/642实际约束)7.28s报告不可行。仅说明冻结L24维输出头不能满足这套保守广TRAIN约束，不证明所有模型不可行；不重新解、不导出/valtest/烧录。原E网另有24隐藏维已付出运行资源，下一步可验证利用两成员共48维联合判别是否有额外可分信息，无须增加模型/运行RAM。wake-head-preserve-ux201/closure.json保留本次失败，任务未完成。


### UX202 计划 — 2026-10-01
L24广保留约束不可行后，只利用已经运行的E额外24隐藏维做一次联合48维判别；两个现有线性head联合拟合，无第三模型/推理库/结构/RAM/MAC增加。补提E TRAIN隐藏、8例C逐层parity，复用UX201 L/分数；6387实际E分数/隐藏对齐，未标注环境不拟合。同630076约束唯一30s LP/INT8原scale，舍入一次，合并bias一次拆分保持原E/L偏差至1整数；两head各4096parity/5主机检查，原val中45粤33全保持、负<=原5/636、compact>=15及device/partial/premature不退步、实际12正12负正确，失败不优化。全过才原72独立源码只head/seed/version变/1540096B/guard应用更新及18实机+45s环境+3连续会话门槛。所有失败留档不看test/不扩大音频组。wake-joint-head-ux202/plan.json，整体未完成。


### UX202 结果 — 2026-10-01
补E24隐藏提取12.68s，8例与C逐层精确，全部TRAIN E分数及6387实际E分数吻合。联合49变量/630076保留约束唯一LP17.92s仍不可行；只否决本次强约束原表征线性方案，不推断所有分类器不可行。停止线性head求解，不导出/valtest/烧录/重解。两个原模型完全保留，原72设备未动；wake-joint-head-ux202/closure.json。后续若尝试非线性规则必须单独冻结计划与验收，不能包装成现成修复。


### UX203 计划 — 2026-10-01
停止线性头后，只做一个depth3负例否决树；E/L/头/scale/740/gain1全不改，利用既有48隐藏INT8特征，在原3块高分后、detector投票前最多3次比较，无额外等待/模型/RAM/任务。TRAIN高分负和有效正投票帧+24实际TRAIN源，按clip归一权重，实际负权8，唯一CART(entropy/minleaf4/seed202610014/sklearn本机1.9.1)，只有0正且至少2负source_group叶子可否决，其余放行；环境不训练。单一原val准入中>=44粤>=32负<=3/636、compact>=15及device/partial/premature不退步，实际12正12负正确；失败不实现C/扫树/看test。通过才C11默认OFF集成、特征随机/节点/6387 raw parity、66主机+有无audio构建、原72独立源码仅gate/hash/version/buildflag变。normal1540096B目标；如实测需保留区，明确新wake-verifier guard上限+1024B且不越1572864B槽、不冒用protocol诊断，分区/2MiB上下文/448KiBclip不变。guard/固定18源0近似负、45秒环境、3连续会话/48KiB门槛全过才保留，否则还原。wake-veto-tree-ux203/plan.json，任务未完成。


### UX203 结果 — 2026-10-01
唯一depth3 CART/15节点/33001高分TRAIN帧(32233正/768负/78实际输入)完成；8叶均混有正例，0可用纯负否决叶，故不会过滤任何误触发，直接否决而不看val/test或集成C/烧录/修改guard预算。未改树深度/seed/权重后补调参。原两个NN/head保持完全不变；现场USB确认原0.11.72-summary/740/gain1、idle、voice/wake off、上下文与UX198完全相同，USB已释放。此前抬阈值/固定等待、两次整网小修、末层/联合线性与本次小树均未形成可交付修复；停止本轮这些方法，不把有限诊断结束标成误唤醒解决。docs/WAKE_FALSE_TRIGGER_DIAGNOSIS.md与wake-veto-tree-ux203/closure.json说明当前状态/证据/限制，整体目标未完成。


### UX204 计划 — 2026-10-01
已有声调诊断中正/近似词音高重叠，不能据此做硬声调门槛。转查UX196连续第三轮40368samples/limit的独立稳定性故障：现capture_joined混合source/network/join错误，先加三处错误归属，返回/边界/取消/业务均不变，无新缓冲任务，不把第三轮错误直接称10s上限或会话上限。主机同limit三来源故障注入/完整sanitizer、有声无声build；冻结UX196选项E/L740、stable/keywordPCM OFF。正常1540096B以内guard全备份仅应用/非应用不变，一组固定中粤中greeting/reuseON/prefetchOFF/gain0.35，保留录音和状态、失败不补组。观测故障后再按证据修，不清DMA损失/抬限制；无论结果均guard恢复72/voice和wake关闭，保留新历史并释放USB。误唤醒及完整业务目标仍未解决。capture-fault-owner-ux204/plan.json。


### UX204 结果 — 2026-10-01
source读取/ASR上传或poll/收尾join三处错误归属已加入，三个同limit故障注入均验证取消与清理、无PCM/工具/提交。初始64/66通过、2项新测试误用了旧early-commit专用夹具；独立join_fault修正后仅重跑受影响2项，全部66项覆盖通过，错误日志保留。有声/无声编译成功，0.11.219-capture-fault/app1502144B/SHA5625256ab7abc7398dfb61de3699fe257a5e8679736f41d04c43434c2fce287e，正常预算余37952B，完整实现与匹配ESP-SR/vendor归档434文件。唯一中粤中3轮首唤醒3/3、完整输入/任务3/3、minheap54640B、mic_overruns/DMA/复位0，旧第三轮limit未复现、3处错误事件0；因此未证明故障修复，不补组/清DMA/抬限制。USB证据查询改变间隔，不称即时重唤醒/1s或误唤醒验收。两guard全4MiB备份/验证仅应用，非应用相同，恢复原72/740/gain1，自动voice/wake关闭，新6事件与此前历史保留，COM5释放。capture-fault-owner-ux204/closure.json；总体与误唤醒未完成。


### UX205 计划 — 2026-10-01
UX204连续3轮通过但旧limit未复现，诊断返回原72/off。误唤醒继续一个局部距离否决可行性实验：现E/L末层48INT8不变，已知TRAIN小燕实际首次false前4因果块各作中心，r²=所有TRAIN正例全部128块及实际12正例全部块最近距离²/4，无val/test/环境拟合或半径/数量搜索。4中心208B常量/192乘加、无新任务堆或固定等待；先原C detector实际24输入12正全保持/12负0，失败即停。过后原val中45粤33全保持、负<=5、compact>=15及device/partial/premature不退步；再独立原72源C11可选hook/parity/6387raw/全部主机与有无音频build，正常1540096B优先，确需时仅明确wake-verifier余量<=1024B且不改分区。全过才能guard后一组固定18源8正10负与45s未标注环境、3连续语音/minheap48KiB；任何失败恢复72/off并保留数据，不称总体场景误唤醒已解决。wake-local-veto-ux205/plan.json。


### UX205 结果 — 2026-10-01
四局部区域r²45/53/49/28，最近正距离²181/215/199/114；保护6295 TRAIN正例全部块+实际正例共808016块。原C detector回放24已知TRAIN输入12正全保持、唯一小燕误触1->0。原validation的C双NN/逐层隐藏/平滑与原分数逐值相同；中45/45、粤33/33、负5/636、compact15/16均不变，局部否决0块，实际60s未标注环境事件241152也不变。尽管不退步准入通过，此方案仅覆盖已见样本极小范围、没有独立误报改善证据，不作为频繁误唤醒修复继续C集成/编译/烧录或18源补测试。无半径/数量调整或val拟合，不看test。现场原72/740/gain1/voice与wake off、与UX204上下文一致、COM5释放；wake-local-veto-ux205/closure.json。整体目标与误唤醒未解决，末层局部方案未形成泛化证据；下一方向需完整因果声学序列和独立hard negatives，不能延续相同末层调参循环。

UX204软件时钟补充：三轮VAD事件到speaker_start为616/600/885ms，ASR final到speaker_start为342/327/610ms。仅设备软件计时，不等于说完到听到，不是声学1s或立即重唤醒验收；原报告和software-clock.json全部保留。


### UX206 计划 — 2026-10-01
UX204诊断/3连续通过与UX205局部否决无独立覆盖均是进展，不是修复完成。下一步一次完整因果序列可分性诊断：隔离复制现纯C角距离DTW内核，仅64x20，40原INT8 frontend相邻双频带向零除2，每512样本取一帧，过去2.048s无新增等待；沿原band8/warp205，negative+205<positive才否决。每待测source_group完全排除TR模板，variant0非device/TR两声源每语言，各一正及近词负(中小王/粤小燕)，声音/clip按SHA排序固定；正模板到声明event_end、负模板到原mean3 warmup后最高块，8模板4正4负，不复用HiLexin权重。独立Python/C整数parity/chronology/极值/无效输入sanitizer后，一次6声音留声源外实际13触发：12正须全保持、已知小燕须拒绝，否则停止不扫bank/半径。未标注环境独立只报告，不拟合；全过才原val一次中45粤33保持/负<=3/compact>=15及其他非退步，不看test。无生产代码修改/烧录/长训练/物理补组；只有全部离线通过另列C3资源/实时pilot，保留分区2MiBcontext/原72回滚，任何实机阶段至少3连续会话。wake-temporal-diagnosis-ux206/plan.json。


### UX206 结果 — 2026-10-01
隔离64x20纯C时序核sanitizer、chronology/constant/极值/无效银行通过，独立Python/C16例normalize20480值/DTW128分数逐值相等。6个source_group模板排除待测声音的一次实际6387输入回放：中6/6、粤5/6，小燕负误触1->0但正确粤语am_michael/trial13被拦；其pos3087/neg2700，近词pos3286/neg2776，固定margin205把二者都拒绝。未标注背景事件241152仍保留。失败停，不改margin/bank/窗口，不进入val/test或生产C集成/实机。host结构2568B/模板20480B/score静态栈1360B/平均约238.5us仅主机值，不能当C3预算或32ms实测；原HiLexin模板完全未复用，production不变。现场原72/voice和wake关闭、context1248与before一致、COM5释放。wake-temporal-diagnosis-ux206/closure.json；证明完整序列提供不同判别信息，但这个固定模板判定会损伤粤语，非可交付修复。后续若用此表示须TRAIN-only校准/独立验证，不能按这13条重调margin。


### UX207 计划 — 2026-10-01
UX206固定margin时序判断已停(会漏正确粤语)，不按13个触发改阈值。单独一次TRAIN-only策略学习：同64x20时序8个原Q12距离，模板只两种语言各两个不在实际6声音组内的TRAIN声音(即剩余组，按SHA选)，每声一正一近词负，同既定窗/DTW算法。仅原15623 TRAIN前缀、排除168实际输入补集；全部warm滚动2/3可能投票窗口，正例有效endpoint内全部保留，负例全部压制(包含被否决后不产生cooldown的重试机会)。C核一次提8距离/来源/hash，唯一次9整数系数INT8权/bias±2^20、margin32、30s HiGHS MILP最小L1可行性，矛盾/不可行/超时即停不改bank/窗/参数或转更深网。可行先逐整数约束核对再冻系数，原24实际12正全保/12负0、原val中45粤33/负<=3/compact>=15及其他非退步各一次；实际/val/未标注环境不拟合、不看test。全过才另列C3 prime/历史/拒绝不制造cooldown/空间及RAM/32ms/3连续会话pilot，当前不烧录/新物理组/长训练。原72/off与2MiBcontext保留；wake-temporal-policy-ux207/plan.json。


### UX207 结果 — 2026-10-01
全64x20时序的8个Q12距离、模板仅TRAIN两种语言各yunxi/yunyang(排除全部6个实际输入声音)，冻结bank后原15623训练例提取32366投票机会：正31655(6088/6259例有有效机会)、负711(180/9364例)，无完全相同正负距离向量。仅一次9整数系数/INT8权/bias±2^20/margin32的HiGHS MILP在约0.219s判不可行；这是声明的全正保留/全负拦截线性问题，不是模型无法改进的结论。停止本候选，不改bank/阈值，不评分实际/val/test，不训练CNN、不烧录。原72/wake和voice关闭、context1248保持、COM5释放；wake-temporal-policy-ux207/closure.json与constraints/lineage保留完整来源/校验。


### UX208 计划 — 2026-10-01
UX207线性全负分离已不可行关闭，转一个不同的有限非线性否决器：完全复用原TRAIN32366投票窗8个DTW距离及UX207固定bank，不改特征/声音/窗/margin；唯一entropy深4/minleaf8/seed20261001树、每原例按窗数均分且两类各总权0.5。只有0正窗且>=8不同TRAIN负例/>=2负source_group叶可否决，其余接受；无叶立即停。逐TRAIN整数阈值一致、全正保留后冻叶；隔离C11全32366个features判定与Python一致。实际24输入用原C detector事件后否决(拒绝清vote但还原旧cooldown)完整因果重放，12正全保/负0才看原val一次，原中45/粤33/负<=3/636/compact>=15且device/partial/premature不退步；环境不拟合不作小时误报，不看test。任一失败不加深、不放宽纯叶/不改bank或声音；当前不新物理/烧录，唯全离线过才另声明应用空间/RAM/32ms/3连续会话C3pilot。原72关闭wake/voice、持久化与其他功能保留；wake-temporal-tree-ux208/plan.json。


### UX208 结果 — 2026-10-01
唯一TRAIN-only DTW8距离entropy深4树29节点，32366个整数feature路径与sklearn逐值一致；所有叶均未满足冻结的0正窗/>=8不同负例/>=2负source_group条件，可用否决叶0、训练负过滤0。按计划停止，不加深/放宽，不评分实际/val/test、不编生产/烧录。此结果仅该固定表示及depth/purity条件失败，不证明下一声学模型不能工作。原72/wakevoice关闭、context1248与UX207一致、COM5释放；wake-temporal-tree-ux208/closure.json。固定阈值/等待/末层纯叶/固定模板距离线性或纯叶策略均未证明泛化；下一方向改为固定原CNN输出上的小型因果时序网络，先一次有限TRAIN-only训练及C整数一致性，不延续DTW调参。


### UX209 计划 — 2026-10-01
关闭UX207/208的DTW线性/纯叶策略。唯一小型因果声学复核网络(原E/L CNN全固定)：每512取两CNN末层48INT8，PW48->8/ReLU、DWk9d1 signed、PW8/ReLU、DWk9d8 signed、PW8/ReLU、PW8->1 Q8。805参数/764权重B/164biasB、历史576B是估算非C3测量；Q7权、Q12bias、Q5隐层、精确ties-away fakequant，全纯C固定状态，无BN/新任务/堆，候选Q8>=0才接受、拒绝清vote还原cooldown，无固定等待。原15623TRAIN、排除168实际输入补集；中粤正各25%/负50%机会例，正acceptedendpoint BCE、负top3 maskedsoftplus；seed20261001，唯一次2000step/batch32/4threads/lr.001、900s上限、只最终checkpoint，不看val选epoch。训练每语言原valid保>=99.5%、负例触发<=baseline一半；失败不续训/扫架构阈值。独立NumPy/PyTorch/C ring整数/极值/random/6387实际隐藏一致与原C detector/精确prime；实际24输入12正保持/12负0，才看一次原val中45粤33/负<=3/compact>=15及device/partial/premature不退步，不看test。全离线过才另声明原72 C3应用预算(必要显式<=4KiB余量)、堆/32ms/3连续会话pilot；本阶段不烧录、不改原CNN模型/分区/上下文、无私人诊断音上传/新TTS/多日训练，不能把805参数资源算术当实机证明。wake-sequence-verifier-ux209/plan.json。


### UX209 结果 — 2026-10-01
唯一TRAIN-only 2000step完成7.799s，仅最终checkpoint；估算参数原805加总错误前置断言纠正实际705(664weight+41bias)，第一前置优化0step；同一次训练结束np.int64 JSON报表异常，post_report.py只从冻结final.pt/已存scores恢复报告，无第二次优化。QAT/独立float64 Conv1d整数导出全部15623x128分数逐值相等。TRAIN负例触发180->55，但有效普通话3542->2452、粤语2520->1669，远低于99.5%保留。按计划停止，不C集成/hostkernel/实际或val/test评分、不再续训/改阈值/架构。本结果不是主观或实机识别成绩。原72/wakevoice关闭、context1248一致、COM5释放，wake-sequence-verifier-ux209/closure.json。末层后置低维独立网络损伤正确识别；下一方向若扩大模型须先证资源，再训练，不延续小复核器调参。


### UX210 计划 — 2026-10-01
UX209后置小网因正例退步已停。先纯主机验证新的层间交互原型：原E/L两个24通道神经state并行逐层，原stem/DW/各自PW/head/前端/平均/2of3不变，在5个PW加入另一分支24x24 INT8同receiving requant shift；0cross必须与原双独立网全部state/265层trace/分数逐值完全相同。新增5760cross权重B、每256额外5760MAC是估算；复用原2state、无新神经RAM/任务/堆。隔离C11核先bias/各部分累加界/错误/size/position/null+ASanUBSan；4096固定极值random0cross逐层/最终state精确、8组非零/INT8极值cross与独立直接时间索引整数Conv全部12层比对。测host结构/栈/ROM/timing不称C3指标；本阶段不训练/读实际-val-test拟合/固件构建烧录/新物理组，原72/off数据保留。唯host过才另列C3资源pilot(必要显式<=8KiB应用余量、现1572864slot/2MiBcontext不变、真实32ms/堆、guard回滚)；唯实机资源过才另列有限cross-only训练。wake-paired-kernel-ux210/plan.json，不能称误唤醒修复。


### UX210 结果 — 2026-10-01
隔离逐层并行双CNN交互C11内核ASan/UBSan、NULL/size/position/额外cross partial-int32边界通过；初次独立exe漏libm导致原PCEN sqrt链接失败，原日志保留，仅补-lm重连，未改算法。zeroCross4096固定/极值/random输入对原两个C网逐帧265层、input、完整神经state字节精确，共2170880层值；8个非零(含INT8极值)cross*256帧由独立NumPy直接时索引、TorchConv1d、C ring逐值一致1085440值。原两state6252B，原型仍6252B、新神经state0B；cross权重5760B、host-O2 step栈320B，host含ctypes/不含FFT每256原13.491us/交互14.717us，不是C3耗时/应用/RAM证明。保留host内核、不训练/不评分实际-val-test/不固件烧录。重要移植契约：host数组2state连续，但原fusion.first.neural/second不连续，C3前须改为两个独立state指针接口并再次精确parity，禁止强转fusion为该数组；还须独立guard资源pilot证明app/heap/32ms才可交互权重有限训练。原72/wakevoice关闭、context1248一致、COM5释放；wake-paired-kernel-ux210/closure.json、docs/WAKE_PAIRED_KERNEL_REPORT.md。总体目标/误唤醒仍未修复，待新声学判别训练与独立验收；语音响应/VAD/streaming/连续3轮目标不撤销。


### UX211 计划 — 2026-10-01
上一goal turn为progress：有限失败策略关闭、新纯C逐层交互内核通过原网/非零整数证据；无同一外部blocker。仅一次C3资源pilot，先把连续2state主机API改为两个不重叠独立指针(原fusion两个字段不连续)，gap/alias/错误不改state+ASanUBSan、4096原网全state/trace和8冻结非零fixture全部重核。完整原72/733文件source.zip独立project，新增默认OFF KWS_PAIR_DIAGNOSTIC USB bench，不替换原正常唤醒计算；版本0.11.220-cross-budget。只有专门flash_guard互斥标签allowance<=8192B(1548288<现1572864slot)、默认1540096及protocol1024/CRC/分区检查保留并测试边界，才可安装。256blocks*原/0cross/冻结小非零cross3阶段一次，自产syntheticPCM，无麦克风/扬声器/ASR/LLM和私人诊断上传；5760B constFlash，不常驻额外state/任务/渲染堆。原/0cross前端/主trace/两head/融合/事件CRC相等；每512两256完整计算计时之和(排除产生数据/CRC，明确边界)max<32000us、workspace12632/神经6252及测量期free>=48KiB、释放session/栈/heap与数据记录。guard完整4MiB备份+校验只app+读回nonapp一致，任失败停且已装则guard恢复SHA精确原72/off/原context/释放COM5，不留未训练候选。唯资源pass才另声明有限cross-only训练；本阶段不训练/评分实际-val-test/新的物理唤醒或对话组，完整语音目标与物理3连续会话要求保留。wake-paired-board-ux211/plan.json。


### UX211 映像门槛结果 — 2026-10-01
独立状态指针/gap/alias、零交互2170880值与非零1085440值主机证明通过，USB基准同源CRC通过。修复诊断FFT include、SDK asm拼写与uint32打印类型后编译通过，app1548512B，超过已冻结1548288B诊断预算224B，guard明确拒绝；停止此pilot，未烧录/未C3测量/未训练/未改变原72。原分区表SHA一致；完整source-0.11.220.zip/映像/hash保留。主CMake后续C11属性覆盖了较早诊断source编译选项，故不宣称USB静态栈报告；pair RISC-V静态176B不是整任务栈。总体/误唤醒未完成。


### UX212 计划 — 2026-10-01
先关闭UX211映像失败，保留完整原诊断source.zip/应用。此阶段只压缩USB基准JSON为版本化固定顺序协议、改label221、修正编译属性顺序；两状态/模型/交互5760B/固定输入/计时/CRC/堆采样不变。只重核受影响USB基准同源ASan/UBSan与预期CRC，一个新构建，仍1548288B上限/原分区；不过即停，不加预算。过才一次3phase×256块C3资源pilot，完整guard备份仅应用，始终恢复原72/off/上下文/灯。无诊断麦克风/云请求/训练；总目标未完成。


### UX212 资源门槛结果 — 2026-10-01
一个紧凑诊断构建/唯一资源pilot完成。源内核/权重/输入/计时与CRC不变，受影响USB基准ASan/UBSan原始、零交互、非零CRC2941312350/2941312350/497709998通过。app1548256B（诊断上限1548288余32B、原槽余24608B，不能作为正常发布包），SHA2a0030ad5ed5317e36cab1813f02363dd394bcd4b735a93f0ea035766b022149；736文件完整源码SHA402124c1a695a2cec01d9628eba19add6dbfde86a40543c0965f7609c031f7eb。两guard全4MiB备份验证、仅应用写入/回读、非应用相同；C3每阶段256×512样本，最长计算9894/9835/11102us（包含双256前端/NN/融合，生成/CRC/yield不计），work12632/state6252/权重5760B；堆采样before/low/after82184/82012/82164B，启动系统min80036B分开报告，USB任务raw栈2080。LTO保留，不凭缺失USB .su推断整栈。完整恢复原72/SHA22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07，off/off，1248事件/876424B/next4475/灯与此前一致，COM5释放。没有麦克风/扬声器/ASR/云请求/训练；资源门槛允许下一阶段另行有限训练，但误唤醒/整个goal仍未完成。size命令初试--format json当前SDK不接受，采用实际支持的json2保存两份报告。


### UX213 计划 — 2026-10-01
以UX212实测资源通过为前提，声明唯一cross-only训练：固定原E/L全部层/偏置/位移/双末层，只训练五处双向24×24交互5760参数。先验证FP32整数部分和界限、全部冻结fixture逐层数值、原TRAIN cached heads/平滑/真实C detector、有效梯度；过才seed20261001/2000step/batch32/threads4/AdamW0.01/900s一次最终checkpoint。仅原TRAIN前15623，排除168实际补充；P中粤各8、已有误触N8、其余N8，按真实2of3/3block平滑的有效端点最高强度计算softplus，阈值仍740/Q8268、margin64。全TRAIN输出对F64整数参考/16例C导出精确，然后要求中粤各保留>=99.5%、N至少减半且不新增误触、不新增提前事件；不过即停，不看actual/val/test。不重跑/改loss或阈值，不烧录；只有依次TRAIN→已知24实际输入→独立val门槛都过才另行实机三轮集成。模型尚未改善识别，不改总goal。


### UX213 有限训练结果 — 2026-10-01
FP32整数部分和上界1977039<2^24、8冻结fixture1085440值、16原TRAIN heads/平滑、35真实C detector流、5760有效梯度前置通过。预检报告误把NumPy export交给torch.count_nonzero；改为NumPy计数并缓存原x，未更新训练参数。唯一2000step/48.473s cross-only训练完成，INT8非零4673/5760、范围[-8,9]，全TRAIN7998976 head值对F64、16例2170880 C逐层值一致。最终JSON np.int64输出失败，保留原train.py、最终checkpoint/分数，独立报告恢复0个optimizer更新、仅补16例C证据；未重跑训练。TRAIN N180→24（-86.67%），中P3542→3432、粤P2520→2407，新增N5，新增提前粤P1，未过99.5%正例保留/新N/提前门槛。停，不查看actual/val/test、不改阈值/补训/烧录。TRAIN审计原有效P丢失中119/粤138，另新获中9/粤25；256丢失因固定阈值下分类不足，1因时间/冷却，遍及8个声音且原始variant0也退步，不能归咎仅噪声或USB时序；不删除难例。当前设备原72/off/off/WiFi，context1248/876424/next4475保留、COM5释放。后续需要在分类学习中明确保留正例并约束新增负例，而非重复抬阈值。整体goal/误唤醒仍未完成。


### UX214 计划 — 2026-10-01
关闭UX213后，根据256/257丢失来自分类而非时序，声明一次教师约束训练：仍只5760交互、原E/L与前端/阈值/推理内存不变、从0开始。原正确P在第一原成功窗口保留2of3分数floor=min(old,332)，原普通N上限=min(oldwarmpeak,267)，P词尾接受区间前上限=min(oldearly,267)；主误触N用softplus目标204，原未命中P用有效峰目标332。每例host乘子P/N初4、提前初2，每次违反+1封顶64；batch中粤P各8、旧误触N8/其他N8，各组内不放回。固定4000step/AdamW0.01/seed20261001/900s一次最终checkpoint，没有验证集模型选择。先仅测试新约束语义，复用hash相同的数值证明，再按同99.5%中粤/N<=90/无新增N或提前门槛，不过停、不读actual/val/test、不扫阈值/补调或烧录。后续物理对话仍>=3轮，goal未缩减。


### UX214 有限训练结果 — 2026-10-01
新目标5项语义检查通过，原数值模块hash不变。唯一4000step/98.415s教师约束训练完成；5760交互中4420非零、范围[-11,8]，全7998976末层值对独立F64及平滑一致，16例2170880 C逐层值一致，原E/L不变。TRAIN中3542→3510（净-0.903%）、粤2520→2509（净-0.437%）、N180→80（-55.556%）；新增N1、新增提前中P1。仍未过普通话99.5%保留/无新增N或提前门槛。停止，不读actual/val/test、不补训/扫阈值/烧录。原72/off/off、context1248/876424/next4475不变、COM5释放。教师约束缩小了退步，但未证明频繁误唤醒修复；整体goal继续。


### UX215 计划 — 2026-10-01
UX214已关闭、不再评分或调参。独立检查重监听保护期：音频已有PCM送入而忽略hit，疑似仍消耗模型24000samples冷却、votes带入开放。先原C固定分数复现再修：每块armed决策门，guard保留声学/神经/平滑/时钟、清votes、不产生事件及新冷却；保留真实冷却、warmup、阈值与500ms保护，无新状态/任务/模型。ASan/UBSan含legacy/realcooldown/gap/PCM历史一致、缓存hash再生成、完整KWS与受影响Agent检查、有声无声build正常预算。过后仅一组中粤中真实连续对话、不加轮间证据查询、不重试/扫参数；guard全4MiB备份仅应用、始终guard恢复原72/off/off、历史保留并释放COM5。此修正不能宣称解决背景人声近似词；goal继续。wake-rearm-gate-ux215/plan.json。


### UX215 保护期决策修正结果 — 2026-10-01
原C复现2of3/4连续两模式：保护期被忽略hit各1，后续有效运行各0，残冷却8640/9664samples。新增每块armed门后guard仍推进前端/NN/平滑/样本钟与warmup，清votes、不接受hit或新冷却，真实已接受冷却保留，原窗口500ms/阈值/权重不变；无新增状态。原C对16384正常armed块/220事件所有输出与状态字节一致；256双/单PCM各全声学状态/分数一致，guard/真实冷却/gap/参数/noallocation ASan/UBSan及全部KWS6/Agent66检查通过，缓存内容相同只更新hash，有声/无声build通过。222 app1502192B/SHA5caa44d714e7a14ea2fcdcf4193600060087250adaad7279c8775429110da9c0、正常预算余37904B、463完整源码文件与匹配vendor/ESP-SR留档。唯一一组中粤中连贯3轮，首唤醒/完整输入/任务3/3，后两轮真实source相对@done间隔266/265ms，无轮间采集/状态查询额外等待；minheap54624B，mic_overruns/复位0。软件VAD事件到实际speaker_start [616, 601, 885]ms，不含真实说完到VAD；初报误查cached progress_playback为空，按已有native speaker_start报告修正，无重播/再请求。两guard全4MiB备份验证、仅应用更新/回读、非应用一致，恢复72/740/gain1/WiFi、灯保持，新6历史保留（1254/879268/next4481），off/off、COM释放。保护期吞冷却可复现问题已修；不能据3轮成功宣称所有立即唤醒或背景误唤醒/1s主观体验通过，overall goal继续。wake-rearm-gate-ux215/closure.json。


### UX216 因果标签诊断计划 — 2026-10-01
UX215为已执行进展：保护期决策修复、72主机/3连续首唤醒通过且完整恢复72，背景分类仍未完成。现在只审计原TRAIN15623/保存E-L分数，排除168 actual，不训练/重推理/读valtest/音频或烧录：原正例首个有效事件处的精确因果feature前缀是否同时是负例前缀，hash筛选后逐字节证实，列同历史矛盾/来源与负例后续分歧、可行更晚正例机会。不得删除难例或改变标签/门槛，一次有限诊断，按证据决定后续独立实验；goal完整保留。wake-causal-label-audit-ux216/plan.json。


### UX216 因果标签诊断结果 — 2026-10-01
3项hash索引/实际字节证明/全重复检查通过。原15623 TRAIN/6062首个原有效正事件、61种前缀长度扫描仅3.411s；跨正负首事件因果前缀匹配0，全256帧跨标签重复0。未发现假设中的直接矛盾，不能称全部标签正确或约束一定可行，不改标签/删除难例。没有新模型推理、训练、actual/val/test或烧录，设备原72/off/off/context1254/879268/next4481不变、COM释放。下一有限原型可增加现有两支时序信息交互，先证明无需新历史内存和整数一致，不据此预言分类改善；goal继续。wake-causal-label-audit-ux216/closure.json。


### UX217 时序交互原型计划 — 2026-10-01
原5760 PW交互仅瞬时通道混合，分类目标仍未达标。先有限验证增加5个DW位置(1/3/5/7/9)另一支同通道5tap因果滤波，dilation沿用1/2/4/8/16：新增1200 INT8及每256frame1200MAC，两原支历史原6252B复用，必须先计算双支输出再同时提交历史，原stem/DW/PW/head/量化不变。独立状态指针/间隔/alias/上界检查，零4096帧每层及状态完全对原，旧8 PW fixture不变，新增DW-only/随机联合/极值/延迟impulse/环wrap NumPy直接时间索引+F64Torch+C逐层一致，FP32整数部分和<2^24/frozen/6960有效梯度。只主机C11/数值原型、不optimizer/评分data/物理录音/USB/烧录；过后另行声明C3资源，资源不过不得训练。不降低完整goal或误触验收，wake-temporal-kernel-ux217/plan.json。


### UX217 时序交互主机结果 — 2026-10-01
C11两支先算再一起提交原history，防branch0更新污染branch1交叉DW；原DW bias可NULL按0处理，margin PW48/DW10个极值项独立校验，unsafe DW原单支有效而cross无效用例通过。ASan/UBSan alias/间隔/尺寸/指针/位置/边界通过；零4096×双265=2170880层值及每帧完整原NNstate一致。原8 PW fixture1085440值保留，7新增DW-only/联合随机/INT8极值/因果延迟impulse各512帧=1899520值，NumPy直接时间索引/F64Torch/FP32STE/C ring全一致。仅6960可训练参数，head-only梯度6960有效，原buffers不变；所有整数部分和上界1977039<2^24。两支history仍6252B无增量，权重新增DW1200（总6960）；host O2栈352B、sanitizer656B，不称C3或整调用栈结论。冻结50文件主机源码/fixtures SHAdbdf5369c9770a6f4e1f12c7bdc556d7fbb594d972a78b0a4bcedfaa410f5947。不训练/评分data/actualvaltest/USB物理组/烧录，不把数值或容量称误触改善；后续必须先另行C3资源pilot。原72/off/off/context1254/879268/next4481保留、COM释放。overall goal仍未完成。wake-temporal-kernel-ux217/closure.json。


### UX218 时序内核 C3 资源计划 — 2026-10-01
UX217数值/梯度及原状态无增量已完成，按门槛先C3再训练。私有提取已冻结222当前Agent(含保护期决策修正)，diag223-temporal-budget，不动root。单构建/单C3资源pilot原/6960零/PW-only/同PW+DW四phase各256×512samples；3只读数组20880B只属diag，训练后正常只一6960B。CRC覆盖双支265层及所有6252NN历史，复用first.trace当secondary，只有externaltrace时开启不改正常调用；无新增trace堆栈。计时每块双256前端/NN/融合不含生成/全CRC/yield，无麦克风扬声器云或训练。相同源码host ASan/UBSan+fake资源/真实CRC，冻结源码/应用/分区，正常1540096B不增预算才guard。各max<32ms/heap>=48KiB/rawUSB栈>=1024、全CRC一致、会话释放、无DMA/reset，始终guard恢复原72/off/off/历史/灯，COM释放；不重pilot/缩上下文/称分类或全栈通过，过才另行有限训练。wake-temporal-board-ux218/plan.json。


### UX218 时序内核 C3 资源结果 — 2026-10-01
相同bench host ASan/UBSan/参数CRC/空会话与sequence边界通过，四CRC1482158500/1482158500/2101441176/1617045471与C3双层trace/全部6252NN字节一致。私有冻结基础归档发现managed_components/YAML/TEN-RV遗漏，补554+2+33文件、43固定VAD SHA检查，保留3次未完成配置与kissFFT include/C11 SDK asm两类编译失败，同数值/权重最后一映像；1055源码SHA/完整归档可复编译材料保留。diag223-temporal-budget app1526112/正常1540096余13984，槽余46752，不借诊断预算/改分区。idf size默认ccache触发意外重编译，仅终止自己的size进程树在链接前，原app/map/ELF仍逐字节相同；直接esp_idf_size json2读冻结map报告1525964(section总和非BIN大小)。一次4×256合成块、无mic/speaker/cloud/训练，原/零/PW/PW+DW最长10969/11509/11738/11548us(每块32ms预算)，采样heap最低85076、会话后全局min83064，USB raw栈1564>=1024，workspace12632/NN6252不增，复用既有trace。采样heap前后差28不归为NN泄漏，释放后idle100432vs100460，有预留功能回归仍需后续；不称完整voice堆/全部栈/分类或响应速度通过。install/restore两次guard均完整4MiB设备校验backup/app-only/readback/非app逐字节一致，恢复exact72/off/off/context1254/879268/next4481/2MiB分区/204800历史及灯，COM释放。唯一资源pilot通过，未训练且频繁误触仍未解决；下一另行有限训练而非默认多日/重复扫参数，完整goal未完成。wake-temporal-board-ux218/audit.json。


### UX219 时序交互有限训练计划 — 2026-10-01
UX218真实C3资源/CRC及完整恢复已通过，是新进展不是阻塞。一次有限训练只6960 PW/DW新增权重(其中时序1200)、原E/L所有权重/头/前端/阈值/6252NN状态不变；从0开始、不用失败213/214checkpoint，沿用相同教师首成功窗口/普通N/提前P约束及host乘子、seed20261001/4000step/batch32/threads4/AdamW0.01/900s，唯一最终checkpoint以隔离时序容量作用。复用hash相同的UX217数值/梯度及UX214五约束证明，新preflight只16原TRAIN cached heads/整数平滑/真实C事件/6960双tensor。仅原TRAIN15623、排除168actual；不先读取heldout，TRAIN每语言>=99.5%原正确、N180至少减半、无新N/提前P，不过即停，不重训/选checkpoint/扫阈值/删难例。过才一次known24实际PCM(新零PCMpriming)、再过才原独立val，test/flash不在本阶段，后续物理每组>=3连续业务，不缩完整goal。wake-temporal-train-ux219/plan.json。


### UX219 时序交互有限训练结果 — 2026-10-01
新6960双tensor/16原TRAIN cached head/19真实C事件序列preflight通过，复用未改数值/目标语义证明。唯一4000step/132.283s、完整导出及评分共196.664s，原E/L buffers不变；PW4320/DW823非零、范围[-10,8]，7998976末层值对F64及平滑全一致，16例2170880 C逐层值一致。TRAIN N180→65(-63.889%)无新增N，但中总有效3542→3494、粤2520→2496、新提前中P1，不通过，停在TRAIN、不读取独立actual/val/test、不重训/选checkpoint/扫阈值/烧录。源及checkpoint/6960B导出留档59文件SHA4e5788cb57e3dde7acb6135e2473d0c48d4af9ecccbde71cfe976873d433d4c7。只用保存输出再诊断：中原正确丢51/新增3、粤丢37/新增13，实际旧命中交集保留98.560%/98.532%，不能以净总数掩盖退步；88丢例87分数不足/1投票冷却，八声音和未增强变体均有，全部至少采样1次(粤>=4)，原P无漏采。最终首正窗口floor违反434、普通N ceiling106、提前P ceiling63、host乘子最高22<64，软hinge没有提供硬保留保证，不能归为次数未采到/上限太低/数值不一致。309普通N从未采样仍没有新增N，记录采样覆盖非泛化证明。后续先有限检验定义/硬约束可行性，不能重复同软目标调参或称新增时序已解决误触。原72/off/off/context1254/879268/next4481不变、COM释放，完整goal仍未完成。wake-temporal-train-ux219/closure.json。


### UX220 TRAIN 标签有限审计计划 — 2026-10-01
UX219 已关闭且不采用。只读原 TRAIN15623 的词面／语言／来源／截断标签，以及已保存原 E/L 和唯一候选分数；不加载 x/y、私人 PCM、168 actual、validation/test。一次120秒内检查完整目标词跨标签、现有普通话同音接受集合、按语言/文本的触发集中及截断标注证据。优化器/模型推理/调阈值/改标签/删除/云调用/烧录均0，不凭文字疑点自动重标或重训。设备原72及既有上下文保持，整体目标不缩减。wake-label-audit-ux220/plan.json。


### UX220 TRAIN 标签审计结果 — 2026-10-01
一次3.58秒只读审计：完整目标词跨标签0、现有普通话同音接受词被标负例0；原180触发中小燕/小叶86、时间截断38。截断边界未做人类音节标注，是限制而非已证实错误；文字审计不证明所有声学标签正确。没有足够证据重标/删样本，所有数据与设备保持。保存逐语言/文本计数、8文件源码归档与hash；模型推理/训练/actual/val/test/云/烧录均0，整体目标未完成。wake-label-audit-ux220/closure.json。


### UX221 新时序表示的全局保留可行性计划 — 2026-10-01
UX220 没有支持重标依据。独立诊断只冻结UX219的6960整数系数及原上游，原TRAIN15623/排除168actual，提取两支末层前24维/128block，全部saved score与8固定C全层一致，4thread/32batch/180s。只一次48权+联合偏置LP/30s/minL1，原6062正确首事件各两个强票>=271；唯一候选未触发的9299负例所有index61..127分数<=264，原65触发集合仍计误触、不删除；正例词前原低票同<=264。连续LP只说明该保守线性问题；一次ties-away量化/按原bias差拆分，过才TRAIN整数事件交集>=99.5%各语言/N<=90/无新增原负例及提前。不读actual/val/test，不训练/烧录/扫参/削弱约束；失败退出。原表示UX201/202不可行不直接决定新表示，阶段不发布UX219失败模型。wake-temporal-feasibility-ux221/plan.json。


### UX221 新时序表示的保留可行性结果 — 2026-10-01
8.95秒一次提取，全部保存分数与8固定C全层1085440值一致；独立整数头重建全部score一致、15623 detector流一致、201负例矩阵行索引对齐，未舍入分数最大误差2.0417/3Q8 margin(权重舍入另须实测)。唯一49变量LP在27.672秒判定不可行，844382约束=12124正确anchor+209225词前+623033负例票，保留6062正确并固定候选65负例允许集合。保守问题不可行不证明所有分类器或放宽条件不可行；无量化/重试/新增训练/actual/val/test/烧录。完整57文件诊断源码+固定系数归档及hash，原72/wakevoice off/context1254/879268/next4481一致、USB释放。停止该时序末层修补分支，若重新设计必须另声明有限模型/数据方案，不延续softloss/阈值/线性头重复。整体目标和误唤醒均未解决。wake-temporal-feasibility-ux221/closure.json。


### UX221 报表文字订正 — 2026-10-01
初始 close.py 追加文字中的未舍入分数误差 2.0417 是硬编码笔误；权威 check.json 实测最大为 1.5，三 Q8 余量保持。两份报告及这条记录订正，原脚本归档和初始日志保留，无重复提取/测试/求解。documentation-correction.json。


### UX222 声学信息有限诊断计划 — 2026-10-01
UX220标签和UX221一次LP为进展，输出层分支已关闭。本阶段不重复调参：冻结现有24开发监听PCM及其原源，每六声音/两语言的TRAIN小燕/小叶各SHA固定一源，合计<=72输入。只核原C Mel125Hz下沿/INT8饱和及source-device音高保留；pYIN0.10.2/65..500Hz/1024窗160hop/.8可信/末250ms>=6帧，90/160/240Hz控制<=2%，总240s。manifest仅元数据按train选源，无val/test音频/score、ambient拟合/新录放/云/优化/烧录/阈值F0窗搜索；自动端点/音高不作音节真值或重标，缺证据保留，不用低音高硬否决。先判是否有新声学信息，再另定纯C新模型契约，完整目标保留。wake-acoustic-info-ux222/plan.json。


### UX222 声学信息诊断结果 — 2026-10-01
原C前端68输入0.916秒；唯一pYIN分析13.109秒，90/160/240控制误差<2%、静音无可靠F0。44原源合计978可信帧，24设备32；严格成对24源应为456对32，不能用不等样本总量估计衰减率。24设备末250ms均不足6可信帧，误触小燕及对应正确粤语原录都0；已有32匹配帧音高接近原源。设备INT8饱和最大中0.6717%/粤0.3333%，原源F0<125Hz中7.71%/粤2.42%，不支持量化饱和或Mel下沿是主要原因，更不证明CNN未保留音高或词面标签有误。3个指定TRAIN近词源缺失保留，不替换。下一步单固定C11音高支路带限能否恢复可信匹配证据；无新录放/训练/修改设备/valtest音频score/私人云上传，整体目标未完成。wake-acoustic-info-ux222/closure.json。


### UX223 固定音高支路滤波可行性计划 — 2026-10-01
UX222 严格成对24源可信F0为456→设备32，原INT8饱和低。单新声学支路原型：16kHz一阶HP70Hz+两阶LP1000Hz，Q15系数31880/10642、20B状态、INT64中间/明确ties-away/有界状态，无堆；不改原Mel/识别/ASR音频。一次ASan/UBSan/独立整数/整块随机分块inplace全状态及静音DC脉冲极值控制，所有68旧输入完整因果过滤后固定pYIN参数/240s，对照24原声缓存真实可信点（20ms/1半音），不使用不等总数。至少原32匹配翻倍且录音无滤波截顶才支持继续新特征研究；该门槛只是信息可得性，不是误唤醒通过。无F0/窗/置信度搜索、新录放/训练/评分/valtest音频/云/烧录，失败停该滤波假设，原设备数据保持。wake-pitch-band-ux223/plan.json。


### UX223 固定音高支路可行性结果 — 2026-10-01
一次C11原型20B/无堆，ASan/UBSan、5独立整数病例41035样本、整块/随机分块/inplace输出与全状态一致。68旧输入滤波无截顶；唯一固定pYIN测量12.641秒，90/160/240及静音控制通过，成对原声456可信参考下设备匹配32→16（中14/251、粤2/205），末尾仍均不足，已拒绝、不进固件。保存输出失败审计显示原中/粤有有限F0估计382/192但置信度中位约.018/.020；误触小燕与正确同声粤语原/滤后都无有限F0，不能通过降置信度包装通过，也不据此认定原PCM无音高或麦克风损坏。10文件纯C/脚本/计划源码归档与hash保留，无重新滤波估计/参数搜索/训练/烧录/valtest音频/私人上传。实际读原72、wakevoice off/context1254/879268/next4481完全一致、USB释放。停止该简单音高带限假设，后续需更完整声学/采集依据，不续调F0硬否决；整体目标与误唤醒未完成。wake-pitch-band-ux223/closure.json。


### UX224 固定候选现场输入权衡诊断计划 — 2026-10-01
仅对UX219唯一最终6960B系数作新有限对照，不重训选模型扫阈值；原99.5%/提前门槛失败结论完整保留，不据此发布候选。依据用户此前实验级收尾及自主调试要求，先一次重放25既有关键词原PCM（24标注TRAIN源+60s未标注环境），128帧零PCM priming，原C前端/双头/平滑/事件全帧必须精确复现。标注12原正确全部保留、12负0及无新增提前才一次730条固定validation；新实验准入每语言保留>=95%、误触样本至少减半且无新负/提前，门槛在新评分前声明，不改旧门槛或称真人泛化。环境独立描述不训练；无test/烧录/新物理组。本阶段通过仅准许另行硬件试验，不称修复或发布。wake-tradeoff-ux224/plan.json。


### UX224 对照结果 / UX225 实际输入时序训练计划 — 2026-10-01
UX224原128零PCM priming及25连续原输入的双头/平滑/事件全帧精确复现；固定219候选保留中粤各6/6，但近词小燕1/12及60s未标注环境1次均未改善（同一事件位置），因此止于actual，未读val/test或刷板。新UX225一次从零训练6960时序交互，首度直接用24实际关键词全504帧（含128原PCM priming及真实房间历史），不复用失败权重，不训练未标注环境。原15623 TRAIN前缀不变，512帧统一padding的越界区间排除loss/events；明确batch24原+8actual、4000步/seed20261001225/0.01/threads4/900s只最终checkpoint，泛型masked teacher/negative objective独立测试及C/F64/FP32零模型parity后才更新。实验准入原两语言旧成功交集各>=95%、N180至少减半且无新N，actual12P全留/12N零及无新actual提前；通过才一次固定val并按相同95%/减半/无新N提前门槛，test不读，不自动刷板。保留之前严格门槛失败，不称真人泛化/发布/goal完成；后续另行独立声音上板及至少3连续业务。任何失败不重训扫参，不缩上下文或预算。wake-observed-temporal-ux225/plan.json。


### UX225 数值/识别结果与 UX226 固定验证对照计划 — 2026-10-01
UX225唯一4000步229.09s、final6960B SHAfc00fd51bd00ac8ad1e183cb747415b1403abef51cdb688802cfef1b9feeb9f8。原TRAIN双头7998976值F64/FP32全精确、16源2170880层C一致，实际24源24192双头C/F64/FP32一致；中3521/3542、粤2499/2520原成功交集，分别丢21/21且新增5/13，不混算；N180->98（45.56%减少），新增2，原TRAIN新提前中粤各1。实际12P全留、12N从1->0、无新增提前，是已知TRAIN录音控制而非泛化。既定减半/无新N门槛失败，未看val/test、不刷板不重训。prepare路径优先级ImportError已修正后preflight6checks通过；完整数值跑完在写报告遇NumPy int64序列化错误，保留原日志/源码SHA，只从已存scores恢复统计未重复推理；恢复控制台同类错误但权威evaluation.json已完整保存。UX226另行声明新描述对照：固定225一次730条validation C/F64/FP32，保留旧失败；硬件实验（非发布）准入旧中交集>=44/45、粤>=32/33、N<=3/636且最多1新N、无新增提前、compact中>=15/16，事先声明后才读结果。通过再一次60s未标注环境只描述且不增事件，不训练/扫参/test/录音/云/烧录，通过只准许另行独立声音guard硬件实验与>=3连续业务，完整goal保留。wake-domain-comparison-ux226/plan.json。


### UX224–226 收束记录 — 2026-10-01
UX226固定730条验证的一般中45->43、粤33->30、compact中15->13，N5->3无新N，新增一般中提前1，因此新实验硬件门槛也失败。不作可选ambient重放/test/新录放/云/烧录，不采用或重训失败候选。完整双模型验证747520 F64/FP32输出及8例1085440 C层精确；初始报告把compact16包含在zh一般45基线断言导致退出，原日志/源码保留，只用已存scores正确分组重写报告，未重跑推理/更改先验门槛。源归档90文件 SHA1a6d3cbe9a227ff51ecb964757160d733093fd83b69b01a1a8fddb55cdb54c87，zip每文件SHA校验；root固件未改动。fresh USB原72/off/off/context1254/879268/next4481及2MiB分区/204800历史完全保持，串口释放。误唤醒与完整goal仍未完成；此固定模型分支停止，不能把已知小燕拦截称广泛修复；后续需更广且来源独立的实际输入训练证据。docs/WAKE_OBSERVED_INPUT_REPORT.md、wake-domain-comparison-ux226/closure.json。


### UX227 真实关键词输入扩展采集计划 — 2026-10-01
上一轮225/226已分离已知近词而验证退步，属于新证据；停止该模型。数据审阅：原15623含1686 device_domain标记，但旧采集来自不同录音处理路径；精确关键词输入仅197的24条。固定24未用过original_recording_id TRAIN源（两语言各8正/4小燕负，正8声音/负4声音），每同源normal0.35与minus6 0.175各一次，共48条，source RMS0.14仅数字相对档无距离/SPL说法。原观测218-kws-input BIN1505472/正常1540096、SHAb74c15d163dc4d7deb7368cc52592fb507b8903bcbbf28f7f9a08ca5ca23ac6d与原完整源码/观测器SHA绑定复用，不改root或构建。fresh idle/off/context后guard全4MiB校验备份/app-only/nonapp精确；48×188blocks/6.016s，一秒lead/至少1.2s尾，原EL740/gain1/stableoff，动作云/Flash录音关闭，仅读借用空闲scratch；可选PC耳机麦克风本地420s窗口。任何传输/播放/CRC/序号/DMA/资源错停止整组不替换重录，始终guard恢复exact72/上下文灯音量/关闭监听释放USB。恢复后一次全C分数/事件parity、固定RMS/GCC对齐/PCM满幅审阅；未标注环境不采不训练，不读val/test；每语言>=12正/6负及>=6正/3负来源声音才数据准入。此回合不训练，不将诊断采集当3轮业务验收，不缩完整goal。wake-domain-corpus-ux227/plan.json。


### UX227 采集／数值及恢复结果 — 2026-10-01
固定24未用TRAIN源、两档48条全部完成，原128零PCM priming后9024块/4620288样本CRC/序号/时钟及C双头/平滑/事件精确，无插值；48全部按预先RMS/GCC对齐、PCM无满幅（不冒称rawADCrail证明），未按命中删例。normal中8/8正/小燕3/4误触，minus6中7/8/4/4；normal粤7/8/2/4，minus6粤7/8/3/4，特定近词总12/16不可外推每小时；每语言16合格P/8N、P8声音/N4声音，数据门槛通过不是模型或真人验收。原词模型740/gain1/stableOFF未改，观察器峰值8032us、全局minheap68928；无DMA/资源/reset失败，非完整业务资源测试。PC同步参考录音303.8s/44.1k16bitmono本地窗口、无云。install/restore两次guard均4MiB完整hash/device校验、app-only/readback/nonapp全字节保持，close重新读备份/非app自行校验；恢复exact原72 SHA22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07，fresh USB off/off/context1254/879268/next4481/2MiB/204800历史、灯音量/clip不变，COM释放。诊断源码14文件逐SHA zip校验，archiveSHA63dcf92a5dcc6cac336dd6429380d2b73f6b925059df4f9d65bb6508de1d26cb。本回合无训练/新模型/valtest评分/业务3轮或goal通过；下一阶段另行固定扩大实际输入训练，不重训失败225同数据。docs/WAKE_RAW_DOMAIN_CORPUS_REPORT.md、wake-domain-corpus-ux227/closure.json。


### UX228 扩大实际输入的有限训练计划 — 2026-10-01
冻结原15623 TRAIN，精确原始24+新增48=72条实际输入/44P28N，保留父源与两档关系；旧24窗口原样，新48沿用227自动端点low..high+12800并加128零PCM priming，不作人工音节真值。原前端/EL/阈值268/6960纯C核及6252B神经状态固定，从零唯一4000步/seed20261001228/batch32/4线程/.01/900s，复用225已测masked loss和margin；仅换新数据及取消单负例固定过采样，actual按中粤P/N各2且legacy/normal/minus6逐步轮换。拟合前C/F64/FP32及padding/objective测试；只最终checkpoint。新实验准入提前声明：原各语言正确交集>=99%、N<=117/9364且新N<=2、无新提前；实际41原正确全部保留、28近词0且无新提前。通过才一次既有730开发验证（中交集>=44/45、粤>=32/33、N<=3/636/新N<=1、无新提前、compact>=15/16）。旧219/225/226失败保持，不称真人泛化；未标注环境与TEST不读，不重训扫参，不自动刷板/云/缩上下文。失败停止，过仅另行guard实机和>=3连续业务。wake-domain-train-ux228/plan.json。


### UX228 扩大实际输入训练结果 — 2026-10-01
唯一4000步从零训练234.36s，final6960B SHAdc552750618f34d8ebd8864c5d69323017c6059f39451a340a63467141ea96c9；一次对照70.37s。拟合前6项masked objective通过，原双头7998976/实际72576 F64与FP32精确，16原源2170880层C及72实际双头/平滑/事件全一致。原中交集3510/3542（丢32增5，总3515/3619）、粤2500/2520（丢20增12，总2512/2640）；原负180->98/9364减45.56%、新负4，中粤各新提前1，不用新增抵消丢失或总提前下降抵消新提前。实际中21->21/22、粤20->21/22、全部41原正确保留；28近词13->0，皆TRAIN录放不是真人或每小时率。提前声明99%/减少35%及actual保留零近词通过，新N<=2/无新提前失败，停止分支；无val/test/新录音/云/烧录/业务三轮。完整源码31文件逐SHA及zip验证，archiveSHA4f7f150d33f9b1c30b0426e45b747ab3ed2c36512b565e331bdb1dcbe87e91b0；fresh USB原72 idle/off/off/context1254/879268/next4481与2MiB/204800历史、灯/volume80/clip保持，COM释放。root固件未改，旧219/225/226失败完整保留，误唤醒/完整goal均未完成。docs/WAKE_RAW_DOMAIN_TRAIN_REPORT.md、wake-domain-train-ux228/closure.json。


### UX229 同步双判别决策对照计划 — 2026-10-01
上一回合228训练产生新证据，actual13->0但原新负4/新提前各1，原门槛失败保持，非外部阻塞。不重训：原EL与固定228交互模型同时评分，32ms块取两者各自3块平滑分数min，再走原268/2of3/64块warm/24000冷却及armed保护。min逐点<=原分数，只证明原整冷启动负例无触发时新规则不新增该样本，冷却重置使事件时间/次数未必子集；完整记录。先小C11决策ASan/UBSan及独立scalar/投票/保护/间断/冷却测试，再只一次复用228保存TRAIN/actual分数，全流C对齐。先验准入各原正确交集>=99%、原负<=117且新N=0/新提前=0、实际41原正确全留/28负0；保留正例相对原时间p95新增<=96ms/最大<=256ms。过才一次固定730开发验证（中>=44/45、粤>=32/33、N<=3新N0、无新提前、compact>=15/16及同延时门槛）。运行估计额外6252B NNstate+小平滑state/双NN工作，现有speech_disarm在录音前释放模型；不是C3资源证明。此阶段不刷板、采声、训练、云、环境拟合或TEST，不改root固件/上下文，失败停该决策分支；过仅另行资源及>=3连续业务guard测试。wake-consensus-ux229/plan.json。


### UX229 同步双判别对照结果 — 2026-10-01
小C11/64极值对/ASanUBSan/6144独立状态块通过；TRAINactual复用分数2017888块C与Python事件一致。原中交集3511/3542丢31增1、粤2495/2520丢25增5，原负180->94无新N/新提前；actual中21->21/粤20->21保留41原正确、28近词13->0，时间全相同。各语言TRAIN保留相对延时p95=0、最大64/128ms，TRAIN准入通过后一次730开发验证双头747520 F64/FP32及8例1085440 C层、93440最终C块精确。验证中45->44、粤33->31、compact15->13、N5->3无新N/新提前。粤>=32/33及compact>=15/16先验门槛失败，停止同步min分支；不刷板/TEST/业务3轮或修改旧失败。全部五条损失只读保存分数诊断：1条独立均确认但64ms错位；4条新模型自身无事件，其中3条仅窄峰，1条粤语证据弱。为后续新的主确认/辅短窗契约提供证据，不作五例特判或扫阈值。冻结源码22文件SHAb4568402f28c5d46d104aaadf0cddca3f2b3e4aed9576c3e82c5c0a76bdff587及逐文件校验；fresh原72/off/off/context1254/879268/next4481与2MiB/204800历史、灯/volume80/clip保留，COM释放。root固件未改、新NN额外约6252B及CPU仅估计未C3验证，整体goal/误唤醒未完成。docs/WAKE_CONSENSUS_REPORT.md、wake-consensus-ux229/closure.json。


### UX230 原稳定确认／辅短窗证据计划 — 2026-10-01
上回合229完成2111328块C决策及开发验证和全部五损失诊断，属进展非等待/阻塞；原失败保持。唯一新规则：原每32ms平滑分数268二票/三块（stable可四连），辅同阈值最近三块至少一票；共同确认才接受，64warm/24000仅实际接受冷却，armed保护清两方票不产生冷却，无lookahead或等待计时。同24B检测器votes低4bit原/高3bit辅，重置及stable setter清全票，不混用原step；两NN对额外6252B及平滑/CPU仅估计、现有录音前disarm释放。先C11/ASanUBSan/独立状态及辅恒高与原事件/主状态等价；只一次复用228原TRAINactual及229开发val保存分数，不重推NN/训练/录音/云/阈值票窗扫参或五例特判。沿用先验准入各语言原成功交集>=99%、原N<=117且新N0/新提前0、实际41原正确全留/28N0，延时p95<=96/最大<=256；过才一次730既有开发分数判定（中>=44/45、粤>=32/33、compact>=15/16、N<=3新N0、无新提前及同延时）。已读val用于设计明确不是blind；TEST不读。本阶段不改root/烧录，不缩上下文，失败停该规则；过仅另行资源/完整缓存/guard及>=3业务，整体goal未完。wake-auxiliary-ux230/plan.json。


### UX230 原确认/辅证据结果 — 2026-10-01
C11/ASanUBSan/6144独立状态块、辅恒高4096原判定块、2111328全语料事件块一致。TRAIN中3542->3533(保留3532/丢10/增1)、粤2520->2516(保留2513/丢7/增3)、N180->116无新N/提前。actual保留41原正确，中21/22粤21/22，N13->0/28。开发中45/45粤32/33短中15/16，N5->4/636超过先验最多3，整项未准入，停止票数/窗口分支。无新提前，保留延时p95=0/最大32ms、actual时刻不变。残留近词你好小智(中/粤)、你好小叶(粤)为双模型高分，另1自然粤语；没有改标签、阈值或读取TEST。源码22文件SHAedd67be83876638ee0901ba53db41b8f1d362704d9a6d66c2cd65d497eff39d7，fresh原72/off/off/context1254/879268/next4481与2MiB/204800历史、灯/volume80/clip不变，USB释放。没有root固件变化、训练/推理/新录音/云/写Flash，false_wake_fixed/goal_complete均false。docs/WAKE_AUXILIARY_REPORT.md。


### UX231 前端临时缓冲回收计划 — 2026-10-01
UX230近似词双模型高分，停止票数/窗口调整。先冻结原C源，单独验证内存精简：FFT保持分离输入/输出与原运算顺序，FFT完成后复用已失效输入数组存257个功率；目标单模型/融合各回收至少1024B。旧/新C库比较随机/极值/冲激/正弦及48份已保存原始输入的Mel/PCEN/完整层值、神经状态/分数/事件，污染临时区验证生命周期。静音种子类型字节必须不变，仅更新源hash绑定；原sanitized检查和有声/无声构建及C3 sizeof预算通过才保留。不改权重/门槛/分区/上下文，不新训练/录音/云/读TEST/写Flash；原72/off/off不动，内存优化不宣称误唤醒修复。


### UX231 前端临时缓冲回收结果 — 2026-10-01
FFT保持原分离输入/输出，输入与功率按生命周期C11 union复用；host与C3 RV32IMC/ILP32尺寸单9448->8416、融合12632->11600，回收1032B。旧新不同污染临时区22144帧、Mel/PCEN/完整神经状态/分数/事件全部字节一致、54事件一致；包含4096极值/随机/冲激/正弦与48份保存raw18048帧，不是识别成绩。独立4096帧ASanUBSan通过，6个原检查通过，首轮旧12632写死断言失败后改紧11608上界，仅重跑该项；头/typedef缺失、无声版本夹具及shell引号失败保留。静音种子字节不变/hash绑定更新/check通过；有声0.11.231-scratch1502192B/1540096，余37904；无声1013968B。冻结overlay69文件SHA298332be8a12c1979eebf614cb0491b12058d5d0971111d20313daf982b053d1，非完整SDK源码包；原72/off/off/context1254/879268/next4481和灯/音量/clip、2MiB/204800历史保持，USB释放。没有Flash/新训练/新录音/云/TEST，未测板上heap/延时，内存改动保留源代码但不宣称误唤醒修复或goal完成。docs/WAKE_SCRATCH_REPORT.md。


### UX232 声学权重可训练原型计划 — 2026-10-01
残留近似词为原/辅网络双高分，停止票数/窗口调参；新方向允许原12768个声学INT8权重参与QAT，仍为原双分支拓扑、固定bias/shift/归一化，时序/逐点交互6960，总19728参数；不引入通用运行库或改变C神经历史6252B。本阶段只做梯度/整数协议原型：预先固定每中粤正负TRAIN首4、actual首1和5个极值/随机序列，初始零交互与固定扰动两种全层F64/FP32/C精确比较，26张量梯度有限且非零、固定buffers不变、部分和严格<2^24。无optimizer训练/识别评分/验证/TEST/录放/云/Flash；若通过才另行声明一次有时限的整网拟合。改变权重或非零交互必须重新生成完整hash绑定静音状态，不能复用旧普通E/L缓存。资源仅估计、原72/off/off保持、误唤醒/整体goal未完成。


### UX232 声学权重可训练原型结果 — 2026-10-01
新增training/kws/full_pair_qat.py，仅主机QAT允许12768声学权重+6960交互，共19728/26张量，bias/shift/归一化/拓扑固定，沿用C11参数内核。固定25流7424帧；初始原权重、整数扰动、正负半整数tie三组，每组3934720层值F64/FP32/C全精确，总11804160；26梯度有限非零、buffers固定，最大部分和1977039<2^24。首轮仅C vs参考维度轴不一致，对齐后literal与导出ABI/数值均0差异，失败与诊断保留。无optimizer/识别评分/验证/TEST/新录音/云/Flash；神经6252B/工作区11600仅结构保持，新权重尚未C3实测。源码19文件SHAfee822a54b3e2d02185997e154ca0bf4ea587dc6b7799cd5d5b78c8a60b0e89a，fresh原72/off/off/context1254/879268/next4481、2MiB/204800历史、灯/volume80/clip保持，USB释放。仅为一次有时限整网训练铺好整数链，新权重及非零交互必须重新生成完整hash静音缓存，不可复用旧E/L；误唤醒及整体goal仍未完成。docs/WAKE_FULL_QAT_REPORT.md。


### UX233 单次整网拟合计划 — 2026-10-01
UX232三组11804160层F64/FP32/C及26梯度已对齐，开始一项新声学表示假设：从原E/L声学权重和零交互训练19728参数，固定bias/shift/归一化/前端；原TRAIN15623和同72actual，不用环境/新语料，保持UX228目标/教师/采样/倍率。唯一seed20261001233、CPU4线程、batch32、AdamW.01/wd.0001/clip5、4000步/900秒硬上限，只用最终checkpoint，不延长/挑选/重复。导出完整模型及6960交互先查全TRAIN/actual F64FP32及固定C层值、交集/新增负例/提前/延时；先验中粤各保留>=99%、N<=117/无新N/无新提前，actual41原正确全留/N0，p95新增<=96ms/max<=256ms。仅通过才一次730开发对照：中保留>=44/45、粤>=32/33、短中>=15/16、N<=3且无新N/提前，同延时；TEST不读，开发已复用不称盲测。失败止当前拟合，不扫门槛/投票或改记历史；通过仍需另行新权重+非零交互的完整hash静音种子和C3资源/至少3连续业务验收，不直接采用。不写Flash/云/新录音/环境数据，原72/off/off/2MiB上下文不动，误唤醒及整体goal未完成。


### UX233 单次整网拟合结果 — 2026-10-01
唯一4000步seed20261001233/CPU4、260.45s，19728参数从原声学/零交互开始，bias/shift/前端/归一化固定，无选择checkpoint或重训。TRAIN7998976 F64FP32头、16例2170880 C层和actual72例72576 C/F64/FP32头及平滑/事件精确。中3542->3538(保留3527/丢15/增11)，粤2520->2521(保留2497/丢23/增24)，N180->82/9364但新增8，且两语言各新提前1。actual中21/22、粤22/22，原正确41全留，近词13->0/28；99%交集/延时等通过，新N/新提前准入失败。停止单独233，不读开发/TEST、不刷板/重训/改阈值；已知实录参与训练不称泛化。冻结23源码/最终参数文件SHAb06307b3fa623116056d49d8822e03423d4092daa2e50f60ffa9a378aa157c31，fresh原72/off/off/context1254/879268/next4481及2MiB/204800历史、灯/volume80/clip保持、USB释放。后续可另声明同一230规则以新的233声学表示作辅助，先重新查全准入再查四网络资源；需新hash静音种子，不直接采用或标误唤醒/goal完成。docs/WAKE_FULL_TRAIN_REPORT.md。


### UX234 原确认/新整网辅助计划 — 2026-10-01
原230规则与233单独模型准入失败保持失败。只更换为233新声学表示作为辅助，完全复用230判定/268门槛/原2of3+辅1of3/三块平滑/预热64/仅接受冷却24000/保护清票，不继续扫规则或重训。先一次复用已核验全TRAIN与72actual分数，全C事件对独立Python，保留相同99%交集/N<=117/无新N或提前/actual41原正确全留N0/新增p95<=96max<=256ms。仅通过才一次730开发233辅助推理(F64FP32/8C层)及全C事件，原分数复用旧核验缓存；同中>=44/粤>=32/短中>=15/N<=3无新错误/延时门槛，TEST不读。通过仅允许下一独立C3缓存/资源/至少3业务计划；额外NN6252B/小平滑及辅助原始19728B权重估计，四网络RAM/Flash/CPU未测，旧普通E/L静音缓存不能复用。不写Flash/云/新录音，原72/off/off/上下文保留，goal/误唤醒未完成。


### UX234 整网辅助判定结果 — 2026-10-01
固定233声学模型/固定230规则，无训练/窗口/阈值改变。TRAIN中原正确3537/3542、粤2512/2520，N180->99无新N/提前；actual原41全留/时间不变，N13->0/28。开发中44/45、粤32/33、compact15/16，N5->5/636，既定<=3失败，未烧录。2111328 C决策块、373760辅助FP32/F64头、1085440 C层值精确。残留3近词/2自然粤语；CosyVoice2参考声音名不是引擎，小叶ASR小雨不能据此删改标签。未读TEST、新音频/云/Flash均0。冻结19文件SHAdce3d7dd6b4d5c50b3a3f22d56c2bd556b3b1e193623ce804564faa41dcee9ea；fresh原72/off/off/context和灯/volume80/clip保持、USB释放。停止此组合，后续单独验证权重+偏置声学仿射训练/导出，先数值界限再有限拟合，旧失败和原判定规则保留。docs/WAKE_FULL_AUXILIARY_REPORT.md。


### UX235 声学偏置整数原型计划 — 2026-10-01
UX234残留近词/自然句，数据审计无删改标签依据；新的表示假设允许530个声学INT32偏置参与QAT，原19728权重，总20258参数/50张量。偏置按输出单位参数化再乘固定2^shift并舍入，不按INT8范围截断；每层原/交互乘积保守margin单独求界，所有部分和绝对值<=8388607，低于FP32精确整数上限且INT32安全。固定前端/shift/归一化/拓扑/C ABI/神经历史6252B，不增通用运行库。仅复用232预声明25输入做初始、整数扰动、半整数舍入、偏置极值4例全层FP32/F64/C对齐和50张量梯度/固定buffer检查。无optimizer/识别评分/验证/TEST/录放/云/Flash，通过才另行限定一次拟合；原72/off/off及2MiB上下文保持，未称误触解决。


### UX235 声学偏置整数原型结果 — 2026-10-01
原12768+交互6960+INT32偏置530=20258参数/50张量，梯度有限非零，固定buffers不变；初始导出原metadata一致。25固定输入4组初始/扰动/半整数/极限偏置，15738880 FP32/F64/C完整层值精确；所有部分和绝对值<=8388607，INT32偏置未INT8截断，历史6252B不变。无optimizer/识别评分/开发/TEST/新音频/云/Flash；冻结17文件SHA9b028ef54ea31c6f561a6bfe88c82383dc77eee7d5d896386418d0150f4fcd50。fresh原72/off/off/context1254/879268/next4481、灯/volume80/clip保持/USB释放。通过可另行声明唯一有限仿射拟合，不称误唤醒/goal完成；docs/WAKE_AFFINE_QAT_REPORT.md。


### UX236 单次声学仿射拟合计划 — 2026-10-01
235偏置数值原型通过，唯一从原声学权重/偏置、零交互拟合20258参数；原TRAIN15623+actual72，233/228原目标/教师/采样/倍率不变，偏置单独INT32安全投影。seed20261001236、CPU4、batch32、AdamW.01/wd.0001/clip5、4000步/900s硬限，仅最终checkpoint，无重启/扫参/修饰失败checkpoint。固定230/234原2of3+辅助1of3、各3块平滑/268/暖64/冷24000，全TRAIN/actual F64FP32及16C层值/全C决策先过原门槛：各语言原正确>=99%、N<=117无新N/提前、actual原41全留/N0，p95新增<=96ms/max<=256ms。仅通过才一次开发730/8Ctrace，原门槛中44/45、粤32/33、compact15/16、N<=3且无新增N/提前/延迟过限；旧失败仍失败。TEST不读/不删改标签/无新音频环境/云/Flash/资源及误唤醒完成声明。通过仍需另行新仿射模型+交互+内核hash绑定静音种子/C3额外四网络资源/至少3连续业务；原72/off/off和上下文保持。


### UX236 单次声学仿射拟合结果 — 2026-10-01
唯一4000步/279.89s/20258参数，原声学权重偏置起点、零交互/最终checkpoint，未重训。TRAIN中3536/3542原正确保留、粤2518/2520；N180->108无新N/提前，actual原41全留/N13->0。开发中44/45、粤32/33、compact15/16，N5->4无新N/提前，既定<=3失败/未烧录。全TRAIN/actual/固定C层及2111328全组合决策块对齐，TEST未读。固定事件形状：自然粤语残留1个辅助高块，三近词各2；无新规则评分。导出器补非零DW INT32偏置，旧零偏置字节不变，240偏置/135680实际编译C层值对齐。脚本inspect.py遮蔽标准库已更名support_shape.py，仅元数据复跑，见diagnostic-repair.json。冻结31文件SHA7bf4d827da29b46e557f7dfdf8a9919cc000194b1a897d75199b6f8bef124b99；fresh原72/off/off/context1254/879268/next4481、灯/volume80/clip保持/USB释放。不把部分下降标整体修复，停止此训练/一次支持组合；可另行声明固定缓存的双路独立确认单次验证，不扫参数或重新拟合。docs/WAKE_AFFINE_TRAIN_REPORT.md。


### UX237 双路独立确认有限验证计划 — 2026-10-01
236既定N4准入仍失败，不补训。固定形状证据：残留自然粤语仅1个辅助高块，三近词各2；据此仅新验证两路各自2of3确认，同3块窗口/268/各3块平滑/暖64/冷24000，保护期清全部票且不接受事件；原stable4连续可选保持。不扫票数/窗口/阈值，只这一项通用孤峰抑制假设；24B检测器不增状态。先严格C编译、独立列表Python有限状态对照/ASanUBSan，再一次复用236全TRAIN/actual/开发保存分数，原保留/新增错误/延时门槛不变。无训练/新声学推理/TEST/音频/云/Flash；失败止此新规则，旧236一次支持失败保留。通过只准后续新hash种子+C3四网络资源/至少3连续业务，无现场/真人或误触完成声明。


### UX237 双路独立确认结果 — 2026-10-01
冻结236权重及分数仅一次2of3/2of3确认，数值门槛/窗/暖/冷保持；C严格/ASanUBSan、6144独立状态块、4096普通事件兼容和2111328全决策块一致，24B无增状态。TRAIN中原3531/3542、粤2513/2520，N180->85无新N/提前；actual原41全留/N13->0。开发中44/45、粤29/33、compact14/16、N5->3，粤语/短普通话保留门槛失败，规则未采用。自然语句尖峰抑制只是部分原因，3近词仍触发。停止票数/窗/门槛及固定数据权重/偏置试验，后续先审实际难负例/自然人声覆盖/发音，再有限补充TRAIN公共源，不继续补训失败checkpoint。冻结15文件SHAd9e74df817d2bb00447df65ed123185a5b8b10ca7787669bf26996306e671e13；fresh原72/off/off/context1254/879268/next4481及灯/volume80/clip保持/USB释放，无新推理/训练/TEST/音频/云/Flash，误触及goal未完成；docs/WAKE_DOUBLE_CONFIRM_REPORT.md。


### UX238 实机难负例／自然语音覆盖计划 — 2026-10-01
审计actual72的28负例全部CosyVoice2，其中17小燕，未含小智/小叶/公共自然人声；缺口已证实，但未证明补数据能修复。固定16新TRAIN父源：每中粤2正（af_heart/zf_xiaoni）、3小智/小叶/小王近词、3有文字FLEURS自然录音；排除全部旧播放original ID，父源两固定数字档0.35/0.175，共32次，无重录/替换，每条max188,ceil((源秒数+2.2)/.032)<=257块，PC可选300s本地录音。复用已完整源码hash绑定原observer1505472B，无构建/root修改；guard全4MiB校验备份/app-only/nonapp精确，动作云Flash录音关闭，采完始终guard恢复exact72/context灯音量且关监听释放USB后才分析。一次全C分数/事件/CRC/时钟/RMS-GCC对齐，失败记录保留；各语言>=3可用正/10负、覆盖全3近词及>=4自然录音才数据准入，公开speaker未知不假称多真人ID；不训练/读开发TEST/标现场FAR或完整修复，保持2MiB上下文和整体goal。


### UX238 实机难负例／自然语音覆盖结果 — 2026-10-01
16新TRAIN公共父源/两固定档32记录一次完成，原C 6624块/3391488样本的CRC/序号/时钟/双头/平滑/事件精确；中粤正各4/4，近词8/12触发、自然0/12。trial29自然粤语低档对齐失败原记录保留不重录；可用31，各中粤P4/N12和11/三近词+自然类覆盖通过，不能称现场FAR/真人泛化。原实机28N全Cosy、17小燕/无小智小叶自然的覆盖缺口明确。准备短3秒假设失败已记，负源<=6s/正<=3s，每条188..257块；8030us最大/32ms，最低堆68944、无DMA/reset。一次guard全4MiB+app-only/nonapp精确install后始终guard恢复exact72。冻结20源码文件SHA68b94760c508f2926478da68786981ceff2fa528bafa36eb1404d0694deaf4e6；fresh原72/off/off/context1254/879268/next4481、灯/volume80/clip保持/USB释放，无训练/开发TEST/云/新模型采用。下步只一次生成31完整因果特征并测固定236，不截长自然流（max642frame>旧512fixture），再据证据另限拟合。保持完整goal/误触未完成；docs/WAKE_NEGATIVE_DOMAIN_REPORT.md。


### UX239 新公共输入完整因果重放计划 — 2026-10-01
上一轮为progress：仿射实现/有限验证/32新公共输入采集及31质量准入均有证据改变下一步，无外部阻塞；完整双语/异步ASR/VAD/流式LLM/3连续轮/生动衔接goal保持。仅31质量通过原始输入，128零PCM帧加全部采集256帧（最长642），host可补656但按真实length判事件，不截长自然语音、不用失败trial29训练。原C前端/原双头/平滑/事件先与每块板录一致，存完整x/length/来源/有效区间；只一次固定236声学权重偏置/交互重放，每层FP32/F64/C及原primary/aux1of3决策C/Python精确。报告原正确交集、新近词/自然错误和时刻；旧236/237开发失败保持，本检查不准直接部署。无训练/新规则/录放/云/Flash/开发TEST；根据证据才另限新数据拟合，原72/off/off/2MiB上下文保持。


### UX239 新公共输入完整因果重放结果 — 2026-10-01
31可靠流/16702前端帧、最长642/host补656按real length，不截断；6367原块分数/事件与板录一致，8852060辅助FP32/F64/C层及8351组合判定块精确。固定236中粤各4/4原正确保留/时间不变，近词8->3/12、公共自然0/11/无新N；旧236开发失败保持，未训练/新规则/录放/云/Flash/开发TEST。发现旧训练strength为aux median2of3，实际固定组合aux1of3用max；先另验证原primary必要机会+aux max目标/梯度，再限一次新31参与的从原拟合，不宣称软目标能保证全约束。冻结25文件SHA974d7e63853dcf7aeac1be333a416bc8262e029d50f923801a3c8abf351d5b7c；fresh原72/off/off/上下文灯volume80录音保持/USB释放，误触/完整goal仍未完成；docs/WAKE_NEGATIVE_REPLAY_REPORT.md。


### UX240 辅助判定训练目标对齐计划 — 2026-10-01
239证明旧目标辅助median2of3与固定组合max1of3不一致。新增仅主机auxiliary_objective：原primary三块中位数>=268为必要机会，辅助同窗max；暖64/真实length/有效与提前区间掩码，负例机会与正例提前均252软约束、原成功首时刻教师保持；无原机会正例不强迫不可能命中，标签保留并报告。必要机会是清票/冷却/armed前的上集，不冒充全检测器；先固定C事件必要条件、孤峰/窄正峰、无机会/填充/极值/梯度检查，仅103actual原分数构建掩码。不训练/声学评分/开发TEST/录放/云/Flash，部署规则不变；通过后另声明唯一新数据从原拟合，失败止原型。


### UX240 辅助判定训练目标对齐结果 — 2026-10-01
6144随机块/51C接受全部满足原primary必要机会+辅max>=268；孤峰旧硬罚0/新0.578125且抑制梯度正，足够窄正峰loss0不强迫2辅助高分，低正峰提升梯度/无机会/填充/暖前/极值有限。103actual的49原成功锚点全可表示、50正例有机会，不删除其余正例但跳过不可能主目标。冻结14文件SHA04c0549a6908955dc61c4acb0fbcf34e4b32d57055d85e6945acbc47dd914871；仅原型无optimizer/声学评分/开发TEST/录放/云/Flash，设备未触碰。通过后仅另声明一次新数据从原拟合，部署规则与硬准入不变，误触/goal未完成；docs/WAKE_SUPPORT_OBJECTIVE_REPORT.md。


### UX241 支持目标／新实际输入单次拟合计划 — 2026-10-01
240必要条件/梯度通过，部署仍原2of3+辅1of3/268/暖64/冷24000。唯一从原声学权重偏置+零交互20258参数，改为primary必要机会的辅助max目标/252负例提前软上限；TRAIN15623+旧72+新31，最长642不截断/host补656，父源划分与失败trial29保留。seed20261001241/CPU4/batch32/AdamW.01/wd.0001/clip5、4000步/900s硬限，仅最终，无重复/挑checkpoint；原各语言99%交集/N<=117无新N或提前、actual49原正确全留/N0、p95<=96/max256ms；仅通过才一次已用730开发中44粤32短中15/N<=3无新错误，TEST不读。通过仍需完整hash静音种子/C3额外网络资源/至少3连续业务，失败止此拟合。不录放/云/Flash、不缩上下文或换部署门槛，原72/off/off保留，整体goal未完成。


### UX241 支持目标／新输入单次拟合结果 — 2026-10-01
唯一4000步/336.83s/20258参数从原+零交互；主目标对齐原primary机会+辅max，103actual/最长642全保留。TRAIN中3472/3542粤2479/2520原正确保留、N180->0新增0；actual原49保留49、N21->0/51。训练实际门槛=False；训练／实际输入门槛未通过，因此没有再次评分开发集，TEST始终未评分。总准入=False，没有采用；全FP32F64/固定C层/实际C头与决策一致，TEST未评分。冻结33文件SHA269fd8055c61762e0ccba7b13d250ad7f6a1f88aee822f455fd7984a34f9f128，fresh原72/off/off/context灯volume80原录音保持/USB释放，无录放/云/Flash；失败不补此checkpoint/扫门槛，通过仅下一单独C3计划，误触/完整goal未完成。docs/WAKE_SUPPORT_TRAIN_REPORT.md。


### UX242 固定候选漏唤醒来源审计计划 — 2026-10-01
241辅助max对齐后TRAIN误触180->0，实际21->0/49正确全留，但中70粤41原成功损失导致99%准入失败，不能烧录。只审saved TRAIN原/新score和111漏例父源：原必要机会的辅助峰距268与旧接受锚点差，区分不足峰与清票/冷却效应；不改标签/删样本。无新推理/optimizer/solver/开发TEST/设备。若表征不足峰主导，之后才另声明一次新声学表示的末层可行性证明，原primary机会掩码使其不同于旧全窗LP；不重复整网训练或改变部署阈值。


### UX242 固定漏例来源审计结果 — 2026-10-01
111漏例中，普通话必要机会辅峰不足70/70、粤41/41；其余可能清票/冷却项0，不是据此更改规则。负例只180条有原primary机会/711窗，辅峰最大240。父源/差值清单保存，不重标或删除111例。无solver/训练/新推理/开发TEST/Flash/设备；仅支持下一单独声明的新表示末层有限证明方向，241失败及未采用保持，goal未完成。


### UX243 新声学表示的有限整数末层证明计划 — 2026-10-01
242确认111漏例全为辅助支持不足、负例仅原primary711机会窗；241整网拟合失败保持。冻结241上游全部权重偏置/shift/交互6960，只提取TRAIN15623+actual103的末层前INT8隐藏值，全部saved scores/固定4TRAIN+actual0/24/72/102完整C层一致，180s/4thread/32batch。只一次30s HiGHS整数末层最小L1：48INT8权+两偏置整数和，冻结原bias差；每6062+49原正确首窗选原辅助最强1块线性>=271，每原primary负例/词前机会3辅块<=264，乘24整数矩阵，所有部分和<=8388607。解/timeout/不可行/数值或原TRAINactual门槛失败即停，无约束移除/重解/阈值窗扫参/神经补训。可行后仍须整数头/原C组合事件全核验99%各语言交集/actual49全留N0/无新误触提前及p95<=96max256；不读开发TEST/录放/云/Flash或直接采用，仅决定是否有下一独立开发及资源依据。


### UX243 固定末层整数证明结果 — 2026-10-01
一次7462约束/49整数/6111正锚点、1201负支持、150提前支持，0.145s判不可行，已停止；全saved scores/1933440固定C层相同/提取10.43s。无第二求解/神经训练/开发TEST，固定见证元数据审计有0同块>=271且<=264直接矛盾；只证明这个保守支持位置系统，不证明新表示或任务不可能。首次scipy缺失发生求解前/0call，固定1.13.1 --no-deps修复后唯一call，日志保留。冻结31文件SHA19639033412173744a440d2b24f2375c86e16c869b0dccde0d74fb5efb28ef62；fresh原72/off/off/context灯volume80原录音保持/USB释放，无录放/云/Flash，误触/goal未完成；docs/WAKE_SUPPORT_HEAD_REPORT.md。


### UX244 固定声学特征／时刻约束审计计划 — 2026-10-01
上一turn为progress：主机目标修正及单次拟合/固定头证明产生111支持不足漏例，无外部阻塞；完整双语/异步ASR/VAD/流式LLM/3轮/衔接scope保持。243单块局部271/264冲突为0，不能把不可行归因于该猜测。仅saved TRAIN/actual两支末层前INT8特征：区分三块求和相同的线性碰撞与有序144字节相同的精确C输出碰撞；查6111教师及111漏例的原首有效窗和允许+96ms的合法替代支持，原primary必要机会/真实length/词前保护仍保持。不solver/新推理/训练/开发TEST/录放/云/Flash，不改见证去重解243、不删除或重标；据证据再选下一行动，原72/off/off/2MiB上下文不动。


### UX245 已满足负例停止施压的数值原型计划 — 2026-10-01
244的6111教师/111漏例没有相同特征或局部词前冲突，不能归因于直接信息丢失/早时刻矛盾；不改时窗。241原机会负例峰最大240<252已满足硬安全，但softplus((peak-204)/256)仍梯度抑制；仅移除该无条件soft项，保持其他正例/早区项、掩码/部署。先复用240随机C必要条件/孤峰/窄峰/长度极值数值检查，加已满足负例loss/grad0、16正例loss/grad/residual与原逐位一致。无optimizer/声学评分/开发TEST/录放/云/Flash；过才另声明同241seed/采样/数据/预算的一次从原拟合，不修饰失败checkpoint或调整准入。


### UX244–245 固定特征与安全负例梯度结果 — 2026-10-01
6111教师/111漏例无三块求和/有序特征负例碰撞或局部早区冲突，不能证明全局可分或将243不可行归因于该猜测。安全负例peak240原soft loss0.765930/grad>0，新仅hinge的loss/grad0；超限仍抑制，16正例loss/grad/residual逐位相同，6144C必要条件及49原成功锚点/长度极值通过。无推理/optimizer/solver/开发TEST/录放/云/设备；冻结14文件SHA61ba9c94c5f53776b063919eb2cb395334227f651c7ae531d47f307a3a995445。只改N无条件soft项，不改窗口/teacher/部署；下一仅另声明同241seed/样本/预算的从原单次拟合，旧失败不改，误触/goal未完成。docs/WAKE_SUPPORT_HINGE_REPORT.md。


### UX246 安全负例停止施压单次对照拟合计划 — 2026-10-01
245数值检查通过，仅去掉已满足负例的softplus抑制；正例/早区/原机会mask及实际判定原2of3+辅1of3/268/暖64/冷24000不变。和241同seed20261001241、同原TRAIN15623+actual103/最长642全保留补656、同原声学偏置+零交互、相同采样/倍率/AdamW.01/wd.0001/clip5/CPU4/batch32、4000步/900s单次最终；要求sampling-counts逐位相同，不补失败checkpoint。原准入不降：各中粤99%原正确/N<=117无新N提前、actual49全留N0、p95<=96max256ms；仅通过才一次730已用开发中44粤32短中15/N<=3无新错误，TEST不评分。通过仍需新hash静音seed/C3内存时间/至少3连续业务；无录放/云/Flash、原72/off/off及2MiB上下文保留，goal未完成。


### UX246 单次安全负例hinge拟合结果 — 2026-10-01
唯一4000步/350.78s/20258参数；seed及采样计数与241完全一致，仅去除无条件负例softplus。TRAIN中3510/3542粤2495/2520原正确，N180->2/9364；actual原49全留，N21->0/51，训练准入通过。开发对照普通话45/45、粤语29/33，短普通话14/16；负例5→4/636。粤语、短普通话及负例门槛失败。这是已用开发集，不是盲测。数值对齐通过；开发失败，没有采用／Flash／新录放／云／TEST；冻结35文件SHAc70731f983339344c6f64312f4137a7e06b8b90116a68bc68ab9163a8572b60a。fresh原72/off/off/context灯音量录音保持/USB释放。停止此固定数据重复拟合，转单独有限业务验证。docs/WAKE_HINGE_TRAIN_REPORT.md。


### UX247 有限连续业务复测计划 — 2026-10-01
246开发失败已关闭；停止固定数据拟合。现有231原E/L，无新辅模型，215完整source+231overlay编译文件逐项匹配，复用1502192B/hash1be313aa已验证镜像。唯一中粤中唤醒连续3轮，各1次、不重试，公开普通话remember输入，250ms postdone，fast/capture/prefetchon/reuseon/hold，235s上限。检查录音期ASR上报/最终授权/流式LLM/动态应答续接/历史和heap49152/DMA，外部音频本机分析，不把VAD或能量当真实1s。guard备份全4MiB仅app更新，始终guard恢复72，历史追加保留/原录音在备份可回滚，off/off释放COM5。无新训练/阈值扫描/TEST/私人音频上云。voice-pipeline-ux247/plan.json。


### UX247 连续业务结果 — 2026-10-01
现有231原E/L，编译395文件匹配，479源码冻结；唯一中粤中3轮各1次，全唤醒/完整输入/任务完成/记忆读回，gap265/265ms。录音期ASR连接上传证明、LLM/TTS流式存在；候选全cancelled/noPCM，实录缓存“收到我来处理”，动态接话失败。本机波形/ASR分析最终起声候选7.09/4.87/4.75s，1秒未达，模板失败保留无主观过关。heap48684<49152，网络栈2512/main1792/KWS8157us/DMA0无reset；无100轮或现场误触率结论。guard全4MiB备份只app/非app保留、恢复72/off/off/USB释放，新增8事件/5052B留存。原恢复assert把重启清零请求统计计入持久比较，独立核实历史/summary精确；保留失败，修脚本未重跑。stage parser非JSON遥测保留为缺失，不造统计。无训练/TEST/新阈值/候选采用，完整goal和误触未完成。docs/VOICE_PIPELINE_CHECK_REPORT.md。


### UX248 当前连接分段诊断计划 — 2026-10-01
上goalturn=progress：246失败冻结，247实际3轮全通过但候选~5.5s、无PCM/缓存应答、heap468B不足；完整范围继续，非阻塞。当前231代码启用现成WS/TLS唯一任务时钟和ESP timer调度计数，仅诊断标签248-protocol-diag，优先级/500ms/模型/输入/VAD/分区上下文不动。现成3时钟ASan/UBSan检查，正常1540096预算内编译冻结；唯一中粤中3轮remember各1次，250msgap/no重试/ready等待，保留失败。分DNS/TCP/config/handshake/publicsignature/upgrade/protocol阶段，不以诊断版报速度通过；不调高优先级、不放宽TLS、不重复调wake模型。guard完整4MiB备份仅app，始终guard恢复72/off/off/历史保留/USB释放。voice-connect-ux248/plan.json。


### UX248 当前连接诊断结果 — 2026-10-01
唯一中粤中各1次，2/3唤醒完整业务，第二轮265ms gap粤语漏醒保留、不重试；第三轮gap3969ms不能当即时成功。握手5382/5662ms、父TLS5450/5721ms、升级60/65ms、协议126/147ms；RSA4096 wall1470/1756ms、scheduled223718/225274us，6枚证书验证成功，无trace溢出。调度计数不是独占CPU、阶段嵌套不可相加、未归属等待未知、soft_calls0不等于全硬件。候选两次cancelled，无完整可播放输出；heap48064<49152/KWS8050us/DMA0无reset。app1504240正常预算内/hashd577ec707afaced241d3b527deedcadf364dd5aedcfff5251f8055619aa717de，478源码/395编译文件核实，现有3时钟检查通过。guard备份全4MiB仅app/非app精确保留，fresh原72/off/off/USB释放，新历史4条/1832B保留。仅诊断，未采用/未证明误触、动态应答、1s或连续业务通过；docs/VOICE_CURRENT_CONNECT_DIAGNOSIS.md。


### UX249 TLS状态机协作调度实验计划 — 2026-10-02
上一turn进展：248当前TLS5382/5662ms，RSA4096 wall1470/1756ms与scheduled224ms；第二次粤语265ms重醒失败保留。完整goal继续、fresh blocked计数0。仅固定SDK外层握手step前后callback，候选connect期间优先级4与ASR相同，返回step后delay1tick、原优先级恢复；其他任务callback无动作，保持原crypto/证书/主机名/500ms/模型/阈值/分区。先hash精确适配和变更拒绝、ASan/UBSan所有权/旧adapter/clock、C3正常预算构建与链接证明；最多唯一中粤中3轮remember/各1次/250msgap/no idle等待、不换失败组，保留原漏醒与堆/稳定性失败，音频只本机分析。guard全4MiB备份仅app、始终guard恢复72/off/off、历史保留/USB释放。不训练/阈值/优先级或deadline扫描、不放宽TLS或声称1s。tls-cooperate-ux249/plan.json。


### UX249 TLS协作调度结果 — 2026-10-02
唯一中粤中各1次/250ms约定，首次唤醒完整输入记忆3/3，实际gap265/266ms；动态候选hit3/3、最终输入授权后采用。候选打开2392/2423/2385ms，ASR2408/2406/2326ms变慢；17step每次、最长1183/1205/1203ms含抢占/等待。实录固定对齐全过/本机ASR确认动态及最终文字，动态应答能量候选3.656/4.427/3.585s、最终7.116/8.177/6.935s；声学接受false/1s未知，不造主观通过。heap47360<49152/KWS8060us/DMA0无reset；应用1502432B余37664/hashe5f0e22bac5d574c4b033c2d72095482f49dcbdadb9e425726c401de94c10b3d。489源冻结/395原编译输入核实/SDK未改，9 sanitizer过+适配器修后1单过，初始include/计数/asm及LTO包装检查失败保留。guard全4MiB备份仅app/非app精确保留，fresh72/off/off/USB释放；新增6历史2748B保留。模型阈值分区上下文保持，未默认采用/误触和完整goal未完成；docs/VOICE_TLS_COOPERATE_REPORT.md。

### UX250 完整短语文本复核可行性计划 — 2026-10-02
用户频繁误唤醒仍待解决；249调度实验已关闭且未改唤醒模型。停止固定资料重复训练/阈值扫参，先仅本机SenseVoice固定int8/auto/4threads检查197已核实24条TRAIN公共父源实际PCM；未标注环境/TEST不读。固定原lag全词前后128ms、原首次事件前2.048s两类裁剪，无新对齐/热词/同音扩展/改写，标明裁剪是否全词完整；要求12原正例全留且原1近词触发不留，否则此文本路径停，不调规则适配结果。180s一次37decode上限；无设备/云/训练/烧录，不能将主机ASR当C3离线后端；即使过也只支持下一独立设备路径/内存研究，不直接部署或宣称误触率/goal完成。


### UX250 完整短语文本复核结果 — 2026-10-02
仅已核实24公共TRAIN实际PCM，37本机SenseVoice解码/2.672s；全词逐字确认普通话0/6粤语1/6，首事件前2.048s两语各0/6，已知近词1→0但正例严重损失，按既定门槛失败停止。小严/小贤等转写保留，不加入同音词迎合；2触发窗未含完整源不冒称人类音节缺失。只否定固定模型/规则组合，不能泛化所有ASR；主机模型不能当C3功能，原始前缀65536B预算未准入。无设备/录放/云/训练/TEST/Flash/新阈值，当前72/off/off/历史保持/USB无占用；误触与完整goal仍未完成。docs/WAKE_TRANSCRIPT_CONFIRM_REPORT.md。


### UX251 ASR先完成握手的有限实验计划 — 2026-10-02
上一goalturn=progress：249三轮动态候选全采用但ASR2.4s/heap47360不足，250逐字本机ASR门槛失败并关闭；全范围保持/阻塞计数0。仅协作TLS选项ON时将候选worker触发从asr_connect移到真实asr_connected，录音仍异步进行；不额外等待/堆分配/新模板，预热路径及OFF行为保留。先原/新顺序、重复NULL通知、未连接取消/ASR故障、完整join所有权sanitizer和现有适配/ASR检查，正常预算构建冻结；只唯一中粤中3轮remember各1次/250msgap，不重试替组，记录ASR/候选/heap49152/DMA/WDT及本机实录；guard全4MiB备份仅app，始终恢复72/off/off/历史保留/USB释放。不调训练/阈值/优先级或deadline，不减上下文，未过不采用/goal仍未完成。asr-first-ux251/plan.json。


### UX251 ASR优先握手结果 — 2026-10-02
正常版本单组中粤中首次唤醒/完整输入/记忆3/3，gap266/265ms，动态候选hit3/3；ASR1146/1144/1117ms、候选打开1626/1892/1885ms、17step/最长671/702/681ms。回调先启动后打点导致candidate较connected早1/2ms，源码打开成功顺序不变。14 sanitizer过，首次PS argv配置失败保留并修正精确argv。capture累计52952B，playback52244到tts_done47184<49152，低点调用归属未知，不减上下文/栈；KWS8124us/DMA0无reset。实录前2固定输入门槛过，第三1–2k相关.6134失败保留不替组；前2应答能量候选3.068/3.131s最终7.078/6.101s，第三延迟未知/1s未证。app1502480B余37616/hash9af493efe789c5b9cb2d32d14c5fb6a08c5f31bfec1425877e37e8add51b4fd8，489源冻结/SDK未改；guard全4MiB仅app非app保留，fresh72/off/off/USB释放，新增6历史/2748B保留。未默认采用/误触和完整goal未完成；docs/VOICE_ASR_FIRST_HANDSHAKE_REPORT.md。


### UX252 最终TTS堆分配归属计划 — 2026-10-02
251正常单组三轮ASR1.1s/动态3次、heap47184低点移至playback到tts_done；完整范围保持/阻塞计数0。仅启用既有whole-job16行/512B分配钩子和明确252诊断标签，保留SDK各区累计最低及原49152门槛；钩子排除ISR/cache关闭/重入、记录覆盖跳过，不当全局保证/正常版预测。源冻结/正常app预算内；唯一中粤中3轮remember各1次/250msgap/no ready等待，不重试替组；以真实任务/大小/时钟归属区间、不由大小臆测调用点。guard全4MiB仅app、始终恢复72/off/off/历史保留/USB释放；不减栈/队列/音频缓存/上下文、不训模型或扫阈值、不私音上云/不报诊断时序达标。tts-heap-ux252/plan.json。


### UX252 最终播音堆诊断结果 — 2026-10-02
唯一中粤中三轮首次/完整记忆3/3，动态3次；钩子总量低点49252/49084/49592B，覆盖78/77/74、跳过13/113/31。三轮agent_tts在playback后申请16749B，随后wifi包分配降低空闲；无调用栈不能断定唯一调用点/泄漏，漏采样限制保留。SDK累计46120<49152，原门槛不换；第三实录准入失败保持，不替组/无一秒与听感结论。app1503456B/余36640，hashe7941bb62f6d7b8619231d96d373b4264a071e8f1471d06c96b8789361c7f7ff，488源冻结/SDK不改；14原相关sanitizer沿用未假报重跑。guard全4MiB仅app/非app精确、fresh72/off/off/USB释放，新增6历史/2748B保留。检查到最终HTTP响应已返回仍保留到TTS join的所有权切入点，尚未修改。误触和完整goal未完成；docs/VOICE_TTS_ALLOCATION_REPORT.md。


### UX253 最终HTTP连接释放计划 — 2026-10-02
只在最终answer_end、HTTP producer已返回后关闭其已缓存连接，再join TTS，保留origin handle/tickets；进度应答、feed/on_sent、中间工具轮不动。默认新选项OFF，ON标253；不增缓冲/减栈或上下文。先成功排空/取消错误/失败启动/重复end/进度不释放及平台HTTP和engine相关sanitizer，正常预算冻结；唯一中粤中三轮remember各1次/250ms，不补试，保留48KiB及实录失败。guard全4MiB仅app/非app精确，始终恢复72/off/off/历史保留/USB释放。模型740不改，资源修改不能称误触修好/整体完成。final-http-release-ux253/plan.json。


### UX253 最终HTTP释放结果 — 2026-10-02
默认OFF选项，仅最终HTTP producer返回后close缓存连接再join独立TTS，原进度/中间tools不改；六项ON/OFF/错误取消/排空/HTTP与engine sanitizer过，真实ELF先release再stream_end。唯一中粤中首次/完整记忆3/3，ASR1148/1147/1159ms，动态3次，VAD后应答390/357/349ms；实录三输入均过，本机ASR确认，应答能量2.843/3.076/2.579s、最终5.953/7.136/7.549s，非有用音素/一秒与听感未通过。SDK首轮50200，次轮48660<49152/第三不降；历史差不作确定回收量，未减上下文/栈/缓存/TLS长度。KWS8044us/DMA0无reset。app1502528B余37568/增48/hash2049c9f9e2fc7f5320d59c7dd8a611408ec672b8975dc8a6780c9903eca3f7d8，489源冻结/SDK不改；初始CMake位置hunk失败保留。guard全4MiB仅app/非app精确、fresh72/off/off/USB释放，新增6历史/2748B保持。资源代码局部通过但未默认采用，模型740不改、误触和完整goal未完成；docs/VOICE_FINAL_HTTP_RELEASE_REPORT.md。


### UX254 用户再报频繁误唤醒：新自然人声准备 — 2026-10-02
实机只读确认72/唤醒与语音off、阈值740/增益1，1290事件897144B/next4517；该零计数来自当前启动且监听关闭，不当零误报。优先误唤醒：旧已复现近词及背景分类不足，不再扫描阈值/持续时间/固定资料拟合。审计原自然TRAIN两语各160条；扩展FLEURS下载器为仅TRAIN、原始ID及PCM跨所有旧split排除、空白标点目标排除、CC-BY4.0固定revision/240秒预算及失败关闭。先六项采样/防泄漏检查；唯一下载每语64条新公共TRAIN，冻结全部来源/时间/hash；不训练、评分TEST、录放/上云或烧录。本阶段仅数据准备，不能宣称修好；训练候选另行事前声明，设备和上下文保持。artifacts/voice-fast/wake-user-report-ux254/collection-plan.json。


### UX254 新自然人声准备结果 — 2026-10-02
只读核对72/off/off及原740/增益1；原Phase3自然TRAIN中粤各160条，覆盖限制不冒称根因。六项转换/跨split原始ID/PCM排除/目标词及失败关闭检查通过。唯一23.82秒下载FLEURS固定revision中粤各64条新TRAIN，原音频1529.76秒，旧原始ID与PCM交集0；128原始SHA、格式、原浮点到PCM逐样本与SHA全验证。不是新独立说话人或电视/音乐、真人正例盲测；没有训练、TEST评分、录放、云调用或烧录，当前误触未修好/整体goal仍active。fresh72/off/off/USB释放，1290事件897144B/next4517及204800历史预算/2MiB分区未变；docs/WAKE_NEW_NEGATIVE_CORPUS_REPORT.md及wake-user-report-ux254/closure.json。


### UX255 新自然语音C基线计划 — 2026-10-02
上一goalturn=progress：128新TRAIN原始人声及跨split去重/PCM校验完成；完整双语/流式/VAD/连续三轮/动态应答范围保持，阻塞计数0。只一次固定740、2.048s零预热+1.5s尾静音，对128完整原声用UX197已6387帧实机对齐的原C库评分，验证独立检测器时钟/事件；上限120s，不读取旧validation/TEST、不拟合或烧录。若新原声0误触不启动无依据的同类补训，改查实际麦克风域并另冻计划；若有误触再冻结单候选/预算。原声非实机音频、不代表field FAR或正例盲测。wake-new-natural-score-ux255/plan.json。


### UX256 新自然人声单分支有界训练计划 — 2026-10-02
UX255原C完整128源中粤各2误触，四段离线转写与官方原句为普通阅读，并非目标词；只作为新TRAIN依据，不冒称声学真值/现场FAR。冻结一个L分支仿射QAT、E/前端归一化/shift/拓扑/740及状态不变、不加交互。先全新因果输入和两原C头/平滑逐值对齐，再原15623TRAIN+实际72+全128新源重叠窗口，最终单checkpoint/2000步/CPU4线程/lr.01/600s、无扫描补训。TRAIN原正确两语>=99%、旧N/提前无新增、实际原正确全留、新完整128源4->0且无新增，过后才一次旧dev准入；未过停不看TEST/不烧录。无新增运行缓冲/库/状态或压上下文，后续物理需另冻计划/至少三轮业务/guard。完整goal未缩小，原72/off/off保持；wake-natural-branch-ux256/plan.json。


### UX255 新自然人声C基线结果 — 2026-10-02
唯一3.185秒原C完整128源评分，中粤各2段误触；所有原PCM及独立2of3/时钟精确。四段一次本机转写与官方原句为普通阅读，不当发音真值/现场FAR/独立正例证据。新增原C评分工具，未训练/开发或TEST/实机录放/云或烧录；新数据支持另冻一次单分支候选。证据wake-new-natural-score-ux255/score/report.json、local-context-asr.json。

### UX256 新自然人声单分支训练结果 — 2026-10-02
先全新因果输入112424目标值精确，初始/非零DW偏置导出各1085440层三方相等；只一次seed2026100256/2000步/CPU4线程最终checkpoint，49.75秒。最终24流2170880层QAT/C/F64全同，训练后窗口56212平滑值与完整C相等。新中粤负例各2->1/64，但原四清除后各引入1；旧N180->304/9364新增128，实际N13->17/28新增4，旧中正确原3538/3542保留、粤2512/2520，新增提前中6段/粤12段。固定TRAIN准入失败，停止候选、不读开发/TEST、不补训/烧录。只读覆盖审计：旧N4004/9364未抽、实际N28/28未抽，新误触128中80见过/48未见；两个新源全部窗口见过，残余约束仍不达标，不能断言补采样即可修复。没有量化/因果转换误差证据；不再将准备72输入等同于实际72参与训练。fresh72/off/off、1290事件897144B/next4517、2MiB分区及204800历史预算不变、USB释放；默认安装包/固件未替换。只读元数据探针WSLstderr UTF8解码告警和一个猜测close.py路径读取失败保留，未重跑训练或掩盖结果。完整goal仍active/阻塞计数0，误唤醒未修好；docs/WAKE_NEW_NATURAL_TRAIN_REPORT.md。

UX256后续训练约束：下一次拟合前，按全部分组大小、batch配额和总步数证明至少一遍覆盖可行；实际负例必须显式纳入采样和损失并记录逐条采样次数。预检发现无法覆盖即不启动训练，不再以汇总loss宣称所有约束满足。该约束尚未作为新训练验证过。


### UX257 完整覆盖与实际负例训练计划 — 2026-10-02
上一goalturn为progress：新源原C/一次训练/逐层精确及失败覆盖审计完成，完整goal保持/阻塞0。新增有限洗牌覆盖采样器，五项覆盖/缺组/预算/重现检查通过；训练前9364旧N四配额不可覆盖即拒绝，所有分组必须划分全部16649例。原L新起点、E/归一化/shift/结构/740不变，seed2026100257/2000步/batch32/CPU4线程/600s最终一次；配额中7粤7旧N7实际中2粤2实际N1新高1其它5，actualN显式252/weight8，原正支持与早触/旧N上限及teacher.02保持。先干跑全2000批SHA/逐条次数证明全部覆盖，真实训练必须完全相等；不是只靠loss/总量。新原声4->0及actual13->0、旧无新增N/提前、原正确两语99%与实际原正确全留才看旧dev，失败停止不补训/TEST/烧录。fresh72/off/off/USB释放，历史与2MiB/204800预算保持；wake-coverage-train-ux257/plan.json。


### UX257 全覆盖训练结果与UX258固定原证据保护计划 — 2026-10-02
五项覆盖检查通过；干跑全16649例/64000次，真实训练批SHA及逐条次数完全相同，旧N9364全部1–2次、actualN28全部71–72次。一次2000步50.29s，24流2170880层QAT/C/F64精确；actual原41正确全留、44全正确/近词13->0，但旧N180->232新增80，中粤新增提前8/14段，新源中2->0/粤2->2（均新），solo准入失败、不看dev/TEST/烧录/补训。下一个独立诊断UX258仅固定权重：原E/旧L平滑与原E/新L平滑取逐块min后单268/2of3检测，不先min头、不AND已完成的独立事件。一次原TRAIN/actual/128源准入，过才一次旧dev；失败停止无阈值/票数/窗口扫描。额外神经因果状态3126B与少量元数据是算术代价，未称C3资源通过；共享前端/E，物理另冻三轮/guard，完整goal保持。wake-original-guard-ux258/plan.json。


### UX258固定保护开发结果与结束核对 — 2026-10-02
TRAIN保护180->150/9364无新增，原中3536/3542/粤2509/2520、实际41全留/近词13->0、128新源原4->0，准入通过。唯一已知dev预测730例，原缓存逐值相同、八流542720层C相等；初次错误断言要求整个cold prime低阈值而中止，第5块483但暖机末三块-2759/-2738/-2729，零票证明成立。保留原错误/脚本，仅恢复已保存分数统计，无重算/换权重/门槛。开发中45->44、粤33->30、短中15/16保持、N5/636保持；粤语门槛失败，不部署/扫窗或补训。保留时延中P95/max0、粤17.6/32ms、短中0；逐例只读：普通话同块支持交集250不足，三粤新支持186/212/189，本身不足而非只时间错位。残留小智/小叶及两自然粤语；source_group的Kokoro是参考身份，四原PCM hash全等，生成记录用CosyVoice2两语言模型，非已证语言错标/人工发音确认。TEST未评分指项目另存TEST，已知dev含FLEURS上游test，不能称所有上游test未读或盲测。新额外3126B只是结构算术，开发失败未加固件状态/构建或烧录。fresh72/off/off、1290事件897144B/next4517与2MiB/204800历史预算不变，USB释放。新增coverage_sampling及五项检查、一次全覆盖50.29s与固定保护/来源诊断是本turn进展；完整goal active、阻塞0，无模型改善的现场/真实1秒结论。docs/WAKE_COVERAGE_TRAIN_REPORT.md。


### UX259 原双分支否定证据有限诊断计划 — 2026-10-02
用户频繁误唤醒继续优先。fresh USB核对72/off/off、740/gain1、1290事件897144B/next4517；关闭时零计数不是验收。仅原E/L固定一个符号否决：保留原平均与3块平滑，任一分支同3块整数和<0时只将本块决策分数置0，原268/2of3/暖64/冷24000不变；不调符号界限、权重、窗口或训练。原TRAIN正确交集各>=99%、N<=117/无新增N提前，actual原正确全留/N0、新128原4->0为准入；一次120s内，独立标量因果/极值及原C缓存逐值检查。失败停止规则、不读取开发或项目TEST，不写固件或播放录音；通过仅支持另冻C11实现及三轮实机计划。完整目标保持active，历史/分区和USB文本可用。wake-field-ux259/plan.json。


### UX259 原分支符号否决结果与收尾 — 2026-10-02
一次固定规则无训练/扫参：原TRAIN N180->164/9364无新N，原普通话正确保留3527/3542（丢15/增1）、粤语2492/2520（丢28/增3），实际原41正确全留但近词13->10/28，新128自然人声4->3（中2->1/粤2->2）。准入失败、规则停止；原平均不是充分修复点，不读开发或项目TEST、改C3/烧录或新录放。8192独立标量/8因果前缀/INT16极值过，8原C头及72实际18144有效分数/128原源C缓存精确。首次误比较288个补齐尾槽失败保留；事前冻结仅有效长度掩码修正，原C未存故重算，未声称复用缓存，正常修正12.62s不训练。fresh72/off/off、740/gain1、1290事件897144B/next4517、2MiB分区/204800历史预算、音量80和录音4824ms不变，USB释放。完整goal active/误触未修好/阻塞0；docs/WAKE_FUSION_SIGN_REPORT.md。停止同类否决门槛或窗口扫描，后续声学分类设计必须先验证C3预算。


### UX260 48通道C11内核准入计划 — 2026-10-02
上一turn progress：原分支符号否决仅实机已知13->10/新源4->3且漏醒，已拒绝；完整goal/误触范围保留、阻塞0。先C11纯内核compile-time24默认/48可选，固定同12层/40前端/127帧感受野，缓冲及529层trace匹配宽度，不加入通用运行库或运行时扩容。单个固定seed2026100260密集非零48探针trained=false，仅数值/资源前提，不训练、不作唤醒质量候选。修改前冻结22源码和原C参考；原24 ABI大小、两支各4096特征及512PCM与修改前一致；48 NumPy/Float64 Conv1d/C全层精确、流式因果、ASan/UBSan/零运行分配/非法拓扑及溢出拒绝。原E/L静音typed seed必须字节相同，只更新源码hash绑定并通过现有6项检查。此阶段不读开发/独立TEST、不录放/云或烧录；通过后另冻C3预算/guard/资源计划，正式训练在C3数值/32ms/Flash/RAM证明后开始。现有72/off/off/2MiB上下文保持。wake-wide-kernel-ux260/plan.json。


### UX260 48通道内核主机结果 — 2026-10-02
默认24布局与修改前完全相同（主机single8416/fusion11600/神经3126/trace650），两支2170880层及1024完整PCMtrace精确。48 C/直接索引NumPy/Float64 Conv1d在4096帧2166784层全等、512PCM对齐；主机工作区11952B/神经6134/trace1178，比当前双24多352B，不当C3数据。7项原/新sanitizer与48流式浮点检查过，错误宽度/trace/topology/overflow拒绝，原typed静音seed字节相同仅新hash绑定。固定密集18528权重探针trained=false，未训练或识别改进证明；13.01s主机流程完成。fresh原72/off/off、原安装workspace12632/历史1290/897144/next4517/2MiB/204800预算保持，USB释放。完整goal active。docs/WAKE_WIDE_KERNEL_REPORT.md。


### UX261 未训练48通道C3资源计划 — 2026-10-02
260主机通过后，独立显式48/probe构建，扩展USB trace按529层输出，默认24容量/行为不增；label261-wide-probe、trained=false无分类能力声明。先正常1540096B构建/分区/编译macro/源码hash门槛；guard全4MiB备份仅app校验，唯一512PCM完整trace及输入CRC/错误序号/取消/互斥测试、唯一60秒麦克风推理资源窗，不播音/保存录音/云或业务。每双256帧总算时<32ms、实际512样本max<32ms/p99<=16ms、SDKmin>=49152/连续块>=24576/DMA0，失败不补组或缩上下文。始终guard恢复fresh原app/72/off/off/上下文clip灯音量/释放USB，不正式训练直到资源门槛通过。三轮完整业务留给训练后实际候选，不以无分类探针冒充验收。wake-wide-board-ux261/plan.json。


### UX261 C3诊断传输失败与恢复 — 2026-10-02
正常预算应用1499840B/余40256B；guard全Flash备份验证/仅app/读回/非app相同。首帧2356B级完整trace超过2048B USB原子TX容量，frame0超时；frames0、未读新质量数据、未执行麦克风窗口，不算数值/资源通过。SDK usb_serial_jtag_write_bytes直接xRingbufferSend整项，runtime未分块是已核实故障。原失败与源/固件保留UX261。guard使用fresh备份原app精确恢复22faca4f...c07，Flash全体after SHA与安装前SHA相同；fresh72/off/off/历史clip灯音量与summary保持、USB释放、误触未修好。


### UX262 USB原子发送修复与一次重验计划 — 2026-10-02
261未得到资源测量，先对实际esp_agent_write函数编译+原子2048B sink复现2356回复丢失，再按现有ring容量分块、输出锁覆盖整条回复，不扩大堆/TX队列、不改模型或阈值。只为这项已定位传输故障另列一次重验；保留失败，不重复已失败识别规则/资源组。复用既有IDF构建目录与同options，独立262 label/源归档/固件。仍唯一512全529trace及协议CRC/顺序/取消、唯一60s mic实际推理；原32ms/p99<=16ms/SDK48KiB/连续24KiB/无DMA损失门槛，measurement<=170s不含备份恢复。仅guard写入/始终fresh原72/off/off恢复、数据保持。正式训练继续待准入，probe不能作为误醒率证明或3轮业务验收。wake-wide-usb-ux262/plan.json。


### UX262 48通道C3准入与USB修复结果 — 2026-10-02
原函数在2049B边界复现原子队列整条丢失；同一实际函数编译回归修复后0/1/1536/2048/2049/2356/2484/4096/16384B、部分返回、0/-1超时/锁失败过，24短回复一调用行为保持，无新堆或TX扩容。应用1499856B/正常预算余40240B。一次512 PCM/311808前端+层值精确、CRC/序号/取消/互斥过；两256帧合计最大10037us。一次60s实际mic 1882块/max9110us/p99上界9500us、workspace11952B、true SDKmin68884B/连续块最小查询65536B、DMA无新增。测量76.23s，备份/安装/恢复另计；未训练trained=false不能当零误唤醒证据、没有业务或云回归。两次均guard仅app备份/读回/非app验证并fresh原app恢复，Flash全体SHA精确相同，72/off/off/上下文1290/897144/next4517、clip/light/volume/summary不变、USB释放。261失败不覆盖。完整goal active、误触仍未修好；后续准入为48训练/量化/导出与双语/自然人声对照，不再小模型阈值/窗口扫参。wake-wide-usb-ux262/closure.json。


### UX263 显式48训练/量化/导出链计划 — 2026-10-02
262原SDK堆/32ms/C3数值通过且原72/数据恢复后，扩展现有trainer/calibration的编译宽度元数据；默认24及旧缺省checkpoint兼容，旧校准JSON/C字节完全相同。48仅显式选择、校验stem/tensor/config、static assert防C24误编译。每宽度唯一1步人工随机8clip链路测试，48导出4096全层/512PCM数值对齐及错误宽度拒绝，主机120s/不重试。无真人或实际TRAIN/DEV/TEST语料/播放采集/云/Flash、不修改当前部署模型，不把synthetic loss当识别质量；不声称现有QAT脚本支持48。完成后才正式语料fit。wake-wide-export-ux263/plan.json。


### UX263 48训练导出链结果 — 2026-10-02
6.69s人工随机8clip每宽度唯一1步CLI，梯度/参数finite，24默认/48显式checkpoint19105参数，宽度元数据与stem匹配。旧无channels字段24checkpoint由旧/新校准整份JSON及C字节一致；新48 PTQ18528权重/529trace，C/NumPy/F64 Conv1d4096帧2166784层全等、512 PCM全trace精确，workspace11952B。缺48字段/错24或48tensor/36/boolean宽度、错C24编译及Python24trace拒绝。未读实际语料/DEV/TEST、无声录放/云/烧录，不能用synthetic loss证明识别；原QAT脚本未声称扩宽支持。fresh72/off/off/1290/897144/next4517/2MiB/204800预算保持、USB释放。正式训练可在另冻有限数据/一次fit计划后开始；误触仍未修好、完整goal active。wake-wide-export-ux263/proof.json及source.zip。


### UX264 一次fresh48声学训练计划 — 2026-10-02
263工具链通过后，首次完整48浮点/BN从随机初始化，不继续失败QAT checkpoint；训练后只以固定TRAIN512校准新shift/bias，不做QAT/校准回退。全部15791 TRAIN、原72+公共31实际输入、128自然源954窗；长实际流stride128/256窗及无效尾mask，不截短素材。CPU4/seed2026100264/2000步batch32/lr.001/AdamW.0001/grad5、420s，final only/一次无重试；原masked BCE8另加.25负例三块mean/twoof3峰机会loss，固定268/64warm/24000cooldown。七组全覆盖；每源/声线及全部hash冻结，不读DEV/独立TEST、不采放/云/Flash。原前15623正确各>=99%/N<=117无新N提前、extra168另测原C并不得新增、103实际原正确全留/所有N0、新128全N0，批F64与原C/新C103+128全流及8old/4096全层/512PCM/独立事件数值先过；任何失败保留并停止此候选不调参数。宽度/训练表示/样本构成同时改变，不声称宽度单独改善。训练后的C3业务还需另冻三轮完整回归，probe/已知回放不当真人质量证明。wake-wide-train-ux264/plan.json。


### UX264 fresh48完整训练与拒绝结果 — 2026-10-02
17066窗口/15791旧TRAIN+103实际父源+128自然父源954窗，七组全部最少见1次，长实际流无截断/尾mask；一次2000步36.34s，含固定TRAIN PTQ38.72s，final only/没有中间选择或DEV/TEST。4096特征2166784层/512PCM全trace/C-NumPy-F64精确；8old/103实际/128完整自然流179146 head值批F64/C精确、4096独立mean/极值/前缀及原C detector事件精确、原cache前15623验证，extra168另测原C无错用cache。旧前15623 N180->229（新增123）；中原3542保留3361丢181/增11，粤2520保留2312丢208/增18；新增提前中18/粤64。原实际N13->6/28、公共N8->2/23，原49正确保留48丢1粤语；extra168 N2->3/132新增1、正原35保留30。新128自然4->0无新，仅这一项不能抵消回退。准入失败停止此候选，未读DEV/项目独立TEST、未再fit/QAT/校准网格或烧录。fresh72/off/off/阈值740/gain1、1290/897144/next4517/2MiB/204800预算、clip4824ms/音量80/灯/summary保持、USB释放。完整goal active/误触未修好/阻塞0；宽度运行前提已证但分类并未获足够提升，不直接加训练次数。wake-wide-train-ux264/closure.json及docs/WAKE_WIDE_TRAIN_REPORT.md。


### UX265 同checkpoint浮点/INT8定位计划 — 2026-10-02
UX264完整fit虽数值正确但识别回退，先冻结同一final/BN/归一化与既定268判定；一次180秒CPU4只读全部15791旧TRAIN、103真实长度实际流、128完整自然TRAIN流。F32头round-away到Q8后原三块向零mean/twoof3/64warm/24000cooldown，先同模型流式/分块/批身份/独立F64 BN折叠证明，再复现所有264 INT8统计、记录阈值与有效/提前事件翻转及固定22条层误差/饱和。无fit/校准/扫阈值/DEV/独立TEST/采放/云/Flash。extra168原事件由已核对264事件差异重建，明确非新C推理。仅定位，不把浮点主机结果当C3可部署模型。保留原72/off/off/持久数据及完整goal，失败不重复。wake-float-int-diagnostic-ux265/plan.json。


### UX265 同模型F32/PTQ诊断结果 — 2026-10-02
一次21.86s，无fit/校准/阈值网格/DEV/独立TEST/云/采放/Flash。同final流式/分块/批/因果前缀检查、4096独立mean/Q8极值过；F64 BN折叠最大误差1.24e-5，固定22流PTQ层/缓存与全部264统计重现。原9364 N180，F32237，INT8229；F32原中正确3542保留3359丢183、粤2520保留2307丢213，提前新增27/71。F32实际N6/28+2/23与INT8相同，原49正确只保48；extra原35保30，N2->3；新128自然均0但不能抵消回退。量化仍造成事件翻转，但不是主要识别失败来源，不能继续只改量化。层clipped统计包含ReLU低端截断，不可误称约48%上饱和。首次WSL启动因Windows反斜杠file-not-found，未开始实验；保留错误，只修argv后唯一运行。fresh72/off/off、历史/录音/音量/灯/summary保持，USB释放，完整goal active/误触未修好/阻塞0。wake-float-int-diagnostic-ux265/closure.json及docs/WAKE_FLOAT_INT_REPORT.md。


### UX266目标审计与UX267一次事件目标训练计划 — 2026-10-02
266全部TRAIN窗口无完整x相反标签碰撞，原正确接受高分块中3542/18410中、2806/13460粤标签为0；新浮点漏醒183中24、213粤67存在足够接受机会却无接受事件，其余缺支持。只能说明目标/判定不一致，非量化主因或唯一原因证明。新增host event_loss前向精确Q8/向零三mean/median twoof3，4096随机极值/实际C事件/因果前缀/梯度/暖机/尾mask/无本地接受正父源/拓扑检查过；单测初次Tensor.fill拼写失败保留，修fill_后重验，不重跑质量。267独立fresh48 seed2026100267/4000步batch32/lr.001/AdamW/300s/final一次，accepted268/256+1、forbidden268/256-1平方hinge+.25剔除冲突的pointBCE，三块机会全窗口/提前后段压制，不改正式门槛。复用原17066全覆盖数据与真实长度/全因果父源，原准入条件与264 C evaluator完全相同，无DEV/独立TEST/新采放/云/Flash。宽度、训练目标/步数共同作为新候选，不作单因素归因；失败保留不补训或扫参。完整goal active，wake-event-fit-ux267/plan.json。


### UX267事件目标结果及UX268归一化差异诊断 — 2026-10-02
原17066全部覆盖，旧N各3–4次/原中7–8粤10–11/实际正102–103次/实际N24–25/自然窗16–17；84个正父源窗口无本地允许端点明确转负监督，其余6367有效正窗口。一次4000步84.90s、含固定PTQ87.27s，final only；原C/独立NumPy/F64 2166784全层、512PCM，179146完整头值/4096mean/C事件精确。旧N180->233/9364新增134；中原正确3542保3485丢57/增43，新提前9；粤2520保2480丢40/增86，新提前0。实际原72近词13->0/28、公开31的8->0/23，52正全正确/49原正确全留/无新提前；extra168 N2->1但粤丢2，128自然4->1，其中1粤新触发。因此原准入失败拒绝，不读DEV/独立TEST、不补训/烧录。266追加raw head核对1125/19461中、931/14039粤标签0，约6%；19–21%是平滑判定块，差异只能作为目标对齐线索不是唯一根因或所有块必要证明。268四个固定平衡32批、128已知错误/控制case，同权重frozenBN与shadow trainBN；漏中正确9->18、漏粤6->17，新误触32均0正确、原干净32->27；非因果全批/未来统计不是部署方案，不能单改BN解释所有残余，scratch buffers丢弃/权重与frozen状态不变。三个阶段源归档、模型hash、失败与原unit拼写日志完整保留。fresh72/off/off/历史1290/897144/next4517/2MiB/204800预算、clip4824/音量80/灯/summary保持，USB释放；完整goal active/误唤醒未修好/阻塞0。docs/WAKE_EVENT_OBJECTIVE_REPORT.md与wake-event-fit-ux267/closure.json。


### UX269负例监督审计与UX270独立分组训练计划 — 2026-10-02
上一goalturn为progress：事件目标/一次完整fit/BN只读诊断，完整goal保留、阻塞0。269审计796近词(旧N8.4%)贡献原15623误触150/233、新误触78/134，每条原仅3–4次；fragment5190、other1285、natural2225均3–4次全部见过，不称遗漏样本。新分组helper不改label/split、未知文本归other、自然源优先，32重排/正例排除/坏形状及标签拒绝过。270 fresh48 seed2026100270/6000步batch32/CPU4/360s/final一次，原event_loss与C判定不变，old中6/粤6/近词6/片段4/other2/natural2/实际中1粤1负1/新自然3；fixed cosine .001->.0001，不选中间、不继续任何失败模型，不作单因素归因。所有17066至少一遍预检与真实次数完全相等后PTQ固定TRAIN512；原完整准入不变，原C/NumPy/F64/全103实际与128父源对齐后一次评分，失败保留不额外fit或阈值网格。无DEV/独立TEST/新采放/云/Flash；fresh72/off/off/原历史与2MiB/204800预算不变、USB释放。wake-balanced-fit-ux270/plan.json。


### UX270平衡训练结果 — 2026-10-02
一次6000步128.85s，含TRAIN512 PTQ131.23s；旧中9–10/粤13–14、近词45–46、片段4–5、other9–10、old自然5–6、实际正76–77/负36–37、新自然18–19次，全17066次数等于干跑；未继续失败checkpoint。C/NumPy/F64 2166784层/512PCM、179146完整头值/4096mean/C事件精确。原旧N180->36/9364(新增22)，原中3542保3538丢4增69、新提前2；粤2520保2519丢1增117无新提前。实际52正全正确/49原正确全留、51近词N21->0，但一粤语新提前；额外168原正保留与无新增门槛过；128自然中粤均0。总量/99%保留通过，新增事件门槛未过，单48候选拒绝；无DEV/独立TEST/新采放/云/Flash、不补训或扫阈值。fresh72/off/off/原持久数据保持、USB释放。源码/失败/权重完整留档，后续共同确认必须另冻单一规则与完整准入，不自动把此失败模型作通过。完整goal active/误触未修好/阻塞0，wake-balanced-fit-ux270/closure.json。


### UX271冻结原版与平衡48共同判定计划 — 2026-10-02
270单模型明确失败不作通过；新独立组合只在每512端点对原E/L平均后三块mean与新48三块mean逐块min，送同一个268/twoof3/64warm/24000cooldown原检测器；不先min头/AND已完成事件，不扫窗阈值或继续fit。全部旧15791/原103实际/128完整自然源原门槛不变，extra168另算原C；C原detect与Python独立scalar/极值/前缀/全实际事件一致。120秒一次主机；三NN共享前端，若先用48容量槽约24KiB仅估算。读音频owner源码确认capture开始disarm释放KWS，不据此冒充SDK峰值/32ms/Flash或三轮业务通过。无DEV/独立TEST/新采放/云/Flash，失败停组合不回退网格；过才另冻knownDEV。完整goal保留/active，wake-balanced-guard-ux271/plan.json。


### UX271共同分数拒绝与时刻诊断 — 2026-10-02
唯一固定min(mean_old,mean_new)组合10.95s，独立原C检测器/4096scalar min/极值/前缀与所有评分块事件一致，原cache180/9364复现、extra168另算原C、原新三模型前端mean/inverse相同。旧N180->10无新增N/提前，但原中3542只保3346丢196、粤2520保2197丢323；103实际原49只留45丢1中3粤，51N0、无新增提前、新128均0，extra保留失败。完整门槛失败拒绝，不看DEV/独立TEST/固件。一次只读519损失支持审计：中4/粤1缺一侧，153条两侧同端点各自有twoof3却共同高分成员不同；其余361条证据分离，最小间距中32–224ms/粤32–576ms，全部但2条<=256ms。仅诊断支持，不称替代确认规则通过；新机制若利用近时刻证据须另冻并保持原已接受事件的一次性所有权/冷却，不能用滚动原机会凭空新增旧检测器没接受的事件。未做网格或第二fit。候选组合未增加运行代码/堆，约24KiB工作区只是估算，不当SDK/实时证明；capture_start disarm释放KWS源码已核实，不据此越过实测。fresh原72/off/off/持久数据/clip/灯/summary保持，USB释放，完整goal active/未修好/阻塞0。docs/WAKE_BALANCED_TRAIN_REPORT.md；wake-balanced-guard-ux271/closure.json。


### UX272已接受事件匹配计划 — 2026-10-02
独立原E/L与270新48检测器各保268/twoof3/64warm/24000cool，只把已接受事件在4096samples/256ms内配对，后来端点一次输出并消费双方；过期、非连续和disarm清证据，不复用滚动高分，不改阈值/窗口扫参/fit。依据271同端点153/错开361/缺5诊断，256ms亦为既定DEV延迟上限；不预判通过。完整旧15791/实际103/自然128原准入不变；C独立scalar/因果前缀/1048576块边界与所有评分事件校验，既有统计Schema不变。120秒一次主机，失败拒绝不扫网格；全过才另冻knownDEV。默认固件不编入，新C模块仅独立host测试；无DEV/独立TEST/Flash/新采放/云。完整goal active/阻塞0，wake-event-confirmation-ux272/plan.json。


### UX272事件匹配失败与锚点审计 — 2026-10-02
一次9.69s固定accepted/accepted4096匹配：C/Python所有评分块与因果前缀一致，ASAN/UBSAN1048576原检测块、边界/时钟/丢块/disarm/唯一所有权通过，matcher状态32B；默认固件未编入。旧N180->11无新增/提前，中3542留3535丢7，粤2520留2442丢78，103实际原49只留39丢5中5粤；51N0/新自然128全0，但粤/实际/extra保留失败，拒绝、无DEV/TEST/Flash。另冻只读原已接受valid anchor的附近证据审计：85条旧损失都无256ms内新twoof3支持；实际10损失中9条有支持却被新模型独立accepted/cooldown挡住。271机会时刻最近距离不能冒充相对于已接受原anchor的距离；不扫更大窗口、不称回收519通过。后续应把真实正例的原合法anchor加入训练监督，并由一个原accepted事件拥有输出，而不是再扩大窗口或阈值。源码archive校验/fresh原72/off/off/历史1290/897144/next4517/2MiB/204800/clip/灯/summary不变，USB释放。完整goal active/阻塞0；wake-event-confirmation-ux272/closure.json。


### UX273原合法事件锚点监督准备 — 2026-10-02
先冻结TRAIN-only准备，不fit或评分另一规则：旧原正确事件只能在已标注positive允许区间提供anchor，实际parent完整事件按provenance平移进入局部窗口，排除prewarm/tail/out-of-accepted；原负例误触和自然negative不作positive教师。新增精确Q8/三块mean/twoof3锚点hinge，后续与原event_loss禁止/早触惩罚共同使用。独立标量/梯度/空mask/因果前缀/坏metadata单测；不删正例/改标签/split/DEV/TEST，不训练烧录；只证明目标准备，完整goal active/阻塞0。wake-anchor-supervision-ux273/plan.json。


### UX273监督通过与UX274一次锚点训练计划 — 2026-10-02
273五个单测/标量梯度/空mask/因果尾改动过；17066窗口中6146合法positive anchors，原中3559/粤2538/实际49，natural954与所有negative/prewarm/tail均0；parent时钟平移保留。准备通过不是识别通过，273源码归档hash核对，无fit/另类评分/DEV/独立TEST/Flash。274 freeze fresh48 seed2026100274/6000step/batch32/CPU4/360s/final单次，原完整采样/cosine/PTQ512不变，event_loss加weight1原合法anchor exact opportunity hinge，同margin1，不改negative/early代价。预先固定后续only一个原E/L accepted拥有输出、同时要求新48三块mean的twoof3机会，辅助侧无accepted冷却、不等未来、不扫时间窗；最终时刻是原事件同时间子集，延迟0。先单模型完整数值/诊断，再该唯一复合原准入，全过才knownDEV，失败不另fit/阈值/控制器。完整goal保持active/阻塞0，wake-anchor-fit-ux274/plan.json。


### UX274数值/单模型诊断与UX275既定原事件拥有者 — 2026-10-02
274单次6000步116.58s、含PTQ118.95s，完整覆盖，无DEV/独立TEST。C/NumPy/F642166784层值、512PCM及全179146头/事件对齐，32.60s诊断；单48旧N180->61但新增43，中原丢2/粤3，实际原丢2粤且新增2粤提前，单模型不得采用。275仅执行274fit前明示的唯一控制：原E/L accepted保持时间/冷却，同端点新48三块mean独立twoof3机会作为核验，不另accepted冷却/未来等待/配对窗/阈值选优。最终只能是原事件同时间子集，完整原准入不变；C保护/冷却/不同vote成员/262144随机块及全部事件与oracle过才判断。失败不得换另规则/fit；全过才knownDEV和C3资源/三轮业务。无DEV/TEST/Flash/采放云。完整goal active/阻塞0，wake-original-owner-ux275/plan.json。


### UX275纠正缓存与UX276固定knownDEV计划 — 2026-10-02
275初次extra168误用271保存的270 new分数，整份准入无效，不作为失败/通过使用；初始源码与结果保留。预声明repair只重算该168：274当前C43008头、owner21504块对齐，current old-scores[15623:] hash不变；纠正后额外原中17/粤18全保、N2->0、无新增/提前。主群中3542留3541丢1、粤2520留2518丢2，N180->16无新增，实际原49全部保留/51N0/新128N0。预先固定复合所有原TRAIN门槛通过，不选替代模型/规则。276冻结一次已知730DEV，明确非盲并可能含上游test素材，仅项目独立TEST不读；既定中44/粤32/短中14/N<=5/无新增或早触/延迟P95<=96max256门槛保留；730原E/L完整C复现cache、730新48完整C、8全层NumPy对齐和Cowned事件/冷prime检查。120秒失败停候选无fit/grid；过后也需C3混宽/资源/三轮，当前未烧录。完整goal active/阻塞0。wake-original-owner-ux275/evaluation-corrected.json及wake-owner-development-ux276/plan.json。


### UX273–276关闭、knownDEV召回失败 — 2026-10-02
2736146合法锚点/5单测/数据时钟不改；274一次fresh48/全17066覆盖/116.58s fit118.95s含PTQ，完整C数值通过但单48旧N61/新增43/提前，单模型拒绝。275按fit前原owned固定规则，262144块ASAN/UBSAN/全部Cowned事件过；初次extra168误读270分数导致整份准入无效，原始记录保留。只该168用274C43008头重新核对、owner21504块，corrected后完整TRAIN门槛过：原prefixN180->16无新增/提前，中3542保3541/粤2520保2518，extra原17中18粤全保/N0，实际原49全部保/51N0，新128N0。276known730DEV非盲，730原E/L C全cache复现/730当前48 C，8trace1083392层值一致。首执行在额外未声明的全prewarm原分数低于268断言失败：只有blocks1–7高，warm末三块-2759/-2738/-2729，original/owned均无已接受prime事件。保留失败，修正为既定no accepted events并只重复8trace/prime/统计，不重跑730NN或改model/rule/gates。knownDEV中45只保43、粤33只保28、短中15保14，N5->3无新/早触，所有输出同原时间新增延迟0；普通中/粤保留失败，最终拒绝部署，未将TRAIN提升当现场通过。只读8损失时刻：中3里2有此前<=256ms支持，粤5里4有此后<=256ms支持，2没附近支持；未评分替代时窗/控制器。四阶段源码归档/开始sha/计划输入hash校验；fresh原72/off/off/原context/clip/灯/summary不变、USB释放。完整goal active/阻塞0，尚需双语/异步ASR/VAD/流式/连续三轮及衔接完整实机。docs/WAKE_ANCHOR_OWNER_REPORT.md与wake-owner-development-ux276/closure.json。


### UX277原接受事件拥有短时核验计划 — 2026-10-02
上一goalturn为progress：目标代码/一次freshfit/完整数值TRAIN过、knownDEV召回失败改变下一动作，阻塞0。冻结一个256ms双向证据窗口：原accepted token一次所有权，新独立twoof3可早/晚256ms，当前端点输出且不早于owner，过期/gap/disarm清pending/support；辅助无accepted冷却，原268/warm64/24000不变。原事件间距1.5s保留，实际输出最短可能1.244s，明确不冒充额外输出cooldown；确认前不启动ASR/录音/动作。原完整TRAIN/actual/natural和known730DEV门槛不变，包括P95新增延迟96ms/max256；120秒一次C/独立oracle/前缀/262144块ASAN。extras只用271 baseline和274 current old_scores尾，DEV缓存bound276全部730C证明，修复旧cache版本混用不复发。不fit/另窗/阈值/规则选优，不独立TEST/Flash/采放/云；失败停控制器记录延迟。全过才能另冻C3混宽/资源/三轮，完整goal active/阻塞0，wake-temporal-owner-ux277/plan.json。


### UX277短时证据控制器结果 — 2026-10-02
一次8.25s，C/Python所有评分/因果前缀/262144块ASAN与时钟/断块/disarm/含256ms界/过期/一次owner过，状态56B；原owner时钟冷却不变，输出最短1.244s，不冒充额外1.5s。原prefix中3542/粤2520全部保留，旧N180->18、无新增/提前；extra当前274分数原35全保；实际原49全保/51N0、新128N0。knownDEV中45/45、粤32/33、短中14/15，N5->3，无新/提前，中新增延迟P950/粤104ms、max224；超过原96ms目标8ms，整份准入失败保留，不改门槛或窗/模型。当前源码默认未编入，无Flash/新采放/云。源码归档校验/fresh72/off/off/原context/clip/灯/summary不变，USB释放。为按完整目标继续前进，可另立纯主机混宽内核诊断，明确与失败控制器准入分开；不把96指标标通过、不默认采用。完整goal active/阻塞0，wake-temporal-owner-ux277/closure.json。


### UX278纯主机混宽内核诊断计划 — 2026-10-02
277延迟P95104超过96仍失败，不改指标/模型/窗，不把控制器准入标通过或默认采用。按完整目标继续必要的可逆实现：48静态容量中只有显式KWS_ALLOW_MIXED24可接受完整注册24模型，无flag默认24/48维持原拒绝/ABI/资源；修复actual layer copy和mixed24 feed512 head index。4096特征完整层/极值、256PCM前端/trace和512feed事件对齐原证明库，非法mixed布局/flags sanitizer，default128zeroPCM seed重新生成须字节完全相同，只更新源码hash绑定，跑prime/default tests。先备份4可变文件，若默认回退则精确还原；未改IDF构建选择，不编入默认混宽/多模型资源。没有新识别质量预测/DEV/独立TEST/训练/Flash/采放/云，C3预算和三轮仍未证明。这是诊断内核能力，不绕过277准入；完整goal active/阻塞0，wake-mixed-kernel-ux278/plan.json。


### UX278混宽诊断内核通过 — 2026-10-02
10.48秒host：无flag24/48保持固定拓扑拒绝与ABI，只有48+KWS_ALLOW_MIXED24能完整接受24/48；actual层输出copy和mixed feed512实际head索引已实现，否则24head误读529尾部。default/proven及mixed/default各4337664完整层值一致，768PCM前端/trace、384feed512 rawhead/事件一致，非法24flag/36width/混合层/INT32bias溢出拒绝，ASAN/UBSAN/noalloc/哨兵过。default128zeroPCM seed inc字节067251...完全不变，只更新NN源码hash绑定；kws/prime/gate三个default测试通过。48单handle11952B，只是主机容量，未实现三头共享前端/typedprime/runtime或测C3资源，不冒充约24KiB预算过。IDF构建选择未改/不默认编入mix、不烧录，不把277104>96ms延迟失败改通过；后续资源诊断保持未达项，目标含双语/ASR优先并发/VAD/LLM流式/连续三轮/动态应答全保留。前后源snapshot/sourcezip/startsha校验，fresh原72/off/off/context/clip/灯/summary不变、USB释放。完整goal active/阻塞0，docs/WAKE_TEMPORAL_OWNER_REPORT.md及wake-mixed-kernel-ux278/closure.json。


### UX279共享前端运行时与typed初态主机计划 — 2026-10-02
保留277粤语P95104>96ms失败，不变模型/阈值/窗/指标。实现24E/24L/48验证器一次FFT、三独立NN历史；128zeroPCM导出明确C字段、24历史紧凑保存、无指针/FFT临时区，源码模型hash绑定，fresh恢复无需推理。增加16ms半帧unarmed立即清理证据，避免重启监听残留。4096极值随机、512PCM、已知TRAIN103实长流，对照278三独立模型完整head/首265层/平滑/因果事件；typed恢复对照真实128帧状态，ASAN/UBSAN/哨兵/noalloc、1前端3NN、gap/reset/取消/错误模型/未训练禁止输出/stalehash拒绝。已先写草稿，冻结后才首次执行；sourcezip和plan.json记录。无DEV/独立TEST/训练/采放/云/Flash，不改IDF默认选择，未证明C3资源，不采用277或声称误唤醒修复。完整goal active/阻塞0。


### UX279共享前端/typed初态主机通过 — 2026-10-02
12.55秒：62206帧/186618 rawheads/16484590首模型层值，与278三个独立head/原平滑/277因果控制事件完全一致；103已知TRAIN流用实际512/656长度，未读DEV或独立TEST。一次PCM FFT+三NN，workspace24328B、typed seed12992B，恢复初态与128实际zeroPCM因果状态相同且0NN/FFT。16ms半帧unarmed即时清证据，gap先复位，错模型/配置/非fresh拒绝prime，ASAN/UBSAN/哨兵/noalloc/参数/未训练禁止输出/seedhash有效及stale拒绝过。首运行遗漏PCEN链接依赖exit1保留，唯一补依赖resume0。源archive/hashverified，fresh72/off/off/context/clip/灯/summary不变，USB已释放。无IDF默认改动/Flash/采放/云/训练/新质量筛选；C3预算与连续3业务轮仍未证明，277104>96ms延迟失败仍保留，不声称误唤醒修复/默认采用。完整goal active/阻塞0。


### UX280 C3共享前端资源诊断计划 — 2026-10-02
279主机通过后显式IDF opt-in三模型+typedprime，只资源诊断，不默认采用/不改277延迟失败。备份7可改源，独立build/0.11.280-verified-resource标签；麦克风检测只计数、不触发录音/动作，USB仍比完整3head/平滑/事件及首265有效层+48容量闲槽。正常app1540096上限，source/分区/hash过才烧。唯一512PCM+60s mic，pair/max<32ms、p99<=16ms、SDK48KiB/最大连续24KiB、无DMA/reset；measure170s不含全Flash备份/恢复。只flash_guard全4MiB验证备份+仅app+读回/nonapp相同，总是恢复fresh原72/off/off/上下文录音灯摘要不变。无采放/云/业务训练/DEV/独立TEST，失败不降context/栈/TLS预算，保留原记录，完整goal active/阻塞0，连续3真实业务轮仍必需。


### UX280 targetABI脚本断言失败与精确恢复 — 2026-10-02
独立应用1530208B/正常余9888B、C11/source/models/typedseedhash/分区过；guard备份与app-only读回/nonapp过。installed workspace24320，runner误把64bit host24328作为C3 equality，exit1，parity0/mic0，不能报资源或质量通过。context所有字段仍相同；freshbackup原app经guard恢复，整4MiB SHA与烧前4f752f...bc4相同。fresh原72/off/off/数据/USB释放。失败source/runner/命令包归档。下一步只用已编译C3命令静态验证sizeof，修host/target8字节差异的脚本等式，不变NN/模型/规则/指标，只一次未执行的资源测量续验。277104>96ms仍未达，完整goal active/阻塞0。


### UX281 C3共享前端运行预算通过 — 2026-10-02
与280完全同app1530208B/余9888B，真实C3 staticassert确认workspace24320B/typed12992B。唯一512PCM共311808trace值+三head/双平滑/因果事件全部相同，最大512pair15482us；唯一60s mic1883块，max14027us/P99上界14500us，SDKmin55956B/最大连续最小53248B，prime886us/32768samples，passive检测0，无DMA丢失/复位，resourceonly未触发录音/动作。77.844s测量，3默认sanitizer/prime/gate过，源包hash不变/target断言首失败保留。两次guard均全4MiB验证备份、仅app/读回/nonapp过，最后全Flash4f752f...bc4精确恢复原；fresh原72/off/off/1290事件897144B next4517/2MiB上下文204800预算/录音灯摘要不变、USB释放。仅运行预算，不是现场每小时误醒或三完整业务轮，277粤语P95104>96ms仍未达；未默认采用/未称误醒修复。完整goal active/阻塞0，WAKE_VERIFIED_RUNTIME_REPORT.md。


### UX282受限实机业务集成计划 — 2026-10-02
C3资源过后，同24E/L+48模型/256ms规则/740阈值/typedprime，移除资源only触发抑制，独立experimental label，仅一次中粤中连续3轮已知public remember脚本，单唤醒尝试、done后250ms不等ready。ASR capture优先、prefetch/reuse on、capture hold；不改新网络选项/模型/门槛或训练。严格min49152/完整输入/动态ack/最终连贯/复醒/无DMA reset，声学1秒另记录。277104>96失败仍保留、不准入/默认采用，只用户已授权可逆业务诊断。只guard备份/应用写/恢复fresh原72/off/off，新增对话保留，旧clip可从完整备份恢复，无私人录音上传/独立TEST预测，失败不加组/重试/降context栈预算；完整goal active/阻塞0。


### UX282共享三模型实际业务回归失败并恢复 — 2026-10-02
app1530080B/余10016B；唯一中粤中3连续回放，单尝试首次唤醒3/3、gap265/281ms，无reset/watchdog/DMA。全输入/任务仅1/3：前两上传36736样本/2.296s，ASR你记住/请记住；第三86912/5.432s完整记住小星星。ASR首连1127/1125/1097ms；动态candidate0/3，SDKmin39508<49152，不能称3完整轮/内存/快速应答通过。离线3输入common-offset固定对齐门槛失败，声学回复延迟unknown，不放松对齐或把缺词完全归于VAD。freeze前两noise116/93第三119，没有高noise阈值证据；新24k状态与旧网络/TTS重叠需要寿命审计，不能靠降context。已知原validation公共声音实际回放，不是独立TEST；原plan DEV_predictions=false仅未重跑离线DEV corpus，此含义明确补录。两guard全4MiB备份/仅app/读回/nonapp过；恢复fresh原22faca...c07/72/off/off。新增对话及summary保留、2MiB ctx/204800预算保持、旧clip在fullbackup可恢复、USB释放。模型/规则/阈值不变，277104>96延迟失败保留，不默认采用/不称误触修复，完整goal active/阻塞0。下阶段有限采集链/影子端点保留缺词录音、分配寿命审计；不重跑同组/搜索阈值。


### UX283有限输入链路与影子端点诊断计划 — 2026-10-02
同固定24/24/48/740/256ms/prime，不改模型/VAD/阈值；现有capture-probe8s+endpoint-trace开启，一次已知公共中粤中3轮remember，每轮一次唤醒，保留每轮clip和CRC notice。本机ASR/固定波形门槛+原C逐帧重放区分源/采集/提前端点，失败保持unknown不补组。导出和长采集明确排除速度/快速复醒验收；独立TEST不读、无训练/质量搜索。host3类现有检查、正常app1540096/seed/C11/SDK/partition过才guard上板；fresh4MiB备份/仅app/读回/nonapp，结束恢复原72/off/off，新增历史保留，2MiB/204800不减。完整goal active/阻塞0，277104>96及28239508堆/漏句失败保持。


### UX283影子端点输入诊断闭环 — 2026-10-02
app1532432B/余7664；唯一已知公共中粤中3轮一次唤醒、每轮8s clip保留，完整输入3/3，固定对齐3/3、原C1200帧/CRC notice逐帧相同、本机full/prefix6次均完整。shadow4860/5060/5040ms，用户末音4150/4210/4140，后710/850/900ms，无截句，未复现282间歇缺句。长采集及export排除速度/快速复醒/正常3轮验收，不能称8s修复；candidate transport3/3但泛化嗯我来记一下无话题复述。SDKmin34192<49152，查询最大连续块最低22528<24576，无reset/watchdog/DMA，资源仍失败。analysis wrapper错误调用speed analyzer被正确拒绝exit1、原日志保留不绕保护/重跑；input+endpoint分析0。只guard全4MiB fresh备份/app写读回/nonapp，恢复原22faca...c07/72/off/off，新增6历史保留1302事件/902484B/next4529、2MiB/204800不变，USB释放。读代码发现wake_network(false)先于HTTP release，可能重叠，尚无动态分配因果证据；下阶段有限寿命诊断。不默认采用/不称误醒修复；277104>96延迟失败/完整goal active/阻塞0保持。


### UX284有限分配寿命诊断计划 — 2026-10-02
上一283为进展：3录音/1200帧/6本机转写完成，未复现截句。当前增加仅诊断16x12B=192B transition ring，记录answer_end、rearm、KWS alloc和HTTP release原顺序，无分配/I/O/行为改变，默认编译掉。同模型740/256ms/prime，恢复正常采集、endpoint-trace+heap-hooks，仅一组已知公共中粤中3轮单尝试，逐轮保留clip/notice，缺失/overflow/skip保持未知，不称完整对象追踪；SDK regional min和callback实际free分开。门槛49152/24576、2MiB/204800不减；host安全+OFF+retention/trace、正常app1540096/seed/SDK/C11/partition过才guard。结束fresh应用恢复72/off/off、新增历史保留、USB释放；不调模型/规则/质量、不读独立TEST、无相同重复组。完整goal active/阻塞0，277104>96与整轮资源/速度失败不豁免。


### UX284正常采集/分配寿命诊断闭环 — 2026-10-02
app1533456B/余6640；新增诊断point ring192B+计数8B，默认编译掉；14 host检查和5 offline检查通过。唯一已知公共中粤中正常3轮单尝试，完整输入/源对齐/本机ASR各3/3，CRC+原C760帧一致，逐轮clip保留。endpoint5700/4860/4560ms，末音后1570/680/420ms，未复现282截句；trace/export排除速度与快速复醒验收。SDK累计区域最低37752<49152；callback同时free最低49128/48664/48664，skip2/9/3、非全程，不能替代SDK门槛；每轮7transition且无skip、无KWS marker，不能排除observer停止后重叠。低点均在final producer return后、HTTP未释放前，TTS16749B分配；晚release free增加3692/3692/3500B含其他任务非对象精确值。查询最大连续块最低26624，无reset/watchdog/DMA，candidate transport0/3/主题应答未过。SDK实现heap_caps.c306-314明确累加各区域历史最低，与同时总free分开报告，不放松48KiB gate。guard fresh4MiB/app-only/readback/nonapp保持，恢复原22faca...c07/72/off/off，新增6历史保留1308事件/905240B/next4535，2MiB/204800不减，USB释放。下一步仅试已实现的opt-in final HTTP早释放，不盲改rearm/模型/阈值/栈/上下文。不默认采用/不称误醒修复，277104>96仍失败、完整goal active/阻塞0。


### UX285有限最终HTTP提前释放计划 — 2026-10-02
上轮284为进展：正常完整输入3/3、760帧C一致、观察到TTS在最终HTTP return后仍与已完成连接重叠。仅使用现有default-OFF early-release option，producer返回后owner先close再join独立TTS；不改监听恢复。全部trace/hooks/probe OFF、同740/24-24-48/256ms/prime/ASR/VAD/预算，唯一已知公共中粤中3轮单尝试，后两轮done后250ms不等ready，无export轮间停顿，失败不替换。host final-only/ownership/cancel/idempotence sanitizer、app1540096/C11/seed/SDK/partition后才guard。48KiB/24KiB、完整输入任务、candidate主题/速度门槛不放松；2MiB/204800不减。结束fresh app恢复72/off/off、新历史保留/USB释放；不训练/读独立TEST/调模型规则/默认推广。完整goal active/阻塞0，277104>96失败保持，不能称误醒已修。


### UX285正常最终HTTP提前释放闭环 — 2026-10-02
唯一行为为opt-in early-final-HTTP release，全部diagnostic/probe OFF，六ownership sanitizer过；app1530112B/余9984。唯一中粤中正常组单尝试，后两轮done后265/281ms无ready等待；first wakes3/3但完整输入任务2/3，第三只请记住。candidate transport1/3且泛化无主题，SDK累计min39636<49152，observed最大连续块最低27648，无DMA/reset/watchdog。fixed external repeated源对齐失败，声学/一秒未知，不补跑；接口语音默认OFF保持。第三原clip36544样本/2284ms未覆盖，恢复后read-only导出与fresh测试后4MiB备份/原C及独立Py解码逐字一致，committed header/encoded CRC过、本机ASR也是请记住；upload只记样本无PCM CRC不称上传字节/云处理精确一致。截短已保存，exact endpoint trace未采集/完整源对齐失败，不能只凭长度猜唯一原因。preflight宏作用域错误在USB前退出，修正为voice单位guard、原wrapper保留；stack-evidence标志冻结前移除，组无诊断插入。两次fresh4MiB guard仅app/readback/nonapp，恢复22faca...c07/72/off/off，新增6历史保留1314事件/907904B/next4541，2MiB/204800及摘要灯音量保持，USB释放。实验未采用，不称误醒修复/资源因果收益；277104>96/完整goal active/阻塞0，下一步有限失败输入/端点owner诊断，不原样重复组。


### UX286有限重复确认诊断计划 — 2026-10-02
上轮285为进展：3首次/2完整，第三2.284s失败输入完整保留，original72/off/off已恢复。缓存是verifier三block均值，不能反推原raw；当前只固定检查均值后的重复twoof3改为当前一票支持。原EL3/268/warm64/twoof3/24000cooldown及辅助3均值/256ms owner窗不改，gap/disarm/unique/expiry不改，低当前分数无OR历史支持。一次120s现有C已验证cache的全TRAIN/随机prefix/262144块ASAN；TRAIN全过才knownDEV一次、96ms/256ms及原召回/无新增门槛不改。失败不再换票/均值/窗/阈值，未训练/新模型推理/读独立TEST/USB/采放/云/改运行固件/默认采用。完整goal active/阻塞0，现有资源/完整三轮/主题应答及旧277104>96失败不豁免。


### UX286重复确认一次固定检查结果 — 2026-10-02
仅当前已3block平滑verifier一票支持，原EL/窗口/阈值不变；7.94s，C/Py2121581 controller块精确、causal prefix/262144块ASAN过。原正确中3542/粤2520全保、extra原正确全保、已保存原49全保，新128自然N0，无新增负/提前相对原owner。但旧N180->40（原two-vote18），已保存N21->1（原two-vote0），未清除原误触因此TRAIN gate失败，不读knownDEV/独立TEST，不换票/均值/窗/阈值，不训练/模型推理/Flash/采放/云/改运行内核。保留原双票保护，归档核对/fresh72/off/off/USB释放，1314事件907904B/next4541、2MiB/204800/clip2284/摘要灯音量不变。整轮资源/三完整对话/话题应答及原277104>96仍失败；不默认采用、不称误醒修好，完整goal active/阻塞0。下阶段有限核对该一条已知残留负例及附近正例时刻目标，不质量或规则扫参。


### UX287已知单条误触的有限来源/时刻核对计划 — 2026-10-02
286只减少辅助重复确认使保存N0->1，已否决且未读DEV/TEST。现在只读已冻结actual72缓存及来源，确定唯一残留负例及同语言原正确时刻最近6正例，原事件前后12block，核对原/双票/单票已有事件与分数。一次30s，无推理/新规则/重平滑/反推raw/扫参/训练/DEV/独立TEST/采放/Flash/云/运行修改。来源标注和现场泛化分开，不靠单例选择新规则。原72/off/off及全goal active保持，失败和预算不豁免。

### UX287有限来源与时刻核对闭环 — 2026-10-02
一次只读0.056s：actual72缓存唯一残留index41/new17/标注你好小燕/minus6，原事件73728；辅助均值72704/274只一次高于原268，64ms后原EL973而aux-218，已有256ms窗口可绑定先前支持。双票无支持拒绝，一票放行。固定同语言原时刻最近6正确index5/42/0/33/39/46，三已有判定事件时刻完全相同；仅既有标注/缓存时序，不是人类独立标注或整体delay证明。没有反推raw/新模型推理/规则替代/扫参/训练/DEV/独立TEST/录放/Flash/云/运行修改/默认采用。源码zip/hash全核对，fresh7只读USB确认原72/off/off/1314事件907904B next4541/2MiB/204800/clip2284/摘要灯音量不变，USB释放。近似词的短辅助尖峰已定位，误醒/原277104>96/正常三完整轮/48KiB/动态主题应答仍失败，全goal active/阻塞0。后续声学区分与完整业务问题分别保留，不据单例加黑名单/减确认或无界重测。


### UX288三模型紧凑状态有限实现计划 — 2026-10-02
287为进展：定位标注你好小燕单辅助尖峰，未减确认/调模型。此次只减运行时冗余：私有借用缓冲view共用C11数学内核，两个NN24用真实历史，NN48不变，独立FFT前端去常驻层trace/不用detector；原默认handle ABI/数学/两票/256ms/模型保持。变更前源码已zip/hash核对，主机4096特征/512PCM冷热/103已知TRAIN实长完整分数层事件对照278独立backend及279全运行时，sanitizer/noalloc/gap/disarm/prime/默认6test，旧typed seed文本字节必须不变、source hash含新header和stale拒绝。一次600s不训练/质量选择/DEV/独立TEST/采放/Flash/云/降上下文或预算。先主机，后另行C3/至少3完整业务组，277104>96及正常堆/漏句/话题应答仍未通过，全goal active/阻塞0不采用修复版。


### UX288紧凑状态主机闭环 — 2026-10-02
私有borrowed buffers共用C11内核，NN24实际历史/无常驻trace，host24328->17112回收7216B，typedseed12992文本不变。62206帧186618head/16484590层值/31231事件块精确对278独立backend及279原全runtime，含1024冷热PCM/103已知TRAIN实长；默认6sanitizer/冷热3head/noalloc/disarm/gap/untrained/typedprime/stalehash过，默认ABI17字段8416B精确。首wrapper错用197历史9448 ABI停止于corpus前，保留exit1；改用变更前zip源码编译baseline只修验证器，未改运行代码，唯一未执行corpus续验exit0。旧EL seed inc和新verified seed inc都字节不变，绑定新buffers header及源hash；前后zip/执行zip归档核对。fresh原72/off/off/context1314事件907904B next4541/2MiB204800/clip2284/灯摘要音量不变，USB释放。无模型规则质量调优/DEV/独立TEST/训练/Flash/录放/云，C3资源及至少3完整业务组未证明；277104>96/正常48KiB/漏句/话题应答仍失败，全goal active/阻塞0。下步有限C3资源验证，不据host称误醒修复/默认采用。


### UX289 C3紧凑运行时资源计划 — 2026-10-02
288主机省7216B且分数/事件/旧typed初态/默认ABI全等；本次真实IDF C3编译object symbols先证明target sizeof不猜host，resource-only新标签289，原固定24/24/48/740/两票256ms/ASR/VAD/上下文/栈/预算不变，默认不开。复用已冻结281固定512PCM完整trace/heads/事件，唯一USB parity+60s mic，不录音/采放/云/对话；max32ms/P9916ms/SDK48KiB/largest24KiB/normalApp1540096不变，正常3完整业务仍另行必须。guard fresh4MiB验证备份/app-only/readback/nonapp及全Flash精确恢复原72/off/off，保留clip2284和1314历史2MiB/204800。不训练/调质量/DEV/独立TEST/砍上下文/自动采用；277104>96和原正常堆/漏句/话题失败保持，全goal active/阻塞0。


### UX289 C3紧凑运行时单次资源闭环 — 2026-10-02
{"stage":"UX289","complete":true,"C3_resource_prerequisites_passed":true,"application_bytes":1530208,"normal_margin":9888,"target_workspace_bytes":17096,"old_target_workspace_bytes":24320,"target_reclaimed_bytes":7224,"seed_bytes":12992,"PCM_frames":512,"trace_values":311808,"all_three_heads_scores_events_exact":true,"USB_pair_max_us":15980,"microphone_blocks":1883,"microphone_max_us":14327,"microphone_P99_upper_us":14500,"true_sdk_min_heap":63264,"minimum_queried_largest_block":59392,"prime_us_samples":[872,872],"passive_detections":0,"error":null,"source_archive_verified":true,"resource_only_no_capture_or_actions":true,"default_six_tests_passed_on_host":true,"full4MiB_restored_exactly":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"final_context":{"ready":true,"mode":"LOCAL","events":1314,"pending":1314,"recent_turns":128,"used":907904,"bank_size":1048576,"generation":18,"tail_recovered":false,"lamport":4501,"cursor":0,"acked":0,"last_local":38406,"history_budget":204800,"prompt_turns":0,"prompt_bytes":0,"trimmed_turns":0,"request_bytes":0,"prompt_locked":false,"partition_bytes":2097152,"archived_record":3224,"next_record":4541},"persistent_data_unchanged":true,"USB_released":true,"latency_gate_277_still_failed":true,"business_three_turns_proven":false,"adopted":false,"false_wake_fixed":false,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0}
仅资源诊断，无采放/存录音/云/对话/训练/DEV/独立TEST/质量选择。原判定/277104>96失败及业务三完整轮/快速话题应答仍必须；不据此声称误醒修好或默认采用。


### UX290紧凑运行时正常三轮计划 — 2026-10-02
289实际省7224B/512PCM全同/SDK63264资源通过并精确恢复，为进展；现在去resource-only，原285已实现早release开启，其余同固定模型/740/两票256ms/prime/ASR/VAD和预算，所有trace/hooks/probe OFF。唯一已知公共中粤中3正常轮单尝试，done后250ms不等ready/轮间无导出，原SDK49152/largest24576/全输入任务/快速话题门槛保持。host6ownership整合、app1540096/C11/seed/SDK/partition/hash后guard fresh4MiB/app-only/readback/nonapp；结束恢复原72/off/off保留新增历史/2MiB204800/失败clip/USB释放。无训练/新质量选择/离线DEV/独立TEST/规则/VAD/频谱/素材调整/默认采用/补跑。原277104>96、2852/3截短及topic失败不豁免，完整goal active/阻塞0。


### UX290紧凑运行时正常三轮闭环 — 2026-10-02
{"stage":"UX290","complete":true,"normal_groups":1,"rounds":3,"attempts":3,"rows":[{"round":1,"language":"zh","first_wake":true,"actual_gap_ms":null,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":83200,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true},{"round":2,"language":"yue","first_wake":true,"actual_gap_ms":281.99999989010394,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":78592,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true},{"round":3,"language":"zh","first_wake":true,"actual_gap_ms":281.99999989010394,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":77952,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true}],"application_bytes":1530128,"host_sanitizer_checks":6,"target_workspace_bytes":17096,"SDK_cumulative_min_heap":46840,"SDK_heap_gate_passed":false,"observed_minimum_largest_block":34816,"contiguous_gate_passed":true,"continuous_full_dialogues_passed":true,"full_input_and_task_count":3,"first_wakes_hit":3,"all_runtime_health_passed":true,"candidate_transport_hits":0,"naming_topic_specific_count":0,"topic_specific_ack_passed":false,"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"acoustic_source_alignment_passed":true,"one_second_reply_passed":false,"one_second_reply_status":"Not certified by energy candidates alone; retain acoustic report.","original_application_restored":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"final_context":{"ready":true,"mode":"LOCAL","events":1320,"pending":1320,"recent_turns":128,"used":910652,"bank_size":1048576,"generation":18,"tail_recovered":false,"lamport":4507,"cursor":0,"acked":0,"last_local":38534,"history_budget":204800,"prompt_turns":0,"prompt_bytes":0,"trimmed_turns":0,"request_bytes":0,"prompt_locked":false,"partition_bytes":2097152,"archived_record":3224,"next_record":4547},"appended_history_preserved":true,"old_clip_recoverable_from_backup":true,"USB_released":true,"known_validation_physical_replay":true,"independent_project_TEST_unread":true,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"execution_archive_sha256":"5947f8c12697172f1dd7791e22f0eacaf5046329499925e46de016bcd84f8109"}
唯一无诊断组，失败轮不替换/不补组；实际声学报告保留，不能把内部playback/energy候选当有用首音或一秒通过。新增历史保留，恢复原应用而非整片退回测试前，误触/277延迟/未过业务和动态话题目标保持。全goal active/阻塞0。


### UX291 内容相关记忆应答 — 2026-10-02
{"stage":"UX291","began":"2026-10-02T06:51:18.440649+00:00","before_hashes":{"plugins/speech/intent.c":"138c8249736c2ef4fd0c928d9af0020eed846a2f82222183185633b20d088dd3","plugins/speech/intent.h":"f958d6cc9925ec7f4f4abca3a47d63e91f0f1070ef4cd92b25f3c6652b0fdae8","plugins/speech/candidate.c":"12ef756eecb0affd1914b70bbfc6d2957890457ec08dd58445abb06db7c35ff3","plugins/speech/candidate.h":"6731dd3b23761de3930a5b14ded93376f4e4e8b8ad44ff090530639355df95eb","host_tests/test_candidate_local_first.c":"25010da34fad6e9c3b58db6915d979d28f3ccb43857de276661f3e75b6fd65fe"},"change":"In existing opt-in local-first policy, remember receipts wait for informative content; publish its immutable bounded source as THINK with a memory-origin flag. Final full memory intent, source prefix, language, cancellation/repair and output-source guards remain required. No new arena/field/task/transport or timing change.","tests":["Original API/ABI/identity/quick-answer/lamp compatibility.","Mandarin/Cantonese topic, immutable publication, changed ASR/intent/language, empty/incomplete/invalid UTF8, cancellation/repairs, source-boundary and final-only authority.","One saved UX290 ASR timeline replay; no new cloud or speaker interaction."],"maximum_seconds":120,"evaluations":1,"source_bytes":65,"source_code_backup_sha256":"63bddd54d41f89046ed911cf12ee47eb2110a8fe9116c418bfdb6becea589098","model_changes":false,"threshold_changes":false,"window_changes":false,"new_training":false,"independent_TEST_read":false,"Flash_writes":false,"USB_mutations":false,"audio_capture":false,"default_policy_change":false,"resource_gate_290_still_failed":true,"latency_gate_277_still_failed":true,"false_wake_fixed":false,"full_goal_retained":true,"goal_complete":false,"stop":"One sanitizer batch and timeline replay; preserve failures. No additional group, timing increase, model search or default adoption. A successful host proof needs a later declared C3 normal three-turn regression before using it as a device-response improvement."}


### UX291 内容应答主机结果 — 2026-10-02
{"stage":"UX291","complete":true,"host_sanitizer_checks":7,"saved_preview_count":58,"source_final_grounding_passed":true,"prompt_lead_to_final_ms":[374,341,51],"new_persistent_fields":0,"new_buffers":0,"original_preview_policy_unchanged":true,"existing_opt_in_local_first_only":true,"default_policy_OFF":true,"device_validation_pending":true,"actual_generated_or_played_acknowledgements":0,"actual_latency_improvement_proved":false,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"context_unchanged":true,"context_events":1320,"history_budget":204800,"context_partition":2097152,"USB_released":true,"Flash_writes":0,"acoustic_tests":0,"cloud_calls":0,"new_training":0,"independent_TEST_unread":true,"false_wake_fixed":false,"resource_gate_290_still_failed":true,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"source_archive_sha256":"9b84d34943f6226cdbe9246277f0869d1587de4cf2c59e7ea792a6a8e37d6d4d"}
七项一次sanitizer检查通过，58个已存ASR预览经生产C新旧门控重放；原文和完整最终输入绑定通过。原ASR sentence ID未日志化，重放使用单调替代revision，不作为端点对齐或实际音频/时序验收。只在既有local-first可选策略中启用，默认仍OFF；无新字段/缓冲/任务，当前设备不变。不标记误醒/48KiB/104ms/一秒/真实动态应答通过。完整goal active。


### UX292 ASR优先与话题应答设备整合计划 — 2026-10-02
{"stage":"UX292","version":"0.11.292-topic-cooperate","began":"2026-10-02T07:13:03.743760+00:00","previous_turn":"Progress: normal compact flow captured all3inputs but SDK46840 and candidate0/3 failed;291 implemented source-grounded opt-in memory topics and seven sanitizer checks/58preview C replay. Device original72/off/off,1320history,full goal retained.","change":"Integrate existing ASR-first cooperative TLS with compact24/24/48 and291 local-first memory topic policy. Both are declared integration changes, not a controlled causal A/B. No model/controller/threshold/VAD/context/radio/audio/stack changes.","groups":1,"rounds":3,"maximum_attempts":1,"max_group_seconds":260,"files":{"zh":"artifacts/kws-phase2/curated-zh-bounded1/dataset-zh-zf_xiaoxiao-1-012-bounded1.wav","yue":"artifacts/kws-phase2/curated-yue-bounded1/dataset-yue-zf_xiaoxiao-1-020-bounded1.wav","group":"tools/voice/continuous_rewake.py","device":"tools/voice/device.py","recorder":"tools/record_speaker.py","player":"tools/wave_play.py","prompts":"artifacts/voice-cloud/prompts-01/manifest.json"},"hashes":{"zh":"684c58d069be402f92d77b503f01403fce8397fc38f7beda684ddd3c557f22f6","yue":"ca5353598250f5e680456f9327f9485ac9b34c177b8c3a91d3034244a3f388d0","group":"993d4bc1b9a098d236e24897c132ddf2576f5cff4d2787fbd1e0733af72d442d","device":"6f7692aa56643ba7d7d87046e9c9700d92940208312ec223abcdd92ad16a0ff8","recorder":"19708d5e1597b7bbbaa29db0658c73bbce12e595586be56d6598f634b3b535d9","player":"b1ca4f2c90f9af526956a9a7a7b681f05723b61604c3542ba67bb3e83ad017af","prompts":"2193b839aa869acccaf515961e14553e68b508461024223f0f7350663a205dc3"},"settings":{"mode":"fast","preconnect":"capture","prefetch":"on","reuse":"on","capture_output":"hold"},"fixture":"Known public zh/yue/zh remember, same prompt/volume740 threshold/model/256ms controller/prime. Later wake250ms after done, no ready wait.","host_proof_sha256":"d2b72402e7f6dcab5aec650bb4091ad2aaf0ae102fe60aea599bcf7c1f9dc90b","board_proof_sha256":"7da0c7ea92e32a228424251bc55a75a166d579fb3989d205e655391fb0e89cb4","seed_manifest_sha256":"3695e6ba076809a125f40f2fdaac19233f98718e299081b67f689bcf8451ad13","target_workspace_bytes":17096,"normal_app_budget":1540096,"gates":["291 seven existing source/ownership checks reused; five additional relevant cooperative transport/SDK/local-first integration checks once.","Normal C11 build, exact model/seed/source/pinnedSDK, linked cooperative callbacks, LOCAL_FIRST ON. All probes/trace/heap hooks/resource-only OFF.","One normal zh/yue/zh three-turn group, single attempt each,250ms postdone/no ready wait/export. Require full input/task/rearm,SDK49152/largest24576; keep strict topic and actual acoustic limitations.","Fresh flash_guard4MiB backup/app-only/readback/nonapp install and restore. Preserve new history,2MiB context/204800budget/failed clips; final original72/off/off andUSBreleased."],"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"known_validation_physical_replay":true,"offline_DEV_dataset_reprediction":false,"independent_project_TEST_unread":true,"model_training":false,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"stop":"One group or failed preflight. No repeated/substituted turn, threshold/window/model search, VAD/stack/volume/context reduction, or default adoption. Preserve actual failed input. All old quality/277added104>96ms/290SDK/topic/speed failures remain; integration cannot prove field false-wake repair.","topic_proof_sha256":"c12b065428f18d51f4aeeb50e9d6ebd41476d25b080c025c5db6a7c3a84ee2e3","cooperate_prior":"artifacts/voice-fast/asr-first-ux251/"}


### UX292紧凑运行时正常三轮闭环 — 2026-10-02
{"stage":"UX292","complete":true,"normal_groups":1,"rounds":3,"attempts":3,"rows":[{"round":1,"language":"zh","first_wake":true,"actual_gap_ms":null,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":94080,"candidate_transport_hit":true,"candidate_texts":["嗯，我给这盏灯取，我想想。"],"naming_topic_specific":false,"runtime_health":true},{"round":2,"language":"yue","first_wake":true,"actual_gap_ms":280.9999999590218,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":104320,"candidate_transport_hit":true,"candidate_texts":["嗯，我给这盏灯取，我想想"],"naming_topic_specific":false,"runtime_health":true},{"round":3,"language":"zh","first_wake":true,"actual_gap_ms":281.00000019185245,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":94848,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true}],"application_bytes":1530912,"host_sanitizer_checks":5,"reused_291_sanitizer_checks":7,"target_workspace_bytes":17096,"SDK_cumulative_min_heap":41504,"SDK_heap_gate_passed":false,"observed_minimum_largest_block":20480,"contiguous_gate_passed":false,"continuous_full_dialogues_passed":true,"full_input_and_task_count":3,"first_wakes_hit":3,"all_runtime_health_passed":true,"candidate_transport_hits":2,"naming_topic_specific_count":0,"topic_specific_ack_passed":false,"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"acoustic_source_alignment_passed":true,"one_second_reply_passed":false,"one_second_reply_status":"Not certified by energy candidates alone; retain acoustic report.","original_application_restored":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"final_context":{"ready":true,"mode":"LOCAL","events":1326,"pending":1326,"recent_turns":128,"used":913400,"bank_size":1048576,"generation":18,"tail_recovered":false,"lamport":4513,"cursor":0,"acked":0,"last_local":38662,"history_budget":204800,"prompt_turns":0,"prompt_bytes":0,"trimmed_turns":0,"request_bytes":0,"prompt_locked":false,"partition_bytes":2097152,"archived_record":3224,"next_record":4553},"appended_history_preserved":true,"old_clip_recoverable_from_backup":true,"USB_released":true,"known_validation_physical_replay":true,"independent_project_TEST_unread":true,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"execution_archive_sha256":"981739447a33d8ed7fc34caa07eaa5533ca20d8ffa7fb58279aa14326eb4df2e"}
唯一无诊断组，失败轮不替换/不补组；实际声学报告保留，不能把内部playback/energy候选当有用首音或一秒通过。新增历史保留，恢复原应用而非整片退回测试前，误触/277延迟/未过业务和动态话题目标保持。全goal active/阻塞0。


### UX293 完整输入核对预测话题 — 2026-10-02T07:45:53.980863+00:00
{"stage":"UX293","began":"2026-10-02T07:45:53.980863+00:00","before_hashes":{"plugins/speech/candidate.c":"9ef02d0cc95c13f35cf80307d90b9826fd8c6d88c1d57a139b740e578c130e0b","plugins/speech/candidate.h":"2220658c1be0303304e47f33dde51331920a2fbd4811ed4c76c6e98fcb8ee8d5","platform/espidf/voice_candidate.c":"9f3b588e729bfd61bbb8d4b381c1fc85804e3acbde935a11f9ffeccd2e68feb0","platform/espidf/runtime.c":"ecd0661d785135168c460e60515d990fc1e1c3f0fd78a46e36a9e71a3f64e8a2","host_tests/test_candidate_local_first.c":"d5082c6ba9e53aa7f7ad65e8bdbf0c7b0e143cf016e1dc8caa587d8cd3d4b265","host_tests/test_voice_local_first.c":"ab3c0d1e04d8b9df684dde02952406b2c4761919ee2483b87497170a9805039b","host_tests/test_voice_candidate.c":"21ab0f70f8e97e5a773e822b4cc6272b440bc46bae1f9884d661e2d8a4ef0c2d","host_tests/CMakeLists.txt":"a4d9d09b79f5518c1ae0b3f4fee0009661619f4ca8f9fe0d4b2a1a8fcaf31493"},"change":"Opt-in LOCAL_FIRST THINK may cache a fluent word completion silently. Playback and hit require full final input to retain nominated source/intent/language and ground all topic content. Keep original API and default policy strict; one borrowed immutable final-input pointer published before play_permitted, valid until joined workspace handoff. No early actions or factual claims.","tests":["Correct/wrong word completion, Mandarin/Cantonese, source revision, cancellation, repair, UTF8/length, unchanged original strict API.","Completed/active/deferred worker: no prefinal playback; rejected topic frees all cache/transport, cancel/error joins, no tool/history effects.","One ASan/UBSan batch including default compatibility and LOCAL_FIRST with probe ON and normal probe OFF; one production C replay of saved UX292 text."],"maximum_host_seconds":180,"evaluations":1,"Flash_writes":false,"USB_mutations":false,"audio_capture":false,"cloud_calls":false,"model_changes":false,"threshold_changes":false,"window_changes":false,"new_training":false,"independent_TEST_read":false,"default_policy_change":false,"wait_deadline_change":false,"context_change":false,"source_bytes":65,"candidate_cache_bytes":24576,"heap_gate_bytes":49152,"contiguous_gate_bytes":24576,"false_wake_fixed":false,"full_goal_retained":true,"resource_gate_292_still_failed":true,"latency_gate_277_still_failed":true,"goal_complete":false,"stop":"One host batch and saved-text replay; preserve failures and stop on production correctness failure. No board group, retries, model search, threshold/window/VAD/stack/context changes or default adoption. Host proof is not a field false-wake/latency/resource result.","source_backup_sha256":"949207cf795743ac9d07bfb11d15dfbdf4081c51b9c3bdaa30c0bd50ea559bfd"}

UX293 主机收尾：{"stage":"UX293","ended":"2026-10-02T08:07:16.119807+00:00","complete":true,"change":"Final-grounded neutral predictions, borrowed final input published before playback permission, completed/active/deferred worker guards and fluent shortest-topic prompt.","sanitizer_unique_checks_passed":9,"initial_failed_fixtures":3,"first_repair_failed_fixtures":2,"original_failures_preserved":true,"production_source_unchanged_during_fixture_repairs":true,"original_default_prompt_same":true,"original_strict_reply_API_unchanged":true,"added_gate_fields":0,"new_resident_buffer_bytes":0,"new_tasks":0,"borrowed_pointer_opt_in":true,"target_pointer_bytes":4,"target_struct_padding_unmeasured":true,"saved_text_replay_results":[{"round":1,"nominated":"待续：我给这盏灯取","actual_reply":"嗯，我给这盏灯取，我想想。","original_strict_accept":true,"final_grounded_accept":true,"wrong_prediction_rejected":true,"new_audio_generated":false},{"round":2,"nominated":"待续：我给这盏灯取","actual_reply":"嗯，我给这盏灯取，我想想","original_strict_accept":true,"final_grounded_accept":true,"wrong_prediction_rejected":true,"new_audio_generated":false},{"round":3,"nominated":"待续：我给这盏灯取","actual_reply":"嗯，给这盏灯取名，我想想。","original_strict_accept":false,"final_grounded_accept":true,"wrong_prediction_rejected":true,"new_audio_generated":false}],"source_hashes":{"plugins/speech/candidate.c":"7d974ac29c0110d5a873abfb7f3a8b3e560ca1050ba2343bf38d8120b23fc0fb","plugins/speech/candidate.h":"c073ef3d5e22ae8fcda42bdeb987dcc842ef5331153247a3916857fed38afb2a","platform/espidf/voice_candidate.c":"fcafa1fe1786c7650ea1554bdb0348a0a1f83c05062bc19a9815c07ba560d1a7","platform/espidf/runtime.c":"ecd0661d785135168c460e60515d990fc1e1c3f0fd78a46e36a9e71a3f64e8a2","host_tests/test_candidate_local_first.c":"70cc7f0caf483c627776cc6b5b99ae9c27a77c069f1782d6700bc4ffe89d6bd0","host_tests/test_voice_local_first.c":"bff2e71c04be0ac5670820a92517f79695a434412ae91a3e3a668069c721700c","host_tests/test_voice_candidate.c":"8bbfec467c5453221375d91ca247fef855b25458acc15f84b843df695f98fb70","host_tests/CMakeLists.txt":"e6ee606cee5c7c00fbaa164bc35568b6a9f1e2ee2e183dce3bea3e892e9b23b0"},"after_source_sha256":"b9656e232cab69b677f01701a2c625b0aede128f8591f9fd29a4f8652f2234f5","source_lifetime":"ASR returns before voice_run. Published final is work_buffer.input, not captured arena. provision/submit/new voice honor busy/work_lock; deferred resume and producer join occur before worker releases work_lock and agent_end_turn. Pthread tests poison/free final input after join before playback join, with ASan/UBSan.","Flash_writes":0,"cloud_calls":0,"new_audio_capture":false,"USB_released":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"context_unchanged":true,"context_events":1326,"context_bytes":2097152,"history_budget":204800,"quality_adopted":false,"false_wake_fixed":false,"one_second_certified":false,"resource_gate_292_still_failed":true,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0}


### UX294 历史重放瞬时内存 — 2026-10-02T08:25:03.474534+00:00
{"stage":"UX294","began":"2026-10-02T08:25:03.474534+00:00","before_hashes":{"plugins/context/context.c":"e11243b9d3c9e844a6b8f463da74a6eb4b6cb9b3d65dc623c1386399ff04e550","plugins/context/context.h":"6d80ed5899e5c6b7aef1d0d09556652da8faf29e282444c16c608fd0c8613fcd","host_tests/CMakeLists.txt":"e6ee606cee5c7c00fbaa164bc35568b6a9f1e2ee2e183dce3bea3e892e9b23b0","host_tests/test_context.c":"4fc942705085076570cf6902f99ca82df5f23ea96ec13c867dafade7dc516afa","core/json.c":"57105c4a0b455c454194e4f7d524e7bbf56bc35823ee1e5dca3f5fffa83d12ab","core/json.h":"d3b68ef1b6b3a302f5daf7c4356b3041bc7390ae8ea9f6aed5e3a3f2951d8d4c","plugins/context/wal.c":"bc8c1dfa0f0e5ef2332805f00e6fc4913134a4a1a0573d29b21ee492dcd4bc2a","plugins/context/wal.h":"7f2e2a9a285e5da3b4f5df61ee72edb67075d83eebef807cfcb0abb8b820c340"},"source":"C:\\Users\\PC\\Documents\\ChatGPT\\cogd\\backups\\kws-voice-flow-20261002-071903\\flash-after.bin","source_sha256":"29fab897075bce4b7ef218e5383effa28e31a4c81f759611f9166551959652e5","source_context_generation":18,"source_records":128,"source_records_sha256":"a2a338e3f0284db90fc6ae5625fd0bea02c6817b5e4eec026710b6e1b047c519","change":"First measure allocations of production JSON parsing/serialization on actual saved recent128turns. If a meaningful transient allocation exists, cache only canonical raw message spans validated against the already-parsed tree at ingestion/rebuild, protected by record CRC and bounds. Replay still reads and validates WAL CRC; noncanonical/numeric records use unchanged cJSON fallback. No new general JSON parser, record schema or reduced budgets.","maximum_seconds":300,"host_census":1,"host_batch":1,"minimum_relevant_raw_peak_bytes":4096,"cache_metadata_max_bytes":1024,"tests":"Differential exact canonical bytes, existing context/streaming selection/budget/cancel/cache/compaction/CRC checks; new zero-allocation fast path, noncanonical and numeric fallback, shifted/reopened index and altered payload with fresh CRC.","Flash_writes":false,"USB_mutations":false,"cloud_calls":false,"new_audio_capture":false,"model_changes":false,"threshold_changes":false,"window_changes":false,"stack_changes":false,"context_capacity_changes":false,"independent_TEST_read":false,"new_training":false,"false_wake_fixed":false,"goal_complete":false,"full_goal_retained":true,"stop":"If measured allocations are too small or correctness fails, retain evidence and do not claim resource improvement/adoption. One saved128record census and one relevant host batch, no cloud/device group, parameter search or repeated unchanged experiments. C3 normal resource/voice behavior requires a later declared guarded three-turn integration."}

UX294 有限诊断收尾：{"stage":"UX294","ended":"2026-10-02T08:33:20.328490+00:00","complete":true,"census":{"rows":128,"host_raw_parse_peak_max":3516,"maximum_record":{"seq":4484,"payload_bytes":1341,"canonical_message_bytes":1024,"host_peak_raw_alloc_bytes":3516,"allocations":89,"nodes":36,"host_cJSON_node_bytes":64},"allocations_are_host_raw_requested_bytes":true,"C3_allocator_overhead_and_peak_unmeasured":true,"C3_heap_benefit_proven":false,"eligible_for_declared_cache_check":false},"canonical_cache_implementation_stopped":true,"production_source_unchanged":true,"reason":"Measured largest host raw parse allocation3516B, below declared4096B relevance gate. Even its removal cannot explain/guarantee the required7648B SDK improvement. No new metadata table/JSON scanner is justified by this census.","next_mechanism":"The deferred request already streams its body but retains a full24577B record/request scratch while candidate is live. Investigate preparation-only scratch sized from validated recent record lengths, preserving full capacities before sinks/tools and preserving large-record fallback and200KiB history/2MiB partition.","Flash_writes":0,"cloud_calls":0,"USB_released":true,"context_events":1326,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"false_wake_fixed":false,"resource_gate_292_still_failed":true,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0}


### UX295 按数据大小准备请求工作区 — 2026-10-02T08:43:14.407520+00:00
{"stage":"UX295","began":"2026-10-02T08:43:14.407520+00:00","before_hashes":{"plugins/llm/engine.c":"59c24185d44d42530d23e2340fa167d3b20810ad784af0116705b0428a414157","plugins/llm/engine.h":"f8eb3b56808db4e7ff219b9cb09789a45f710a264d218fe0f21d4bffa60796af","plugins/context/context.c":"e11243b9d3c9e844a6b8f463da74a6eb4b6cb9b3d65dc623c1386399ff04e550","plugins/context/context.h":"6d80ed5899e5c6b7aef1d0d09556652da8faf29e282444c16c608fd0c8613fcd","platform/espidf/engine_memory.c":"7a671d8466c1ddea753ab1db5a373fbc2c846c37baa242ff9215e4cac4c185b8","platform/espidf/engine_memory.h":"6e311a5febb7ff8ff5aed55c0d2312dd402c49968ce7dd457d224f94a6ccd0d5","platform/espidf/runtime.c":"ecd0661d785135168c460e60515d990fc1e1c3f0fd78a46e36a9e71a3f64e8a2","CMakeLists.txt":"c2d158b35fb30fd1a67149d4a9269e14895f26910ae8488dd3deccab5cd260cb","main/CMakeLists.txt":"9f1713bcacf17595fba69084578cd29216d2385942358312b4dd568424e391a4","host_tests/CMakeLists.txt":"e6ee606cee5c7c00fbaa164bc35568b6a9f1e2ee2e183dce3bea3e892e9b23b0","host_tests/test_engine_memory.c":"a8c0eb5762fb4ec1cfb269c4e772562e50114d62fc935478f0a9ad6b7a587440"},"change":"Opt-in preparation-only scratch8192/16384/full24577 selected from safe quoted user envelope bound, each recent record raw length plus exact canonical array length, and free WAL space.256B canonical-length index measured from already validated tree at ingest/rebuild; no raw-span cache, parser change or skipped CRC/replay. Before user append preflight system/facts/summary; oversize joins full path without duplicate persistence/request. After body, join candidate then retire small scratch and bind original full buffer/state before SSE/TTS/tools/WAL use. Original full prepare and same-buffer contract stay strict; only explicitly smaller staging permits replacement.","defaults_enabled":false,"metadata_max_bytes":256,"minimum_scratch_bytes":8192,"original_full_scratch_bytes":24577,"request_limit":24576,"HTTP_stream_limit":360448,"context_partition_bytes":2097152,"history_budget":204800,"context_capacity_changes":false,"tests":"Original buffer/parts/workspace compatibility; small/medium/full sizing from actual records and canonical expansion; all byte-exact deferred request paths, long history, large/escaped inputs, snapshots/prompt fallback, locks/cancel/errors, buffer replacement only after joined producer, fail ownership, no retry/double append, full SSE/TTS/tool capacities, shifted/reopened/compacted metadata. One relevant sanitizer batch.","maximum_host_seconds":300,"host_batch":1,"Flash_writes":false,"USB_mutations":false,"new_audio_capture":false,"cloud_calls":false,"model_changes":false,"threshold_changes":false,"window_changes":false,"VAD_changes":false,"stack_changes":false,"new_training":false,"independent_TEST_read":false,"false_wake_fixed":false,"full_goal_retained":true,"goal_complete":false,"stop":"No device build/Flash/group in this host stage; correctness or insufficient bounds stops adoption. Preserve failures and only rerun affected checks after a fix. A later separately declared normal C3 three-turn integration must prove actual heap/behavior; no guaranteed runtime saving inferred from raw byte reduction."}

UX295 主机准备缓冲闭环：{"stage":"UX295","ended":"2026-10-02T09:09:38.571556+00:00","complete":true,"opt_in_sanitizer_checks_passed":14,"original_sanitizer_checks_passed":6,"initial_failures":0,"exact_wire_and_WAL":true,"original_same_full_buffer_contract_unchanged":true,"explicit_small_buffer_replaced_only_after_join":true,"full_capacity_before_SSE_TTS_tools":true,"large_system_snapshots_records_and_compaction_fall_back":true,"user_event_not_duplicated":true,"two_KiB_worst_escaped_input_and_IDs_passed":true,"canonical_length_shift_reopen_compact_and_CRC_passed":true,"saved_actual_recent_records":128,"saved_records_prepare_bytes":8192,"original_prepare_bytes":24577,"raw_preparation_reduction_bytes":16385,"new_length_index_bytes":256,"new_engine_capacity_field_target_unmeasured":true,"actual_C3_heap_benefit_proven":false,"default_OFF":true,"source_hashes":{"plugins/llm/engine.c":"f9296b4f40b3e1915f6fd6d735dd8f0f9173b14bc7de155293432a490fad4a82","plugins/llm/engine.h":"eab7611648aeb17918beb4c542f8b33f98f3c37ef8fd4f7c989220652177edda","plugins/context/context.c":"5dc62f50cb8e42a8a8569235f6bea4a02e17c376bc9b5394ddf83b74676d401a","plugins/context/context.h":"977dbe86e776e5ff0b6d88ec6d2174724a3348bf01884ba3b033bdd65d4b5bd5","platform/espidf/engine_memory.c":"8695698865bb5c344b6eda93c3f1df43b53b3758c5aa3b7ddcc7543a7615cf3f","platform/espidf/engine_memory.h":"199ac4693b20a21ba6f4c22ece452785d7cf7677b06e622c12c22f77768f5c51","platform/espidf/runtime.c":"0b65e5318aba74bbaf7fe4bbe2dea64fcc13522d67d7996b9e42ef52f78544f9","main/CMakeLists.txt":"64eaeebdacdf3f19333d8af5833b7bf6eef278067c4e8f3560a2f8af428ca22e","host_tests/CMakeLists.txt":"b74a3b916df7faf0827492eb1e938b37dd88dfd494e28afbaf8a331a27321d6d","host_tests/test_engine_deferred.c":"1646821ba724d5c32978d7e6005dc0c723b532944c9993fca82d84e68f9f606f","host_tests/test_engine_memory.c":"2c246d5a06019687b55de2f100cb3728b30281ccf8a6c924e76f24d779468b71","host_tests/test_engine_workspace.c":"d6f5514860d5fc253b80618bdbf10cce61dd8013ddd0a2e8b7a2e8a45632a4c3","host_tests/test_context_preparation.c":"6bd5b64726bdf7ad61d6602e65c1cee420dc87264fc8c8f19f78004d48fd846d"},"source_archive_sha256":"3739146e7b171a2cc5cef86b6dc8fe80d710da1a7ec4ac2c128726ceaf3b111f","host_seconds":17.938000000081956,"Flash_writes":0,"cloud_calls":0,"new_audio_capture":false,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"USB_released":true,"context_unchanged":true,"context_events":1326,"context_bytes":2097152,"history_budget":204800,"false_wake_fixed":false,"resource_gate_292_still_failed":true,"latency_gate_277_still_failed":true,"quality_adopted":false,"one_second_certified":false,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0}


### UX296 请求准备容量正常设备整合计划
{"stage":"UX296","version":"0.11.296-request-sized","began":"2026-10-02T09:14:20.445173+00:00","previous_turn":"293 final-grounded topic receipts and295 request-only scratch completed host proof;20 sanitizer checks, actual128recent uses8192.294 raw-span cache stopped below relevance threshold. Device unchanged original72/off/off,1326history.","change":"Integrate293 final-grounded topic receipts and295 preparation-only8/16/full scratch with unchanged292 normal24/24/48 two-vote ASR-first flow. One event reports actual preparation bytes; all diagnostic hooks/probes remainOFF. This is not a controlled causal A/B or field false-wake proof.","groups":1,"rounds":3,"maximum_attempts":1,"max_group_seconds":260,"files":{"zh":"artifacts/kws-phase2/curated-zh-bounded1/dataset-zh-zf_xiaoxiao-1-012-bounded1.wav","yue":"artifacts/kws-phase2/curated-yue-bounded1/dataset-yue-zf_xiaoxiao-1-020-bounded1.wav","group":"tools/voice/continuous_rewake.py","device":"tools/voice/device.py","recorder":"tools/record_speaker.py","player":"tools/wave_play.py","prompts":"artifacts/voice-cloud/prompts-01/manifest.json"},"hashes":{"zh":"684c58d069be402f92d77b503f01403fce8397fc38f7beda684ddd3c557f22f6","yue":"ca5353598250f5e680456f9327f9485ac9b34c177b8c3a91d3034244a3f388d0","group":"993d4bc1b9a098d236e24897c132ddf2576f5cff4d2787fbd1e0733af72d442d","device":"6f7692aa56643ba7d7d87046e9c9700d92940208312ec223abcdd92ad16a0ff8","recorder":"19708d5e1597b7bbbaa29db0658c73bbce12e595586be56d6598f634b3b535d9","player":"b1ca4f2c90f9af526956a9a7a7b681f05723b61604c3542ba67bb3e83ad017af","prompts":"2193b839aa869acccaf515961e14553e68b508461024223f0f7350663a205dc3"},"settings":{"mode":"fast","preconnect":"capture","prefetch":"on","reuse":"on","capture_output":"hold"},"fixture":"Known public zh/yue/zh remember, same prompt/volume740 threshold/model/256ms controller/prime. Later wake250ms after done, no ready wait.","host_proof_sha256":"d2b72402e7f6dcab5aec650bb4091ad2aaf0ae102fe60aea599bcf7c1f9dc90b","board_proof_sha256":"7da0c7ea92e32a228424251bc55a75a166d579fb3989d205e655391fb0e89cb4","seed_manifest_sha256":"3695e6ba076809a125f40f2fdaac19233f98718e299081b67f689bcf8451ad13","target_workspace_bytes":17096,"normal_app_budget":1540096,"gates":["Reuse293 nine candidate and29520 memory/context/default sanitizer proofs; once run five affected ownership/TLS/HTTP normal integration checks with compact ON.","Normal C11 IDF build; models, seed, controller, threshold740,256ms, VAD, radio, stacks, partitions and context/history unchanged. app<=1540096. Linked TLS callback and SDK source verified; every diagnostic/probe/resource-only and heap hookOFF.","One normal public zh/yue/zh continuous group, one attempt each,250ms postdone/no wait-ready, no export between rounds. Require full input/task/rearm, realSDK49152/largest24576; topic-specific acknowledgement text and acoustic evidence kept distinct.","Fresh flash_guard4MiB verified backup/app-only/readback/nonapp for both install and original72 restore. Keep new history,2MiB partition/204800budget/failed clips. Final72/off/off/USB released."],"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"known_validation_physical_replay":true,"offline_DEV_dataset_reprediction":false,"independent_project_TEST_unread":true,"model_training":false,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"stop":"One group or failed preflight. No repeated/replaced round, quality/threshold/window/model search, VAD/stack/volume/history reduction or default adoption. Keep failed input,277104>96,normal heap/topic/acoustic failures. Unknown acoustic first-useful sound cannot be called one-second success.","topic_proof_sha256":"c12b065428f18d51f4aeeb50e9d6ebd41476d25b080c025c5db6a7c3a84ee2e3","cooperate_prior":"artifacts/voice-fast/asr-first-ux251/","host_293_proof_sha256":"a52ba70dfacd99d92be2beebe919d49226a5ea3647f8a7173ebf0cb037b00e25","host_295_proof_sha256":"875427fcf43a9fa0fb6769be0090cb8209dd8b98e8dda629cf0fa2ffb4d8cd7e","defaults_enabled":false,"context_capacity_changes":false,"model_changes":false,"threshold_changes":false,"window_changes":false,"VAD_changes":false,"stack_changes":false,"new_independent_TEST_read":false,"before_source_sha256":"9968f2163a1aa0d15798df874763425a393e2bb3d7bde8557c307129ce3e22ca"}


### UX296紧凑运行时正常三轮闭环 — 2026-10-02
{"stage":"UX296","complete":true,"normal_groups":1,"rounds":3,"attempts":3,"rows":[{"round":1,"language":"zh","first_wake":true,"actual_gap_ms":null,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":79744,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true},{"round":2,"language":"yue","first_wake":true,"actual_gap_ms":281.99999989010394,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":93568,"candidate_transport_hit":true,"candidate_texts":["嗯，这盏灯取，我想想。"],"naming_topic_specific":false,"runtime_health":true},{"round":3,"language":"zh","first_wake":true,"actual_gap_ms":281.00000019185245,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":90752,"candidate_transport_hit":true,"candidate_texts":["嗯，给这盏灯取，我想想。"],"naming_topic_specific":false,"runtime_health":true}],"application_bytes":1532304,"host_sanitizer_checks":5,"reused_293_sanitizer_checks":9,"reused_295_sanitizer_checks":20,"target_workspace_bytes":17096,"SDK_cumulative_min_heap":46444,"SDK_heap_gate_passed":false,"observed_minimum_largest_block":32768,"contiguous_gate_passed":true,"continuous_full_dialogues_passed":true,"full_input_and_task_count":3,"first_wakes_hit":3,"all_runtime_health_passed":true,"candidate_transport_hits":2,"naming_topic_specific_count":0,"topic_specific_ack_passed":false,"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"acoustic_source_alignment_passed":true,"one_second_reply_passed":false,"one_second_reply_status":"Not certified by energy candidates alone; retain acoustic report.","original_application_restored":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"final_context":{"ready":true,"mode":"LOCAL","events":1332,"pending":1332,"recent_turns":128,"used":916148,"bank_size":1048576,"generation":18,"tail_recovered":false,"lamport":4519,"cursor":0,"acked":0,"last_local":38790,"history_budget":204800,"prompt_turns":0,"prompt_bytes":0,"trimmed_turns":0,"request_bytes":0,"prompt_locked":false,"partition_bytes":2097152,"archived_record":3224,"next_record":4559},"appended_history_preserved":true,"old_clip_recoverable_from_backup":true,"USB_released":true,"known_validation_physical_replay":true,"independent_project_TEST_unread":true,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"execution_archive_sha256":"fbe70cc7ed56171b0cd2c6cf443e2acd2cd27f5d2e3c919febeb752579dce1a2"}
唯一无诊断组，失败轮不替换/不补组；实际声学报告保留，不能把内部playback/energy候选当有用首音或一秒通过。新增历史保留，恢复原应用而非整片退回测试前，误触/277延迟/未过业务和动态话题目标保持。全goal active/阻塞0。

UX296补充时钟/容量与失败区间：{"stage":"UX296","closed":true,"rows":[{"round":1,"request_lead_to_final_ms":37,"first_PCM_relative_final_ms":449,"actual_prepare_bytes":[],"candidate_result":"cancelled","candidate_texts":["嗯，这盏灯取名，我想想。"],"final_input_complete":true,"task_complete":true},{"round":2,"request_lead_to_final_ms":708,"first_PCM_relative_final_ms":-153,"actual_prepare_bytes":[8192],"candidate_result":"ok","candidate_texts":["嗯，这盏灯取，我想想。"],"final_input_complete":true,"task_complete":true},{"round":3,"request_lead_to_final_ms":755,"first_PCM_relative_final_ms":-233,"actual_prepare_bytes":[8192],"candidate_result":"ok","candidate_texts":["嗯，给这盏灯取，我想想。"],"final_input_complete":true,"task_complete":true}],"SDK_min_bytes":46444,"SDK_min_difference_from_292":4940,"comparison_not_controlled_causal_AB":true,"new_low_interval":{"begin":{"stage":"done","time_ms":25871,"heap_time_ms":25871,"free_heap":94112,"min_heap":49204,"largest_block":59392,"observed":1258626.796},"end":{"stage":"capture_begin","time_ms":27829,"heap_time_ms":27829,"free_heap":106744,"min_heap":46444,"largest_block":32768,"observed":1258628.687}},"interval_is_not_exact_allocation_attribution":true,"PCM_readiness_is_not_acoustic_speech_onset":true,"first_candidate_rejection_was_cancelled_deadline":true,"do_not_increase_deadline_to_hide_failure":true,"app_bytes":1532304,"budget":1540096,"margin":7792,"preserved_history_events":1332,"fresh_closure_version":"0.11.72-summary","USB_released":true,"false_wake_fixed":false,"resource_gate_passed":false,"topic_ack_gate_passed":false,"full_goal_retained":true,"goal_complete":false}


### UX297 采集工作区反向交接计划
{"stage":"UX297","began":"2026-10-02T09:44:14.308791+00:00","change":"In the existing default-OFF compact preparation route, track actual heap-owned state allocation capacity in the platform owner. Transfer a split adopted capture arena back to capture after joined sinks/context borrowers, freeing only the separate request buffer. Ordinary/unknown/contiguous allocations retain original release/calloc fallback. Preserve original buffer/parts/scratch logical contracts. Reused capture bytes are reset like calloc before use.","evidence":"296 SDK49204 at first done then46444 at next capture_begin, free94112/106744; interval only, not exact allocation attribution. Capture was adopted into engine but current next-turn path frees it and allocates again.","before_hashes":{"plugins/llm/engine.c":"f9296b4f40b3e1915f6fd6d735dd8f0f9173b14bc7de155293432a490fad4a82","plugins/llm/engine.h":"eab7611648aeb17918beb4c542f8b33f98f3c37ef8fd4f7c989220652177edda","platform/espidf/engine_memory.c":"8695698865bb5c344b6eda93c3f1df43b53b3758c5aa3b7ddcc7543a7615cf3f","platform/espidf/engine_memory.h":"199ac4693b20a21ba6f4c22ece452785d7cf7677b06e622c12c22f77768f5c51","platform/espidf/runtime.c":"75f90535caf3ef87154cc6920f381b658a163a19cddda78e7accaf07c398a480","host_tests/test_engine_memory.c":"2c246d5a06019687b55de2f100cb3728b30281ccf8a6c924e76f24d779468b71"},"host_batch":1,"maximum_host_seconds":180,"tests":"New actual-capacity/unknown/static/contiguous/insufficient/active-sink/locked-history ownership checks and repeated reciprocal transfer with no state realloc, failed adoption ownership/canaries, original buffer/state APIs; existing default checks reused and affected ON/OFF checks once.","no_new_feature_flag":true,"default_OFF":true,"Flash_writes":false,"USB_mutations":false,"cloud_calls":false,"new_audio_capture":false,"model_changes":false,"threshold_changes":false,"window_changes":false,"VAD_changes":false,"stack_changes":false,"context_capacity_changes":false,"new_training":false,"independent_TEST_read":false,"heap_gate_bytes":49152,"largest_gate_bytes":24576,"normal_app_budget":1540096,"false_wake_fixed":false,"full_goal_retained":true,"goal_complete":false,"stop":"One host batch, preserve errors. No device build/Flash/group, quality search or default adoption. Allocation churn reduction is not a proven C3 heap benefit; later separately declared normal three-turn integration still required."}

UX297采集工作区交接主机闭环：{"stage":"UX297","ended":"2026-10-02T09:49:40.403688+00:00","complete":true,"opt_in_sanitizer_checks_passed":6,"original_sanitizer_checks_passed":3,"initial_failures":0,"reciprocal_transfers":32,"state_allocations_during_transfers":0,"total_original_arenas":1,"total_request_allocations":32,"actual_capacity_tag_required":true,"unknown_static_contiguous_insufficient_and_active_owners_rejected":true,"bytes_unchanged_by_transfer":true,"caller_resets_full_logical_capture_before_use":true,"original_split_capacity_contract_unchanged":true,"original_same_full_buffer_and_small_deferred_contracts_passed":true,"no_new_feature_flag":true,"default_OFF":true,"actual_C3_benefit_proven":false,"new_owner_capacity_field_target_struct_padding_unmeasured":true,"source_hashes":{"plugins/llm/engine.c":"f34b7847098de55d12ad6581306870945ffb09d778c78bd2364f1b5d0e337cc7","plugins/llm/engine.h":"ded9b192b345a59ff49802925e4063f1fb78309d85794eeee83aa9702b6d55cc","platform/espidf/engine_memory.c":"8f02342724da8c7eef9a0160f58ed8c2384d89aa0bfa7755c22b987ce12becba","platform/espidf/engine_memory.h":"5dd238dc8641b920cabf70a02b09f9810f22eba36acd1f5bf63c13a05e0de22b","platform/espidf/runtime.c":"690eaca4133ebf5a20fa0eef33c742f6b2fc5b357d62f622dd8bc075436a3a2c","host_tests/test_engine_memory.c":"78748fda8f8c2df03a86ee163f3448f5d183b6976c3ba81c60d1bc5b0e7e8c2b"},"source_archive_sha256":"146b86594e766445104295bb53325b0d83e148cf80a436bdaa45f375d0a14e4f","host_seconds":16.281000000191852,"Flash_writes":0,"cloud_calls":0,"new_audio_capture":false,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"USB_released":true,"context_unchanged":true,"context_events":1332,"context_bytes":2097152,"history_budget":204800,"false_wake_fixed":false,"resource_gate_296_still_failed":true,"topic_gate_296_still_failed":true,"latency_gate_277_still_failed":true,"quality_adopted":false,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0}


### UX298 采集工作区复用正常设备计划
{"stage":"UX298","version":"0.11.298-capture-reuse","began":"2026-10-02T09:52:24.148455+00:00","previous_turn":"296 normal three turns3/3 complete butSDK46444/topic0/3 failed.297 implemented reciprocal known-heap capture transfer,32 host cycles without state allocation; sixON/threeOFF sanitizer checks pass. Original72/off/off,1332events,USBreleased.","change":"Integrate297 reciprocal capture arena transfer with otherwise unchanged296 normal flow. Defaults remainOFF. Reuse same known heap allocation and zero before admitting the next capture; unknown/whole/small allocation uses original fallback. No model/threshold/window/VAD/stack/history/prompt/deadline/timing-policy change.","groups":1,"rounds":3,"maximum_attempts":1,"max_group_seconds":260,"files":{"zh":"artifacts/kws-phase2/curated-zh-bounded1/dataset-zh-zf_xiaoxiao-1-012-bounded1.wav","yue":"artifacts/kws-phase2/curated-yue-bounded1/dataset-yue-zf_xiaoxiao-1-020-bounded1.wav","group":"tools/voice/continuous_rewake.py","device":"tools/voice/device.py","recorder":"tools/record_speaker.py","player":"tools/wave_play.py","prompts":"artifacts/voice-cloud/prompts-01/manifest.json"},"hashes":{"zh":"684c58d069be402f92d77b503f01403fce8397fc38f7beda684ddd3c557f22f6","yue":"ca5353598250f5e680456f9327f9485ac9b34c177b8c3a91d3034244a3f388d0","group":"993d4bc1b9a098d236e24897c132ddf2576f5cff4d2787fbd1e0733af72d442d","device":"6f7692aa56643ba7d7d87046e9c9700d92940208312ec223abcdd92ad16a0ff8","recorder":"19708d5e1597b7bbbaa29db0658c73bbce12e595586be56d6598f634b3b535d9","player":"b1ca4f2c90f9af526956a9a7a7b681f05723b61604c3542ba67bb3e83ad017af","prompts":"2193b839aa869acccaf515961e14553e68b508461024223f0f7350663a205dc3"},"settings":{"mode":"fast","preconnect":"capture","prefetch":"on","reuse":"on","capture_output":"hold"},"fixture":"Known public zh/yue/zh remember, same prompt/volume740 threshold/model/256ms controller/prime. Later wake250ms after done, no ready wait.","host_proof_sha256":"d2b72402e7f6dcab5aec650bb4091ad2aaf0ae102fe60aea599bcf7c1f9dc90b","board_proof_sha256":"7da0c7ea92e32a228424251bc55a75a166d579fb3989d205e655391fb0e89cb4","seed_manifest_sha256":"3695e6ba076809a125f40f2fdaac19233f98718e299081b67f689bcf8451ad13","target_workspace_bytes":17096,"normal_app_budget":1540096,"gates":["Reuse297 sixON/threeOFF,29520,293nine and296five checks; do not repeat unchanged passing batches.","Normal C11 build/app<=1540096, exact frozen models/controller/typedseed/SDK/partition; all probes/heap hooks/resource-onlyOFF. Actual preparation bytes and capture transfer event retained.","One normal public zh/yue/zh3-round group, one wake attempt each,250ms postdone/no wait-ready/export. Require3first/full/task,SDK49152/largest24576,unchanged strict topic and acoustic gates.","Fresh flash_guard4MiB backup verify/app-only/readback/nonapp install and original72 restore, preserving new history andfailed clips,2MiB/204800. Final72/off/off/USB released."],"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"known_validation_physical_replay":true,"offline_DEV_dataset_reprediction":false,"independent_project_TEST_unread":true,"model_training":false,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"stop":"One group or failed preflight. No replacing/adding failed rounds, model or controller search, threshold/window/VAD/prompt/deadline/stack/context/volume change, new training/independentTEST or adoption. Keep actual failures and all277104>96,296heap/topic/one-second/field false-wake limits.","topic_proof_sha256":"c12b065428f18d51f4aeeb50e9d6ebd41476d25b080c025c5db6a7c3a84ee2e3","cooperate_prior":"artifacts/voice-fast/asr-first-ux251/","host_293_proof_sha256":"a52ba70dfacd99d92be2beebe919d49226a5ea3647f8a7173ebf0cb037b00e25","host_295_proof_sha256":"875427fcf43a9fa0fb6769be0090cb8209dd8b98e8dda629cf0fa2ffb4d8cd7e","defaults_enabled":false,"context_capacity_changes":false,"model_changes":false,"threshold_changes":false,"window_changes":false,"VAD_changes":false,"stack_changes":false,"new_independent_TEST_read":false,"before_source_sha256":"146b86594e766445104295bb53325b0d83e148cf80a436bdaa45f375d0a14e4f","host_297_proof_sha256":"20d86b56347e2145bebbf96141fd27f3da6f4901be35fcad4964d882522447b7"}


### UX298紧凑运行时正常三轮闭环 — 2026-10-02
{"stage":"UX298","complete":true,"normal_groups":1,"rounds":3,"attempts":3,"rows":[{"round":1,"language":"zh","first_wake":true,"actual_gap_ms":null,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":69760,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true},{"round":2,"language":"yue","first_wake":true,"actual_gap_ms":265.99999982863665,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":79232,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true},{"round":3,"language":"zh","first_wake":true,"actual_gap_ms":265.00000013038516,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":84352,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true}],"application_bytes":1532496,"reused_297_sanitizer_checks":9,"reused_293_sanitizer_checks":9,"reused_295_sanitizer_checks":20,"reused_296_sanitizer_checks":5,"target_workspace_bytes":17096,"SDK_cumulative_min_heap":46444,"SDK_heap_gate_passed":false,"observed_minimum_largest_block":32768,"contiguous_gate_passed":true,"continuous_full_dialogues_passed":true,"full_input_and_task_count":3,"first_wakes_hit":3,"all_runtime_health_passed":true,"candidate_transport_hits":0,"naming_topic_specific_count":0,"topic_specific_ack_passed":false,"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"acoustic_source_alignment_passed":true,"one_second_reply_passed":false,"one_second_reply_status":"Not certified by energy candidates alone; retain acoustic report.","original_application_restored":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"final_context":{"ready":true,"mode":"LOCAL","events":1338,"pending":1338,"recent_turns":128,"used":918896,"bank_size":1048576,"generation":18,"tail_recovered":false,"lamport":4525,"cursor":0,"acked":0,"last_local":38918,"history_budget":204800,"prompt_turns":0,"prompt_bytes":0,"trimmed_turns":0,"request_bytes":0,"prompt_locked":false,"partition_bytes":2097152,"archived_record":3224,"next_record":4565},"appended_history_preserved":true,"old_clip_recoverable_from_backup":true,"USB_released":true,"known_validation_physical_replay":true,"independent_project_TEST_unread":true,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"execution_archive_sha256":"05eb7534dbf24d29d829709dae4990b830f178bc778cc74cecd4f1ad8d9b0035"}
唯一无诊断组，失败轮不替换/不补组；实际声学报告保留，不能把内部playback/energy候选当有用首音或一秒通过。新增历史保留，恢复原应用而非整片退回测试前，误触/277延迟/未过业务和动态话题目标保持。全goal active/阻塞0。


### UX299 监听前释放请求缓冲：限定实机阶段 — 2026-10-02
{"stage": "UX299", "version": "0.11.299-idle-release", "change": "Only under the existing compact-request flag and selected cold capture mode, finish HTTP/context cleanup before wake rearm and detach the joined capture arena/free request scratch. Do not eagerly restore that idle arena. Normal text/sync/non-selected/prime/expired-candidate paths restore as before. No new model, threshold, VAD, prompt, wait, stack or history policy.", "reused_checks": "297nine,295twenty,293nine,296five unchanged; no new host implementation and no unchanged batch repetition.", "groups": 1, "rounds": 3, "maximum_attempts": 1, "stop": "One declared integration only; do not replace failed rounds, tune a cutoff from this group, waive SDK49152, or certify useful acoustic latency from internal clocks."}


### UX299紧凑运行时正常三轮闭环 — 2026-10-02
{"stage":"UX299","complete":true,"normal_groups":1,"rounds":3,"attempts":3,"rows":[{"round":1,"language":"zh","first_wake":true,"actual_gap_ms":null,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":84352,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true},{"round":2,"language":"yue","first_wake":true,"actual_gap_ms":265.00000013038516,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":79488,"candidate_transport_hit":false,"candidate_texts":[],"naming_topic_specific":false,"runtime_health":true},{"round":3,"language":"zh","first_wake":true,"actual_gap_ms":281.99999989010394,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":78336,"candidate_transport_hit":true,"candidate_texts":["嗯，给这盏灯取名，我想想。"],"naming_topic_specific":true,"runtime_health":true}],"application_bytes":1532768,"reused_297_sanitizer_checks":9,"reused_293_sanitizer_checks":9,"reused_295_sanitizer_checks":20,"reused_296_sanitizer_checks":5,"target_workspace_bytes":17096,"SDK_cumulative_min_heap":47684,"SDK_heap_gate_passed":false,"observed_minimum_largest_block":38912,"contiguous_gate_passed":true,"continuous_full_dialogues_passed":true,"full_input_and_task_count":3,"first_wakes_hit":3,"all_runtime_health_passed":true,"candidate_transport_hits":1,"naming_topic_specific_count":1,"topic_specific_ack_passed":false,"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"acoustic_source_alignment_passed":true,"one_second_reply_passed":false,"one_second_reply_status":"Not certified by energy candidates alone; retain acoustic report.","original_application_restored":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"final_context":{"ready":true,"mode":"LOCAL","events":1344,"pending":1344,"recent_turns":128,"used":921644,"bank_size":1048576,"generation":18,"tail_recovered":false,"lamport":4531,"cursor":0,"acked":0,"last_local":39046,"history_budget":204800,"prompt_turns":0,"prompt_bytes":0,"trimmed_turns":0,"request_bytes":0,"prompt_locked":false,"partition_bytes":2097152,"archived_record":3224,"next_record":4571},"appended_history_preserved":true,"old_clip_recoverable_from_backup":true,"USB_released":true,"known_validation_physical_replay":true,"independent_project_TEST_unread":true,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"execution_archive_sha256":"39164d57adc27f077fc4fb1f41ee305b6789532f75caaac99c902447bfdf65c7"}
唯一无诊断组，失败轮不替换/不补组；实际声学报告保留，不能把内部playback/energy候选当有用首音或一秒通过。新增历史保留，恢复原应用而非整片退回测试前，误触/277延迟/未过业务和动态话题目标保持。全goal active/阻塞0。


## UX298–299：工作区复用与监听前交接

UX298确认反向复用实际发生了三次，不能将未改善归因于复用没发生。
唯一正常中/粤/中三轮均首次唤醒、完整转写并完成记忆任务，最低SDK堆仍46444B，
最小观测连续块32768B。候选三轮全部拒绝：前两轮达到原等待截止后取消，
第三轮协议/文本形状失败。final_topic_changed在cancelled状态下不能独立证明话题补全错误。
实录最终答复能识别，能量候选5.965/5.243/7.373秒不作为可用首音或一秒验收。
应用1532496B；恢复原72/off/off，1338事件保留，USB释放。

UX299只在已有紧凑请求开关、选中的冷启动采集模式中，把HTTP结束和上下文刷新
移到监听恢复之前；已join的原采集分配交回采集所有者，释放分离请求缓冲。
两处空闲恢复保留这个未借用的采集区；文本、同步、其它模式、预热和失效候选照原路径恢复。
模型/两票/740/256ms、VAD、任务栈、提示词、截止、容量和分区未变；没有新主机实现，
复用297九项、295二十项、293九项、296五项已通过检查，未重复运行相同批次。
准备脚本初次误假设CRLF，在写源码前停止；验证源hash不变后仅继续剩余修改，
保留原LF和失败文本。正常C11编译及冻结检查通过，应用1532768B，预算内余7328B。

唯一正常中/粤/中三轮首次唤醒/全输入/记忆任务3/3，后两次@done间隔265/282ms。
每轮capture_idle_retained时free增加25604B；之后两轮没有新的SDK低点。
累计最低SDK堆47684B，最小观测连续块38912B；低点移到第一轮最终TTS期间，
仍低于49152B，不能将1240B组间差值解释为纯单变量因果收益。
候选1/3、命名话题1/3；第二轮虽补全取名仍因截止取消，第三轮正确应答播放。
实录最终答复均识别为“记住了，这盏灯就叫小星星。”；最终答复能量候选
6.145/5.580/7.337秒不是应答首音/一秒认证。没有重置SDK计数、加截止或补跑失败轮。

两次安装/恢复都各自使用fresh完整4MiB备份、只写应用、readback和非应用区一致检查。
原0.11.72-summary/off/off恢复，1344事件/921644B/next4571保留，2MiB上下文和204800B
历史预算不变，USB释放。UX277粤语新增P95104>96ms、现场误醒、SDK和动态应答/一秒
门槛保持失败或未证；两个候选没有默认采用，完整goal active。
证据各在voice-recycle-flow-ux298/和voice-idle-release-ux299/的closure、supplement、
source/build/guard manifest、三轮report/serial/speaker.wav和offline-analysis中。


### UX300 提前记忆话题：主机限定阶段 — 2026-10-02
{"stage": "UX300", "began": "2026-10-02T10:34:19.279580+00:00", "before_hashes": {"plugins/speech/candidate.c": "7d974ac29c0110d5a873abfb7f3a8b3e560ca1050ba2343bf38d8120b23fc0fb", "plugins/speech/candidate.h": "c073ef3d5e22ae8fcda42bdeb987dcc842ef5331153247a3916857fed38afb2a", "platform/espidf/voice_candidate.c": "fcafa1fe1786c7650ea1554bdb0348a0a1f83c05062bc19a9815c07ba560d1a7", "platform/espidf/voice_candidate.h": "72878b47685506601296521daaad6971c533936aabb64df281caca55b098dff5", "platform/espidf/runtime.c": "332a7e407654b2075ca76d459867fd24c9c5e5a006b9218d82239dcaf60702ce", "host_tests/test_candidate_local_first.c": "70cc7f0caf483c627776cc6b5b99ae9c27a77c069f1782d6700bc4ffe89d6bd0", "host_tests/test_voice_local_first.c": "bff2e71c04be0ac5670820a92517f79695a434412ae91a3e3a668069c721700c", "host_tests/test_voice_candidate.c": "8bbfec467c5453221375d91ca247fef855b25458acc15f84b843df695f98fb70", "host_tests/test_candidate.c": "5ec3422d07b93416d047893f669d27b3409c8c2793d7ebbba9412936536c6c88", "host_tests/CMakeLists.txt": "b74a3b916df7faf0827492eb1e938b37dd88dfd494e28afbaf8a331a27321d6d"}, "change": "Only LOCAL_FIRST memory speculation may nominate at five informative Han characters (generic remains six), and uses same-width 记下： marker so the fast model knows remembered intent. Supply fluent-complete-word examples; no factual/action authority. Silent cache only until original full-final intent/prefix/language/UTF8/repair/cancel and ordered topic check pass.", "evidence": "Frozen UX299 serial: object partial 我给这盏灯 occurs18653/33587/48724; actual old nomination20054/34854/48739, final20102/34903/49306 approximately. Parse authoritative serial/report in replay, not these typed times.", "tests": "Six affected default-memory sanitizer targets plus compact ON normal-local adapter and engine-deferred. One old/new production C replay of all saved299 partials; old C renamed symbol-isolated to compare the immediately preceding policy, not the pre-291 fixed prompt.", "source_bytes": 65, "cache_bytes": 24576, "heap_gate_bytes": 49152, "contiguous_gate_bytes": 24576, "default_policy_change": false, "new_state_fields": false, "new_buffer": false, "models_changed": false, "threshold_changed": false, "window_changed": false, "VAD_changed": false, "stacks_changed": false, "wait_deadline_changed": false, "context_changed": false, "independent_TEST_read": false, "Flash_writes": false, "audio_capture": false, "cloud_calls": false, "quality_adopted": false, "false_wake_fixed": false, "maximum_host_seconds": 180, "maximum_replay_seconds": 60, "stop": "One host batch and one frozen text replay; correctness failure stops and is retained. No device group/default adoption in300. Previously failed SDK, topic, one-second and277/field gates remain; full goal active."}


### UX300提前记忆意图主机闭环 — 2026-10-02
{"stage": "UX300", "complete": true, "actual_sanitizer_checks": 8, "initial_checks": 7, "sole_missing_check_run": true, "failed_checks": 0, "nomination_advance_ms": [1401, 1269, 15], "final_lead_ms": [1451, 1318, 317], "full_final_checks": 3, "wrong_prediction_rejected": true, "default_prompt_C_preprocessed_identical": true, "fresh_version": "0.11.72-summary", "context_events": 1344, "context_unchanged": true, "wake_off": true, "voice_off": true, "USB_released": true, "board_group": false, "new_cloud_calls": 0, "new_audio": false, "new_model": false, "stack_change": false, "one_second_certified": false, "false_wake_fixed": false, "SDK_gate_299_still_failed": true, "latency_gate_277_still_failed": true, "goal_complete": false, "full_goal_retained": true}
初批CTEST正则将engine_deferred_request错写engine_deferred，因此实际7项而非8项；保留原日志，仅补唯一未执行检查。无生产失败/重训/上板/云调用。65B发布槽不增，完整输入核对保持；回放使用合成revision，不能当端点句ID或可用声学速度。完整goal active，未默认采用。


### UX301提前记忆话题正常实机阶段 — 2026-10-02
{"stage": "UX301", "version": "0.11.301-early-memory", "change": "Integrate300 early five-Han memory speculation and same-width explicit remembered-intent marker/prompts. Otherwise exact299 normal resource/ASR-first/idle handoff. No model/threshold/window/VAD/stack/deadline/capacity change.", "reused_checks": "300eight +297nine +295twenty +293nine +296five unchanged proofs; no repeated host batch.", "groups": 1, "rounds": 3, "maximum_attempts": 1, "stop": "One declared integration only; do not replace failed rounds, tune a cutoff from this group, waive SDK49152, or certify useful acoustic latency from internal clocks."}


### UX301紧凑运行时正常三轮闭环 — 2026-10-02
{"stage":"UX301","complete":true,"normal_groups":1,"rounds":3,"attempts":3,"rows":[{"round":1,"language":"zh","first_wake":true,"actual_gap_ms":null,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":83904,"candidate_transport_hit":true,"candidate_texts":["嗯，给这盏灯取名，我想想。"],"naming_topic_specific":true,"runtime_health":true},{"round":2,"language":"yue","first_wake":true,"actual_gap_ms":265.99999982863665,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":80256,"candidate_transport_hit":true,"candidate_texts":["嗯，给这盏灯取名，我想想。"],"naming_topic_specific":true,"runtime_health":true},{"round":3,"language":"zh","first_wake":true,"actual_gap_ms":280.9999999590218,"full_input":true,"core_task":true,"ASR":"请记住，我给这盏灯取名叫小星星。\n","uploaded_samples":83136,"candidate_transport_hit":true,"candidate_texts":["嗯，给这盏灯取名，我想想。"],"naming_topic_specific":true,"runtime_health":true}],"application_bytes":1533104,"reused_300_sanitizer_checks":8,"reused_297_sanitizer_checks":9,"reused_293_sanitizer_checks":9,"reused_295_sanitizer_checks":20,"reused_296_sanitizer_checks":5,"target_workspace_bytes":17096,"SDK_cumulative_min_heap":47728,"SDK_heap_gate_passed":false,"observed_minimum_largest_block":38912,"contiguous_gate_passed":true,"continuous_full_dialogues_passed":true,"full_input_and_task_count":3,"first_wakes_hit":3,"all_runtime_health_passed":true,"candidate_transport_hits":3,"naming_topic_specific_count":3,"topic_specific_ack_passed":true,"response_latency_acceptance_eligible":true,"rapid_rearm_acceptance_eligible":true,"acoustic_source_alignment_passed":true,"one_second_reply_passed":false,"one_second_reply_status":"Not certified by energy candidates alone; retain acoustic report.","original_application_restored":true,"current_version":"0.11.72-summary","wake_off":true,"voice_off":true,"final_context":{"ready":true,"mode":"LOCAL","events":1350,"pending":1350,"recent_turns":128,"used":924392,"bank_size":1048576,"generation":18,"tail_recovered":false,"lamport":4537,"cursor":0,"acked":0,"last_local":39174,"history_budget":204800,"prompt_turns":0,"prompt_bytes":0,"trimmed_turns":0,"request_bytes":0,"prompt_locked":false,"partition_bytes":2097152,"archived_record":3224,"next_record":4577},"appended_history_preserved":true,"old_clip_recoverable_from_backup":true,"USB_released":true,"known_validation_physical_replay":true,"independent_project_TEST_unread":true,"quality_adopted":false,"false_wake_fixed":false,"latency_gate_277_still_failed":true,"full_goal_retained":true,"goal_complete":false,"blocked_consecutive_turns":0,"execution_archive_sha256":"92f8ac95b12b3d8a93a51e83dedecf87e4fa643d991e126075db02ef343dd604"}
唯一无诊断组，失败轮不替换/不补组；实际声学报告保留，不能把内部playback/energy候选当有用首音或一秒通过。新增历史保留，恢复原应用而非整片退回测试前，误触/277延迟/未过业务和动态话题目标保持。全goal active/阻塞0。


### UX302固定同板误触对照计划 — 2026-10-02
首先完整guard备份现有Flash与录音；原72与已冻结301各同一18来源一次，中粤各4正确/10干扰，740/gain1、RMS .14/播放 .35，尾观察1.2秒。最多两组/36回放，单组240秒、正常执行600秒，无被动循环、补跑、训练、云调用、TEST揭盲或生产修改。停止自动语音以仅检查关键词，故不是连续业务验收。301三轮连续业务证据复用，47728B资源不足、277粤语104ms目标未达、一秒未证保持。最终app-only guard恢复原72/off/off，保留上下文、摘要、灯、音量与配置，旧录音由首次完整备份保留。证据artifacts/voice-fast/wake-compare-ux302/。


### UX301证据补充与UX299归因订正 — 2026-10-02
仅整理已关闭原始证据，无新硬件/云调用/训练或验收改动。301正常三轮命名应答3/3，实录应答候选2.93175/2.6544375/2.79209375s；完整答复8.05175/7.1844375/8.45209375s，均非一秒认证。SDK47728B不足，网络剩余栈1844B。原72/off/off、1350/924392B/next4577。299的25604B是清理交接区间增量；24577B裸请求之外还有HTTP/上下文释放，已订正叙述，不改变原关闭证据或宣称因果收益。supplement.json与VOICE_MEMORY_TOPIC_REPORT更新，完整goal active。


### UX302固定同板对照结束 — 2026-10-02
395.06秒两组36次完成，旧72中4/4粤4/4误触2/10；新301中3/4粤4/4误触2/10，没有改善且新增sapi-huihui-05漏醒，明确拒绝采用。误触是两语小燕来源。进度评论曾在统计完成前误报8条保住，已立即订正7/8；正式report/closure保留3/4与4/4，没有补跑。原72/off/off恢复、1350/924392B/next4577、上下文/灯/摘要/volume80保持，USB释放、旧clip全量guard备份；没有云/训练/TEST。证据wake-compare-ux302/、docs/WAKE_LATEST_COMPARISON_REPORT.md。完成测量不等于修复通过，完整goal active。


### UX303三条触发前输入诊断计划 — 2026-10-02
仅302的一漏/两误触来源，每条6.016s/188块一次，最多三次、不重放失败。复用完整hash验证的197原E/L观察固件，借空闲scratch输出触发前实际PCM/CRC/分数，不启动声音/录音Flash/云。不是301现场原输入：旧observer版本与CPU负载不同，只能在新采PCM上用冻结C后端预测核验，不混成质量成绩。完整guard安装/恢复、上下文/灯/摘要/配置/录音保持，capture阶段70s/device阶段240s，原72/off/off最终恢复。不训练、改阈值/模型/窗口或读独立TEST。证据wake-prefix-ux303/。


### UX303三段原始前缀复核结束 — 2026-10-02
硬件144.34秒/三次188块，564块CRC/时钟/原C3评分/新运行时原头全一致；两负例仍各预测一次误醒，P本次原E/L也漏。两误例辅助当前-4/-167却由短窗旧支持接受，为下一步负证据审计提供依据，未认定唯一根因或新规则通过。无满幅/丢样。首次离线分析在推理前路径读取失败，保留原文件，仅路径兼容修复后唯一0.116秒数值分析，无重复硬件/云/训练。原72/off/off与1350/924392B/next4577、clip/灯/摘要/volume80保持，USB释放，完整goal active。闭环wake-prefix-ux303/closure与报告。


### UX304负证据撤销旧支持固定原型计划 — 2026-10-02
只一规则：verify<0清last_support与辅助votes，保留原pending/268/twoof3/256ms/64预热/冷却。复用冻结TRAIN分数一次，C/Python事件须一致，保留两语言原正确各99%、extra168/actual49全保留、无新误/提前、旧负例<=18、实际/新128零。任一失败停止，不读DEV/TEST、不加第二阈值/窗口/训练。零固件源修改/烧录/USB/云，35秒计算预算，当前72/off/off；观察原型不代替误醒或完整goal验收。证据wake-negative-evidence-ux304/。


### UX304固定负证据原型结束 — 2026-10-02
仅一规则，原TRAIN180→16误触、两语言原正确3534/3542与2516/2520，extra168各新增漏一条而拒绝。49原实际正例全留、新128零、303两负例同一已存C数据能拦，仍不当真人/field通过。首次所有评分已运行后汇总KeyError，原exit1及报告保留；只正确汇总已存结果，不重算TRAIN，未日志保存的总计数器不声称精确记录。零生产源码/模型/训练/固件/云改变，DEV/TEST未读。最后新鲜USB确认72/off/off与1350/924392B/next4577，clip/摘要/灯/volume80不变；303前后全部非appFlash另比对精确。完整goal active，prototype停止，不扫第二规则。


### UX305模型监督覆盖审计计划 — 2026-10-02
复用冻结TRAIN分数与303三段PCM：由实际层结构计算感受野，分组统计预热前被损失屏蔽的负例高分机会，检查误触时是否覆盖完整词能量范围及C前端饱和。40秒预算，无训练、质量重算、硬件、云、阈值或模型修改，不读独立TEST。若完整词已经在感受野内，不以延长缓存当修复。完整goal保持，证据wake-supervision-ux305/。


### UX305监督覆盖诊断结束 — 2026-10-02
实际127特征帧/2048ms声音窗口；两误词源的20/30/40dB能量范围均在触发窗内，三输入C前端饱和比例<0.0008，没有证据支持增加环形缓存。796近词负窗口中114有预热前高分而预热后为零，分箱显示并非都在前8块。训练损失屏蔽了该区间，但不认定唯一现场根因。初录音能量边界受房间底噪污染，原audit保留，补证改用固定源能量并明确不是音素听审；未重跑模型评分或任何质量集。零新训练/硬件/云/生产源码变化，完整goal active，误醒未修好。


### UX306负例前缀监督唯一训练计划 — 2026-10-02
新增仅主机negative_prefix_loss：已标注负类在预热前真实帧也施加原268/margin1两票机会hinge与.25 BCE；正父源排除，填充尾排除，设备预热/阈值/原规则和24/24/48拓扑不变。先64随机掩码/梯度/错误元数据检查，一次fresh48/seed20261002306/6000步/210秒训练，固定17066窗口全覆盖、原6146锚点、512 TRAIN校准，final-only。不加另一权重/种子/备选checkpoint。零设备RAM增加、无烧录，TRAIN准入后才读DEV，独立TEST不读。完整goal保持。


### UX307唯一前缀监督模型整数准入计划 — 2026-10-02
只306最终checkpoint，C11编译、4096帧全层C/独立整数oracle与512 PCM验证后，固定TRAIN/extra168/103实际/128自然各一次评分。实际原触发owner与twoof3辅助256ms规则保持，原正确中粤各99%、额外与实际正确全保留、旧误<=18、实际与新自然零、无新增提前/误触、旧正确P95额外延迟<=96ms。180秒计算预算；任一准入失败停止，DEV/TEST不读、无上板/云/第二训练。原模型与固件继续保留，完整goal active。证据wake-prefix-admission-ux307/。


### UX306–307唯一监督修复训练与准入结束 — 2026-10-02
64随机掩码/梯度检查通过；两次执行在优化器前因填充assert和NumPy legacy32种子范围停止，原文件/log/exit1保留，只修填充assert并将同一已声明种子对legacyAPI取模，实际只有一次6000步训练123.458秒（含WSL等墙时131.328秒）、17066全覆盖和最终512校准。量化C11全层2166784值、完整头179146值、C/Python决策2163045块一致，唯一评分58.439秒。预热近词114→0，但旧负例180→20，高于预声明<=18而停止；原正确中3541/3542、粤2520/2520，extra17/17与18/18、实际49全留、新128零。不继续种子/权重/阈值扫描、不读DEV/TEST、不构建烧录；不能将TRAIN下降当现场改善。新鲜原72/off/off，1350/924392B/next4577、摘要/灯/volume80保持，USB释放。audio clip_ms=0/ready=false与304实际状态完全一致，摘要中曾提5196ms不当当前已加载录音事实。完整goal active，误醒未修好。


### UX308 C3实际工作区布局核对 — 2026-10-02
仅旧301构建配置下C3编译两个布局探针：原17024候选前缀和artifact头原型16512，核实max(采集,引擎)实际分配是否随前缀减少512。30秒预算、零生产修改/硬件/云，不重复训练，不把静态栈尺寸差冒称真实堆收益，完整goal保持。


### UX308布局核对结束 — 2026-10-02
C3实编原采集28216B/引擎29836B/实际29836B；候选前缀减少512后采集27704B，但实际分配仍29836B。拒绝将该栈/前缀变化计作堆收益，未改生产代码或上板。下一步采用有实际分配依据的TLS发送内容缓冲2048B固定实验；不改变栈、上下文、输入/回复预算、接收16384B和证书验证。完整goal active。


### UX309固定TLS发送缓冲实验计划 — 2026-10-02
仅2048B发送record，接收16384B、dynamic模式、认证、任务栈、C3声学模型、上下文、消息预算与取消/超时规则不变，默认关闭。先从当前SDK原函数提取写入器，18个2K/4K及大请求/短写/WANT/零/错误用例验证；构建后仅一正常中粤中连续三轮命名业务、每轮一次。实测SDK>=49152B、连续块>=24576B及原调用栈余量，未通过保留失败并退出，不做第二record大小或减栈凑数。全Flash新鲜备份、app-only guard安装与恢复原72/off/off，保留新增历史/配置，禁止整片擦除。全goal与误醒/277延迟/声学一秒未达状态保持。


### UX309固定TLS发送缓冲三轮闭环 — 2026-10-02
18项SDK原写入器ASan/UBSan检查通过；首编因SDK符号告警失败，保留日志，仅主机include隔离告警后才首次运行用例。单次构建应用1533120B，SDK配置仅OUT4096→2048，RX16384和所有栈/模型/阈值/上下文不变。首次冻结检查误将main宏用于KWS组件而停止，原脚本/错误保留，仅核验作用域修复，不重建。唯一中粤中三轮、各一次：first3/3，完整输入与任务3/3，话题应答3/3；SDK最小47856B，最大连续块最小40960B，48KiB关卡未通过。与301差128B仅描述不同轮次，不归因成纯缓冲收益。新鲜全Flash备份、app-only安装/恢复及非app一致均通过。原72/off/off、上下文1356事件、2MiB/204800B历史、摘要/灯/volume80保留，USB释放。误唤醒/277额外延迟/声学一秒仍未通过；默认不采用，不补另一组或减栈，完整goal active/阻塞0。证据voice-tls-tx-ux309/closure和实录。


### UX310前缀监督与当前负证据唯一组合计划 — 2026-10-02
只用306同一最终模型和304同一verify<0撤销支持规则，不训练/换种子/阈值/窗。复用307整数缓存，先逐组重建旧规则报告和逐源事件，须与307全部一致，才做新规则C/Python因果验证。最多45秒、两语言原正确各99%、extra168和103已知正确全留、旧负<=18、已知负与新128零、无新提前/误触、原正确P95额外<=96ms。失败停止DEV/TEST/烧录及第二规则，零固件源修改。这不是重用307失败配置宣称通过；完整goal和误醒/48KiB/277延迟/一秒未过保持。证据wake-prefix-owner-ux310/。


### UX311撤销规则训练契约对齐计划 — 2026-10-02
310额外普通话漏样本在原合法锚点当前-72、三票中位901，旧anchor损失允许该状态，新撤销规则拒绝。新增仅主机revocation_anchor_loss，对该固定规则的合法新支持机会作正锚点监督，原负例/前缀损失和mask保留。公式max(min(a,b,c+T),min(b,c),min(a,c,b+T))，T268不变。一次实际C支持状态/独立整数平滑/64随机流/因果梯度/观察反例检查，30秒预算，零新训练或质量评分、DEV/TEST不读、固件与模型不改。这是训练契约验证，不是该候选已经修复或上板；完整goal保持。


### UX310–311固定组合拒绝与训练契约闭环 — 2026-10-02
310一次8.173秒计算/9.844秒实际进程，旧缓存报告与事件全部重建一致，C/Python4272778块一致。旧误180→18、原正确中3536/3542粤2510/2520，但extra168中16/17掉一条而停止；未读DEV/TEST、无训练或烧录。该正确锚点辅助当前-72/三票中位901，旧anchor目标已满足而新撤销规则拒绝。311实现仅主机revocation_anchor_loss，保留原负例/前缀损失与锚点metadata，64随机流/16382实际C支持状态/独立整数平滑/因果与梯度检查通过，5.488秒，仅新增监督函数，未加入默认训练器。原默认模型、固件、C控制器与309冻结非文档文件完全保持；新鲜七项USB查询原72/off/off、1356事件/927140B/next4583，上下文/摘要/灯/volume/clip精确相同，USB释放。无新增音频/云/Flash，误醒未修好、48KiB/277延迟/声学一秒未过保持，完整goal active、阻塞0。首次诊断读取猜测的train.py不存在，未执行训练或评分；通过rg定位fit.py后只读检查，保留工具错误。证据wake-prefix-owner-ux310/与wake-revocation-objective-ux311/closure。


### UX312判定一致正锚点唯一训练计划 — 2026-10-02
复用306同一seed20261002306、17066完整TRAIN窗口、6146原锚点、48结构、6000步/batch32/CPU4、210秒上限、原优化器和512 TRAIN校准。仅增加311已核对的revocation_anchor_loss，权重等于原anchor_weight=1，不改原正锚点/负例/前缀项，不换种子/权重/checkpoint。最终模型对同一verify<0撤销原型做整数准入；失败则停止DEV/TEST/烧录和重训。默认训练器/生产C/运行资源与上下文不改，完整goal及48KiB/277延迟/声学一秒未过保持。证据wake-revocation-fit-ux312/。


### UX313撤销契约模型整数准入计划 — 2026-10-02
只312最终48模型、同一verify<0撤销原型，不改变推理窗/阈值/结构。复用310/311已通过判定语义，因新权重重新核对4096帧全层C整数、完整C/F64头及TRAIN/extra168/103已知/128自然决定。180秒预算、原正确中粤各99%、extra与已知正确全留、旧误<=18、已知负与新128零、无新误/提前、原正确P95额外<=96ms。失败停止DEV/TEST、固件、第二模型/规则或重训。C3资源与现场误触未用主机评分替代，完整goal保持。


### UX314剩余误触来源只读审计计划 — 2026-10-02
313唯一准入旧负21>18拒绝，其余关卡通过。只从已存整数分数恢复180个原负事件的来源与21个保留事件，分类标签/重叠来源、检查此前普通话合法锚点最新分数，不重跑NN/训练/质量集。20秒预算，不改标签或准入门槛、不读DEV/TEST、不烧录；完整goal保持。


### UX315最终浮点与整数剩余案例归因计划 — 2026-10-02
只314选出的21已知整数误触和此前修复的一条正锚点，沿用同一312最终checkpoint；核对训练与推理输入逐字一致后执行一次浮点路径，整数复用313缓存。比较固定撤销规则的事件，区分浮点学习与PTQ问题。20秒预算、零重训/新checkpoint/标签或关卡变化、DEV/TEST不读，不将选中子集推成整体浮点质量，不新增硬件/云/Flash；完整goal保持。


### UX312–315训练、准入拒绝和剩余案例归因结束 — 2026-10-02
唯一6000步同seed/同采样/同17066与6146锚点训练130.776秒，实际138.422秒，final-only/512TRAIN校准。首准入脚本父目录错误在编译和评分前退出1，原文件/log保留；仅parents2→3后才首次整数准入36.159秒/实际37.797秒。全层2166784值、C/F64头179146值、C/Python2109797块一致。此前漏样本当前-72→789、extra中17/17粤18/18，原正确中3541/3542粤2520/2520、已知49全留、新128零；唯旧负21>18失败而停止DEV/TEST/烧录及重训。剩余21窗口/12clipID含18截断片段、2粤自然、1近词；时间截断非音素边界，不据此改标签。22选中案例一次浮点诊断输入与训练相同：21整数误中19浮点也误、2整数新增，正例两路接受，不能推整体浮点质量。截断负例采样4–5次且非未见，不断言提高采样必修。收尾首次因NumPy环境缺PySerial在import退出，保留源/error；离线核对数组后用既有IDF环境七查询，未安装依赖、未重训或重评分。生产冻结非文档源不变，新鲜七查询原72/off/off与1356/927140B/next4583、上下文/摘要/灯/volume/clip精确保持，USB释放。零新音频/云/Flash，完整goal active、阻塞0，误醒/48KiB/277延迟/声学一秒未过保持；证据各阶段closure与总执行包。


### UX316剩余错误的监督/采样/归一化审计计划 — 2026-10-02
315明确多数残余错误已在浮点模型。本轮只已知21负例与修复正例，固定312最终权重；重建6000步采样并核对数组，检查每个触发位置的有效负监督、输出梯度和最后draw，比较eval与实际最后draw上下文的可丢弃train-BN副本。这不是历史训练损失，不重跑质量集、优化器、校准或DEV/TEST；60秒预算，阈值/标签/窗与固件不变，无音频/云/Flash。完整goal保持。


### UX317来源限额与确定性输入冲突审计计划 — 2026-10-02
316所有21负事件有效且hinge/输出梯度非零，train上下文仍18错误；不能把问题归为无监督或纯BN。本轮只TRAIN输入：6146正锚点与21负例有效机会的135帧精确上下文比对，有相同输入的冲突即停止重采样计划。无冲突仅排除本范围精确矛盾，不证明声学可分。按clipID全部负variant总计draw限额，目标45来自既有近词最少draw，附加来源公平列表且不替换原6000步采样。60秒，不训练/推理/校准/标签修改或DEV/TEST，无设备/云/Flash，完整goal保持。


### UX318唯一来源限额回放训练计划 — 2026-10-02
316/317已完成，所有残余负监督与梯度有效，所查精确135帧标签冲突为0但不证明可分。只按317冻结表增加331负draw，clipID全部variant总数限额45；9个compact同源误窗原已41draw，因此只额外4，不各自加40。原6000步batch32 sampler/RNG及每个正锚点采样完全保留，331个指定step临时batch33；seed/模型/损失权重/学习率/512 TRAIN PTQ不变。一次fresh/final训练、210秒内部/245外部/270包装上限，失败不重启、不选中间checkpoint。随后一次固定整数准入，旧负<=18与中粤保留/已知自然/延迟关卡不变；未过停止DEV/TEST和烧录。无音频/云/Flash，完整goal保持。


### UX319来源限额最终模型整数准入计划 — 2026-10-02
只318唯一final48模型，313相同撤销判定与全部关卡。复用原控制器语义证明，因新权重重做4096帧全层、C/F64完整头、TRAIN/extra168/103已知/128自然一次准入；旧负<=18、中粤99%、extra与已知全留、负零、无新提前及P95额外<=96ms保持。180秒，失败停止DEV/独立TEST和烧录，无第二模型/训练/阈值或校准选择。仍需C3和现场证明，完整goal保持。


### UX320来源限额模型一次已知DEV计划 — 2026-10-02
319全部整数TRAIN关卡通过，旧负12<=18、extra中17/17粤18/18与49已知全留、新128零。只冻结730已知开发集；沿用276的中44粤32/compact中14、负<=5、新负/提前零、额外P95<=96及最大<=256ms。同319撤销规则，全部730原C缓存复现、新48C/F64头、8全层NumPy、C/Python决定和cold prime核对。120秒、失败停止独立TEST/烧录，不改参数或再训；已知DEV不是盲测/现场/真人泛化，完整goal保持。


### UX321已拒绝DEV漏样本固定分数归因计划 — 2026-10-02
320仅粤语29/33低于32而拒绝，普通话44/45、compact15/15、负5→3及延迟关卡通过。只复用730分数缓存，重建同一判定的票/支持/撤销/原事件待确认，检查5个正例漏样本；不新推理或评估另一规则。20秒，不重训/改阈值/窗/标签，不读独立TEST，不烧录，完整goal与失败状态保持。


### UX316–321来源限额回放与粤语DEV拒绝结束 — 2026-10-02 23:22 CST
316所有21残余负机会与梯度有效，最终实际批上下文train仍18误；同源compact9窗合计41draw，不能独立各加40。317所查6146正锚点对21负有效机会无精确135帧冲突，不证明可分；来源限额45冻结331额外负draw。318一次same-seed/fresh/final6000训练128.677秒、实际133.141秒exit0，base计数逐项同312、来源上限和无正类extra通过。319整数一次33.857秒/实际35.468秒exit0，全层2166784/C-F64头179146/C-Python2109797一致；全部TRAIN准入通过，旧负21→12，原正确中3541/3542粤2520/2520，extra17/17与18/18、49已知全留和新128零。320一次730已知DEV7.333秒，完整原C/cache、新头/F64、8全层NumPy、C/Python及cold prime一致。中44/45、粤29/33、compact15/15、负5→3，唯一粤语至少32关卡失败拒绝；独立TEST/烧录停止。321只缓存归因5漏正：粤3无原事件近邻两票支持、粤1和中1早支持被负分撤销，未另测规则或新推理。生产非文档源冻结、新鲜七查询原72/off/off与1356/927140B/next4583、上下文/摘要/灯/音量/录音状态保持、USB释放。无第二训练/参数/校准，零新音频/云/Flash。主命令：local tts Python prepare318/process318/prepare319/process319；WSL cogd-kws evaluate320(150s外部120s内部)；local tts audit321；IDF Python close321。全部实际终止exit0；closure与执行manifest/zip在321。完整goal继续active，误醒/48KiB/277延迟/声学一秒未过，阻塞0。


### UX322粤语DEV来源与链路审计计划 — 2026-10-02
上轮训练集准入通过但粤语DEV拒绝，属于进展而非阻塞。只元数据/已有730整数分数，检查5漏正来源、TRAIN及附加来源标识是否覆盖、raw/device分组与归一化特征饱和。标识检查不等价真人说话人身份或物理音量，不将DEV加入TRAIN，不新预测/训练/阈值/标签、独立TEST不读、无设备/云/Flash。20秒，完整goal保持。


### UX323合同完成与UX324唯一窄频带增强训练计划 — 2026-10-02
322五漏例均属于两留出TTS音色，TRAIN及额外来源标识未见，raw和实录均含失败；只是泛化方向推断，不证明因果或真人身份。新增可选host frequency_mask，Bx40xT拷贝、最大4连续band、概率.5、归一化均值0，未做时间mask/warp。323实际合同先执行0.060秒后补录plan，未倒填前置记录：809280值、16可复现批、梯度/来源不变/时间轴及10非法输入通过。参考SpecAugment原论文arxiv1904.08779仅ASR证据，C3效果未知。本次单一配方不网格搜索。保持318数据/331来源限额回放/6146锚点/基础采样/模型seed/结构/损失/LR/6000步/512 TRAIN PTQ。增强使用独立同seed RNG；一次fresh/final，210秒内部245外部270包装上限；过整数TRAIN才读一次已知DEV，未过停止再训/参数选择/独立TEST与烧录。生产默认训练与固件不改，无新增设备算法/音频/云/Flash。完整goal保持。


### UX325来源限额最终模型整数准入计划 — 2026-10-02
只324唯一final48模型（host频带增强），313相同撤销判定与全部关卡。复用原控制器语义证明，因新权重重做4096帧全层、C/F64完整头、TRAIN/extra168/103已知/128自然一次准入；旧负<=18、中粤99%、extra与已知全留、负零、无新提前及P95额外<=96ms保持。180秒，失败停止DEV/独立TEST和烧录，无第二模型/训练/阈值或校准选择。仍需C3和现场证明，完整goal保持。


### UX327已拒绝频带配方的缓存差异审计计划 — 2026-10-02
325仅旧负20>18失败，其余原关卡通过；326生成脚本未执行、独立TEST/烧录停止。只比较319与325已存TRAIN分数的同一判定，重建12与20误窗，分类新增/保留/抑制来源，不新推理或另一配方选择。20秒、零重训/阈值/标签变化及设备/云/Flash，完整goal保持。


### UX322–325、UX327频带增强拒绝收尾 — 2026-10-03 00:26 CST
322只来源/已缓存分数审计，5漏例集中两留出TTS音色，raw与device均有，不等于真人身份或单因果证明。323可选host频带模块检查809280值/16批/10非法通过，plan后补事实保留。324保持318数据/331负回放/6146锚点/计数/种子/结构/损失/PTQ，只加独立RNG最大4band概率.5；唯一6000-step final fit127.580秒、包装132.062秒exit0，77023/192331次mask。325脚本生成器首次absent UX318标记exit1发生在生成/评分前，原样保留并只修标记；一次整数58.978秒/包装60.578秒exit0，全层2166784、C/F64头179146、C/Python2109797精确。唯一质量失败旧负20>18；中3539/3542粤2519/2520、extra17/17与18/18、49已知全留与新128零。不跑生成的DEV326、不读独立TEST、不再训或改mask/阈值/校准/规则。327缓存差异12→20，留10/新增10/消除2，新误为近词与截词；不推广为增强一般无效。新鲜七查询原72/off/off、1356事件/927140B/next4583，完整context/summary/light/音量/录音状态保持、USB释放。生产非MD冻结，零新云/音频/Flash。命令：local tts prepare324/process324/stage325/prepare325/process325/audit327/prove324；IDF Python close327；所有实际进程已终止，无活跃训练。部分较早标题复制10/2，本次实际已跨10/3 CST，以原UTC时戳和本记录为准。源码只读看到原两24模型保留penultimate特征，复用小判别头仅后续方向未实现/未验证。docs/WAKE_FREQUENCY_MASK_REPORT.md与327 execution manifest/ZIP/closure保留。完整goal active，误醒/48KiB/277延迟/声学一秒仍未过，阻塞0。


### UX328原双语特征与非线性小头有限计划 — 2026-10-03
上轮频带配方拒绝收尾属进展，goal完整、阻塞0。UX202联合线性头已不可行，不重跑LP。本次固定原24+24表征，新增可选host48→16ReLU→1 QAT与独立C11原型，801参数/852B权重，0新神经历史/784MAC每16ms帧是结构预算，C3堆/栈/代码体积未证。先150秒内F32/F64/C整数与20k安全/C缓冲getter证明、17066TRAIN特征及旧缓存对齐，6146锚点与标注负例五块精确冲突检查；任一失败停止。全过仅一次原seed6000步、同318计数/331负draw/四损失的固定shift8/3 QAT，无PTQ/BN/增强。原TRAIN18误/双语99%/extra和已知全留/自然零/无新误/96ms门槛保持，过后一次已知DEV，独立TEST不读；不选16宽/shift/seed/权重/阈值，失败留证停止该配方。无新设备音频/云/Flash，生产默认源及2MiBcontext保留。plan及新源hash在328，整体目标未完成。


### UX328合同通过与UX329唯一QAT开始 — 2026-10-03
修复前precheck在数值执行前exit1已保留；修复后唯一合同实际29.953秒/计算27.739秒exit0。2048头F32/NumPy/C值、20kASan/UBSan、4梯度张量、4096有/无trace C帧共102400表示/头值一致；17066窗口全部提取，15791旧E/L头及隐藏缓存逐项一致。6146正锚点对685652负postwarm五块上下文无精确冲突，不证明非线性可分或整体目标一致性。单独拟合仅801参数，原双24冻结，shift8/3固定。329同318种子/6000步/batch32+331来源负draw/覆盖计数/四损失/margin1、CPU4线程、210秒内唯一final QAT，无BN/增强/PTQ/DEV/TEST/录音/云/烧录。原TRAIN/DEV全部关卡保持；失败留证停止，不选另一个头/seed/shift/阈值。C3资源与三轮业务仍须另证，当前完整goal未完成。


### UX329拟合完成、UX330唯一整数准入计划 — 2026-10-03
329唯一6000步固定head QAT33.396秒/实际36.594秒exit0，801参数、backbones和shifts8/3冻结，基础计数逐项同318，331额外负来源draw相同，无PTQ/BN/增强/候选选择。330重编实际final C头+原撤销控制器，所有15791旧窗口及actual/public/new128完整流C/F32/NumPy头一致，每一判定与原Python规则一致，然后同旧关卡检查。TRAIN失败停止DEV/TEST/烧录/另头；通过才一次已知DEV，再单列C3资源/现场/三轮业务，不称主机通过即完成。无新声学素材/云/Flash，完整goal保持。


### UX328–330冻结双语特征小头拒绝收尾 — 2026-10-03 01:12 CST
328合同2048头F32/NumPy/C、20kASan/UBSan、4096帧getter/trace与旧缓存一致；6146锚点对685652负上下文0精确冲突不证明可分。329唯一6000步801参数QAT33.396/实际36.594秒，backbones/shift8和3/计数/331负draw/全部损失保持，无PTQ/校准/BN/增强/另模型。330唯一整数10.036/实际11.922秒exit0，4223804头C/NumPy/F32、2109797判定块精确。原正确中3540/3542粤2518/2520、extra17/17和18/18、49观察正例全留；旧误180→180、实录13+8→21、自然3不满足18/零/零三项，停止DEV/TEST/再训/参数选择/烧录。编译种子和漏链接的两个评分前失败原样保留。生产非MD源534项保持；新鲜七只读查询原72/off/off与1356事件/927140B/next4583、完整context/摘要/灯/音量/录音状态相同，USB释放。命令：local tts prepare/process328→prepare/process329→prepare/process330；IDF Python close330。有效流程均实际终止，无活跃任务；源/计划/模型/全部缓存/失败/执行manifest及ZIP在330，docs/WAKE_FROZEN_HEAD_REPORT.md。后续保留更早频带信息是新方向，尚未实现，不重复本小头配方。完整goal active，误醒/48KiB/277延迟/物理一秒未过，阻塞0。


### UX331原频带与双语特征联合的有限资源前置计划 — 2026-10-03
UX330小头失败后停止；完整goal保持，阻塞0。新增可选raw40+冻结E/L48→因果24 C11原型，不改生产534非MD源、两支原权重、FFT、上下文和判定门槛。10129参数、9840INT8权重、辅助3270B为结构预算，C3总资源未证；直接支RF127帧、经冻结表征有效原始RF253帧，不声称更短。UX331仅一次150秒内C/NumPy/F64、stream/alias/guard/安全/PCM检查；过后UX332仅一次未训练C3资源试验、fresh4MiB flash_guard更新与精确恢复原72，不执行外设动作或云业务。资源通过后才允许UX333一次6000步同采样/损失训练，原TRAIN18/99%/全留/零误/无新增/96ms与DEV44/32/15/≤5原门槛不改。无阈值/结构/seed/shift sweep、DEV回灌或独立TEST读取。任何失败留证停止对应配方；全业务连续三轮、ASR/VAD/流式LLM/快应答/48KiB与1秒物理有效回复仍必须，当前误醒未解决。


### UX331合同结果与UX332唯一C3资源试验前置 — 2026-10-03
纯C/NumPy/F641085440层值、随机块1085440值、8条TRAIN两种别名输入1191936值、512数学PCM前端/头/辅助trace通过；初始C布尔拼写编译退出，随后数值全跑完后502/534 provenance清单计数断言退出。原记录保留，只补534hash核对、不重跑数值。实际C3编译确认工作区17096→14232B，省2864B；尚非板上最低堆或识别效果证明。UX332独立.local源码/唯一资源图像，原生产源保留；未训练、事件和capture/actions关闭。正常app1540096预算、512USB原/辅助全trace、一分钟麦克风max32ms/P99≤16ms、SDK≥48KiB/连续块24KiB/noDMA门槛不改。每次Flash仅guard鲜备4MiB、app-only并精确恢复原72与完整上下文；无云/播音/训练/DEV/TEST。资源通过后才可单次正式训练，完整goal仍未完成。


### UX332资源通过、UX333唯一正式候选计划 — 2026-10-03
332实际一次512数学PCM USB全314880值一致、pair max13611us；1858实时麦克风块max11554us/P99上界12000us，SDK最低65932B/查询连续块最低63488B/noDMA丢失，测量77.125秒。应用1509120B/正常余30976B、工作区14232B。原72/off/off完整4MiB精确恢复与context/clip保留，USB释放。此前仅配置早展开错误17.302秒，注册后检查修复、不改模型/预算、无先前Flash。只据此进入UX333一次seed20261002306/fresh/final6000/原coverage与331回放/原6146锚点四损失的24分支训练，原40+冻结48合入88、均÷32；PTQ只原method512 TRAIN，channels/topology显式检查。CPU4/内部210秒，失败不重启。原TRAIN旧误≤18/中粤99%/已知和extra全留/自然零/无新误与提前/96ms、DEV44/32/15/≤5保持。资源图像未训练、不得称识别或全业务通过；goal三轮连续/ASR/VAD/流式LLM/话题应答/48KiB业务/1秒声学及误醒仍未完成。


### UX334联合特征最终整数准入计划 — 2026-10-03
UX333唯一6000步final与一次512 TRAIN量化成功；基础采样和331负draw完全保留。只此模型，原319固定负分撤销/原E/L归属/全部门槛，不读DEV/TEST不改变决策。180秒一次：4096个88维极值/随机帧C/独立NumPy/F64全层；完整旧15791缓存输入、实际/公开103、自然128中C/F64全头及C/Python判定。40输入在主机C中经两冻结模型再合入88，F64对照复用328旧隐藏并对完整新流因果提取，验证部署输入链。失败留证停止此配方、DEV/TEST与trained烧录，无第二candidate/fit/阈值/窗口/PTQ；资源通过仍不能替代质量或业务三轮，goal未完成。


### UX331–334联合特征资源通过、训练质量拒绝收尾 — 2026-10-03 02:23 CST
331 C/NumPy/F64层1085440、随机块1085440、TRAIN别名1191936、安全67840一致；precompile布尔拼写与posttest502/534计数两个实际exit1保留，只修来源校验、不重做算术。332独立资源图像1509120B、workspace14232B（省2864）；512 USB314880值一致/max13611us，mic1858块/max11554us/P99上界12000us，SDK min65932B/queried largest63488B/noDMA。配置早展开17.302秒失败保留，注册后同 guard build110.277秒成功；两次flash_guard鲜4MiB备份/app-only/readback/nonapp一致，原72整片Flash精确恢复。资源最低堆不代替全业务48KiB。333唯一same-seed/final6000训练91.018/包装95.362秒、base与331负draw逐项同318、512 TRAIN PTQ；334 final层1085440/head179146/decision2109797精确。首轮最后47/64批错22.222秒、初始前缀推理丢失，保留后截相同批长并重算前缀；不重训、不重做层proof。补齐30.944/包装32.864秒。旧负180→39>18、实录13+8→8+3、extra中16/17新增漏一条，三个关卡拒绝；中3540/3542粤2520/2520、49观察全留、新自然4→0、P95/max新增0ms只描述保留事件。训练模型未构建或烧录，DEV/TEST与第二fit停止。259输入、534原非MD、1123冻结资源源码hash一致；新鲜七只读USB原72/off/off、1356/927140B/next4583，完整context/summary/light/volume/clip同332closing，USB释放。命令：prepare/run/finish/target331→prepare/build/freeze/run332→prepare/process333→prepare/process334→IDF close334；所有实际进程终止，错误状态原样保留。执行manifest/ZIP在334、docs/WAKE_CONDITIONED_REPORT.md。下一有限诊断先对照保存失败片段浮点/PTQ，不盲目再训。完整goal active，误醒/全业务48KiB/277延迟/物理1秒未过，阻塞0。


### UX335保存失败片段的浮点/PTQ归因计划 — 2026-10-03
334拒绝后只用同一final checkpoint及已保存53失败片段：旧负39、实录负8、公开负3、旧正漏2、extra正漏1。60秒/CPU4/一次F32与F64冻结BN前向，Q8半远离零转换及相同C/Python撤销/归属；复用integer cached分数并同一oracle核对layer差异/饱和，区分float已有失败与PTQ引入。没有新训练/PTQ/候选/阈值或规则，不评全部数据、DEV和独立TEST不读，无USB/录音/播音/云/Flash。不将这组选择性诊断当作泛化/FAR或可采用模型。只作下步方向依据，完成即停止该诊断，完整goal仍active。

UX335两项实际诊断退出保留：初始误计extra负例导致old40而非39，未做浮点前向；只限制old15623后R1首样本F32/F64一层1/6144值未过原rtol2e-5/atol2e-6，最大3.31087e-6，尚未做事件评分。保持原容差失败标记而非调宽，R2只完成F64参照的53片段归因，另报F32层超差和判定差。首样本临时前向重算一次，模型/量化/门槛不变，无新训练。


### UX335失败片段归因完成 — 2026-10-03 02:35 CST
53既有失败片段F64参照：旧39负中33仍误/6仅PTQ，实录8与公开3全部浮点仍误；旧漏2为PTQ，extra漏1浮点也漏。合计45原有/8量化引入，不是全组评分，不能给浮点FAR或泛化。53 F32/F64分类相同但80层值未过原容差、最大9.505e-6，2平滑Q8差；严格层容差失败保留未放宽。16296整数头同缓存/24444 C-Python块精确；postwarm无上界饱和、反量化head RMSE.381/max1.776。原选择extra负例边界误计与R1首case精度退出保留，首case前向重算一次，无质量门槛变化。实际R2计算6.469秒/包装9.017秒exit0，所有流程终止；零新训练/PTQ/候选/DEV/TEST/音频/云/Flash。534生产源、323既有归档hash一致；新的七读原72/off/off、1356/927140B/next4583与摘要/灯/音量/clip保持，USB释放。下一方向是先查完整流和训练裁剪的因果历史、BN分布及监督，不只是量化；这些方向尚未验证，不盲目再fit。命令：local tts prepare335/process335→repair335/processR1→continue335/processR2；IDF close335。执行manifest/ZIP/closure在335、结果并入docs/WAKE_CONDITIONED_REPORT.md。完整goal active，误醒/业务48KiB/277延迟/物理1秒仍未通过，阻塞0。


### UX336完整流/裁剪因果历史与BN有限审计计划 — 2026-10-03
335浮点已有44误证明不能仅修PTQ。只同一final/75现有失败TRAIN窗口（39旧负+3漏正+11实录负的33裁剪），CPU4/90秒/一次诊断；复用335完整头，对比原重置crop、保持原骨干历史、再加126帧新支左历史。原训练器6000采样和331来源回放重建且count逐项核对；固定末批同组一槽替换，只训练BN前向与eval对照，不优化、不保存改变的BN或权重、不改阈值/事件/标签。核对raw/hidden一致和全历史输出2e-10内复现，沿原event/prefix/anchor/revocation loss统计失败，不选择另一候选。无DEV/独立TEST、USB/音频/云/Flash，不能当完整质量或真人泛化。完成后据证据选择下一有限修改，完整goal继续active，前次是进展、阻塞0。


### UX336审计结果与UX337因果训练输入合同前置 — 2026-10-03
336实际2.973秒/包装5.842秒exit0：75现有窗，raw334400/cache hidden405504精确，22/33实录裁剪hidden不同，11crop重置压分但连续支有支持；保全历史复现原F64头最大差0。39旧误中33eval有支持、仅5被末批训练BN压低；33实录crop31被训练BN压低，均不是完整评估或单因果泛化证明。新增可选training/kws/contextual.py，只host：256监督帧前126真实左历史、连续48表示，每层遮蔽假padding以保持冷reset旧窗口、masked BN排除padding/tail；原metadata/6146 anchors/阈值/C11架构不改。337 CPU4/120秒一次：103父完整表示准备、321 raw一致、3旧负+3漏正+33实录crop F64与既存全流2e-10；独立native gather BN2e-12/梯度1e-10、padding突变不变/非法相位长度拒绝/四原loss梯度。不训练/PTQ/候选选择/DEV/TEST/设备/云/Flash；失败停止，过后才另冻单次fit。完整goal active、阻塞0。


### UX337合同通过与UX338唯一连续历史训练计划 — 2026-10-03
337计算2.282秒/包装4.389秒exit0：103父表示、321raw一致，39窗9896头F64与既存全流差0、9984padding突变不变，2040 masked BN值/运行统计/梯度同native gather；原四loss梯度有限、真实左历史梯度非零、假padding梯度零，6非法拒绝。新增可选contextual_calibrate，只同512 TRAIN索引和原power2方法，以同连续输入body真实激活校准。338唯一fresh/same seed20261002306/final6000步、CPU4/210秒，原17066/321观测ID/331来源回放/基础采样/6146 anchors/四loss/margin1/LR与架构10129参数保持，仅真实因果历史与padding BN纠正。无BNburnin/窗口/阈值/结构/seed/校准选择或DEV回灌，不以延长冷warm隐藏缺陷。原TRAIN旧误≤18/双语99%/extra和已知全留/实际负零/自然零/无新增或提前/96ms与DEV44/32/15/≤5保持。失败留证停止该配方，不再fit、不读独立TEST或烧录；C3现场与三轮完整业务仍须另证。生产534非MD源、2MiB上下文与原模型不动。完整goal继续active、阻塞0。


### UX339联合特征最终整数准入计划 — 2026-10-03
UX333唯一6000步final与一次512 TRAIN量化成功；基础采样和331负draw完全保留。只此模型，原319固定负分撤销/原E/L归属/全部门槛，不读DEV/TEST不改变决策。180秒一次：4096个88维极值/随机帧C/独立NumPy/F64全层；完整旧15791缓存输入、实际/公开103、自然128中C/F64全头及C/Python判定。40输入在主机C中经两冻结模型再合入88，F64对照复用328旧隐藏并对完整新流因果提取，验证部署输入链。失败留证停止此配方、DEV/TEST与trained烧录，无第二candidate/fit/阈值/窗口/PTQ；资源通过仍不能替代质量或业务三轮，goal未完成。


### UX338–339结果及UX340自然窗口连续历史合同计划 — 2026-10-03
338唯一6000 fit/PTQ156.573秒/包装160.906秒exit0，同318计数、331回放、同512校准ID逐项不变。339一次整数31.581秒/包装33.516秒exit0，1085440层/179146头/2109797判定精确；实录13+8→0，原正确49全留、extra17/17和18/18，但旧误28>18、新自然1仍失败。原中3538/3542粤2518/2520、P95新增0ms/最大32ms，无新提前/旧负；不跑DEV/TEST、不烧录或另fit。残余自然index102对应既存954自然窗，原256准备仅保证骨干RF127，联合RF253需延续隐藏和新支历史。340只把既存source/start_frame/length映射到generic ContextualInput，准备128完整原骨干表示，核对全部954 raw及既存17066suffix；保持321观测上下文和原标签/6146anchors/窗口时长/阈值。CPU4/90秒一次对source102所有窗F64与完整流2e-10对齐、非法源相位长度拒绝。零第二训练/PTQ/DEV/TEST/设备/音频/云/Flash；当前candidate仍拒绝，不能将51已知负零当现场FAR。完整goal保持active，前一turn进展、阻塞0。


### UX336–340连续历史修正收尾 — 2026-10-03 03:30 CST
336审计完整/重置输入差异，337 masked BN/前文/梯度数值合同均过，host可选代码不改C11推理。338唯一6000训练/512 TRAIN PTQ156.573/包装160.906秒，原base与331draw及calibration IDs逐项同原；339唯一31.581/33.516秒整数层1085440/头179146/判定2109797精确。已知实录21→0、49正全留、extra17/17与18/18；旧28>18、自然1仍拒绝，双语保留99%/P95新增0ms/max32ms。拒绝后不DEV/TEST/另fit/阈值或校准搜索、不构建或烧录trained应用；不当现场FAR和真人泛化。340扩展954自然窗同历史映射：128父、raw9768960值及标签/长度同原、321obs10790736值/122622mask不变，source102十一窗2786 F64头对齐差0/旧hidden38704值不同，3非法拒绝，2.454/包装4.558秒。自然扩展尚未训练，下一阶段只在另冻有限单配方后执行；本配方停止。535输入与534生产非MDhash一致；新鲜七读原72/off/off，1356/927140B/next4583、context/summary/light/volume/clip保持，USB释放，零本轮Flash/采放/云。命令：local tts prepare/process336→337→338→339→340；IDF close340；实际全部terminal exit0。docs/WAKE_CAUSAL_CONTEXT_REPORT.md与340 execution manifest/ZIP/closure留证。完整goal active，误醒/全业务48KiB/277延迟/声学1秒/完整三轮仍未完成，前后turn均进展、阻塞0。


### UX341全部连续历史唯一训练计划 — 2026-10-03
上一goal turn为progress，阻塞0；核实338/339/340均实际terminal exit0，340全部954自然输入合同通过。当前534生产非MD哈希同309。只把341的321观测连续行扩到1275行/231父来源，旧15791冷窗口保持；原17066数据/标签/事件/6146 anchors/coverage/331来源限额draw不变，fresh seed20261002306/6000步/CPU4/210秒/final only、原四loss、BN合同和512 TRAIN校准索引/方法保持。不修改阈值、warm、冷却、控制器、模型、校准或选checkpoint，不引入新素材或DEV回灌。一次原整数准入：旧负≤18、双语原正确99%、extra/观察正全留、实际/自然负零、无新增/提前、P95≤96ms；失败停止此配方，不第二fit/DEV/TEST/Flash。通过才另冻DEV与设备三轮完整业务验证。零计划Flash/采放/云，完整goal保留active，误唤醒和全业务预算尚未证明。证据：artifacts/voice-fast/wake-all-context-fit-ux341/plan.json、fit.py与process。


### UX342联合特征最终整数准入计划 — 2026-10-03
UX341唯一6000步final与一次512 TRAIN量化成功；基础采样和331负draw完全保留。只此模型，原319固定负分撤销/原E/L归属/全部门槛，不读DEV/TEST不改变决策。180秒一次：4096个88维极值/随机帧C/独立NumPy/F64全层；完整旧15791缓存输入、实际/公开103、自然128中C/F64全头及C/Python判定。40输入在主机C中经两冻结模型再合入88，F64对照复用328旧隐藏并对完整新流因果提取，验证部署输入链。失败留证停止此配方、DEV/TEST与trained烧录，无第二candidate/fit/阈值/窗口/PTQ；资源通过仍不能替代质量或业务三轮，goal未完成。


### UX342拒绝与UX343一次量化归因计划 — 2026-10-03
341唯一6000fit/512校准158.400秒/包装162.890秒；342整数30.506/32.484秒，层1085440/完整头179146/判定2109797精确。新自然128来源1→0、51已知负零、49正全留；旧误31>18，extra中16/17漏1，粤18/18，旧双语99%保持/P95新增0ms/最大中32粤64。该配方停止，不第二训练/DEV/TEST/Flash。只36保存失败：31负+旧正4+extra正1，60秒/CPU4的一次同checkpoint F32/F64 BN eval及Q8舍入/C决策；原整数缓存/完整C/独立oracle对齐，诊断浮点已有错误或PTQ引入，非质量重评或候选选择。记录F32/F64头差和判定差，不用脆弱中间层float32容差触发反复重跑。无阈值/校准/训练/新素材/音频/云/Flash；完整goal active、现场误醒未修复、阻塞0。


### UX343计数断言失败与UX344范围纠正 — 2026-10-03
343实际terminal exit1，未开始任何float/C/新oracle前向，selection/heads/rows/diagnosis未写。错误是遍历15791时把extra132的1个已知负误触混入预先声明的旧31负，变成32负；342 extra report原已记录该负，不修改质量结果。保留343原脚本/plan/process/log。344只纠正index<15623负样本边界，仍同36例(旧31负+旧漏4+extra漏1)、同权重/阈值/控制器。第一次float诊断60秒，未重做fit/校准/完整质量检查，不以误差容差改头或重新筛选。原模型仍拒绝，无DEV/TEST/Flash/音频/云，完整goal active、阻塞0。


### UX344浮点归因结果与UX345固定BN/目标审计 — 2026-10-03
344纠正旧负边界后首次float诊断6.136秒/包装8.316秒exit0，旧31负全部F64仍误，旧漏4中3为float已有、1仅PTQ，extra漏1为float已有。9216完整C/oracle头、4608缓存分、13824决定块精确；F32/F64事件与失败分类36/36一致，最大头差1.013e-5。量化不是旧残误主因。345只31固定负例、60秒/CPU4：重建same6000末批+331 schedule，原采样计数完整核对，逐例替换同组第一个slot、每次恢复相同checkpoint；部署BN头同保存344核对，同真实上下文/掩码和event/prefix目标比较训练BN与部署BN分数、误触及loss。单固定批不能外推所有BN历史/泛化，无optimizer/权重修改/fit/PTQ/新候选/阈值，无DEV/TEST/音频/云/Flash，完成停止此审计，完整goal active、阻塞0。


### UX341–345全部连续历史实验收尾 — 2026-10-03 04:02 CST
341唯一fresh6000/512 TRAIN PTQ158.400/包装162.890秒，321→1275连续输入/231父，原采样/331draw/calibration ID相同。342整数30.506/32.484秒，层1085440/头179146/决定2109797精确。自然1→0、已知51负零/49正保留；旧31>18、extra中16/17失败，粤18/18、旧99%与P95新增0ms保持，另extra132负仍1原触发。343计数边界错误terminal1，仅校验失败未float；344纠正旧负范围后首次诊断6.136/8.316秒，31负全float已错、旧漏4中3float+1PTQ、extra漏float。9216头/4608分/13824决定精确，F32/F64事件及分类36/36同。345同原末批33/恢复同权重BN/完整连续历史，7936F64头差0，训练仍误29/31、margin违反31；eval/train loss277.119/263.073，2.164/4.506秒，无optimizer或权重更新，不作BN单因/现场泛化结论。未第二fit、未DEV/TEST/训练构建或烧录。下一步核对片段监督再声明有限困难样本回放，保留正锚点和门槛。新鲜七USB读原72/off/off，1356/927140B/next4583及context/summary/light/volume/clip保持，570声明输入与534生产非MDhash一致。零新采放/云/Flash，USB释放，实际进程4个exit0与1个前向前校验exit1均terminal，无活训练。命令local tts prepare/run341 fit→342 admit→343→344→345；IDF capture345；local close/verify345。docs/WAKE_CAUSAL_CONTEXT_REPORT.md与345 execution manifest/zip/closure留证。完整goal active、progress、阻塞0。


### UX346困难负例来源与监督审计计划 — 2026-10-03
前goal turn为progress，核实345关闭/所有进程terminal、USB释放；误醒未修复。先31残负(24片段、4近词、3自然)，60秒一次，核对父正例/来源TRAIN/切半几何/标签与anchors，检查全部原合法正锚点与负例冷输入前缀是否完全一致而标签冲突。原paired明确未音素标注，时间40–60%不作人工发音证明；不更改/删除数据，不以无精确冲突代替语音人工核验。无新预测/fit/PTQ/候选/阈值/DEV/TEST/音频/云/Flash。若确定来源或正负目标冲突，先纠正再训练；若仅已知时间切半且无矛盾，可按此严格片段任务另声明有限来源回放，原正锚点和门槛保留。完整goal active、阻塞0。


### UX346审计通过与UX347困难回放合同 — 2026-10-03
346来源/几何检查0.930秒exit0：31负全部TRAIN，24片段对应15父正例、12保存cut公式核对；6097旧合法正锚点/2015负前缀无精确冷输入监督冲突。未音素核实，不改标签/删除数据。新增可选host hard_replay.py；4个unittest0.031秒通过，覆盖来源/角色cap、已有超cap保留、基础计数不变、顺序反转确定性、增加相关未选变体不扩大预算、唯一有限步骤及非法元数据。347真实数据60秒合同，选同36失败+extra132唯一保留负=37(32负/5正)。benchmark为原合成near_word原始+channel来源基础曝光最小值，非评分选择；按来源+正负角色封顶，已有331与基础coverage保持，只均匀安排每步至多1新row、同来源内优先低曝光窗口。不改变6146正锚点/标签/素材/阈值/warm/架构或学习参数，不读DEV/TEST，不训练/PTQ/云/音频/Flash。通过才另冻唯一fit，完整goal active、阻塞0。


### UX347合同通过与UX348唯一困难来源回放训练 — 2026-10-03
347合同0.137秒exit0：37 TRAIN旧窗口(32负/5正)、27来源+标签角色，按原合成near_word原始+channel来源基础最少270draw封顶，新增5456=4416负+1040正；旧基础192000/331保持，反转选择结果精确同一，最大batch34、6146 anchors原样，输入/标签不变且新draw只旧TRAIN15791，无DEV/独立TEST用于选模型。348唯一fresh seed20261002306/final6000/CPU4/210秒，原架构10129、全部1275连续输入、BN合同/四loss/margin/LR/原512校准索引方法保持，唯一变化是347冻结回放。基础coverage和旧331计数仍逐项校验，新增角色draw独立保存，不混写原331字段为总负draw。无checkpoint/seed/阈值/校准/结构/数据选择；原TRAIN全部门槛及DEV关卡保持，失败停止配方，不第二fit/DEV/TEST/训练固件烧录。生产534非MD同309。完整goal active、阻塞0。


### UX349联合特征最终整数准入计划 — 2026-10-03
UX348唯一6000步final与一次512 TRAIN量化成功；基础采样和331负draw完全保留。只此模型，原319固定负分撤销/原E/L归属/全部门槛，不读DEV/TEST不改变决策。180秒一次：4096个88维极值/随机帧C/独立NumPy/F64全层；完整旧15791缓存输入、实际/公开103、自然128中C/F64全头及C/Python判定。40输入在主机C中经两冻结模型再合入88，F64对照复用328旧隐藏并对完整新流因果提取，验证部署输入链。失败留证停止此配方、DEV/TEST与trained烧录，无第二candidate/fit/阈值/窗口/PTQ；资源通过仍不能替代质量或业务三轮，goal未完成。


### UX350一次固定已知DEV计划 — 2026-10-03
349唯一TRAIN整数全部通过：旧负17<=18，中3542原正确全留/粤2518，extra17/17与18/18、49观察正全留，51观察负及128自然零。只此final模型，不修改参数或评分第二checkpoint。730已知开发，原348已冻门槛中44/45粤32/33、compact15/16、负<=5、新负/提前零、P95<=96/max<=256ms。全部730原E/L完整C头与F64/保存baseline一致，联合24完整C/F64头及8真实全层C/NumPy对齐，C/Python撤销及原真实silent prime无触发。120秒单次；失败停止独立TEST/设备/第二fit与阈值/校准选择。这是已知DEV，不是盲测/现场/真人泛化；无音频/云/Flash，完整goal active。


### UX349训练准入/UX350开发拒绝与UX351归因计划 — 2026-10-03
348唯一6000/512 PTQ166.089/包装170.610秒，原基础/331与5456额外角色draw精确。349整数30.568/32.594秒全部TRAIN通过：旧负180→17、旧中3542全保留/粤2518，extra17/17与18/18、观察49全留/51负零、新自然128零，无新误/提前，P95额外0/max32ms。350只同模型已知730 DEV7.985秒exit0，所有原C头/cache/F64、新C完整头及8层trace/C-Python/cold prime精确；中45/45、粤32/33、负5→4，唯compact13/15未达至少15而拒绝；不独立TEST/Flash/再fit。351只3个已评分漏例(粤1/compact中2)，60秒同权重F32/F64 BN-eval、 signedQ8原控制器与缓存整数一致；归因用，不DEV反传或选参数/重新评分所有DEV、无第二fit/PTQ/阈值/音频/云/Flash。完整goal active，真实现场/三轮/业务预算和1秒仍未证明，阻塞0。


### UX351归因与UX352完整词/片段来源角色合同计划 — 2026-10-03
351一次3漏例0.124秒exit0：粤语漏与1个compact漏已在F64，另1个compact漏仅PTQ；F32/F64分类/事件3/3同，768整数头/384保存分/1152 C-Python决定精确，无DEV反传/参数选择。只依据346 TRAIN片段15父来源统计完整词曝光，核对是否负角色已270而对应正角色不足。352不引入DEV样本ID/声纹/音频，完整正例按现有标签、合法局部事件，同原270cap另冻第二来源回放；原5456+331+基础192000保持，所有6146锚点不变，每步至多再加1正row、总batch<=35。60秒纯输入/角色合同，不模型预测/fit/PTQ/新候选。若过，只供下一阶段另冻唯一配方，本348模型仍因compact13拒绝，不重评DEV/独立TEST/Flash；全目标active、阻塞0。


### UX346–352有限困难来源实验收尾 — 2026-10-03 05:58 CST
31旧残误TRAIN来源核对、无6097正锚点/2015负前缀完全相同冲突；未音素边界核实，不重标。347来源角色270cap，新增5456=4416负+1040正，原192000+331保持。348唯一6000 fit166.089/包装170.610秒；349 TRAIN全过旧负17<=18、原正确中3542/3542粤2518/2520、extra17/17及18/18、51负零/49正保留/128自然零。350已知DEV中45/45粤32/33负4/636，compact13/15失败，停止此配方；351两float漏一PTQ漏，未DEV训练。352仅TRAIN 15来源完整词41..90 vs负270，冻结2973正draw合同通过，未第二fit，不读独立TEST/训练构建或Flash。7实际进程terminal0，454输入/流和534原非MDhash通过；七USB读原72/off/off，1356/927140/4583、context/summary/light/volume/clip保持，USB释放，零采放/云/Flash。命令各prepare/run346..352、IDF capture352、local close352，源码/权重/合同/实际日志执行包留证。完整goal active，本轮progress、阻塞0；未作现场误醒/真人泛化或完整目标完成结论。


### UX353一次完整词/截断片段角色平衡训练计划 — 2026-10-03
上一348配方已拒绝并封存，352合同冻结且263执行文件核对后，另声明唯一fresh seed20261002306/final6000/CPU4/210秒。仅加352预先冻结的2973完整正TRAIN draw；原192000基础+331负+5456困难角色保持、总batch<=35。同15 TRAIN父来源角色封顶270，不从DEV漏例选择声纹/ID/音频；不能将曝光假设称已证原因。1275连续输入、6146 anchors、结构10129、四loss/margin/LR、原512 TRAIN校准ID及方法、阈值/控制器全保持。所有原TRAIN门槛保持，随后已知DEV44中/32粤/compact15/负<=5/新增负或提前0/P95<=96/max<=256ms保持。失败停止本配方，不第二fit/校准/threshold/checkpoint搜索；独立TEST未读，过关才开展整机真实3轮，新资源模型须验证实际controller/性能/48KiB/快速prime，不只主机质量过关即部署。完整goal继续active，本轮progress、阻塞0。


### UX354联合特征最终整数准入计划 — 2026-10-03
UX353唯一6000步final与一次512 TRAIN量化成功；基础采样和331负draw完全保留。只此模型，原319固定负分撤销/原E/L归属/全部门槛，不读DEV/TEST不改变决策。180秒一次：4096个88维极值/随机帧C/独立NumPy/F64全层；完整旧15791缓存输入、实际/公开103、自然128中C/F64全头及C/Python判定。40输入在主机C中经两冻结模型再合入88，F64对照复用328旧隐藏并对完整新流因果提取，验证部署输入链。失败留证停止此配方、DEV/TEST与trained烧录，无第二candidate/fit/阈值/窗口/PTQ；资源通过仍不能替代质量或业务三轮，goal未完成。


### UX355一次已知DEV检查计划 — 2026-10-03
354 TRAIN全过后仅此6000最终模型，730已知DEV原门槛中44/45、粤32/33、compact15/16、负<=5、新负/提前零、P95新增<=96/max<=256ms不变。原E/L C/F64/cache、全部186880新头和8全层C/NumPy、C/Python判定、真实silent prime同前置核对。120秒一次评分，无DEV训练或parameter selection；失败停止本配方、独立TEST/烧录。全部通过后另冻独立TEST与实际runtime验证，不把DEV当现场或真人泛化。完整goal active。


### UX353–355结果与UX356五漏例固定归因计划 — 2026-10-03
353唯一fresh/final6000，163.497秒计算+导出/176.360秒包装，新增2973正draw精确，所有原采样/锚点/校准ID保持。354 TRAIN全过31.096/33.000秒：旧负17→8，原正确中3541/3542、粤2520/2520；extra17/17、18/18、49观察正保留/51负零/128自然零，所有C数值与判定一致。355已知DEV普通话44/45、粤语31/33、compact13/15、负3/636，无新负或提前；粤及compact门槛失败。此配方停止，不第二fit/校准/独立TEST/训练镜像/Flash。356只固定5漏例60秒同权重F32/F64归因，不DEV反传或参数选取，不重新评分全部DEV。完整goal active、progress、阻塞0。


### UX356结果与UX357静音初始化状态合同计划 — 2026-10-03
固定5已知DEV漏全在F64已有，非PTQ单因，F32/F64事件与分类5/5相同，1280头/640缓存/1920判定一致；不再次训练或DEV参数选择。本配方已停，独立TEST不读，训练固件不构建或烧录。另做启动延迟结构修复：新可选prototypes/kws_conditioned_seed按同模型/前端/310控制器生成64零PCM的类型化常量状态，省去逐次静音计算，不能声称已缩短板上731ms。无新增堆，workspace保持14232；60秒一次主机ASan/UBSan2048帧全状态/trace/决策对照、7gap、disarm、拒绝不变及已取得支持被负证据撤销；C3只编译对象，记录Flash常量和栈，不写设备，不作现场/业务48KiB或1秒结论。该合同用353已拒绝模型仅做数值状态，不改变其拒绝结论或部署许可。完整goal active、progress、阻塞0。


### UX356结果与UX359首次静音状态数值合同计划 — 2026-10-03
固定5已知DEV漏全在F64已有，非PTQ单因，F32/F64事件与分类5/5相同，1280头/640缓存/1920判定一致；不再次训练或DEV参数选择。本配方已停，独立TEST不读，训练固件不构建或烧录。另做启动延迟结构修复：新可选prototypes/kws_conditioned_seed按同模型/前端/310控制器生成64零PCM的类型化常量状态，省去逐次静音计算，不能声称已缩短板上731ms。无新增堆，workspace保持14232；60秒一次主机ASan/UBSan2048帧全状态/trace/决策对照、7gap、disarm、拒绝不变及已取得支持被负证据撤销；C3只编译对象，记录Flash常量和栈，不写设备，不作现场/业务48KiB或1秒结论。该合同用353已拒绝模型仅做数值状态，不改变其拒绝结论或部署许可。完整goal active、progress、阻塞0。


### UX353–359实验收尾 — 2026-10-03 06:28 CST
唯一6000fresh/512 TRAIN PTQ：353新增2973正draw，原基础/331/5456/calib IDs相同；354 TRAIN全过旧180→8、中3541/3542粤2520/2520、extra17/17及18/18、49观察正保留/51负零/128自然零。355已知DEV中44/45粤31/33 compact13/15、负3/636，粤与连读未过；356五漏全F64，不只PTQ，停止本配方。另加可选纯C typed静音状态模块；357合同编译seed溢出前停1，30输入快照保持；358fixture路径错误child2无数值；359先依据实际2修复守卫，首次2048帧ASan/UBSan全状态、1249280 trace值、7gap/disarm/guard与negative撤销过。C3仅对象14232 workspace/8align，seed9616+text418=10034 Flash/dataBSS0/stack32/无allocator；无板上速度声明。7stage计算进程terminal(0/0/0/0/1/2/0)，target六命令0；478输入/流、534生产非MD保持。七USB读原72/off/off，1356/927140/4583与context/summary/light/volume/clip保持；USB释放。无第二fit、DEV训练或参数选取、独立TEST、训练固件构建、采放/云/Flash。命令prepare353→run353 fit→prepare354→run353 admit→prepare355→run355→prepare/run356→prepare/run357/358(失败留证)→prepare/run359→compile359→IDF capture359→local close359。docs训练与seed报告、359 execution/closure/verification留证；完整goal active、progress、阻塞0。


### UX360有限TRAIN切词语义与声学覆盖诊断计划 — 2026-10-03
上一353唯一配方及359状态合同已封存、进程全部终止。只用346已冻结的24困难片段/15父来源，核对原PCM/hash/TRAIN身份，按已有fraction/cut及adapter .5默认恢复来源域片段；不重建后续noise/channel/respeed特征。已有离线SenseVoice INT8/CPU4/auto/ITN，一次unique PCM加500ms零尾，120秒；不提高gain/做去噪或重复选择转写。普通话同音转写仅作可疑标记，粤语只字面完整目标；ASR不能证明音素边界，不自动改标签/删样本。统计TRAIN正词语言/来源/速度/时长覆盖；不听DEV/独立TEST、不KWS模型评分/训练/设备采放/云/Flash。如出现可疑完整词负标签，先保留原输入/输出，不跳到训练；完整goal active，本轮progress、阻塞0。


### UX361有限TRAIN切词语义与声学覆盖诊断计划 — 2026-10-03
上一353唯一配方及359状态合同已封存、进程全部终止。只用346已冻结的24困难片段/15父来源，核对原PCM/hash/TRAIN身份，按已有fraction/cut及adapter .5默认恢复来源域片段；不重建后续noise/channel/respeed特征。已有离线SenseVoice INT8/CPU4/auto/ITN，一次unique PCM加500ms零尾，120秒；不提高gain/做去噪或重复选择转写。普通话同音转写仅作可疑标记，粤语只字面完整目标；ASR不能证明音素边界，不自动改标签/删样本。统计TRAIN正词语言/来源/速度/时长覆盖；不听DEV/独立TEST、不KWS模型评分/训练/设备采放/云/Flash。如出现可疑完整词负标签，先保留原输入/输出，不跳到训练；完整goal active，本轮progress、阻塞0。


### UX361语义结果与UX362固定疑似切点归因计划 — 2026-10-03
361首次完整31离线ASR decode，15原词/16唯一时间切，原词字面旗标6/15、切词1/16；原粤10仅2字面命中，不能用ASR来证明其他标签错误。TRAIN来源929、8音色、合成speed1.0/1.15。唯一compact中suffix cut15040，删掉能量约0.0565%，原bounds起点320sample，实际大能量集中0.9秒后。362固定这一来源与同切片各一次同ASR取tokens/time，不改变解码器、起点或gain。30秒，CTC时间不是音素边界真值；不自动改标签/训练/DEV/TEST/设备。完整goal active、progress、阻塞0。


### UX362结果与UX363 TRAIN声学截断合同计划 — 2026-10-03
362固定原词及suffix CTC仍均为完整目标，但你token时间0.84秒早于0.94秒切点，CTC不能作精确音素边界。保留该诊断与原数据/标签；新可选energy_cut.py按干净TRAIN参考累计能量40/50/60%切点，20ms渐变，1/99%能量core至少80ms、保留和移除均至少10%，不以房间底噪选择切点，不改变正词端点。363一次120秒：4个合同测试，929普通TRAIN+23 compact TRAIN及3旧audited captures，比较旧/新prefix及suffix声学能量并保存全部异常，不过滤失败、不参数搜索；仅source域，不重建后续channel/noise特征。旧脚本/素材/标签/weights保持，不能把声学合同作片段音素真值或现场误醒已修复。无ASR/KWS评分/训练/DEV音频/独立TEST/采放/云/USB/Flash；完整goal active、progress、阻塞0。


UX363四合同测试0.040秒通过；复用resolver的AST namespace漏json，进入manifest读取前exit1。未做source cut计算/训练/更改数据，原plan/源码/日志/进程保存。UX364仅注入json及阶段输出路径，所有cut常量/输入/算法保持，第一次完整source合同；无参数重试。


### UX362结果与UX364 TRAIN声学截断合同计划 — 2026-10-03
362固定原词及suffix CTC仍均为完整目标，但你token时间0.84秒早于0.94秒切点，CTC不能作精确音素边界。保留该诊断与原数据/标签；新可选energy_cut.py按干净TRAIN参考累计能量40/50/60%切点，20ms渐变，1/99%能量core至少80ms、保留和移除均至少10%，不以房间底噪选择切点，不改变正词端点。363一次120秒：4个合同测试，929普通TRAIN+23 compact TRAIN及3旧audited captures，比较旧/新prefix及suffix声学能量并保存全部异常，不过滤失败、不参数搜索；仅source域，不重建后续channel/noise特征。旧脚本/素材/标签/weights保持，不能把声学合同作片段音素真值或现场误醒已修复。无ASR/KWS评分/训练/DEV音频/独立TEST/采放/云/USB/Flash；完整goal active、progress、阻塞0。


### UX364合同结果与UX365固定修复片段检查计划 — 2026-10-03
364首次完整合同3.735秒通过955 TRAIN/5730切片，旧17声学失衡来自7父来源，其中8保留>90%、9保留<10%；这是固定40/50/60%诊断切点，不能将17直接称已存在的训练错误数。新切片零合同失败，972hash/4测试过。365只UX361可疑中TRAIN父词的.5新prefix/suffix各一次同ASR，cut19243；30秒，无分数/解码器选择。新suffix移除约53.63%参考能量，原数据/标签保留；若仍转写完整目标则停止输入替换，不自动重标。不ASR重跑完整词或其他原语料，不训练/DEV/独立TEST/采放/云/USB/Flash；全目标active、progress。


### UX365检查结果与UX366 TRAIN输入修复计划 — 2026-10-03
365固定两片段1.297秒同ASR：新prefix你好、新suffix哦小严，无完整目标旗标；原suffix仍完整目标。仅机器筛查、无音素真值和自动重标。366一次120秒WSL：重放23 compact TRAIN的原seed202609221/621增强，只保留>90%干净参考能量的旧.5负切片换成固定能量.5切；静音类旧负保持。预期9条suffix(15257..15265)。必须92160旧C前端值逐值同缓存、RNG流一致、全部正词及其他负词x/所有y/端点/元数据逐值不变。生成新的TRAIN及delta，原archive不改；冻结头/cache需另重建，不能把旧E/L/hidden用于新x。无fit/PTQ/模型评分/DEV/独立TEST/采放/云/USB/Flash；完整goal active、progress、阻塞0。


### UX366输入修复结果与UX367冻结缓存重建计划 — 2026-10-03
366 WSL12.852秒/包装13.375秒exit0，23 TRAIN/621原seed增强重放，9中suffix改x(15257..15265)，92160旧C前端值同原缓存；全部正词、其他负词、所有标签/端点/元数据原样，原archive保持。367一次120秒重建9条新x的两冻结模型heads及48维histories，旧/新各9全部C/F64核对，新17066 train及E/L/hidden仅9条可变，其余逐值保留，6146 anchors与1275连续上下文父源保持。因输入改变重算原模型TRAIN基线，并保留旧报告，不自动重标或删除/放宽质量门槛。无candidate评分/fit/DEV/独立TEST/采放/云/USB/Flash；成功后才另声明一次唯一fresh final6000，原TRAIN/DEV门槛保持、仅输入更正，无第二seed/fit/checkpoint/PTQ/阈值搜索。全目标active、progress。


### UX367缓存合同结果与UX368唯一输入更正训练计划 — 2026-10-03
367缓存14.911秒/包装17.672秒exit0，旧/新9条230400 C/F64值一致；只有9条x/E/L/hidden改变，全部其他输入/cache/标签/元数据/6146正锚点/1275连续上下文保持。原冻结模型负基线180→175仅因输入改变，不算模型效果提升。368唯一fresh原seed20261002306/final6000/CPU4/210秒，10129结构、四loss/margin/LR、192000基础+331+5456+2973draw精确、原512 TRAIN校准ID/方法及全部TRAIN/DEV门槛原样。仅使用366新TRAIN/367新cache，禁止旧heads复用新x；原353失败模型不重新部署。失败停止本配方/第二fit/PTQ/阈值/checkpoint/独立TEST/Flash；成功先TRAIN及730已知DEV，主机过关不等于板上48KiB/实时/三连续业务/1秒已证。完整goal active、progress、阻塞0。


### UX369更正输入的最终整数准入计划 — 2026-10-03
368唯一fresh6000/一次原512 TRAIN量化完成后，只此模型一次180秒原完整准入。新366 TRAIN/367 E/L/hidden；旧基线175不是180，positive baselines3542中/2520粤保持。4096随机/极值全层、旧15791全头，8原样本加9更正样本完整C/F64头、103观察及128自然完整C头/判定。辅助旧前热分诊断排除9改变输入，双列一致排除，仅诊断，不用于任何准入关卡。原负<=18、召回99%、extra和观察全保留/负零、自然128零、无新误/提前、P95<=96门槛不变。失败停止DEV/TEST/第二fit/PTQ/阈值/窗口与训练固件烧录。无DEV/独立TEST/采放/云/USB/Flash；主机通过不代替板上/业务三轮，完整goal active、progress。


### UX369检查脚本索引失败与UX370首次完整整数准入 — 2026-10-03
368唯一6000/一次512 PTQ159.954秒计算/164.422秒包装成功；没有第二模型。369全层4096 parity及首8行C头通过，在追加9更正行核对时错误地取data[j]而应为data[offset+j]，导致不同输入的头被比较；21.656秒exit1，未形成质量报告或读DEV。原源码/plan/log/parity/进程留证。370仅改该检查索引及输出stage，同368权重/输入/校准/规则/全部门槛，首次完整TRAIN整数关卡。不是数值失败后更改模型/容忍误差，不第二fit/PTQ/阈值或checkpoint搜索。完整goal active、progress。


### UX370准入结果与UX371一次已知DEV计划 — 2026-10-03
370完整TRAIN整数32.001秒全过：更正输入基线175→6，原中3540/3542及粤2520/2520保留，extra17/17、18/18；观察51负零/49正确保留/128自然零。1085440层值、181450头、2109797判定精确。371仅此368最终模型一次730已知DEV，120秒；原中44/45、粤32/33、连读至少15/16、负<=5、新误/提前零、P95<=96/max<=256ms及全部C/F64/真实silent prime检查不变。不DEV反传或参数选取；失败停止独立TEST/设备/第二fit/PTQ/阈值。全目标active、progress。


### UX360–371有限输入修复实验收尾 — 2026-10-03 07:33 CST
361固定31 ASR提示1个完整目标suffix，362 CTC早于切点不作音素真值。363 namespace漏json前停，364首次955 TRAIN/5730声学合同过，旧17诊断切片失衡来自7父源；不称17实际训练错标。365固定新半句为你好/哦小严。366原seed/23来源/621增强重放，9 suffix输入更正、92160旧C前端值同cache，所有正词/其他负词/标签/端点/元数据原样；367仅9条E/L/hidden重建，230400 C/F64精确。原冻结负基线180→175仅输入变化不算模型提升。368唯一fresh6000/一次512 TRAIN量化159.954/164.422秒，全部原draw及校准ID逐项保持。369追加C检查漏batch offset，21.656秒exit1，原材料留证；370只修索引，首次完整TRAIN32.001/33.984秒，原门槛全过175→6，中3540/3542粤2520/2520，extra17/17及18/18、观察49全留/51負零/128自然零，1085440层/181450头/2109797判定一致。371已知DEV730中44/45粤31/33、连读12/15、负4/636，粤与连读门槛失败，停止本配方；不第二fit/PTQ/阈值/checkpoint/独立TEST/训练构建/Flash。12实际进程terminal，3检查失败及材料保持。七USB读原72/off/off，1356/927140/4583与context/summary/light/volume/clip相同，USB释放。零新采放/云/Flash，2MiB上下文不缩减；1775声明输入/hash与534原非MD相同。docs/WAKE_CUT_TRAINING_REPORT.md记录当前质量拒绝；完整goal active、progress、阻塞0，未完成现场误醒/三轮业务/48KiB/1秒回复验收。


### UX372–374有限双语连读TRAIN覆盖计划 — 2026-10-03
371配方已停止：DEV粤31/33、连读12/15未过，不重复fit或门槛搜索。确认旧正常TRAIN中527/粤402带标点或carrier，compact仅23普通话/4声音，未有粤语compact补集。只用当前manifest全部后代均TRAIN的8已有合成身份，固定seed2026100373、两语×speed1.0/1.25共32新来源，原文你好小言/你好小賢，无carrier、不重合成；缓存模型及离线前端/原参考/源文件hash冻结。372六合同测试；373一次native RTX2060生成240秒/外270秒，374最多96固定ASR90秒（原词和能量.5各前后半句），全部失败保留。准入每语>=8、>=4声音、两speed各>=3，原词4字同音/语言/400–2048ms能量、半句无完整目标旗标；失败停止，不放宽或重复生成。仅TRAIN素材准备，不可宣称模型效果/真人发音/泛化；成功后另立C前端/cache/parity及唯一新覆盖训练计划。无DEV/TEST波形、KWS训练/评分、设备采放/云/USB/Flash，原固件72 wake/voice off及2MiB上下文保持，完整goal active、progress、阻塞0。


### UX372–374来源覆盖结果与UX375收尾计划 — 2026-10-03
158输入hash/26缓存模型资产通过；六项合同0.072秒。373一次32来源RTX2060合成80.594秒/包装92.656秒，GPU峰值2042024448B。374首次96固定离线ASR5.093秒/包装10.578秒，普通话11/16通过（7声音，两speed4/7），粤语3/16（3声音，两speed2/1）未过预定8/4/3准入。18失败完整保留；原因14四字发音转写不确定、4能量区间，半句完整目标旗标0/64。ASR只作机器筛查，不把小燕/多字转写当发音真值。停止本配方扩充与训练，不放宽门槛、重合成或烧录。375仅七只读USB及hash/AST/原534非MD/封存检查；原2MiB上下文/固件/历史保持。原宽目标仍active、progress，误唤醒没有修好；待另提出粤语可靠语料/有限归因方案。


### UX372–375有限来源检查收尾 — 2026-10-03
32原PCM/hash、158输入/26模型资产、534原非MD保持；六合同通过，96转写后中11/16粤3/16，18失败与全部音频保留，粤未过预定素材准入。停止扩充/训练，不门槛/模型/ASR搜索或烧录。七新鲜USB读原72/off/off、1356/927140/4583，context/summary/light/volume/clip同371并释放USB，2MiB上下文不改。docs/WAKE_COMPACT_SOURCE_REPORT.md明确素材失败和现场误唤醒未修复；完整goal active、本轮progress、阻塞0。


### UX375 Windows换行验证失败与UX376仅验证修复计划 — 2026-10-03
375原32PCM/158输入/534非MD/设备查询与封存检查过，最后git diff --check将Windows写出的CRLF逐行当作尾部空格，验证exit1；完整失败脚本/日志/375 archive留存不改。376仅采用git -c core.whitespace=cr-at-eol diff --check，保留其他空白检查，对旧archive逐项hash与当前原源码/新文档分别核验，再独立封存。不会重合成/解码/训练/改质量门槛、USB采放/云/Flash；现场误唤醒和目标仍未完成。


### UX376封存验核结果 — 2026-10-03
仅CRLF验证调用修复；原375 archive69文件/逐项hash精确、32 raw文件及158输入/534非MD保持，先前6实际进程全部terminal，375 verifier exit1作为失败留证，其余0。不重合成/ASR/fit/评分/采放/云/USB/Flash，原新鲜72/off/off与2MiB历史保持，数据/模型准入仍失败。已更新来源报告，独立376封存新验证及文档；完整goal active，progress、阻塞0。


### UX377有限粤语参考与裁剪来源归因计划 — 2026-10-03
上一轮进展为32素材试生成失败/停止，无live handle或真实阻塞。纠正覆盖推断：402粤语TRAIN都由你好小賢。今日天氣好好。原文裁成wake词，needs_alignment=False，carrier=True仍指来源，compact_positive缺失不代表没有短词。377只现8全部后代TRAIN参考＋每身份按clip_id固定前2裁剪词共24输入，当前hash/16kPCM/crop长度/身份固定；原SenseVoice INT8 CPU4 auto ITN加500ms零尾每条一次，120秒/外145秒，不选转写、改gain/生成或听DEV/TEST。参考脚本规范化后字符差异仅旗标，不自动改prompt；裁词按原alias/energy作不确定旗标，不改label。无TTS/KWS评分/训练/USB采放/云/Flash，376拒绝状态与原72/off/off和2MiB数据保持，全目标active、progress、阻塞0。


### UX377来源结果与UX378实际粤语半句归因计划 — 2026-10-03
377首次24输入2.625秒/包装2.828秒，参考5/8无字符旗标、3处差异；裁词9/16符合原alias，7不确定（2语言），不改标签或prompt。carrier标志不是当前输入包含后续长句的证明。UX364另有普通TRAIN粤am_michael070 .5prefix保留90.7156%参考能量；paired祖源证实variant0/.5与variant1/.57022755均作为负例入库，尚未复现增强x，不计已证明错标数量。378固定这一父词/两个实际旧prefix/新energy.5prefix最多4unique decode，45秒，先核对366当前TRAIN索引label/variant与来源PCM/hash，原ASRCPU4 auto ITN零尾，仅筛查不自动重标。无重放增强/训练/KWS评分/DEV/TEST/TTS/USB采放/云/Flash；不重复377或373。本轮progress，完整goal active，误醒未修复。


### UX378结果与UX379有限基频特征合同计划 — 2026-10-03
378实际TRAIN13636/13637均paired负例；一次4转写1.453秒/包装1.641秒，原词你好小言、两旧prefix与新energy prefix均你好，不是完整词负标签证据。不按90%能量去自动重标，停止该半句修复方向。近音混淆仍待修复，另原创新增可选C11基频/周期性prototype，不是唤醒分类器，不入正常构建。固定8k/48ms窗口/16ms步/80–800Hz/CMND.15，6类实际信号合同比较与ASan/UBSan、一次C3对象：state<=800、自己的text<=3072、编译器每函数stack<=192/dataBSS0/无heap。120秒有限；不改算法门槛选结果，不训练/ASR/TTS/DEV/TEST/USB/采放/云/Flash，原所有模型质量/业务48KiB与三轮指标保持。该特征改善误醒只是待验证假设，不能从signal/object宣布设备效果，全目标active、progress、阻塞0。


### UX379信号通过/WSL日志包装失败与UX380仅对象预算计划 — 2026-10-03
379 C11 Werror/ASanUBSan构建与信号进程均exit0；630稳态sine/256随机frames、DC/Nyquist/impulse/step/chirp/reset通过，state770、最大Hz误3.1875符合冻结逐频2%/2Hz。WSL UTF16代理warning与C UTF8 stdout被旧run合并，JSON包装exit1，尚未执行C3编译；完整原脚本/日志/binary留证。380只取唯一成功C JSON记录，不重建或重复信号测试、不改源/门槛，首次C3对象同379 text<=3072/每函数stack<=192/dataBSS0/无heap，stdout/stderr分开保存。90秒，仅对象不执行C3，其他scope不变，不能作唤醒改善或固件效果证明，全目标active、progress。


### UX377–380结果与UX381只读收尾计划 — 2026-10-03
377/378诊断均一次终止；旧402粤语输入已是裁词，不能按carrier/compact_positive字段推断缺短句；378两个旧负prefix只为你好，不修label。379信号构建/执行均0，WSL混编码JSON包装1保留；380仅解析成功记录并首次完成C3对象，原算法/测试/门槛不动。基频原型sizeof770、自己text592、dataBSS0、编译器stack0/80、无heap；630稳态频率及256随机噪声、DC/冲激/step/chirp/reset过，但不是板上实时或KWS质量。381只七USB/原534非MD/冻结输入/AST/CRLF文档与zip，60秒；不再运行信号/ASR/训练或烧录，2MiB上下文原样；完整goal active、本轮progress、阻塞0。


### UX377–380结果与UX381只读收尾计划 — 2026-10-03
377/378诊断均一次终止；旧402粤语输入已是裁词，不能按carrier/compact_positive字段推断缺短句；378两个旧负prefix只为你好，不修label。379信号构建/执行均0，WSL混编码JSON包装1保留；380仅解析成功记录并首次完成C3对象，原算法/测试/门槛不动。基频原型sizeof770、自己text592、dataBSS0、编译器stack0/80、无heap；630稳态频率及256随机噪声、DC/冲激/step/chirp/reset过，但不是板上实时或KWS质量。381只七USB/原534非MD/冻结输入/AST/CRLF文档与zip，60秒；不再运行信号/ASR/训练或烧录，2MiB上下文原样；完整goal active、本轮progress、阻塞0。


### UX377–381诊断及特征合同收尾 — 2026-10-03
402粤语原输入已裁wake词，覆盖推断纠正；24参考/裁词只作机器旗标，无重标。378实际两prefix虽保留90.7%能量仍仅你好，停止自动数据替换方向。379/380 C11基频源/630稳态及256噪声/DC/chirp/step/reset与ASanUBSan过；C3自己text592/state770/stack80/dataBSS0/无heap，只对象预算，不是实际耗时或模型效果。379混编码包装exit1原样保留，不重跑信号，380首次对象检查过。七新鲜USB读原72/off/off、1356/927140/4583与context/summary/light/volume/clip保持/释放USB；534原非MD保持，2MiB不减；零TTS新生成/fit/DEV或TEST评测/采放/云/Flash。docs/WAKE_PITCH_FEATURE_REPORT.md保存证据界限；当前误醒仍未修好，全目标active、progress、阻塞0。


### UX382有限板上基频预算计划 — 2026-10-03
固定从UX332隔离源码加C11基频；不进判别器，事件/动作抑制。一次512数学PCM原全trace及基频精确对齐、60s麦克风；app1540096、原workspace14232+最多800、32ms/P99≤16ms、SDK48KiB/最大块24KiB不变。guard鲜4MiB/app-only/非app保持，finally完整恢复原72/off/off/context；无云/存录音/音频播放/DEVTEST/训练，不以资源证明误醒修复。180秒测量上限；全部源冻结，不改正常构建。全目标active。


### UX382结果与UX383–384针对性实时优化 — 2026-10-03
382 math512原及pitch全值一致/maxpair18966us；mic1859块max16963/P99上界17000>16000故停止，原72完整4MiB恢复、USB释放、全部历史保持。383仅把严格界定256*4095^2=4292870400内的差分累计由64改32，累计归一化/分子仍64；2048极限/随机/周期输入全部状态及feature与旧64同、ASanUBSan过，Python ABI/块划分/重置/错误事务2项过。384一次修改后隔离C3：同512gold/60s资源与全部门槛保持，不是重复选最好结果；特征不进模型/动作抑制，不训练/读取独立TEST，guard备份及finally全Flash恢复。首次失败记录保留；全目标active。


### UX382–386板上合同及来源收尾 — 2026-10-03
第一次micP99 17000失败，原记录/备份保持。等值32累计2048 ASan/state/value同后，一次改版512math全315904值同/maxpair16945us；mic1858/max14997/P99≤15000、SDKmin65284/最大块61440/noDMA、78.125s全资源过；仅未训练/事件抑制前端预算，不是误醒或业务48KiB证明。C3workspace15008(+776)/app1509936(+816)，分区不改。385固定32TRAIN(各语言16/8身份)327680旧40维值/y/end同，新16384基频独立存/2.125s；没有新TTS/ASR/fit/DEVTEST或重标，反例/增强/全TRAIN仍待补齐。两版各guard备份安装恢复，总4次app-only，最后完整4MiB精确恢复原72/off/off/1356/927140/4583及摘要灯volume clip；534原非MD保持，USB释放。docs/WAKE_PITCH_RESOURCE_REPORT.md；全目标active、本轮progress/阻塞0，当前误醒尚未修复。


### UX387批处理合同及UX388完整基础TRAIN特征计划 — 2026-10-03
主机C批处理仅合并调用，无DSP参数变更；512独立logmel20480/pitch1024值同、反向输入/重置/非法输入2合同过。388预先冻结所有TRAIN源/WAV/metadata/hash，一次300秒重建15791基础TRAIN：原main1600×4及设备/测得通道/配对半句/完整句/近词/弱声/compact/194/198增强。每row必须原C40维/y/end/组别/domain/键唯一全同，仅新增pitch数组，不改旧样本或略过失败；仅源metadata用于排除DEVTEST，禁止读其波形。9已修compact按原修复再现，其他负label不改。另1275continuous行后续对齐，不能本次称全17066就绪。未训练/模型评分/云/USB/Flash，2MiB/context/原固件保持，完整目标active。


### UX388完成与UX389连续TRAIN基频对齐计划 — 2026-10-03
388首次15791基础TRAIN/161699840旧C输入逐值同，8084992新增基频值、77.377秒，无跳过或原数据变更。389一次180秒：72设备+31原eligible公开采集+128自然TRAIN父流，保留128帧零PCM前缀及原尾部、原C40维全流逐值同，再按原坐标切1275窗口，不重置历史。只新增17066基频及父流/窗口来源，标签、6146锚点与冻结cache不动；无fit/DEVTEST波形/采放/云/USB/Flash。误醒仍未修复，完整目标active。


### UX388完成与UX390连续TRAIN基频对齐计划 — 2026-10-03
388首次15791基础TRAIN/161699840旧C输入逐值同，8084992新增基频值、77.377秒，无跳过或原数据变更。389一次180秒：72设备+31原eligible公开采集+128自然TRAIN父流，保留128帧零PCM前缀及原尾部、原C40维全流逐值同，再按原坐标切1275窗口，不重置历史。只新增17066基频及父流/窗口来源，标签、6146锚点与冻结cache不动；无fit/DEVTEST波形/采放/云/USB/Flash。误醒仍未修复，完整目标active。


### UX388完成与UX391连续TRAIN基频对齐计划 — 2026-10-03
388首次15791基础TRAIN/161699840旧C输入逐值同，8084992新增基频值、77.377秒，无跳过或原数据变更。389一次180秒：72设备+31原eligible公开采集+128自然TRAIN父流，保留128帧零PCM前缀及原尾部、原C40维全流逐值同，再按原坐标切1275窗口，不重置历史。只新增17066基频及父流/窗口来源，标签、6146锚点与冻结cache不动；无fit/DEVTEST波形/采放/云/USB/Flash。误醒仍未修复，完整目标active。


### UX389–391完整特征结果与UX392r1新拓扑合同计划 — 2026-10-03
389仅元数据guard错在父流前停；39032流/96窗口同后重复解压175MBnpZ触发180秒终止。391仅一次缓存原数组，6.249秒完成231连续流/1275窗口，7083920父流与12693520窗口旧C值逐值同；合计17066/8737792基频值，原输入标签不变，失败留证。392注册时错误指定不存在的heads.npy，在编译/执行前停，原prepare/probe留证。392r1固定八条未改变输入，使用原328逐帧heads核对（排除9条已更正行，无质量样本排除）；新可选90输入=raw40+E/L48+pitch2；固定F0每8Hz一单位/周期性1/128，四舍五入、INT8正范围，不拟合或改判定阈值。仅 stem 多144权重、10273参数；旧88默认保持专属validator。一次150秒全层/状态/历史/编码与旧88回归，成功后再实机预算及唯一训练；无DEVTEST或烧录，现场误醒仍未修复，目标active。


### UX389–391完整特征结果与UX392r2新拓扑合同计划 — 2026-10-03
389仅元数据guard错在父流前停；39032流/96窗口同后重复解压175MBnpZ触发180秒终止。391仅一次缓存原数组，6.249秒完成231连续流/1275窗口，7083920父流与12693520窗口旧C值逐值同；合计17066/8737792基频值，原输入标签不变，失败留证。392注册时错误指定不存在的heads.npy，在编译/执行前停，原prepare/probe留证。392r1三项Python合同过，复制runner到stage后ROOT层级错，C构建前停；392r2只修ROOT，复用已过Python合同不重跑，固定八条未改变输入，使用原328逐帧heads核对（排除9条已更正行，无质量样本排除）；新可选90输入=raw40+E/L48+pitch2；固定F0每8Hz一单位/周期性1/128，四舍五入、INT8正范围，不拟合或改判定阈值。仅 stem 多144权重、10273参数；旧88默认保持专属validator。一次150秒全层/状态/历史/编码与旧88回归，成功后再实机预算及唯一训练；无DEVTEST或烧录，现场误醒仍未修复，目标active。


### UX392–393合同结果与UX39490输入实机预算计划 — 2026-10-03
392r1三Python合同过，生成脚本ROOT错在C前停；r2新90全部层/分块/ASan/八TRAIN/512PCM/prime与clock过，收尾同331旧脚本把过滤502比完整534而exit1。393不重跑90数值，另65536编码值同、旧88 ASan/512PCM314880金值全同、完整534非MD同，1.828秒；所有失败状态保留。394一次未训练90资源固件，已验证前端真正输入判别器但禁止事件动作：固定512math及60s实机，旧32ms/P99≤16ms/SDK48KiB/24KiB最大块/app1540096/state15032门槛保持；fresh4MiB/app-only/读回/非app守护及finally原72完整恢复。无云/存录音/音频播放/训练/DEVTEST，不据资源称误醒修复，context2MiB不变，目标active。


UX394首次C3构建74.500秒/exit2，probe C再次含Python布尔字面量，在模型编译时停，零USB/烧录/模型运行。r1仅复用392r2已编译的同一probe C并改导出符号；数学/输入/门槛不变。initial源/plan/manifest和完整build日志留存，只首次完成镜像与设备资源检查。


### UX394资源结果与UX395唯一新基频训练计划 — 2026-10-03
90资源/非零周期64及512math和60s实机通过并完整恢复原Flash后才执行。395一次fresh原seed20261002306/final6000/CPU4/210秒，原四loss/margin/LR、192000基础+331+5456+2973draw、6146正锚点/所有窗口上下文/512 TRAIN校准ID及全部质量门槛保持；唯一新增固定pitch2，10273参数。原368失败模型不再评测或部署；不改阈值或用DEV择优。只有TRAIN、730已知DEV原门槛都过才继续独立测试与生产模型实机；失败停止本配方，不第二fit/PTQ/seed/checkpoint搜索。设备2MiB上下文与旧原模型保持，完整目标active。


### UX395训练结果与UX396最终整数TRAIN检查计划 — 2026-10-03
395一次final6000及原512 TRAIN校准完成，所有原draw/覆盖保持，无第二模型。396一次180秒原完整准入，仅新增pitch2：4096随机极值全层C/F64、15791全头、8原+9更正完整C头、103观察和128自然全流/原判定。原负<=18、双语原正确99%、extra/观察全保留、实际负零、自然128零、无新误/提前、P95≤96ms门槛不变。失败停止DEV/TEST/第二fit/PTQ/阈值或训练模型烧录；context2MiB保持、原72已恢复，完整目标active，资源通过不作识别效果证据。


UX396新395模型4096最终全层C/F64核对通过；基础TRAIN尾批47行的pitch缓存错误取64行，质量报告前exit1，原plan/source/log/完整parity保持。UX397只将pitch尾切片与raw/hidden相同终点，复用已过parity和同一编译库，不重跑4096数值、不重训/重新校准或改变任何模型/数据/质量关卡；首次完整质量结果，全部失败留证。


### UX397质量准入结果与UX398固定DEV特征计划 — 2026-10-03
397首次完整26.632秒，原负175→8、原正确中3541/3542及粤2520/2520保留；额外17/17、18/18，观察51负零/49正确全保留，自然128零，原关卡全过。96%减少属于TRAIN窗口，不是现场每小时误醒。398一次60秒原730已知DEV输入基频，全部原C40/y/端点/组别同，不省略失败、不读TRAIN/TEST波形或做模型评分/新训练；成功后仅395候选一次原DEV检查，失败停止后续部署，阈值与门槛保持，目标active。


### UX397 TRAIN结果与UX399一次已知DEV计划 — 2026-10-03
397完整TRAIN整数检查26.632秒通过：原负175→8；原中3541/3542、粤2520/2520保留，extra17/17与18/18、观察49/49保留，观察51负与128自然父流零触发。399仅此395最终90输入模型一次730已知DEV，120秒；原中44/45、粤32/33、连读至少15/16、负<=5、新误/提前零、P95<=96/max<=256ms及全部C/F64/真实silent prime检查不变。398全部DEV波形重建7475200旧C值精确，标签及坐标未改，无选择。不DEV反传或参数选取；失败停止该候选独立TEST/设备/第二fit/PTQ/阈值。全目标active。


### UX387–402有限基频训练结论 — 2026-10-03T03:31:54.380154+00:00
完整17066TRAIN与730DEV波形/C原输入、标签及连续上下文对齐；新可选C11 90输入、9984权重/10273参数。392/393主机整数及旧88回归通过，394仅未训练资源探针：app1510240/state15016，512math和64非零周期精确，mic最大15359/P99≤15500µs、SDKmin63532/最大块61440、无DMA，原完整4MiB恢复。395仅一次final6000/512 TRAIN PTQ，162.770秒。397 TRAIN旧误175→8/95.43%，中3541/3542粤2520/2520保留，51已知负零/49正确全保留/128自然零，通过；不作为现场率。398原730 DEV输入全同。399评分前目录guard1保留，r1仅包装修正；8.058秒全C/F64/判定同、质量失败：负5→0，中43/45<44、粤31/33<32、短中15/15保留、延迟和无新增过。400只读四漏集中zm_yunxia/分数不足或支持撤回，不搜索阈值/改label/训练同一DEV来源；立即停止该配方独立TEST/新训练固件/第二fit/PTQ。401七查询+close后不存在clip_id断言1保留；402复用现有状态、核对实际片段字段，原72/off/off/1356/927140/4583/2MiBctx和摘要灯volume片段状态、534非MD全同，USB已释放。新模型未部署，现场误醒未修复，全部原错误留证；完整goal active，本轮progress/阻塞0。docs/WAKE_PITCH_TRAIN_REPORT.md。


### UX403–404诊断与UX405固定前端合同计划 — 2026-10-03
403/404仅TRAIN缓存读数：6097原正确，429旧设备窗口触发前半秒389无基频，改看标注声尾仍388无；277/429整窗无基频，不能只归因句尾静音。原395配方继续停止，不按四DEV换阈值/规则或重训。405仅可选纯C两级1200Hz低通(betaQ15=12313、Q1状态/32位有界运算)，随后原YIN门槛/.15/能量/lag全同；原logmel/缓存/标签/源码默认不动。一次45秒4096极值独立64参考、630频率/噪声/reset/无heap合同；失败停止该系数，不扫滤波器。过后仅小型固定TRAIN源/设备对比，改善特征可靠性才能板上预算/新训练；无DEVTEST/云/USB/Flash，2MiB上下文保留，goal active。


### UX405合同及UX406固定44TRAIN对比 — 2026-10-03
405首次仅seed字面量溢出编译失败/零运行，原源/日志保留；r1显式uint32取模保持同PRNG，滤波器/门槛不动。4096极值1048576滤波值独立64参考同，630稳态/256噪声/DC/Nyquist/reset/ASan/UBSan全过，state780比原770多10B；仅原型。406固定所有原44TRAIN录音及其源，无筛选/新录音；原40/y/end/原pitch逐值同，仅pitch链加LP；源按旧lag对齐非F0真值。一次45秒，正词声尾voicing须严格增加、可比≥64且参考±20%≥90%、干净源保留≥98%；失败停止系数，不扫cutoff或新训练。无模型分数/DEVTEST/云/USB/Flash，完整goal active。


### UX403–408设备周期证据结果 — 2026-10-03T04:03:07.790823+00:00
403原正确6097基础TRAIN：429设备触发前半秒389无F0；404改标注声尾仍388无，277整窗无，不能只归句尾。405仅新增可选两级固定1200Hz LP、Q1/INT32/state780B，原YIN/logmel不动；首次seed字面量编译1/零运行留证，显式mod r1同PRNG/同滤波，4096帧1048576值及完整state独立64参考/630音高/256噪声/DC/Nyquist/reset/ASanUBSan通过。406首次复现尾噪声与原RNG不符、在质量前停；r1仅复现原TRAIN背景抽样与8variant推进，450560原40及22528原pitch/y/end同。固定22正/22负2.316秒，正声尾周期8→7、源可比48<64，两项失败，源44/48同频/clean1215/1216保留。立即停止此滤波，不另cutoff/阈值/全特征/板上/训练。407只原方差：正声尾704/704过能量，696无可靠周期，中位RMS468.61>约64，不据此降门槛。408七只读原72/off/off/1356/927140/4583/2MiB与摘要灯volume/片段状态、534非MD全同；USB释放。本轮无Flash/实时采放/云/新模型分数/训练/DEVTEST，原395仍拒绝，误醒未修复；全目标active，progress/阻塞0。docs/WAKE_PITCH_INPUT_REPORT.md。


### UX409弱周期候选固定诊断 — 2026-10-03
承接408已证尾部能量足/LP无益；只原44TRAIN录音/干净源，原RNG重建和40/y/end/原pitch全同。一次45秒NumPy独立CMND1..101及原阈值首谷解码精确；对原无可靠基频帧仅固定10..100全局最小、并列最短、原抛物线Q8，输出候选频率和0..4096连续置信值，不改原voiced/唤醒/阈值，不扫描或纠正倍频。正源可比≥64且±20%≥90%才考虑C/全特征/训练；失败停止这一定义，不选择另一峰/阈值。无模型/DEVTEST/USB/Flash/新采放，完整目标active。


### UX409失败及UX410独立周期强度数值接口 — 2026-10-03
409唯一原44TRAIN2.785秒，原40/y/end/pitch全同、独立NumPy解码45056值精确；620弱正可比仅377/60.81%同频(中201/346，粤176/274)，低于90%，原全局最小弱频率表示立即停止，不能修正倍频/阈值或训练这版。另与源一致/不一致周期强度中位2108/851，仅观察非选线。410新可选语义：原可靠频率/强度全部原样，无可靠频率永远0，只保留连续minCMND强度；warm/能量无效全0，state770/无heap、不改原模块。仅一次45秒独立4096/630音高/固定SNR/reset数值接口；不是质量准入，后续需单独TRAIN信号对比才可预算或新训练。无weak频率/置信阈值/模型/DEVTEST/USB/Flash，2MiBctx/全goal保持active。


### UX410数值结果及UX411周期强度TRAIN准入 — 2026-10-03
410仅1.55秒数值合同通过：独立NumPy16384值、630原音高、ASanUBSan、state770B，无可靠频率永远0；固定SNR20/0/-10强度中位4075/2833/1042.5。不是唤醒质量或C3证明。411另冻结原44TRAIN录音/22正和同一原噪声注入，原lag/crop转移来源语音边界，完整48ms窗、背景距语音100ms、warm至少3帧，不借分类器定mask。一次45秒C/独立缓存全同；至少90%正片段语音强度中位高于背景、汇总中位差至少512Q12才可资源原型。负词仅观察，周期强度不是唤醒/voiced判断。失败停止此强度特征、不得改mask/cutoff/阈值/筛片。无DEVTEST/训练/新语料/USB/Flash，完整goal保持active。


### UX411结果及UX412有限C3资源门槛 — 2026-10-03
411首次导入语法错误，r1包装自hash重命名文件未落盘错误，二者质量未运行，原源/终止码保留。r2只语法/文件落盘，mask/gates/RNG/数据全同，0.634秒首次质量过：22正20片段说话强度高于背景，中10/12粤10/10；汇总Q12 1028对187差841。22负18也高，绝不作为关键词能力。22528 C/独立值全同，原可靠F0/强度保持、未知F0=0、state770。412只隔离未训练90资源固件，同512完整层/64音高/60秒mic，workspace≤15032/app≤1540096/max32ms/P99≤16ms/SDKmin48KiB/最大块24KiB不放宽。新的强度真实输入辅助网络、probe false禁动作；fresh4MiB/app-only/读回/非app及finally原72全Flash恢复。不训练/DEVTEST/云/播放录音，2MiBctx和完整goalactive。


### UX413完整连续周期強度TRAIN特征计划 — 2026-10-03
412资源通过且原Flash完全恢复后，才一次420秒原15791PCM recipe及231连续父流/1275窗口复现；原40/y/end/metadata全部相同，不跳/换/改label。只另存trustedF0+continuous strength数组，未知F0恒0，所有原可靠F0/强度与391缓存精确，原默认后端和缓存不动。新checkpoint topology/encoding独立且拒绝395，UNIT编码相同；模型10273参数。失败立即停止，不全量训练或筛样。本阶段无DEVTEST语音/云/USB/Flash，goalactive，完整2MiBctx保持。


### UX414一次连续周期强度分类器训练 — 2026-10-03
以412实机资源通过/完整恢复、413完整17066输入及231连续父流对齐为前提。独立topology/encoding，只连续strength新增、未知F0零；395失败配方仍停止。一次fresh原seed20261002306/final6000/CPU4/210秒；原四loss/6146anchors/所有draw/512 TRAIN PTQ不动。TRAIN与原730DEV全门槛通过才读独立TEST/烧录；失败停止不第二fit/PTQ/阈值。资源或TRAIN改善不得宣称现场误醒修复。无USB/Flash/新音频/云，goal保持active。


### UX415新强度模型原TRAIN准入 — 2026-10-03
仅414固定训练终止成功后一次180秒，新权重4096全层C/F64/torch整数精确，不复用396旧模型parity；预计算新strength2输入、所有基础15791/103观察/128自然流、原规则/原全部门槛保持。首次已修正尾批pitch切片边界，与raw/hidden同47行，不等失败再改。基础负<=18、双语原正确99%、extra/观察全保留、实际负零、自然零、无新误/提前、P95<=96ms。失败停止DEV/TEST和任何第二fit/PTQ/阈值，不烧录训练模型。不是实际PCM事件/现场误醒率；完整goalactive。


### UX416原730DEV周期强度输入对齐 — 2026-10-03
仅415原TRAIN全门槛通过才执行一次60秒全部730已知DEV PCM重建；旧raw40/y/end/group逐值同，不择片、不改类别。只独立新strength缓存，原399失败模型仍停止，原门槛和划分保留。无TEST语音/新训练/USB/Flash/网络。


### UX417一次730已知DEV强度模型完整C质量检查 — 2026-10-03
仅415 TRAIN准入/416旧输入精确后；只414最终模型一次120秒全部730 DEV，不改原中≥44/45、粤≥32/33、连读≥15/16、负<=5、新误/提前零、P95<=96/max<=256ms。全部C/F64/八整层/原cache/判定与silent-prime核对；topology/encoding严格新语义。开发集已知并非盲测。失败停止独立TEST/训练模型烧录/第二fit/PTQ/阈值选择。没有再次用395模型评分，完整goalactive。


### UX409–419有限周期强度结论 — 2026-10-03T04:57:18.696149+00:00
409全局最小弱F0620帧仅377/60.81%同频拒绝，不补倍频。410独立强度接口可靠F0/强度保持、未知F0恒0/state770B，16384C/NumPy值/630音高/SNR/ASan过。411导入语法1/r1包装自hash1均质量前停，r2只bootstrap修正，原44TRAIN/22正20语音强度高于背景、汇总1028-187=841，负22中18同样高；仅特征准入。412未训练90真实新强度C3资源：app1510336/workspace15016/512最长17480us/mic最大15368/P99<=15500us/SDKmin65036/最大块61440/0DMA，全部数值过，fresh4MiB/app-only/非app/读回及finally原72全Flash170622...精确恢复。413完整17066/231父流/1275窗口原40标签坐标同、F0强度可靠全同，82.880秒。414唯一final6000/seed20261002306/原loss/draw/512 TRAIN PTQ，166.970秒，无第二fit。415原TRAIN门槛过负175→8/中3541of3542粤2520of2520/观察正确49全/51负零/自然128零及1085440C/F64全层同。416全部730DEV7475200旧C值同。417唯一8.108秒C全同：负5→3、中44of45粤32of33过，连读14of原15失败；立即停止该配方TEST/生产烧录/第二fit/PTQ/阈值。418仅已有分数：3残余误均近似词，3漏同留出声音/两短高后撤回+一支持不足，不改变规则。419七只读原72/off/off/1356/927140/4583/2MiB/204800与摘要灯音量片段状态、534原非MD全同，USB释放。新增源可选，默认不改变，现场误醒/1秒回复/3轮业务未完成；全goalactive，progress不是wait，blocked审计0。docs/WAKE_PERIODICITY_REPORT.md。


### UX421 TRAIN词尾类别标注合同 — 2026-10-03
上一414配方保持停止。新独立假设：电脑训练时在倒数24维增加8种完整短语分类监督，导出删除辅助头，端侧图/参数/RAM不增加。先一次120秒复现15791原TRAIN PCM并对413全部SHA；复制原RNG状态转移源能量结束边界，不消费原RNG、不改标签/来源。完整正词及796近词从原high至+160ms标注，碎片/环境/连续窗口忽略，八类每语种至少16行；失败不fit或按结果换边界。无DEVTEST音频/云/USB/Flash，完整目标仍active。


### UX421r1 修正标注边界断言 — 2026-10-03
UX421第41行因代码额外假定所有原片段尾部留满160ms而exit1；尚未出标注/训练/评分。仅改unsupported高界/至少10帧断言；原registered门槛、布尔mask公式(high..high+2560与既有256帧相交)、源位置/标签/RNG/PCM均不动，不另选窗口或跳样。原错误源码/过程保留，一次r1完成标注合同后才考虑训练。


### UX422一次TRAIN辅助词尾分类训练 — 2026-10-03
421r1全部15791 PCM/标签/来源SHA精确，6295正词+796近词/70559有效帧、8类双语覆盖通过。辅助分类数值/零梯度/共享backbone/同输出图/4个metadata拒绝测试exit0。新目标在倒数24维加入训练专用24→8(200参数)逐帧CE，按当前batch各类均值平衡，权重1，导出彻底删除，不改变C图9984权重/15016B工作区。一次fresh原seed/final6000/CPU4/270秒，其余四loss/全部draw/512 TRAIN PTQ/判定和质量门槛保持；不是重复414配方。失败即停，不进行第二fit/PTQ/阈值/规则扫描或导入DEV来源。完整目标active、默认固件/2MiB上下文不动。


### UX423 新词尾辅助模型原TRAIN/C整数准入 — 2026-10-03
422唯一final6000及一次512 TRAIN PTQ终止后，才一次180秒新9984权重12层C/F64/Torch整数一致和全部原TRAIN质量准入。导出辅助头为false严格检查；旧判定/所有原质量及时间门槛保持。原175个负窗目标<=18、双语保留99%、额外/观察全保留、观察负及128自然连续父流零；失败停止DEV/TEST/固件及第二fit/PTQ/阈值。预计算输入非现场每小时误醒。


### UX426 一次19个失败TRAIN浮点/整数诊断 — 2026-10-03
425复用分数定位18负(14近词/2自然/2碎片)、1额外粤语正例；在任何新方案前，只对这19行冻结422浮点backbone及训练专用头做一次推理，与423已存整数C结果、同268/原pending-owner规则比较。60秒，不选阈值/量化/权重/种子/DEV或TEST，不把辅助头变成新部署分类器，也不部署失败422模型。


### UX420–427 词尾监督阶段结果及关闭 — 2026-10-03
421r1原15791 PCM hash/所有标签精确，6295正词+796近词/70559帧；初421多余尾部空间断言失败/源已归档，只修断言不改mask/RNG/原门槛。422仅一次6000 fresh训练174.086秒/512 TRAIN PTQ、辅助头导出删除。423新整数1085440全层、181450头、2109797判定值精确，但负175→18、额外粤语18→17、观察负51仍1，因此候选停止，DEV脚本未执行、TEST未读，不第二fit/PTQ/参数/规则选择、不烧录。425残误18=14近词+2自然+2碎片；426仅19失败TRAIN诊断：浮点15负触发/整数18，同粤语正例float435/491/153通过而int307/323/-38撤销，量化参与漏例；辅助头把真小言认小燕，不能直接部署。424原72实际60.219秒无外部播放/voiceoff，0触发/0DMA新增，短时环境未经安静确认不能宣称修复；无云/新应用Flash。427七条新USB原72/off/off空闲、1356事件/927140B/next4583、2MiB上下文/204800预算、摘要/灯/音量/片段状态及534原非MD(含32引用)保持、USB释放。详见docs/WAKE_PHRASE_CONTRAST_REPORT.md；完整目标保持active，这轮有监督合同及量化诊断进展，未complete。


### UX428 C精确90维整数训练合同 — 2026-10-03
上轮取得标注及量化归因进展，非verified wait/无进展，阻塞计数0。新独立路线将固定偏置/shift、9984整数权重以及每层取整/限幅直接放进训练前向，原422浮点辅助配方仍停止；复用旧纯整数Trainable模块，不引入端侧运行库。先一次120秒初始及模拟权重更新各4096完整层FP32/F64/C精确、全63shift/前缀尾部掩码/梯度/metadata护栏和CPU35×382成本合同；只使用源422模型确定整数尺度，不训练、不PTQ或读DEVTEST、不USBFlash。失败不开始QAT拟合；完整目标保持active。


### UX429 一次固定整数尺度的声学拟合 — 2026-10-03
428初始/模拟更新2170880全层FP32/F64/C精确、63shift/掩码/梯度/护栏通过；批35×382实测69.24ms中位、6000步估415.45秒。新独立训练目标直接优化每层取整/限幅后的9984 C整数系数，初始422整数权重只作来源，原浮点辅助配方仍停止；无辅助CE，偏置/shift/norm固定、不二次PTQ。一次原seed20261002306/final6000/CPU4/540秒，单位权重LR.01→.001/cosine、weightdecay0固定；原四loss/所有数据/抽样draw/6146anchors/质量门槛不改。TRAIN/DEV/TEST任何失败停止，不第二QAT拟合/尺度/阈值/模型选择。现在无USBFlash/云，完整goal保持active。


### UX430 新整数目标原TRAIN准入 — 2026-10-03
429一次6000终止/无PTQ导出后才一次180秒完整原TRAIN；新权重4096全层C/F64/Torch整数之外，直接载入最终QATcheckpoint验证1085440个训练前向值=导出C，并逐层确认直接导出相同。原控制器/全部门槛保持，负<=18、原双语99%保留、extra/观察正确全保留、观察负及128自然零、新误/提前零、延迟门槛保持。失败停止DEV/TEST/训练模型烧录及再次整数拟合/尺度/阈值选择。完整goalactive。


### UX430失败与UX431固定TRAIN监督审计 — 2026-10-03
429一次6000步196.82秒；430训练前向/C全层1085440精确，旧175误触发降18，ZH3538/3542、Yue2520/2520、额外ZH17/17但Yue17/18、观察负例1/51，TRAIN拒绝，停止429，不DEV/TEST/第二QAT/PTQ/尺度/阈值/部署。新丢失额外正例index126不同于422的106。431只读缓存检查额外168原版基准、6146anchors、抽样次数与原触发时序，60秒上限；无新推理/训练/规则，完整goal保持active。


### UX431审计接口失败及UX431r1 — 2026-10-03
431只读审计未完成：读取metadata遗漏negative_groups必需的sampling_group，KeyError在汇总前终止，原脚本和失败terminal封存。431r1仅补取此已有字段，范围/数据/规则/60秒上限均不变；没有重复拟合或改变标注。


### UX430失败与UX431r1固定TRAIN监督审计 — 2026-10-03
429一次6000步196.82秒；430训练前向/C全层1085440精确，旧175误触发降18，ZH3538/3542、Yue2520/2520、额外ZH17/17但Yue17/18、观察负例1/51，TRAIN拒绝，停止429，不DEV/TEST/第二QAT/PTQ/尺度/阈值/部署。新丢失额外正例index126不同于422的106。431只读缓存检查额外168原版基准、6146anchors、抽样次数与原触发时序，60秒上限；无新推理/训练/规则，完整goal保持active。


### UX428–432整数训练与监督审计收尾 — 2026-10-03
428新90维固定INT8训练合同初始/模拟更新2170880层值及63shift精确，包含掩码/梯度/ABI。429仅一次6000步196.82秒训练9984整数权重，无BN或二次PTQ，偏置/shift固定。430新模型1085440训练前向/C层值、181450头、2109797判定块精确，但旧负175→18、ZH3538/3542、Yue2520/2520、额外Yue17/18、观察负1/51，停止且不DEV/TEST/第二拟合/量化/阈值/部署。431读取遗漏sampling_group失败，原文件和terminal封存；r1仅补字段的缓存审计2.656秒通过，额外168 teacher一致、35正确owner全覆盖6146anchors，不存在漏训/端点不一致。旧漏例15729已恢复，新15749抽样14次、确认scores438/331/-13，负分撤销；13近词+4碎片+1自然残误，小叶仍在观察负例。继承training.json中masked_BN=true是旧统计字段，新实际模块不含BN，不能将该字段当本轮使用BN的证明。432七条新USB原72/off/off/空闲、1356事件927140B/next4583、2MiB上下文204800预算、摘要/灯/音量/片段状态及534原非MD(含32引用)保持、USB释放，未重读Flash。详见docs/WAKE_INTEGER_QAT_REPORT.md。完整目标active，本轮有新整数训练合同和真实数值/监督审计进展；不complete，不宣称现场误唤醒已修复。


### UX433固定近词声音来源诊断 — 2026-10-03
上一轮428–432为新整数合同/一次训练及监督审计进展，所有进程terminal，429拒绝配方保持停止。433只固定431r1中的13残余近词TRAIN窗、两个相关粤语正例和公共小叶index6来源。原冻结replay/RNG恢复15窗并核对413 PCM hash及标签；源音频也校验TRAIN身份/hash，现有本地SenseVoice INT8 CPU4 auto/ITN各unique PCM一次+500ms零尾，无增益/去噪/选转写。90秒重建、150秒总上限。仅发音/重复波形/ASR文本语言诊断，不自动重标/删样、不KWS评分/第二QAT/DEVTEST/设备采放/USB云Flash；完整goal active，阻塞0。


### UX433波形复现平台差异及433r1 — 2026-10-03
Windows NumPy FFT计算channel median与原WSL差最多1.776e-15，原assert_array_equal停止，尚未ASR，不放宽断言/PCM校验/标签。原脚本及terminal1封存。r1只拆环境：既有WSL NumPy精确复现15固定TRAIN窗及原来源，保存hash固定NPZ；既有Windows SenseVoice按同一auto/CPU4/ITN/500ms零尾一次解码。固定样本/数据/判断/90秒波形与150秒ASR上限保持，不训练、不设备/云/Flash。


### UX433r1原WSL波形复现与Windows离线转写 — 2026-10-03
上一轮428–432为新整数合同/一次训练及监督审计进展，所有进程terminal，429拒绝配方保持停止。433只固定431r1中的13残余近词TRAIN窗、两个相关粤语正例和公共小叶index6来源。原冻结replay/RNG恢复15窗并核对413 PCM hash及标签；源音频也校验TRAIN身份/hash，现有本地SenseVoice INT8 CPU4 auto/ITN各unique PCM一次+500ms零尾，无增益/去噪/选转写。90秒重建、150秒总上限。仅发音/重复波形/ASR文本语言诊断，不自动重标/删样、不KWS评分/第二QAT/DEVTEST/设备采放/USB云Flash；完整goal active，阻塞0。


### UX433r1结果与UX434仅语言统计接口修正 — 2026-10-03
原WSL完整replay遍历15791行，固定15PCM/标签hash完全一致，14原来源；Windows一次29unique离线转写2.563秒，未发现选定样本正负同PCM，所有近词未被转写成完整小言。源小叶/小明等多数保留词语；新漏正例原来源被auto识为日语、加通道/回放后读作你好主演，仅可疑旗标、不改标签/门槛。433r1把<|zh|>/<|yue|>与zh/yue直接比而错误汇总为0，434只读缓存strip固定tag外壳，源13/14语言一致、窗12/15；原ASR结果/输入/hash不动，不二次解码或选输出。完整goal active，不宣称现场误醒修复。


### UX435新端侧八类短语输出数值接口 — 2026-10-03
433–434显示多数已知误触素材可被独立离线转写区分，未发现选定正负同PCM，不因此重标。新独立架构原型保留8类C11整数投影，score=class0−max其余7类，逐类Q8后以32位做差并限INT16；不同于429单输出QAT或422导出时删除辅助头，两旧配方仍停止。复用现有最终ReLU24维current，不增加因果状态/堆；新增192 INT8权重及8偏置/shift。435只固定合成头及429拒绝backbone作为数值fixture，一次120秒全层/类别C-FP32-F64一致、63shift/overflow/metadata/gradient，报告编译栈/代码开销。无新训练/校准/门槛/DEVTEST/USB声音Flash，不以接口通过宣称识别改善；完整goal active。


### UX435通过与UX436 C3对象预算 — 2026-10-03
435旧backbone1085440层值、8类/差分36864值及C角点73143值精确；16Python/13C元数据护栏、6输入护栏、所有类/表示梯度通过，复用current经trace核实，因果状态仍3276，无新heap/state。仅合成头，不是识别通过。436在现IDF6.1 RISC-V GCC以rv32imc/ilp32/Os/stack-usage编译新头、现requant及192权重/偏置/shift/描述符，门槛新增text+const≤4096B、头栈≤128/嵌套≤160、无可写data/动态分配。60秒、无应用/设备/Flash，不将对象预算当业务堆/延时验收；完整goal active。


### UX436统计失败及436r1同对象预算核对 — 2026-10-03
436三项C3对象/size/su与disassembly已生成，但仅累计.rodata漏.srodata.shift8B而244!=252断言，terminal1和原脚本封存。r1仅解析同一已冻结对象，包括小常量及小data/bss，无重编/改门槛。新head text354B+常量252B=606B，头栈48B、连现requant静态嵌套80B，可写data0，无分配引用，原对象hash保持。是C3编译预算，不是完整链接/实机时延/业务堆或识别证明；尚未训练新八类模型，原429停止/原固件未动，完整goal active。


### UX433–437近词源诊断与原生八类头合同收尾 — 2026-10-03
433 Windows FFT微差在原断言停止，r1原WSL保持同15帧PCM/hash/标签，再Windows29unique离线ASR；无选定正负同波形，多数完整近词转写有区别，疑似粤语正例仅旗标未重标。434修正<|lang|>统计接口，源13/14、窗12/15语言一致，原ASR不变。435新增C11八类输出及host integer projection（class0−max近词，INT32差后限幅），独立36864输出+73143角点+1085440原backbone值精确，复用current/no新heap或因果state；合成头未训练。436对象常量漏.srodata统计失败已封存；r1同C3对象text354+const252=606B，头栈48/嵌套requant80B、可写0，未应用链接/实机时延/业务堆。437七条fresh USB原72/off/off/空闲、1356事件927140B/next4583、2MiB上下文204800预算、摘要/灯/音量/片段状态及534原非MD(含32引用)保持，USB释放。详见docs/WAKE_NATIVE_PHRASE_HEAD_REPORT.md。本轮改变数值接口并取得源/资源新证据，属progress，阻塞0；完整goal active，现场误触/全语音流程仍未验收，不complete。下一步只能注册新的原生多类别训练及完整原门槛，不能再执行已停止429单输出配方。


### UX438定向补足C类别竞争分支 — 2026-10-03
435 full-pipeline合成头经核查因最大类同饱和，4096个contrast全0；独立shift/梯度和Torch竞争测试通过但不足以证明该C路径命中。438只复用已编译.so，预先固定6个类0胜/负类胜/tie/正负溢出用例及513个低增益零和权重/固定seed随机表示，C/F64/FP32对齐且核对NULL输出同score。不重编/训练/选参数/数据打分/设备/Flash；旧数值结论保持其范围，不把合成类别胜出统计作唤醒质量。


### UX439 原生八类保留架构训练合同 — 2026-10-03
438定向C类别竞争4671值/519空指针分数一致，非词语质量。新模块固定保留24→8投影及class0−max近词输出，旧scalar末层冻结只为ABI占位；训练首11层9960权重+192类权重=10152，偏置/shift不变，初始429仅来源、原单输出配方仍停止。先一次120秒初始/模拟更新4096全层和类别C/FP32/F64对齐、类别平衡CE/掩码/共享梯度/冻结checkpoint及35批成本，无语义fit/质量评分/DEVTEST/USBFlash。通过后才注册一次有限新架构拟合，不扫阈值或重启旧fit。


### UX439通过及UX440一次原生类别架构拟合 — 2026-10-03
439初始/模拟更新2244608 C/FP32/F64逐层/类别值精确、类别平衡CE/忽略零梯度/共享backbone梯度、7 checkpoint护栏及掩码通过，35批中位37.84ms。440仅一次保留原生八类头的新架构训练，旧single-output/导出删除头配方保持停止。首11层9960+类192整数权重，旧标量24冻结不用，bias/shift固定无BN或PTQ，原17066输入/6146anchors/所有draw/seed6000/CPU4/540秒和四loss保持，另固定完整短语尾mask的类别平衡CE权重1。最终6000唯一候选，TRAIN/DEV/TEST任何失败停止，不第二fit/尺度/阈值/规则/选择。无设备Flash或私人录音上传，完整goal active。


### UX438 类别竞争定向验证结果 — 2026-10-03
固定6个C胜出/平局/正负限幅及513随机表示，4671 C/F64/FP32值与519次NULL输出分数精确，terminal0。435全流水线4096差分均零的饱和局限明确保留；438的46正/457负只是合成分支覆盖，不是识别质量。docs/WAKE_NATIVE_PHRASE_HEAD_REPORT.md已保存，437原source及438脚本/receipt补验保持，没有设备USB/Flash。


### UX441 原生类别最终模型完整TRAIN准入 — 2026-10-03
440唯一final6000终止后，441仅一次180秒原全TRAIN/103观察/128自然父流及同controller门槛；新C副本在conditioned输出后以保留八类差分替换heads[2]，原默认runtime不改。最终训练checkpoint逐层/八类输出与C/独立F64 oracle验证，所有原负<=18、双语99%、额外及观察正确全保留、观察及自然负零、新误/提前零/延迟门槛保持。失败即停新原生拟合，不DEVTEST或训练模型烧录，不第二fit/量化/阈值/规则选择。完整goal active。


### UX439–443 原生八类训练失败及关闭 — 2026-10-03
439初始/模拟更新2244608层/类别C-FP32-F64精确、CE/梯度/7 checkpoint护栏通过。440唯一6000步232.484秒，无BN/PTQ，10152训练整数权重及保留类头；441新checkpoint完整1085440层+36864类别、181450完整输出及2109797 controller块精确，原TRAIN负175→88、ZH3541/3542/Yue2516/2520保留，extra ZH17/17但Yue16/18，49观察正确全保留但负21→8/51，自然128仍1，四门槛失败，停止440，不DEVTEST/第二fit/PTQ/阈值/规则/候选烧录。442只读缓存：88残误=71近词+12碎片+5自然；相对前次拒绝429多72/交16，extra漏106/126两个原粤语来源，无重标/删样。443四terminal/hash及scoped lint通过，534原非MD保持；七条fresh USB原72/off/off/空闲、1356事件927140B/next4583、2MiB上下文204800预算、摘要/灯/音量80/片段保持，USB释放，未读写Flash。详见docs/WAKE_NATIVE_TRAIN_REPORT.md。全goal active，属新架构及训练/失败证据progress，不标complete或现场误醒修复。


### UX444 完成HTTP句柄释放计划 — 2026-10-03
上一轮439–443取得新架构/单次训练/残误缓存证据，属progress，原配方质量失败停止；设备72/off/off。本轮转向完整语音链路内存：最终HTTP producer返回后，在已有answer_end、TTS join之前forget完整client，而非仅关闭socket保留句柄/ticket。不触及progress/intermediate HTTP，Qwen与ASR复用不改；默认OFF，跨轮DeepSeek可能失去ticket速度利益需实测。先ON/release/OFF三种所有权sanitizer及已有HTTP cache/engine检查，正常预算构建后仅一组三轮中粤中/每次一次/250ms再唤醒，原49152最低堆、24576最大块、完整输入/工具/话题/音频和误醒未完成状态保持。不减容量/栈，不第二组凑数。新鲜4MiB备份，只app guard写入与恢复原72/off/off、保留新增历史。完整goal active，阻塞0。


### UX444最终HTTP句柄完整释放闭环 — 2026-10-03
新增AGENT_VOICE_HTTP_FORGET可选策略，默认OFF；仅最终LLM结束后、TTSjoin之前销毁已完成HTTP句柄，原进度应答/工具续轮不关闭其仍使用的HTTP；ASR/TTS复用、栈/模型/阈值/VAD/上下文容量不变。4项所有权ASan/UBSan检查通过。单次正常构建1533120B；首冻结检查未识别GCC生成的stream_end.isra.0符号而失败，保留原脚本与失败记录；r1仅验证实际链接call/tail-jump顺序，不重建。唯一中粤中三轮各一次：first3/3，完整输入与业务3/3，话题应答2/3；SDK最低49204B、最大连续块最低38912B，48KiB关卡通过。与309差1348B仅描述不同运行，不能归因全部来自句柄释放；HTTP跨轮TLS票据会丢失，延迟以实录保存。新鲜4MiB备份、app-only安装/恢复与非app一致通过。恢复72/off/off、上下文1362事件/2MiB/204800B历史、摘要/灯/volume80，USB释放。误唤醒/277额外延迟/声学一秒仍未验收；默认不采用、不补第二组，完整goal active/阻塞0。证据voice-http-drop-ux444/closure、实录及linked-final-owner-proof。


### UX445正常录音与上传拖尾诊断计划 — 2026-10-03
上一goal轮444为progress：可选HTTP完整销毁经4项主机验证，实际三轮完整业务3/3，最低SDK堆49204B，但话题2/3和声学一秒仍失败，恢复72/off/off。现只从其已核验恢复前备份解码最后完整录音，用原匹配规则核对本地样本拖尾、上传后处理和已存时间线；零USB/采放/云/训练/Flash，不读取新TEST，不变门槛。普通版没有capture_tail_v2和上传PCM CRC，不能冒充唯一turn-ID或逐字节上传证明。一次≤90s分析，结果据实保留，完整goal active，阻塞0。证据voice-source-tail-ux445/。


### UX445 已存录音拖尾分析收尾 — 2026-10-03
生产C与独立Python v3解码89024样本完全一致、头/body CRC通过；固定单次三频带来源匹配通过，本地一次ASR完整复述命名输入。源能量活动结束后的样本尾1.434秒，本地capture end到上传结束1132ms、再到最终ASR303ms；quiet760/pending_hold0。vad_end为上传末事件，不能当本地停录或声音首音素；记录无turn ID/上传PCM CRC/完整逐帧notice，因果范围保持。首次编译成功后UTF16 WSL警告日志解码失败，原失败保留；同一二进制首次解码/匹配/ASR继续通过，未重编、换条件或重复推理。16输入hash及309以来五项已声明optional源改动核对通过，无新USB/采放/云/Flash，445无runtime改变，沿用444收尾原72/off/off及1362事件。docs/VOICE_SOURCE_TAIL_REPORT.md、voice-source-tail-ux445/closure及source.zip保存。完整goal active，误唤醒未修复，阻塞0。


### UX446 新完整音节序列解码合同 — 2026-10-03
用户误醒仍未解决，当前原72/off/off。播放及录音已暂停wake，旧抬阈值/固定128ms和已拒绝训练不重复。新建隔离C11 CTC-greedy音节合同：两语言四token、言/燕不同tone token、只接未来14类head整数输出，不使用原scalar伪造音节。先synthetic序列/独立oracle65536帧/ASanUBSan及C3对象预算(state<=48/textconst<=2048/stack<=64/无heap)，一次60秒；不进固件source列表，不采音/USB/云/Flash/语义评分/DEVTEST/训练。通过也不是声学误醒改善；新CTC模型尚未训练，原模型及任何已停止fit保持。证据wake-syllable-contract-ux446/plan。完整goal active/阻塞0。


### UX446 完整音节序列原型合同通过 — 2026-10-03
新增独立kws_ctc.c/h及主机target，未进idf source列表、无声学模型/训练/固件替换。14类唯一最大值/CTC折叠/四音节次序/词尾tone及两blank提交，gaps/disarm/invalid清证据；双语人工序列、近词尾/片段/顺序/歧义/边界与ASanUBSan通过，独立后缀oracle65536帧/237事件精确。一次进程2.375秒，C3 ABI state32B/textconst588B/自身静态栈16B/全局可写0/无heap；外部memset调用栈和完整链接/32ms实时/业务堆未测，不能当识别改善。模型尚未训练，原scalar不允许伪造音节输入，已拒绝旧配方仍停止。offline过程0USB；收尾另注册七条只读查询，原72/off/off与1362事件929888B、2MiB/204800历史、摘要/灯/volume80保持、USB释放。docs/WAKE_SYLLABLE_CONTRACT_REPORT.md及wake-syllable-contract-ux446/closure保存。完整goal active/阻塞0，现场误唤醒仍未修复；下一步先完整词标签合同，再有限CTC训练/整数导出。


### UX447 真实CTC声学小样机计划 — 2026-10-03
上一轮446为progress：完整四音节C11合同/65536 oracle及C3 state32/text588/stack16通过；无训练或声学改善，原72/off/off。447只从已有完整词TRAIN proof选择中粤8类各4父词、variant0/devicefalse，共64；片段/不明词不能赋整词标签，近词无独立声调依据统一OTHER词尾。新随机48因果骨干+14类CTC，每512样本输出，一次1200 Adam .003 batch16语言类别平衡、CPU4/270秒，唯一final及一次TRAIN校准/nativeC全层和head14对齐。不是复活旧binary/8类或改其门槛。pilot要求中粤target4/4、56near0误和至少58/64 exact序列及损失下降一半；任何失败停不二次fit/量化/threshold/DEVTEST，过也不烧64词模型。无USB/新音频/云/Flash，完整目标active/阻塞0。证据wake-ctc-pilot-ux447/plan。


### UX447 CTC声学小样机失败及概率诊断 — 2026-10-03
完整词来源TRAIN中粤8类各4/64条及哈希合同通过；新随机48因果骨干+14类CTC/19742参数，唯一1200步/CPU4约15.3秒，loss77.154→.085。原greedy47/64<58；原非连续FFI事件计数无效已封存，显式连续行四unit及114688实际C输入值核对后ZH1/4、Yue4/4、56near0误，仍失败。未执行训练模型量化/C声学推理/DEVTEST或烧录，不删NI音节或改准入。独立CTC前向与TorchF64差1.63e-15，17错greedy均完整目标概率更高，三ZH完整.618对缺NI.380，支持另注册概率解码诊断，非MAP或泛化证明。C头28000整数及65536序列oracle/sanitizer保持，源码和唯一失败checkpoint封存。本阶段无USB/新声音/云/Flash，设备最近446为原72/off/off，不能冒充本轮fresh查询。docs/WAKE_CTC_PILOT_REPORT.md与wake-ctc-pilot-ux447/closure保存；全目标active，progress/阻塞0，现场误醒未修复。


### UX448 固定概率解码诊断计划 — 2026-10-03
447单次新CTC训练失败并封存，不量化/导出该模型。缓存概率核对发现17错greedy均完整目标概率更高。448只实现无词典prefix beam8/blank与nonblank求和/全部14类竞争，独立短路径枚举及分块状态检查，一次64原TRAIN缓存logits打分，原ZH4/Yue4/near0/exact≥58固定。无新fit/模型infer/校准/DEVTEST/USB声音Flash，不扫描宽度、不删NI或强制补字；通过仅证明解码诊断，447原失败保持，不能声称现场修复或直接部署。全目标active/阻塞0。


### UX448 八候选解码失败及共同前缀证据 — 2026-10-03
无词典prefix beam8/所有14类竞争，3unit与1620独立路径求和、重复blank/OTHER/缺首字/分块护栏通过。原64TRAIN缓存一次打分.568秒，ZH4/4、Yue2/4、near0/56、exact50/64<58，失败停止，不扫宽度。14个粤语错误均多插NI；缓存行0/32前118输入帧/1.888s相同，NI峰在第8个32ms输出、概率.3914，指向共同开头先验，PCM静音来源仍需独立核对。不删开头或强制语言来假装改善。447原greedy47失败及模型封存保持；448没有fit/声学infer/校准/C3beam/DEVTEST/USB新声音Flash。报告追加docs/WAKE_CTC_PILOT_REPORT.md，原设备最近446为72/off/off；完整goal active/阻塞0。


### UX449 明确空白监督CTC小样机计划 — 2026-10-03
448全部类概率竞争发现粤语虚插NI，缓存共同前118帧相同；两原失败保持未量化。先重建原两TRAIN完整PCM/哈希及C40特征，核实峰值发生在真实零PCM前缀，生成65536零PCM的C归一化空白输入。新增empty-target CTC逐帧归一化损失，独立blank-logprob/梯度合同；64原词输入不变，新随机48CTC唯一1200步，每步16语言类平衡word加4 blank一次联合BN，word-loss+blank-loss权重1，不设置时钟删音节或换解码。原greedy中4/粤4/near0/exact≥58及空白无token/event固定，≤90秒；失败停不第二fit/阈值/导出/DEVTEST/设备。即使过也仅TRAIN小样机，不授权烧录或现场误醒通过；完整goal active/阻塞0。


### UX449 空白监督小样机失败及源验证收尾 — 2026-10-03
原两TRAIN PCM哈希/放置/C前端20480值精确，真实声源1.892/1.877s才首非零，原NI在.288s数字静音内，确认小样机开头假线索，不能推广为原72每次现场声源。65k零PCM生成真实非零归一化空白特征，empty CTC逐帧损失与独立blank-logprob/全部梯度相同，2unit通过。唯一新1200步约17.8s，ZH0/4、Yue0/4、exact40/64、near0/56、blank错误NI/HAO，失败并停止，不再拟合这套均值空白目标。本轮没有校准/导出/DEVTEST/USB声音Flash；原447/448失败保持。docs/WAKE_CTC_PILOT_REPORT.md/449closure封存，需要直接约束已知静音整条路径，非删音节或时钟过滤。全目标active/progress/阻塞0，现场误醒未修复。


### UX450 已知静音路径约束计划 — 2026-10-03
449均值空白目标失败停止，不扫描其系数/训练时长。新450给原64完整词TRAIN在特征前附加经C前端核实的32768零PCM/128帧，该新增段的唯一合法CTC路径为blank；损失=(前缀blank整段NLL+原完整4音节后缀CTC NLL)/4。这是有真值的路径约束，无自由辅助权重；原词完整输入/标签不删改，原两PCM附零后C全特征对齐。新随机48CTC一次1200步/16语言类平衡/CPU4/≤90s，独立forward/前缀梯度及输入护栏先过。仅原未加前缀64及真实零PCM打分，原C中4/粤4/near0/exact≥58/blank无tokenevent；不以仅增强输入通过宣称可用。任何失败停，不第二fit/前缀长度/权重/seed/门槛候选；旧447–449拒绝保持，不校准导出/DEVTEST/USB声音Flash。全目标active/阻塞0。


### UX450 已知静音路径原输入准入失败 — 2026-10-03
32768真实零PCM前缀C拼接30720值精确，64原输入655360值保留；限定空白路径CTC独立forward/梯度和输入护栏两unit通过。唯一新随机1200步19.6秒，原未加前史输入ZH4/4、Yue2/4、near0/56、exact32/64，失败停止；零PCM无token/event通过，不能用局部改善冒充双语准入。原模型未校准/导出/DEVTEST/USB声音Flash，447–449旧失败保持，不能扫描prefix长度/辅助权重或再fit。新增训练与零状态推理前史不同，只允许另注册一次状态诊断，不追溯改450失败或直接部署小样机。报告及source/checkpoint/closure封存；完整goal active，progress/阻塞0，现场误醒未修复。


### UX451 推理状态前史限定诊断计划 — 2026-10-03
450原未加前史准入失败保持，不再fit/校准导出。只一次64行batch推理，128真实零PCM特征warmup后接原256输入，原C音节decoder在前史disarm，原词段arm；两个fixtures逐帧Torch step与batchrtol/atol1e-5对齐，原suffix bytes保持，样本钟与事件映射保留。只验证状态解释，无新声学准入，不改450失败或直接烧录。一次≤60s，不prefix长度/规则/候选扫；无DEVTEST/USB声音Flash/新训练/权重变更。全目标active/阻塞0。


### UX447–451 小样机与状态诊断最终收尾 — 2026-10-03
447唯一CTC拟合greedy47/64、ZH1/Yue4未过；非连续FFI计数原无效已封存并114688输入值修正；448全类beam8 exact50/64、ZH4/Yue2仍失败，发现NI在实际零PCM开头，449两原PCM/C20480值及首次声源1.892/1.877s确认。449均值blank唯一拟合仍静音NI/HAO、ZH0/Yue0失败；450已知静音路径唯一拟合在原零状态输入exact32、ZH4/Yue2失败，空白无token通过。451只一次匹配前史推理/两step-batchfixtures差1.53e-5，原655360输入值保持，primed ZH4/Yue4/56near0/exact62/64，仅TRAIN状态诊断，不追溯改450失败，不nativeC声学/量化导出/DEVTEST/候选烧录。全部旧拒绝保持，模型、脚本、输入、成功和失败记录封存。最后另注册4条fresh readonlyUSB，原72/off/off空闲、1362事件929888B、2MiB分区/204800历史预算保持；无Flash/新声音，COM5释放。docs/WAKE_CTC_PILOT_REPORT.md/SPEC/ACTIONLOG保存，现场误醒尚未修复、完整goal active；本轮新监督与状态证据为progress，阻塞0。


### UX452 C11连续音节推理状态合同计划 — 2026-10-03
上一轮447–451新监督/真实零PCM源/前史诊断为progress，原失败保持，不导出那些模型。本轮先新增隔离C11 CTC runtime，复用24/48原backbone空间，仅32B序列及少量owner；按真实零PCM logmel0归一化逐层计算常量activation，填因果rings，须与128次原C步进及后续trace完全一致。rearm/gap/disarm清证据并重置前史，disarm不FFT/CNN，不加入两秒额外等待；untrained不能事件。512PCM/14head/严格样本钟及原完整四音节合同，synthetic模型正负/时钟/指针/所有权ASanUBSan，C3新增owner≤128/text≤4096/无heap。一次≤90秒，仅接口和资源对象，无训练/质量打分/DEVTEST/USB声音Flash。默认idf sources暂不接入，完整goal active/阻塞0，现场误醒仍未修复。


### UX452 连续C11合同通过及ABI收据修复 — 2026-10-03
新runtime原NN工作区+56B owner，动态常量前史与16个synthetic24/48模型各128原C步进状态完全一致，后续1626112个trace值精确。合成四音节正词/错误词尾/disarm不FFT/重arm/gap/无效指针及untrained禁止事件ASanUBSan通过。唯一测试/编译约3.3秒，C3对象text976B/可写0/自身reset静态栈80B/总workspace12008B；非实机耗时/整轮堆。原ABI统计因Windows nm CRLF尾未匹配KeyError停，原脚本/退出1封存；r1只splitlines解析同object确认owner56，未重编或重跑。未用任何被拒绝模型导出/训练/质量/DEVTEST/设备Flash，默认idf sources未接入。完整goal active/阻塞0，现场误醒仍未修复。


### UX453 音节运行时移除诊断缓存计划 — 2026-10-03
452合同/16常量seed/1626112后续值通过、C3工作区12008，其中永久trace与scalar detector在新runtime不用。453只换既有frontend/neural buffer views保存previous/FFT/因果state，去除整层trace及旧判定器，seed与14head/序列/所有权不改；同合成模型/16状态/1626112值及护栏ASanUBSan、C3workspace≤11000、payload外owner≤128/text≤4096/无heap。一次≤90秒，无新模型/质量/DEVTEST/USB声音Flash，默认idf暂不接入、全goal active/阻塞0，现场误醒尚未修复。


### UX454 完整基础TRAIN序列监督合同 — 2026-10-03
452–453 C11数学prime/重arm/去trace合同与资源对象通过；现扩大声学前先固定全部15791基础TRAIN，不挑分数。已有完整词proof且PCM/hash一致赋中粤四token；其余旧负输入只赋OTHER单个catch-all类别，不宣称真人音节转录，仅真实零PCM为空target，半词不得赋完整词。所有原x/旧label/metadata保持；新的mixed-length CTC支持0/1/4，固定128已知blank前缀路径，独立forward/梯度/四词旧合同及输入护栏检查。一次≤60秒，不训练/质量/DEVTEST/USBFlash；通过后才另注册一次正式fit，完整goal active/阻塞0。


### UX455 完整基础TRAIN连续CTC唯一拟合计划 — 2026-10-03
453同原C状态/1626112后续值通过，工作区10840B；454全15791基础TRAIN及完整词/OTHER/真实空白0/1/4标签合同通过。本次新随机48CTC/19742参数，种子20261003455、唯一final6000、Adam .003、CPU4、32混合batch/源组均衡、420s上限。训练与评分均固定已核实128零PCM前史，原256输入及旧truth全部保持；不复用失败pilot、不新删音节/时钟规则。一次全原TRAIN真实C贪心解码；每语言完整正确事件召回≥99%、事件不早于原event_start_accept/不晚于原event_end+5120样本（320ms）、负触发≤18、自然/已知近词零、wrong-language/提前零、literal blank无token/event。失败即停该配方，不二次fit/门槛/seed/窗口/epoch选择/量化导出/DEVTEST或烧录；本阶段无USB新声音云Flash，完整语音目标保持。


### UX452–455 连续状态、内存与正式CTC拟合收尾 — 2026-10-03
452模型常量prime/16状态/1626112后续C值精确；453复用借用缓冲去trace，C3对象工作区12008→10840B、代码1012B/可写0，非板上整轮堆或实际音频。454全15791基础TRAIN/161699840原值及0/1/4监督合同通过。455唯一实际fresh48/19742参数/final6000拟合205.1s，访问14112条、全15791评分；有效ZH1054/3637、Yue1182/2658、负46/9496（近词17/796、自然7/2225）、提前2339，质量失败，停止配方。缓存只汇总14小燕/21截词/7自然及事件时序，不删重标/换门槛/重fit；整段CTC缺最终token时间约束，不能把序列字面正确当完整声学词尾。入口Windows路径失败发生于模型创建前，r1只改路径；评分已完成后的numpy bool JSON失败仅缓存汇总修复，两个原失败保持，未补造评分计时。13 CTC Python unit及scoped whitespace/hash封存通过；无训练模型校准/C声学导出/DEVTEST/候选烧录。收尾4 fresh readonlyUSB原72/off/off/空闲，1362事件929888B/2MiB/204800历史预算保持，COM5释放，无新声音/Flash写入。docs/WAKE_CTC_RUNTIME_REPORT.md及全部source/checkpoint/成功失败收据保存；现场误醒与完整语音goal未完成，本轮新状态/数据/单次训练证据为progress，阻塞0。


### UX456 完整词尾时序概率合同 — 2026-10-03
455整段CTC失败停止，新的目标只用已有正词结束标注约束最终token允许时间，不改旧真假、不给自然语音编音节、不新增runtime计时过滤。通过允许类别归一化CTC再减逐帧允许质量log恢复原完整路径概率；独立forward/224全梯度/无约束原合同/护栏先验。draft -inf正向正确但CPU CTC反向NaN，已封存原代码/失败，尚无新fit；数值合同固定finite |logit|≤1024/禁止类-4096以F64下溢为零，避免NaN，不扫描floor。数学准入通过后才另冻结一次新目标完整TRAIN对照；原455模型不救活，完整goal未完成。


### UX457 已有词尾时序监督唯一对照 — 2026-10-03
456原始路径概率/224数值梯度及16新旧unit/全部6295正词时间坐标通过；455已失败停止不部署。新457只更换监督为finaltoken限在原event_start_accept..event_end+2560samples内，允许类别CTC减归一化质量恢复原NLL；同全原15791输入/truth/48模型/种子20261003455/源组draw/final6000/Adam .003/CPU4/420s及全部原准入，独立fresh权重，不复用455。正向事件仍原C四音节两blank，没有runtime屏蔽或计时拖延；自然/半词/近词标签不重编。任何失败停止该新时序配方，不第二fit/校准/epoch/time-window/阈值/seed选择/DEVTEST/设备烧录，完整语音goal未完成。


### UX456–457 词尾时间监督固定对照收尾 — 2026-10-03
456正确原路径质量/224有限差分梯度/16新旧unit及6295词尾坐标通过，-inf反向NaN原草稿保留。457唯一fresh同48网络/seed/全数据/final6000拟合194.9s，draw数组与455逐项相同；同原C decoder/准入，一次全TRAIN评分15.0s。负46→3/9496（窗口减少93.48%）、近词17→0/796、自然7→1/2225、提前2339→5（减少99.79%），有效ZH2671/3637=73.44%、Yue1647/2658=61.96%。原每语言99%/自然零/提前零三门槛仍失败，停止此时序fit，不救活455/改边界/删真假/门槛/第二fit/量化导出/DEVTEST或候选烧录。正负窗口改善只是两新实验的匹配TRAIN对照，不是与实机72的误醒改善或每小时误醒率。已归档checkpoint/输入/hash/对照/成功失败及docs报告，本阶段无USB新声音Flash；沿用本轮455四只读核验的原72/off/off/1362事件929888B/2MiB/204800历史，COM5释放。完整goal仍active，实机误醒未修复，本轮新精确监督及受控结果是progress，阻塞0。


### UX458 固定缓存概率解码诊断 — 2026-10-03
上一goal轮452–457为progress：新的连续状态/去trace/完整0/1/4标签及精确时间约束，受控两fit误窗口46→3/near17→0/提前2339→5，但召回/自然/提前仍失败，未部署。本次仅原447已冻结64全词TRAIN索引/哈希的457缓存，beam8/all14类无词典/补token，输入同原C Q8；固定完整best prefix+两连续唯一blank argmax作预览，每prefix一次，按原开始/结束+320ms核对，不以word约束竞争。中粤各4/near0/exact≥58为诊断准入，≤60s；不是重训/声学新infer或C3连续事件证明，不更改457失败，不扫宽度/规则/校准/DEVTEST/USB新声音Flash。完整语音目标保持active，阻塞0。


### UX458–460 连续概率解码计划 — 2026-10-03
458只64原冻结TRAIN/457缓存的beam8概率诊断中粤各4/4、near0/exact58，原greedy中3/4粤4/4；457全原失败不改。459新有限尾状态合并所有早期历史，独立7776完整路径概率及最佳对齐年龄、2000blank旧词过期通过，同64预览4/4+4/4/near0；未部署。460纯C11 8状态/112候选/最后4token/14全竞争，Q16整数logadd静态769项LUT；概率与最佳路径单独归一防长期整数漂移，age255保留上下文但过3s不提交。两唯一blank/完整中粤尾、同winner不重复；flat14全相等无证据清空，gap/disarm/invalid清空，不调声学模型或加等候。一次90s synthetic/ASanUBSan、独立host数学及C3对象(state≤4096/codeconst≤8192/自身stack≤256/noheap)，未进默认固件，无fit/声学infer/DEVTEST/USB新声音Flash。若通过再单独冻结原缓存全TRAIN及声学C校准等后续准入；完整goal active/阻塞0。


### UX460资源结果及UX461全缓存准入 — 2026-10-03
460连续C11 3856B状态/32B node，C3对象codeconst3666B/全局可写0/自身栈160B/noheap，synthetic断帧/disarm/3千blank旧词过期/flat/4096极值及ASanUBSan通过；非实机CNN/最低堆。461一次15791原完整TRAIN/457已存浮点14头，保持原Q8舍入和truth/时间区间，以实际新C suffix解码；64固定预览须同独立host事件清单。原中粤各99%、负≤18、近词/自然/提前/错误语言/zeroPCM零准入，180s上限。失败停止此概率解码候选，不改width/门槛/时序/准入或fit，不新声学infer/校准/DEVTEST/USB声音Flash。457原greedy拒绝保持，完整goal active/阻塞0。


### UX461拒绝及UX462完整唤醒事件合同 — 2026-10-03
461一次全缓存实际C概率尾解码19.22秒，中2956/3637=81.28%、粤1801/2658=67.76%、负5/9496、near1/natural1/提前6；64与独立host事件完全一致。原各99%/near自然提前零未过，停止该候选、不改beam/门槛/再fit或部署。新462用已有完整词二元真值直接学习同一个中粤wake事件；no_event不是声学静音，负empty仅没有完整唤醒事件，不再给无音节转写自然语音编OTHER音节。48backbone/2head/19154参数，正事件仍仅已有词尾允许区间；推理8个32ms概率路径>1/2且两唯一noevent，48B预计状态，无人为延后词尾或猜语言。先独立全部路径/72数值梯度/stream数学与C实际4096事件、ASanUBSan及C3对象≤128B state/4096B code/128B ownstack/90s，零fit/声学质量/DEVTEST/USBFlash。通过才另冻结有限声学实验，失败旧候选保持；完整goal active，现场误唤醒未修复、阻塞0。

UX462原数学draft正向通过、反向因CTC输出原位相减version错误失败，3unit中2通过/1错误；未fit/声学infer。原代码zip/plan/process exit1/log保留；r1仅把原位相减改同数学functional表达，不改目标/8frame/准入，重新核验同数学和资源合同。


### UX462数学资源通过及UX463唯一完整事件训练计划 — 2026-10-03
462r1同数学functional反向修复通过3unit/72独立有限差分/1..8frame全二元路径、4096 actual C与F64事件一致及8192极值/ASanUBSan；C3对象state48B/codeconst2256B/可写0/自身栈64B，无实机声学执行。463在原15791/161699840值及真值上fresh48backbone+2eventhead/19154参数，同455/457种子和源组draw、final6000/Adam.003/CPU4/420s，一次全原C事件评分180s。原正仅一个共同中粤wake事件、自然/音乐/近词empty为无事件不是声学静音；使用已有词尾区间，无重标/编音节。原每语言99%、负≤18/near自然提前零、literalblank无event/完整开始..end+320ms保持；共享tag不声称语言分类正确。失败停此事件配方，不第二fit/门槛/窗口/seed/epoch筛/量化导出/DEVTEST或烧录。本阶段零USB新音云Flash，完整goal active、误醒未修复、阻塞0。


### UX463完整事件失败与UX464全原真值/覆盖审计计划 — 2026-10-03
463唯一fresh拟合195.473秒，一次全原实际C评分6.278秒；中3339/3637=91.81%、粤2398/2658=90.22%，负100/9496（near2/796、自然42/2225）、提前4，原准入失败且停止。checkpoint71c08386790f86f137187203fe96e5dd0a26f79db35fc0f2098bf224830cd4d0，不量化导出/烧录、二次fit或改门槛。原正负与161699840值保持，源码/21 Python和7 C sanitizer回归通过。464只缓存全部15791逐行特征哈希，核对完全相同输入的真假冲突/互斥正词时间、失败是否抽到训练及源分布，≤90秒；不以归因救活463/评分/重标/DEVTEST/USBFlash。完整goal active、误醒未修复、阻塞0。


### UX458–464收尾 — 2026-10-03
458固定64概率预览通过不等同全质量；459完整7776路径/年龄数学、460 C11连续概率3856B/3666B/栈160B对象通过，461全15791缓存实际C中81.28%/粤67.76%、负5/near1/自然1/提前6，拒绝停止未校准部署。462完整事件原draft反向原位运算失败保留；r1同数学修复通过，state48B/代码2256B/自身栈64B/无heap，仅对象非实机声学。463新48/2head/19154参数唯一final6000拟合195.473秒、全原评分6.278秒，中3339/3637=91.81%、粤2398/2658=90.22%、负100/9496（near2、自然42）、提前4，失败停止，未量化导出/DEVTEST或烧录。464全原特征逐hash无真假重复冲突/正例互斥时序；100误负68抽到/32未抽到、558漏正557抽到/1未抽到，不足以把失败归为未训练；字段误名仅r1缓存更正且首文件保留，无模型重跑/改门槛。18旧CTC+3 event Python/7 C sanitizer/新主机CMake、源hash和scoped diff通过；docs两报告/README更新及全部成功失败/checkpoint封存。最终另4条fresh只读USB原72/off/off/空闲、1362事件929888B、2MiB分区204800历史预算保持，零Flash/新音云调用、COM5释放。本轮新连续概率/更小完整事件监督和覆盖证据是progress，完整goal active/阻塞0，现场误唤醒及完整三轮语音体验仍未通过，不宣称完成。


### UX465已录事件概率证据计划 — 2026-10-03
前goal轮458–464新连续概率/48B完整事件数学、唯一正式训练失败及全原真假/时序冲突和抽样覆盖证据为progress，不是外部阻塞。463未通过仍停止不部署。465只读已存2head Q8输出及首次事件坐标，对全部原误负和有效正词计算事件前8帧已有概率/是否曾unique wake胜出，区分弱概率累积与明确声学误认；≤60s，零fit/新声学infer/重解码/门槛候选/DEVTEST/USBFlash，不把失败模型救活。完整语音goal保持active、误醒未修复、阻塞0。


### UX465概率证据结果与UX466联合监督合同 — 2026-10-03
465只查原已录事件：100误负97有unique wake胜出，自然42中41有；误负最大单帧wake概率中位.8366/自然.7843，有效正5737中33无unique winner，故“弱概率累计”不是这批失败主因，不能仅加argmax规则宣称修复或救活463。新466 fresh同48 encoder训练时16输出=14源支持音节+2完整事件，loss=(原timed14 normalized NLL+原event2 NLL)/2固定，未知负OTHER仍只是catch-all非真人音节转写；无权重扫。部署精确删14训练专用输出，仍19154参数/2head/48B事件state，不重用任何拒绝checkpoint。先3unit/两目标960梯度/浮点整段和逐帧投影一致/冻结类护栏≤60s，无fit/声学质量/校准C导出/DEVTEST/USBFlash；通过才另冻结一次有限全TRAIN，旧所有失败保持、完整goal active/阻塞0。


### UX466合同通过与UX467唯一联合声学训练 — 2026-10-03
466两损失与全部960梯度相等、16→2冻结event投影整段/逐帧一致及护栏3unit通过。467 fresh48共享encoder/16训练输出19840参数，同463全15791真值/输入/source draw/seed20261003455/final6000/Adam.003/CPU4/420s，唯一fit；不加载旧被拒绝checkpoint。训练辅头14仅用原0/1/4源支持targets，equal两归一NLL固定；部署精确去14输出仍19154参数/原2event C/48B state。一次实际学到模型投影fixture/全原C评分180s及全部原各语言99/负≤18/near自然提前0门槛，失败停此联合配方，不loss权重/threshold/窗口/seed/epoch扫或再fit/量化导出/DEVTEST/烧录。本阶段无USB声音云Flash，原所有失败保持，完整goal active、现场误醒未修复。


### UX467联合训练拒绝与UX468上下文回退语计划 — 2026-10-03
467一次fresh联合fit199.634秒，投影19154参数保持，仅原C2评分20.647秒；中3349/3637=92.08%、粤2426/2658=91.27%、负88/9496（near1、自然40）、提前5，原门槛失败停止，不再fit/改权重门槛/量化导出/DEVTEST或烧录。完整goal还有三轮提前应答，444第三轮candidate network回退通用短句。468在已完全接收并settled的empty快路由后，仅按既有final记忆语法/history路由选择已有memory/search/general PCM，defaultOFF可选，无新增素材/heap/协议；取消/失败begin都同owner join。已实际播放完成且speaker timestamp>0后将对应text/language给final prompt，不把排队当已听；通过engine现有progress回调加入cached owner，避免第二播放。已听同类memory/search才跳重复tool阶段notice。ON/OFF同主机flow/失败取消/PCM CRC与finalprompt所有权通过后另冻结单次正常C3/3turn，perturn state≤8B，不改KWS/VAD/SDKbuffer/上下文，不重放工具或整个轮次。完整goal保持、误醒尚未修复。


### UX469缓存回退验收 — 2026-10-04
最终已确认输入才选择既有记忆/历史/通用短句，排队不等于已听，join成功且speaker实际时间戳>0后才能传final连续性信息；已有native/deferred分支和工具权限不变。state≤8B、heap/PCM增量0；一次正常三轮和资源门槛不下调。是否遇到本地fallback须独立记录，fallback不等于用户生动复述或一秒有效回答；现场误唤醒未修复。


### UX465–469有限阶段结果 — 2026-10-04
465只读旧事件/分数：100误负中97有唯一wake胜帧，自然42中41有，非单纯soft路径累积；不重判、改阈或把TRAIN诊断当原固件现场根因。466固定等权双头数学/960梯度/批量和160帧投影3检查过。467仅一次fresh全原TRAIN联合fit，实际199.639秒，中3349/3637=92.08%、粤2426/2658=91.27%、误负88/9496（近1/自然40）、提前5；原门槛失败、配方停止、未PTQ/导出/DEVTEST/烧录。前条计划199.634是记录误差，以process/report的199.639为准。
468新增默认OFF的已确认意图本地回退与实际heard连续性；24 Python/11 C sanitizer相关检查按源码不变成功收据及修复的失败用例完成。初稿测试WAL断言/歧义Yue路由/新target缺realtime.c的失败保留，只改测试无产品路由改变。469首configure缺canonical SDK而OUT4096被拒，日志和配置保留；r1复制原444配置，实际唯一编译106.063秒，app1533520B/1540096余6576B，C3state6B(OFF2)，新heap/PCM0，SDK/model/seed/partition一致，原工程493一致+9声明变化/502；不能声称所有原文件不变。
469 tools/voice/continuous_rewake.py唯一正常中粤中3turn/各一次：首次/完整输入/记忆任务3/3，后两次266/265ms不等ready，reset/watchdog/DMA0。前2动态“嗯，给这盏灯取名，我想想”，第3final_topic_changed拒候选并真实回退“我来保存”，无重复stage；本机SenseVoice及原C memory模板相关.4768/.4325、起点差.5ms确认。3输入固定波形均过；提前2.941/2.728/2.853s，最终7.781/7.008/5.223s是10ms能量候选，非1秒/真人音素验收。SDKmin47856<49152（差1296）资源失败，largestmin34816>24576；不补跑/改栈buffer容量，实验不采用。离线renderer初稿漏ima.c链接失败保留，r1只补原依赖，同一次实录，无额外声/云。
两次flash_guard各fresh全4MiB备份验证/app-only/readback/nonapp相同，恢复原app22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07；新增6业务事件1362→1368、929888→932636B，2MiB/204800历史预算保持。最终4只读原72/idle/Wi-Fi在线/wake和voiceOFF，COM5释放。自动关闭不算误醒修复；全语音goal保持active，当前阶段有有限代码/实机进展，blocked审计0。证据voice-contextual-cache-ux468/469及wake-word-evidence-ux465、wake-joint-contract-ux466、wake-joint-fit-ux467；报告WAKE_JOINT_EVENT_REPORT.md及VOICE_CACHED_FALLBACK_REPORT.md。

### UX470 当前误唤醒同板有限复核 — 2026-10-04
用户再次报告频繁误唤醒；四条新只读USB确认原0.11.72-summary/off/off/空闲/Wi-Fi在线，1368事件932636B、2MiB分区204800历史预算。过去提阈850与连续128ms均损伤双语，不重跑这两条配方。一次使用冻结469正常应用及已核实277/288完整短语辅助确认做同板18源对照，原版/候选各中粤4正+10负、同SHA/声卡/.14归一/.35播放/740/gain1，每源一次，无重训/扫参/新模型/独立TEST。先fresh4MiB校验备份保存现有录音，再测原版；未复现负触发或双语基础不足即停。候选需保住原有效正例、负触发严格减少且无新增负来源；通过才唯一3轮经典流式对话（prefetch/reuse关闭，非失败快速路救活），保持48KiB/24KiB资源、完整输入/任务/首次重唤醒/无DMA复位要求。此前277新增延迟104>96ms及469快速路47856<49152失败仍保留，此轮若通过只能作为经典路径减误触试用，完整一秒、生动应答及语音goal不能标完成。任何失败guard只写应用恢复原版/off/off、保留实际新增历史和原始失败；未通过不重复组、删数据或降低门槛。脚本和证据artifacts/voice-fast/wake-field-diagnosis-ux470/；自动环境事件不能直接当人工标签误醒率。

### UX470 同板对照失败与回滚 — 2026-10-04
实际原版/冻结469辅助复核各唯一18来源、同声卡/原始SHA/归一PCM/740/gain1/.35。两组有效中3/4、粤4/4、负1/10：原版误触Yue小燕001，候选拦住却新增ZH sapi-kangkang08小燕，未减少且新增负来源，违反预注册准入，不采用、不补组/换阈或拟合；ZH同一个sapi-huihui05两组均漏。原/候选监听SDKmin65284/62200B、largest最小61440/59392B、infer max8251/14452us，无DMA/复位；非云语音资源证明。条件失败后未执行云端三轮，0云调用/新历史。首backup-only保存当前录音；安装/恢复均fresh4MiB校验、仅写应用、非应用核对。最终另四只读原72/off/off/Wi-Fi在线/空闲，1368事件932636B/next4595、2MiB/204800保持，COM5释放；原应用22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07。三份备份UTC20261003-170736/171003/171305、全部失败及源清单保留；未改产品C/最新安装包，既有失败训练配方仍停止。docs/WAKE_FIELD_COMPARISON_REPORT.md与artifact收据记录新实际误触迁移，不冒称现场修复/每小时误醒率、完整体验或goal完成；本轮同板新证据为progress，阻塞0。


### UX471 触发前输入与完整分数诊断计划 — 2026-10-04
上一轮UX470同板唯一两组发现原Yue误触被拦、却新增ZH小燕，负总数1→1，未采用/已回滚原72/off/off，属于新实际证据而非外部阻塞。现有诊断口只存两路平滑分数且未记录实际armed门控，不能验证具体三模型/重arm来源。新增仅KEYWORD_PCM编译的v2原始三头与每块真实armed/有效heads标志，保留旧scores和推理数学，借同一闲置scratch无新PCM/任务；先队列ASanUBSan及正常C3尺寸/模型seedSDK分区冻结。只四个已预选来源188块各一次（新ZH近词、旧Yue近词及同声音中粤真词），740/gain1/rms.14/播放.35，无提示/记录/云动作；逐序号CRC/时钟/实际C按armed重放所有头/平滑/事件一致，声源对齐后解释，失败不重试/扫参/拟合。首fresh4MiB备份，安装/结束一律guard仅应用及非应用核对恢复原版，保留历史/录音，USB释放。不是三轮业务或真人/小时误报验收；全部旧失败及完整语音目标保持，阻塞0。证据wake-input-heads-ux471/plan.json。


### UX471 原始三头与门控一致、声源归因未全部通过 — 2026-10-04

KEYWORD_PCM v2 仅增三路原始头与真实 armed 标志，数学/模型/seed/SDK/分区不变、无新增堆分配/任务/常驻PCM。C11 ASanUBSan 队列元数据/发布/并发/取消/CRC 通过；保留初次 PowerShell 未启动 GCC 的错误。单次正常预算构建1537072/1540096B，余3024B，最终有效std均C11。四个预选来源各188块一次，共752块/24.064秒；无CRC/时钟/队列/DMA/复位/满幅，实际C按记录armed重放全部原始3头/平滑2分数/事件752/752一致，max14342us。普通话小燕源对齐相关.656、事件在源范围内且armed，辅助峰Q8 1026；正确词也依靠较早辅助支持，不能凭当前分数任意缩窗。第二粤语事件比原WAV尾晚721样本/45.0625ms，原within-WAV断言失败，按计划停止剩余来源归因，未换对齐/阈值/重播救分；保留空端点格式化错误和断言失败。未训练、云请求或三轮业务，非真人/小时误报/完整资源验收，误醒未修复，旧277/469/470失败不改。fresh guard安装/恢复仅应用及非应用核对均过，恢复SHA22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07；结束新四只读查询72/off/off/空闲/WiFi在线，1368历史/932636B/next4595/2MiB上下文与204800B历史预算保持，USB释放。源码538文件在文档追加前逐hash冻结通过。证据wake-input-heads-ux471/closure.json及docs/WAKE_INPUT_HEADS_REPORT.md。阶段证据完成，完整目标未完成、阻塞审计0，不默认采用诊断应用。


### UX472 TLS片段协商只读可行性计划 — 2026-10-04

UX471完成真实原始输入、三头和门控752块精确一致，声源第二项失败保留且原固件恢复，上一目标轮为进展、阻塞0。完整双语/流式/复醒/应答目标不缩。转向已知TTS内存低点：核对IDF当前动态RX是按实际记录大小申请，不能凭接收配置或协商宏已启用就认为小记录已协商。只对实际DashScope/DeepSeek域各做一次验证证书的主机TLS1.2 ClientHello/ServerHello，申请4096片段、每个20秒上限，无HTTP业务/认证/私人录音/USB写入。明确解析客户端扩展及服务端是否回显；未接受/未知则停止这条小buffer方案，保留16384接收上限、不烧录或宣称C3节省。固定协议标准与本地相关源hash，证据tls-fragment-feasibility-ux472/plan.json。


### UX472 片段协商实测与UX473按源2048计划 — 2026-10-04

两次唯一只读TLS1.2握手均验证证书并解析实际ClientHello申请04。DashScope未回显扩展1，DeepSeek回显04；不能给千问缩RX。当前SDK OUT2048/IN16384，mbedtls_conf_max_frag_len上限=min(IN,OUT)，直接申请4096会被拒绝，保留这条实际约束。472无固件/USB/认证请求变更，阶段完成，非C3性能证明。新增473单一默认OFF按源策略：仅api.deepseek.com申请2048，实际session证实协商，未接受则握手结束后撤请求cap、保持IN16384，不改千问、证书验证、原模型/740/SDK/上下文/栈。只一次2048主机预检、C11契约及SDK绑定测试、一次正常预算构建，再唯一正常中粤中三轮单尝试；heap49152及原速度/误醒失败不降门槛。新fresh4MiB备份、仅应用、一律guard回滚原72/off/off、新增历史保留/USB释放，无重试救分。证据voice-deepseek-fragment-ux473/plan.json，完整语音目标继续未完成、阻塞0。


### UX472–473小片段实机失败与撤回 — 2026-10-04

472唯一两次无认证TLS1.2主机握手验证证书，DeepSeek接受4096、DashScope未接受；SDK实际OUT2048导致473仅请求2048，另一次主机验证双方03。C11策略ASan/UBSan通过但接口桩不覆盖SDK真实重组。一次正常构建1534128/1540096B，余5968；应用7fce2ea8b603361dc9407a0a92dc7c82a8f7a9d2920d73ac4f8ce659ce94053f，模型/seed/740/SDK/上下文/栈/分区保持。未执行命名参数构建初稿、OFF宏冻结误判和既有CRLF diff警告均保留。
唯一正常中粤中三轮、各一次唤醒，输入3/3，但后续DeepSeek连接阶段两次访问异常加一次TLSF断言，共3次实际复位、最终任务0/3，group退出1；外层退出0只代表回滚成功。复位使堆min/协商统计/快速复醒无法验收，无正常最终回复故未运行速度分析、不补组或降低门槛。真实SDK动态记录分配与静态容量握手重组存在需独立验证的怀疑路径，尚非根因证明。
guard安装/恢复均fresh4MiB验证、仅应用、非应用核对；恢复原72/22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07。新3条user WAL保留，1371事件933884B/next4598/2MiB分区204800历史预算；结束另4只读USB确认原72/idle/WiFi/wake和voiceOFF，COM5释放。当前代码4既有文件精确恢复、仅撤5新增文件，原538源码hash一致；SDK/最新安装包不变、未Git reset。失败完整543源码zip b72e022704b0adf9b6040781aaf1055b0e834efc52d1d595d7f8a3855023b399及实录备份失败全部保留。误醒仍未修复，完整目标active/阶段progress/阻塞0，不采用。docs/VOICE_TLS_FRAGMENT_REPORT.md和473 closure/withdrawal记录。


### UX474近词分数可分性只读计划 — 2026-10-04

473按源小TLS片段实机三轮访问异常/断言已撤回、原72/off/off和1371历史恢复，上一目标轮有真实进展/阻塞0，完整双语/流式/快速复醒/应答目标不缩。474回到当前误唤醒，固定471已揭盲四条三头/armed/分数752帧，只一次闭式2-of-3次高分必要边界及普通话已知近词对的支配证据；原Q8=268/256ms保持，不扫阈/改窗/重NN/训练/读独立TEST/新音USB云Flash。用独立组合oracle核对order统计，再检查实际输入hash/连续性/armed与原事件。若正确词可接受的提高门槛均也容许该近词，明确仅否定这个决策家族，不能推广所有时间模式、现场每小时率或声学改善。≤60秒，失败停止不换来源；源码/安装包不变，证据wake-threshold-bound-ux474/plan。


### UX474提高阈值家族反例完成 — 2026-10-04

唯一只读752原帧/四已揭盲合成来源，hash/序号/armed/采样时钟保持；875极值与组合oracle证实2-of-3可接受最高阈等于次高分。固定ZH小燕/小言对，正的主/辅助两票最高566/372；负第70辅助388、第72/73主456/652、此前主未达268，支持距候选64/96ms在原256ms内。因此凡分别提高原两阈268、仍能保住该正例的门槛，也接受该负例；不是扫阈或新模型效果。三raw峰负[1038,1002,1026]亦逐项高于正[939,827,764]。仅否定这家族，不宣称全部时序分类不可行/原72同辅助后端/实际音素真值/现场小时误报。未重新NN/fit/DEVTEST/新音USB云Flash/构建，原538源码逐hash一致；前473最新只读原72/off/off、1371历史/933884B/next4598及2MiB/204800和USB释放状态不冒充本轮fresh查询。docs/WAKE_SCORE_SEPARABILITY_REPORT.md和474 report/closure记录；误唤醒及完整目标未完成，阶段progress/阻塞0。不再用纯提阈掩盖区分失败。


### UX475新序列表示C11合同计划 — 2026-10-04

474已证明候选固定两票/256ms下单提分数门槛不能区分既有ZH小燕/小言，上一轮progress/阻塞0。完整语音目标不缩，新475先验证一层reset-after GRU32（PyTorch2.6 r,z,n），原40维归一INT8/32，Q8 INT8权重、Q15状态、Q8 INT16 bias、2完整事件头；7170参数/6976权重B+388 biasB，state64/scratch64，513 sigmoid查表，定点INT32且明确对称舍入，不新增C++解释器/堆。现有原/拒绝权重不refit，新的表示只是待验证方案，随机模型不证明区分改善。默认生产CMake不加入，先严格C11 ASanUBSan、独立INT64全32state/两头、PyTorch reset位置及bias映射/流式、极值/别名/取消reset及C3编译对象state128/自身栈≤192/代码常量≤8192；host≤90秒。过后才能另冻结板上固定输入资源，再小训练闭环，不能直接几万TTS/多日训练或1000轮。当前零fit/DEVTEST/USB新音云Flash；原上下文/安装包/全部既有失败不改。证据wake-recurrent-contract-ux475/plan。


### UX475序列内核合同通过、尚未训练部署 — 2026-10-04

新增reset-after GRU32独立C11内核/host PyTorch2.6训练结构及固定Q8导出，不重用旧拒绝checkpoint，不加默认固件CMake依赖。7170参数=6976权重B+388 biasB，状态64/scratch64；513 sigmoid表、INT32及对称舍入/完整旧状态保留。严格C11 ASanUBSan，独立INT64原生2056帧/65792状态及NumPy1024帧/32768状态/实际C输出全相同；4 Torch streaming/layout/reset-n-bias/gradient/非法导出检查过，全部step退出0，未修包装或重跑。C3实际编译rv32imc/ilp32代码查表2086B、可写0，模型7364B/state64/scratch64，调用图自有峰栈128B≤192，无malloc/free/浮点未解析符号。是目标对象证据，不是实机整前端、整体heap/任务栈或识别效果；随机未训练不输出业务资格。
原538源码逐hash保持、新7文件冻结及zip7175b5e01956c01bedef5741652c674761d65f445d33de03a025459c94273533；无默认固件/SDK/上下文改动。零fit/DEVTEST/USB/新音/云/Flash，当前原固件状态只引用473最后读回，不冒充本阶段fresh查询。下一步固定输入和完整前端C3资源证明通过，才另冻结一次有限小训练闭环；所有旧失败保持。docs/WAKE_RECURRENT_KERNEL_REPORT.md及475 host/torch/C3/closure记录，完整语音goal active/阶段progress/阻塞0，误唤醒未修复，不发布随机模型。


### UX476完整前端加GRU实机资源计划 — 2026-10-04

上一目标轮475新C11 GRU32数值及C3对象通过为progress/阻塞0；误醒仍未修复，完整双语/异步ASR优先/VAD/流式/复醒/三轮自然衔接目标不缩。476只在当前完整Agent增加默认OFF的随机GRU资源适配，保留原C logmel40/相同E/L归一，明确untrained且检测返回恒false，禁止微音事件对话/工具/录音写入，不用独立小程序空堆冒充完整Agent。无新模型训练、DEVTEST、云或刺激播放。一正常1540096预算构建，一实机512帧256PCM逐logmel/输入/32state/2heads完全对齐，随后60秒实际麦克风；512max<32000us、P99≤16000us、USB256max<16000us、SDKheap≥49152/largest≥24576、无DMA/复位/上下文变化、推理无malloc。新的typed USB gru1拒冒充旧DS层trace；资源mode不得声称识别或三轮业务通过。所有旧失败不改；首fresh4MiB guard备份、仅应用、结束一律回滚原72/off/off/NVS/历史/录音保持/USB释放。过后才另冻结一次小规模训练，失败不补组/降门槛/训练。证据wake-recurrent-resource-ux476/plan。


### UX476完整前端与实机资源结果 — 2026-10-04

随机untrained GRU32默认OFF适配通过，progress/阻塞0，不是误醒修复。548源hash+归一来源固定；一次完整Agent正常预算构建 1491648B/1540096，余48448B，SDK/分区不变。主机ASan/UBSan前端512、分块256及gap/invalid/恒false/执行路径heap0；独立INT64参考512帧/16384state/1024heads一致。初始测试脚本-lm遗漏及未调用KISS导出符号归因错误保留失败日志，修正仪器后无模型/输入/gate变化，固件仍一build/设备仍一group。实机USB512×256 logmel/input/32state/2heads全等，CRC/seq错拒绝，max3691us；60s mic 1881块512max4411us/P99桶上界4500us，workspace4800B/SDKmin75372/largest69632，DMA/reset/detections0（恒false仅诊断权限不是误醒率）。fresh4MiB guard安装回滚非应用字节一致，原72 SHA 22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07恢复/off/off/WiFi/USB释放，ctx 1371/933884B/next4598及摘要/灯/音量不变，2MiB/204800B预算保持，latest包不改，无fit/DEVTEST/cloud/刺激/私音频。监听heap不能冒充完整网络三轮；完整双语/异步ASR优先/VAD/流式/复醒/三轮自然衔接仍未达成，不complete。下一phase另冻结一次有限localGRU训练。证据wake-recurrent-resource-ux476及docs/WAKE_RECURRENT_RESOURCE_REPORT。


### UX477有限GRU32双语声学试验 — 2026-10-04

上一目标轮476真实C3完整前端/512USB/60s资源门槛通过及原app全非应用字节恢复，为progress/阻塞0。477现在固定新GRU32/7170参数，不加载任何旧拒绝checkpoint；保留全部15791原TRAIN/labels/timing/sourcegroup/归一，原word_event2 loss/C2 posteriorquiet2判定。一次seed2026100477/final2000/Adam.003/CPU1/240s fit+一次allTRAIN120s评分；每步投影矩阵INT8Q8及biasINT16Q8可表达范围，不是PTQ静默clamp或已完成整数QAT。两范围/无效原子护栏先unit，所有旧99每语言/负≤18/near自然提前0及真zeroPCM门槛不降；失败停此配方，不补fit/挑epoch/改阈值/DEVTEST/export/USBFlash。TRAIN-only不证明真人、实机误醒率或三轮业务；完整双语/异步ASR优先/VAD/流式/复醒/三轮自然衔接原目标不缩、不complete。证据wake-recurrent-fit-ux477/plan及source hashes。


### UX477双语GRU唯一有限fit结果 — 2026-10-04

一次全原15791TRAIN fresh32 recurrent whole-event2/7170参数/final2000/CPU1/seed2026100477拟合85.353s，60000draw/seen11239；范围投影边界及nonfinite-before-write 2unit通过。一次全TRAIN原C2评分5.183s：ZH2915/3637，Yue2039/2658，neg706/9496，near57/796，natural161/2225，提前25，zeroPCM0。原门槛失败，停止此配方，不第二fit/续训/阈值窗口种子epoch扫/PTQ/C导出/DEVTEST/实机组。缓存审计无新infer：ZH零701/多10、Yue零597/多15，负误已见390/未见316，不主要重复规则或仅未抽行；不据此证明所有GRU能力/投影根因，步数等不同不公平比较旧CNN。checkpoint 88f8d78a3effa7ee2bc3049471b6e0f570d9638051e48a41dc57be254a80ee57、550源码快照/所有预测/事件/失败保留。训练USB云Flash0，结束另4只读实查72/off/off/WiFi/USB释放，ctx1371/933884/next4598与476相同；完整goal不缩/active，progress/阻塞0。下一动作仅在新序列表示检查源支持音节辅助监督及精确部署头投影，不挽救此二类-only拒绝模型；证据wake-recurrent-fit-ux477及docs/WAKE_RECURRENT_TRAIN_REPORT。


### UX478新序列表示的辅助监督部署合同 — 2026-10-04

477原二类-only2000step配方失败已关闭，全部旧失败保持。478只检查新GRU32共享状态/14辅助token+2主event，host7632参数，部署去14输出行精确保留原7170/7364B C ABI，不扩大原C11内核/前端/workspace。equal原timedCTC14与wholeevent2损失，仅已源支持词标签，未知OTHER不是音节转录；5120独立组成gradient、冻结16->2/160帧流式/系数bit同及拒绝未投影export三unit，≤30s。0fit/新dataset质量/DEVTEST/USB云Flash；合同通过才另冻结一个新的联合监督freshfit，不读取旧失败checkpoint或续训二类-only。合同不是声学通过/误醒修复/完整业务goal完成。


### UX478辅助训练头合同结果 — 2026-10-04

三unit通过：5120独立组成loss-gradient值完全相等，host16->deploy2模型系数/共享GRU状态逐值相同、160帧整体及分块输出近浮点精度一致，部署7170参数/7364B模型无改动且未投影16头拒绝export。host7632参数，辅助14行训练后移除，C11核心/40前端/原已测4800工作区不扩大。552源码hash/source.zip固定，0fit/质量评分/新dataset/DEVTEST/USB云Flash。仅合同成立，误醒/声学质量/全业务goal仍未达成；后续仅另注册一fresh辅助监督fit，不续训477已拒绝配方；progress/阻塞0。证据wake-recurrent-joint-contract-ux478/receipt、closure。


### UX479明确音节辅助监督的唯一有限GRU训练 — 2026-10-04

477 event-only2000配方已经拒绝，不续训或加载；478三合同通过。479另注册fresh GRU32/14辅助+2event/7632host，部署精确去14行仍7170参数/7364B，C3核心/前端/资源ABI不变。全原15791TRAIN/原真值/时界/来源/归一及原C2判定/gates保持；equal原timedCTC14+wordevent2固定损失，OTHER不冒充自然负音节。仅一fresh seed2026100479/final6000/Adam.003/CPU1/420s，矩阵bias原Q8可表达范围投影；一次真实学到模型投影fixture+全TRAIN120sscore，失败停此配方beforePTQ/export/DEVTEST/USBFlash，不第二fit/seed/epoch/阈值窗口lossweight扫。训练器一个字段分支，false路径保持原事件-only定义；旧550/552源快照保留。完整双语/异步ASR优先/VAD/流式/复醒/三轮自然衔接goal保持active，声学/现场误醒尚未通过，不complete。证据wake-recurrent-joint-fit-ux479/plan。


### UX479联合GRU有限fit已拒绝 — 2026-10-04

fresh seed2026100479/final6000/CPU1/Adam.003/host7632/部署精确去14仍7170；唯一fit286.211s/180000draw/seen14199；实际学到模型投影系数/状态bit同、输出差1.9073486328125e-06，不冒充整数声学。一次allTRAIN原C2评分3.582s：ZH3197/3637(87.90%)、Yue2116/2658(79.61%)、neg151/9496、near51/796、natural38/2225、提前28、zero0，原各门槛失败，停此配方beforePTQ/export/DEVTEST/USB组，不第二fit/续训/扫。不同步数seed目标不因果比较477。缓存参数/事件审计无新infer：零ZH431/Yue518、多1/8、已见负误126/151；最终269/7424矩阵边界(3.62%)、重复投影1245441，不等于不同元素数/已证明根因。下一动作先有限检查最终冻结目标的约束梯度方向，不盲试新配方。checkpoint c0216fdd60dcadbcc129c83089e79947ae11e6ea9f138ca6cb04770fe14511b7及552源快照/所有证据保留；4只读closing实查72/off/off/WiFi/USB释放/ctx1371/933884/next4598不变，训练USB云Flash0；完整goal保持active/progress/阻塞0/误醒未修复，不complete。详docs/WAKE_RECURRENT_TRAIN_REPORT及wake-recurrent-joint-fit-ux479。


### UX480冻结模型权重范围的有限梯度证据 — 2026-10-04

479失败配方已停；仅从现存最终checkpoint/固定6000抽样序列重建真实最后一batch并核对全部visit-count，不按错误选择素材。CPU1/float64/30s一次backward；矩阵处在原Q8边界且梯度下降向外的元素形成单位L2方向，固定eps1e-4、原点/向外/向内共3次objective评估及中心差分，不epsilon/样本扫或optimizer步，不事件判定/质量评分/新fit/续训/DEVTEST/USB云Flash。记录约束是否实际限制此batch目标，不能据一batchloss声称误醒根因、扩范围即改善或新模型合格；552root源不变/完整goal保持。


### UX480冻结梯度诊断结束 — 2026-10-04

实际最后batch/抽样访问次数一致，CPU1/float64，3次目标评估、1次反传，用时1.900s；约束外向梯度L2=0.325881375，固定eps1e-4向外loss 0.530252424402→0.530219857183，中心差分相对误差5.9e-07。仅证明该batch的Q8约束活跃，不证明误醒根因或改范围后的质量；0优化/fit/事件评分/USB/云/Flash，旧checkpoint和552源码不变。用户最新报告频繁误醒，后续转向当前生产模型的有限同板复核，暂不扩展GRU试验。完整目标保持active，误醒未修复。


### UX481用户频繁误醒反馈与独立Q6表示 — 2026-10-04

四只读USB实查当前0.11.72-summary/wakeoff/voiceoff，ctx1371/933884/next4598不变。原复核路径也有近词误触，暂不重复部署旧拒绝候选。480只证明冻结batch矩阵范围约束活跃；新增独立tagged Q6矩阵/Q8偏置/Q15状态入口，保留旧Q8入口/布局/默认选择。Q6矩阵[-2,127/64]，模型7368B（仅多4B格式tag），状态64/scratch64，最坏reset积1610612736在INT32内。先一次各格式native ASAN/UBSAN与独立INT64/Torch/导出/无效tag和别名测试；0fit/声学质量评分/新录放/Flash。仅数值合同，不称实际误醒修复；通过后才能验证C3资源，再考虑一次全新有限训练。旧失败门槛保持，完整goal仍active。


### UX481 Q6主机合同验证结束 — 2026-10-04

新旧两格式各native ASAN/UBSAN及独立INT64/NumPy/PyTorch通过，累计7184帧，8项Python合同（各4）。非法Q6tag/错对齐/与tag别名无写入，Q6扩展矩阵范围且bias仍Q8，旧Q8分数/状态精确不变。C3目标对象text+只读2224B/dataBSS0，自有函数调用栈上界160B，不含libc或整任务峰值。最后打包误用了旧receipt不存在的passed字段，driver exit1原记录保留；正确complete字段与9个exit0命令核对后关闭，没有重跑测试。555源快照保留。仅数值/编译前提；还没有C3运行时、训练或误醒质量改善，原72/off/off未改。


### UX482独立Q6表示的实机资源准入 — 2026-10-04

481数值合同通过后，保留同32通道/40前端/64状态64暂存。独立Q6模型指针与格式位复用旧工作区填充，预期C3仍4800B；必须实际profile确认。默认OFF且仅no-action resource模式允许显式Q6，tag/源码SHA检查，旧Q8入口保留；本阶段随机权重绝不触发录音/云/动作。一次全Agent正常预算构建，一次guard备份/仅app写入/256固定CRC帧精确对照/60秒麦克风资源，原32ms/P9916ms/SDK48KiB/连续块24KiB门槛不降；结束强制完整校验恢复72/off/off，NVS/ctx2MiB/clip448KiB保持。0训练/新声学质量评分/扬声器回放。只有实际资源通过后才允许另一次fresh有限Q6训练，不延长479拒绝配方。原误醒/完整业务目标仍未通过。


### UX482 Q6实机资源验证通过 — 2026-10-04

一次正常全Agent构建1491888B/1540096B，appSHA 588d33769883598af90b225b23e4f8c38235c3ba44f0cae201714749b9038568；CPU前端与Q6原核固定256CRC帧完全相同，错误CRC/序号拒绝，USB256max3261us。60秒真实麦克风1881个512块，max4484us/P99上界4500us，SDKmin74956B/最小连续块69632B，C3工作区仍4800B、host4808B（格式位占旧填充），推理分配0、DMA丢失/复位0。host夹具首次把Q6常量赋给旧无tag类型而编译失败，失败保留，仅修夹具类型及weights字段后执行尚未运行的检查，旧Q8适配器也通过。随机模型永远detected=false，不能称误触为0或声学质量通过。guard两次后整4MiB逐字节等同原始Flash，包括NVS/ctx2MiB/clip448KiB；原72/off/off及USB释放，ctx1371/933884/next4598保持。0训练/云/声学播放，监听资源不等于整轮语音最低堆；完整goal仍active。证据wake-recurrent-q6-resource-ux482及entire-trial-preservation.json。


### UX483单一权重范围因素的有限训练 — 2026-10-04

482实机Q6资源与全Flash恢复通过。保持479同seed2026100479/最终6000步/CPU1/Adam.003/clip1、完整15791TRAIN/原labels时界来源/归一/抽样/14音节+2事件等权目标/C2判定及原门槛，只改变显式Q6矩阵可表达范围，bias仍Q8。一次全新初始化，绝不加载479checkpoint，先4范围护栏unit，拟合420秒上限、一次allTRAIN120秒；核对访问计数用于受控比较。失败立即停止此配方beforePTQ/DEVTEST/C权重导出/现场组，不续训/调窗阈值/再挑seedepoch。不把TRAIN改善当现场误醒或真人泛化证明。训练无USB/云/Flash，实际自动监听继续关闭；完整goalactive，误醒尚未修复。


### UX483受控Q6训练被拒绝 — 2026-10-04

一次fresh6000步/seed2026100479/CPU1，fit 283.128s，唯一原全TRAIN评分3.586s；4范围护栏unit通过，555源码与注册zip逐文件hash一致。与479的visit-counts逐字节相同，输入/目标/抽样/判定/原门槛保持，单一因素为矩阵Q8→Q6可表达范围。ZH3202/3637(88.04%)、Yue2261/2658(85.06%)；负误151→434/9496、近词51→60/796、自然38→91/2225、提前28→69，不能用召回改善掩盖误触增加。原门槛未通过，此配方立即停止：0 PTQ/DEVTEST/C导出/云/刺激音/Flash，不续训或扫seed/epoch/阈值。实际训练模型去辅助头的输出差1.90734863e-06不证明整数声学。结束4只读USB查询确认原72/off/off及历史全部统计不变，USB释放；误醒未修复，完整目标未完成。checkpoint 5e95a3f325b8193763d16c690c08902c94b5ffc6b13ad6466c3c9455e1066404；详WAKE_RECURRENT_TRAIN_REPORT及wake-recurrent-q6-fit-ux483/controlled-comparison.json。停止盲试训练，保留实际误触录音与旧拒绝证据。


### UX484冻结辅助头的有限诊断 — 2026-10-04

479/483配方均已拒绝。本阶段只问现存14音节辅助输出是否也在已知误触时认成完整词。固定先24近词/24自然负误、每语言16原有效正例、16原干净负例，共最多96条TRAIN；每个冻结checkpoint一次CPU1浮点前向，核对原2类缓存分数，保留全段与原事件前缀的贪心音节序列。60秒上限、0fit/解码重判/PTQ/C导出/DEVTEST/USB云Flash，不改变模型/判定或旧拒绝门槛。选择过的失败和控制只作诊断，不充当准确率或现场根因。若辅助头也有完整词误认，不能把保留辅助头说成现成修复；若有区分也仍需另外完整验证。


### UX484辅助音节头诊断结束 — 2026-10-04

固定96条原TRAIN、两个冻结checkpoint各一次CPU1前向，共2.809s；192条全段及原事件前缀序列全部保留，原2类缓存分数差最大2.86102295e-06。24个选中近词里Q8原误12，其中4个也在原事件前缀输出完整四音节；Q6原误18，其中8个同样误认。自然24里Q8原误10而完整词0，Q6原误20而完整词1。原Q8有效的普通话控制16中仅10个具有辅助完整词，粤语16中原事件前仅14个（全段15）。简单辅助贪心确认不能作为现成修复：会保留部分近词误触并挡住正确唤醒；不能由小选集推断所有概率解码皆无效。0fit/重判C事件/PTQ/导出/DEVTEST/云/Flash，原479/483拒绝和全部门槛保持。四只读USB实查原72/off/off和完整上下文统计不变，USB释放；误醒与完整目标仍未完成。另核实UX250只用了电脑SenseVoice逐字规则，并非当前Qwen云ASR试验；不同路线须先证明前缀采集预算可行，不能直接增加64KiB常驻环形缓存或开展云调用。证据wake-recurrent-head-audit-ux484。


### UX485唤醒前缀的固定C11合同 — 2026-10-04

484有限音节诊断已完成，不能直接采用辅助贪心确认。核实当前工作区会被candidate bind及engine adopt清零，不能未登记地借用。新增不启用的调用方持有前缀模块：16k单声道/512样本/64块=2.048秒，复用现有IMA4bit，每块独立预测器/索引、保留首样本、CRC，总16896B；状态可放在17024B空闲prefix的余下128B。先固定主机状态、丢帧、CRC/别名/时钟和生命周期测试，再C3对象大小/静态栈；0设备动作/云/Flash，不改唤醒模型或任何旧门槛。音频是有损压缩，未测真实词语ASR效果；单纯能容纳不证明实际借用安全或整轮48KiB。真实adapter须在全部工作区复用路径撤销/等待writer，并在ASR读完前禁止candidate覆盖。完整目标继续开放。

### UX485前缀合同及实际覆盖检查通过 — 2026-10-04

首轮只因短别名夹具编译失败，原错误与源码保留；修夹具后四组ASAN/UBSAN检查只执行一次。C3状态48B/数据16896B/对象1348B/dataBSS0，无分配IO，feed含编码保守静态调用栈96B（不含libc/task）。固定13条旧实际触发PCM一次C原核重放831块，首样本/时钟一致；ZH源WAV全6/6、Yue4/6，两条未来尾64/114ms，头部全部保留。容器界不是人工音节界，codec波形SNR22.74–27.61dB不是采集信噪比/ASR效果。模块未加入固件，0fit/云/Flash；必须先解决后触发采集和全部arena撤销/读取生命周期，不能靠扩大回看等待通过。四只读USB核实72/off/off/全部上下文不变并释放串口。证据wake-prefix-contract-ux485/closure.json及VOICE_WAKE_PREFIX_REPORT，误醒和完整目标仍未通过。

### UX486现有云ASR确认路线的有限可行性检查 — 2026-10-04

在继续arena接入前，先核实云ASR本身能否区分正确词与已知误触近词，避免给不成立的确认器实现整条采集链。固定旧24条公共TRAIN实机回放中的13原触发，先唯一近词，再12正例；只用原PCM与时钟，理想连续采集至原事件后128ms，经485同一C模块保持2.048秒，全部源WAV容器完整。这个文件重放不证明当前固件真的连续采到尾部。千问技能fun-asr有界文件客户端、现有fun-asr-realtime与固件800ms/semantic punctuation false参数、默认可信TLS、无热词/提示/别名，沿用250逐字规则。先13文件离线预检；最多13次现有账户ASR，180秒总上限/35秒单次，首个判错或云会话不完整即停、无重试或规则补救。0训练/DEVTEST/新录音/Flash/固件采用；成功也只允许后续评估，不等于现场修复或一秒。输入、脚本/技能版本及结果分别封存。

### UX486逐字确认被拒绝；UX487仅作压缩因素对照 — 2026-10-04

实际2次现有fun-asr会话均协议完整、原音频发送hash一致，近词返回“你好 小燕。”而拒绝；首条正确普通话返回“你好，小猿。”而错误拒绝，依计划停止其余11次，没有加别名/热词/重试。完整WAV区间仍不保证逐字识别，确认器不采用。487只把这同一失败正例的相同源区间换成未压缩PCM，模型/参数/规则完全不变，唯一1次ASR、35秒上限、不重试；区分压缩是否为这次失误的必要前提，不把两个随机服务输出当唯一原因证明。0训练/新录音/Flash/设备采用，当前误醒仍未修复。

### UX487原始PCM通过；UX488单一全采样率缓存候选 — 2026-10-04

487唯一原始PCM会话完整，发送32768样本hash一致，返回“你好，小言，”；旧四位缓存返回“小猿”。单对不证明压缩为唯一原因，也不能就此否定云ASR。488只检查16kHz逐样本μ-law量化，CPython现有audioop作参照，不降采样/改时钟/改名称规则/加热词。它不是标准8kHz G.711流；预计520B独立CRC包×64=33280B/状态≤64B，相对当前29836B idle allocation增量上限3508B，跨阶段metadata/ASR分离仍未实现，未证明C3预算。沿用486同13来源/区间/顺序和首错即停，最多13云会话/180秒，无重试；不采用失败编码器或扫多编码/阈值。准备时13文件已生成，内置audioop无__file__导致仅来源元数据失败，原程序/异常保留，用已创建文件校验补齐，不再次编码或新增云请求。0训练/新錄音/烧录；整轮目标与误醒状态保持。

### UX488逐字规则停止；UX489既有名字规范的独立检查 — 2026-10-04

488实际4次完整会话，无重试：近词“小燕”拒绝，ZH前两条“小言”通过，第三条“小严”触发逐字拒绝，停止其余9次。μ-law波形量化SNR35.91–36.98dB，未测C3时序或听感。现有prepare_compact.py早已规定7个普通话同音写法并排除“小燕/小猿”；489从原AST取完整既有表并冻结hash，不由新结果添加字。新名字候选谓词不使用来源语言标签，四次缓存只读复用，最多9个未执行源会话/150秒/35秒单次，参数/音频字节不变；首错或不完整即停，不重试、加上下文/热词或改原负例。这个不同规则的检查不能覆盖原488失败，也不证明粤语同音字归属、真人泛化、每小时误醒或C3业务；任何歧义保持明确记录。0训练/录音/Flash/固件采用。

### UX489有限确认路线停止，设备保留 — 2026-10-04

唯一新名字规范检查只复用4个原完整会话并执行4个新会话，全部task identity/PCM发送hash/WAV及源码zip一致。已知普通话6/6保留、近词“小燕”拒绝；首条粤语正例返回“你好，小饿，”而拒绝，停止剩余5条、未补名或调用上下文。486–489总11个新增ASR/0重试/0fit/0新录音/0Flash，旧逐字和原本地模型失败均保持；已知源并非独立真人评估。路线未满足两种语言，停止同类编码/名称/上下文循环，不增加33KiB缓存或采用固件。558原源码逐文件保持，四只读USB实查原72/off/off、完整context相同（1371/933884/next4598，2MiB保持），串口释放。完整goal仍active，原误醒未修复；下步本地有限核验须独立登记和实测，不能凭当前缓存源码合同宣布改善。证据WAKE_ASR_CONFIRMATION_REPORT及wake-asr-name-normalization-ux489/closure.json。


### UX490: explicit ASR/TTS scratch loans (2026-10-04)
Keep the full voice objective and false-wake failures. Replace duplicated PCM only through explicit caller-owned, aligned raw-storage loans; legacy typed-stack fallback stays. Native byte/protocol/alias/cancel/BUSY checks and actual C3 stack costs precede one unchanged-stack three-turn physical group. Do not trim stacks from an idle watermark, change acoustic/TLS/DMA/context budgets, or call resource improvements recognition success. At most a separately registered smaller-stack group follows proven margins; all failed gates stay failed.


UX490 result: explicit buffer contracts passed7 nativeO2 and2 strictO3 sanitized checks; normal ZH/Yue/ZH first/full/task3/3 and dynamic ack3/3. SDK49224>=49152 but network stack820<1536: no trim/adoption. LTO inlined old typed464-sample wrapper into voice_run,1024B extra live+LLM call-frame storage. Next step separately freezes an explicit noinline boundary and validates the final linked scope before one changed-firmware three-turn group. Original false-wake/1s/104ms failures remain.


### UX491: contain the legacy ASR stack scope (2026-10-04)
After490net820 failure, freeze one noinline boundary to keep recorded-mode PCM out of fast/live+LLM frames. Stack reservations remain9216/7168/6144,all models/budgets/latency gates unchanged. Final linked scope proof and at most one changed-firmware normal3-turn group precede any further memory decision; preserve490 failed group.


UX491 result: legacy ASR noinline boundary restores final linked network frame656B and actual margin1844B without stack resizing. Sole normal3 group first/full/task3/3,dynamic ack3/3,TTS2792/2796/2792. Group SDK checkpoint49204B but post-cleanup46464B: full observed lifetime48KiB FAIL; no adoption. Preserve first-round alignment uncertainty, >1s later-round acoustics, old miswake/104ms failures. Original72/off/off restored;2MiB/history204800 unchanged.


### UX492: fixed actual-board false-wake pair (2026-10-04)
Latest frequent-miswake request: one original72 vs existing fixed491 verifier18-source pair,4positive/language+10revealed negatives,gain.35/sourceRMS.14/threshold740,no fits or sweeps. Preserve bilingual valid hits; reduce negative triggers; unchanged resource gates. No default adoption while full-voice heap46464 and old104/96ms gates fail. Fresh pre-playback backup,guarded app install/restore,original72/off/off andUSB released.


UX492 result: original72 and fixed491 ordinary wake replay each18,ZH3/4,Yue4/4,N2/10 in both. Both same near-word negatives fired; no false-trigger reduction in fresh microphone-domain test. Keep candidate unadopted,prior stored0/51 not current field protection. Listening SDK65688/63792,largest61440/59392 passed; no cloud/fits/firmware edits. Original72/off/off restored,data unchanged. Next only bounded input-domain diagnosis,not unchanged replay or threshold sweep.


### UX493: fixed complete-phrase probability check (2026-10-04)
UX492 actual fixed scalar guard failedN2/10. Before further acoustic work,check one existing all-class C11 suffixbeam8 on96 saved Q8 auxiliary outputs; no new fits/acoustic forwards/USB/cloud/Flash. Original-event prefixes only for causal confirmation,full windows separate. Preserve all32 valid controls and reject selected original negative errors,else stop. No rule search or requalification of rejected acoustic model; original whole goal/gates retained.


### UX493 fixed complete-phrase decoding closed — 2026-10-04

One unchanged all14-class C11 suffixbeam8 on96 saved Q8 outputs,12288 decoder frames,0.336s. int16 Q8 diagnostic conversion finite/in-range; no acoustic forward or PTQ. By ORIGINAL first-event prefixes,26/32 valid controls retained (ZH11/16,Yue15/16),6 dropped; original22 selected negative errors reduce to7 (near6,natural1),so gate FAIL. Full windows differ and cannot rescue causal prefix failures. State3856B is host sizeof,not a C3 resource proof. 0fit/USB/cloud/Flash,no decoder/model/window/threshold search or firmware adoption; old Q8/Q6 acoustic rejections and whole goal/gates remain. Offline stage preserves last UX492 device evidence,not a new status read. Archive and terminal receipts retained at wake-phrase-beam-ux493. Next: new-data evidence review before any different bounded training,not another decoder sweep.


### UX494 named-word auxiliary contract registered — 2026-10-04
UX493 probability confirmation rejected. Distinguish all8 complete source names as semantic word categories,without inventing phone/tone labels;9 auxiliary logits + original2 event logits shareGRU32,projected runtime unchanged7170/7364B. Equal constituent loss,positiveoriginaltiming preserved,unknown/incomplete no namedwordevent. Contract checks only:0fit/scoring/USB/cloud/Flash. Later training separately frozen; all original gates remain.


UX494 closed:3 independent named-word checks passed; existing training projection and phonetic projection tests passed once. Named9/2 heads7467host parameters; shared state/runtime7170/7364B exactly preserved. Source-label guard rejects fabricated IDs/mismatchedtruth;729 enumerated paths per row and every gradient agree with original-normalized timedCTC. No trained model,quality/device/fieldclaim. Optional mode defaultfalse,existing event/phonetic modes retained. Laterone freshfit separately registered,originalgateskept. Evidencewake-named-word-contract-ux494.


### UX495 named-word auxiliary single fit registered — 2026-10-04
New source-supported semantic9/event2 supervision; existing knownTRAIN15791,sourcegroups/truth/time/blank unchanged. One freshGRU32/7467 hostparams,Q8projection,seed2026100479/final6000/CPU1/Adam.003/batch32/draw30+2blank. Original runtime7170/7364B and C2rule unchanged. Equal constituent objectives,no phonetic labels inferred. Maxfit420s/score120s,oneallTRAIN evaluation;original99%perlanguage/N<=18/near0/natural0/early0/literalblank0 gates. Stop recipe on anyfail beforePTQ/export/DEVTEST/USB/Flash,no seed/steps/threshold search. Same seed/source draws but differentauxwidth/init,so not singlevariablecausalproof. No realcaptured471 DEV source moved intoTRAIN. Fullgoal and oldfailuresretained.


### UX495 preflight failed; UX496 signed-ID storage repair — 2026-10-04
495 existingclass_idint8 rejected at trainer89 before optimizerloop;requestcounterfits1 was intent,actualsteps0/no checkpoint or visits. Rawfailure/terminalretained.496 accepts signedintegerstorage then widensvalues toint64 withsame range/truth guard;floating/unsigned stillreject. Threechangedcontracttests pass includingactualint8 ABI. Register same seed/6000/420sfit/120sscore/immutableinputs/gates as495;one actualfit only,nofailedcheckpoint restart or candidate search. NoDEV/USB/cloud/Flash.


### UX496 named-word recipe closed — 2026-10-04

495 preserved preflight failure had0optimizersteps;496 executed the sole actual6000step fit,268.996s/CPU1 and onceallTRAIN score3.586s. Same180000 drawcounts/hash as479,14199/15791rowsseen; auxvocabulary/width/outputinit differ so no singlevariablecausalclaim. Originalinput/truth/time/sourcegroup/blank preserved. ZH2982/3637(81.99%),Yue2085/2658(78.44%);N321/9496,near25/796,natural103/2225,early36,literalblank0. Original99%/N18/near0/natural0/early0 gatesFAIL:recipeSTOP,noPTQ/export/DEVTEST/newaudio/cloud/Flash/adoption. Frozen trained9aux removal preserves sharedcoefficients/state,mainfloatmaxdifference2.38418579e-6;not integer acoustic evidence. Checkpoint1f1c4abf9b925a27e09bf46ea41dc3f9575efc2523487a5b49c002dff82befb1. Fourread-onlyUSBqueries verify original72/off/off and1383events/939380B/next4610,2MiB/history204800;USBreleased. All old failures and completevoice/1s/3turnobjective remain. Evidencewake-named-word-repair-ux496,with495preflight record and494contract.

UX496 final review: source manifest still exact;changed/new text whitespace clear;training driver terminal exit0;recipe failed quality. Closing preflight usedtrainingPython withoutPySerial and failed beforeUSB,originalscript/errorretained;standard-library/hash close thenusedexistingIDFserialPython,4reads/samecontext,no dependency install or extra fit. VerifieddeviceWiFi true/72/off/off;USB released. LinkedChinese report WAKE_NAMED_WORD_REPORT and primary currentconclusion updated through496. Fullgoal notcomplete.


### UX497 frozen named-head causal diagnostic registered — 2026-10-04
Previousgoalturn progress:one named-word fit failedoriginalgates,no deployment. First24mainnearerrors/24naturalerrors/16valid perlanguage/16clean negative=96 selectedknownTRAIN,beforeonefrozenforward. Namedclass1 competes withlse ofall8nonkeyword classes,round-awayQ8finite/inrange,actualunchangedC2decoder. Prefixat ORIGINALfirstevent,fulltrace separate;retain32validbyoriginalevent/reject48errorprefixes/16cleanwholeelseSTOP. Max60s/CPU1,0fit/PTQ/exports/DEVTEST/USB/cloud/Flash,not rescuefailed496 mainrecipeorprovefield/humanquality. No alternativeheads/projection/rule/model search.


### UX497 named-head audit closed — 2026-10-04
Onefrozen96windowforward1.234s/0fit,originalmainfloaterror2.86102295e-6/C2events exact. Namedkeywordclass1 competes withall8nonkeywordclasses using logsumexp;unchangedactualC2rule. Selectedvalid ZH16/16/Yue16/16 byoriginalevent retained,selectednear24errorprefixes still16/natural24still20;16cleannegative remain0. GateFAIL/STOP,no larger corpus/model/projection/threshold search. Source-level greedynear exact1/24,natural2/24; completeword headalso misrecognizes,not merely discardedinfo. 0PTQ/export/DEVTEST/USB/cloud/Flash,old496rejection/fullgoalgateskept. Latestactualdeviceevidence496,notnewread. Nextcapacityprototypefirstrequiresinteger/ABI/32ms/48KiBproofbeforefitting,not a claimwiderwillfixquality. Evidencewake-named-head-audit-ux497.


### UX498 GRU64 fixed-state contract registered — 2026-10-04
Named-headdiagnosticfailed36/48;firstverifylargerrepresentationbudgetwithout assumingquality. Same40/x32/resetafter/Q8+Q15/event2;tagged64model20872B/20482parameters/state128+scratch128. ShareprivateC11mathbutkeep32Q8/Q6publicABI/results.64Q6notprovided: recurrentresetproductcanreachINT32overflow,64Q8boundsstayvalid. Default64notbuilt. Independentwideoracle/extremes/alias/tag/streaming/old32exact/nativeASANUBSAN/C3objectgatebeforeboardresourcephase;0fit/dataset/USB/cloud/Flash,allwholegoals/gateskept.


### UX498 close / UX499 register — 2026-10-04T04:33:55.774078+00:00
498 strict native test compile failed before Python64 validation; preserve failed receipt/source. Enlarge only test backing storage in499, keep all invalid cases and gates. No model/math/default-build change, no fits/USB/cloud/flash. One bounded contract continuation; no quality or device-resource claim.


### UX499 GRU64合同闭合 — 2026-10-04T04:35:58.925389+00:00
5新/8原Python检查、64整数2048及原32两格式各1024冻结before全等、原ASAN2056/64极值2048+alias/tag/reset/export通过。C364对象2062B/128B自有栈，32双格式2332B/240B；无heap/float符号。零训练/数据/USB/cloud/flash，默认构建不动，不是识别/整轮48KiB证明。498仪器背板编译失败保留，499只修测试背板。docs/WAKE_RECURRENT64_REPORT及两个closure；目标仍active且误醒未修复。


### UX500 registered — 2026-10-04T04:37:30.969201+00:00
One fullAgent64/Q8 random/untrained/alwaysfalse resource check before training:512USB/252Btrace,60smic,32msmax/16msP99,SDK48KiB/largest24KiB;1540096normal app and2MiB/204800history retained. C364workspace predicted4928B mustmeasure. Freshfullflash guard,app-only,alwaysrestoreoriginal72/off/off,entiretrial nonappbytecompare. Zero fit/DEVTEST/cloud/stimulus/privateaudio; not falsewake/three-dialog admission. Failure stops. Source snapshot and plan inwake-gru64-resource-ux500.


### UX500 资源验证闭合 — 2026-10-04T04:46:59.293958+00:00
FullAgent random64/Q8 alwaysfalse passed:512 USB exact/max4966us;60s/1881512blocks max7383us/P99upper7500us;deviceworkspace4928/native4936;SDKmin76860/largest69632;DMA/reset/action0. App1505296B/spare34800B,2MiB/history204800 preserved. Freshfullflash guards/app-only and entiretrial allnonappbytes identical;original72/off/off restored,ctx1383/939380B/next4610,USBreleased.0fit/DEVTEST/cloud/stimulus/privateaudio,not quality/FAR/wholevoiceheap proof. New64 OFF;original32 frontend512trace unchanged. docs/WAKE_RECURRENT64_REPORT and closure/resource evidence. Goalactive,miswake notfixed,next separatelybounded64training.


### UX501 registered — 2026-10-04T04:50:47.961825+00:00
After500 admission,one new64 named9/event2 representation pilot:21067host/20482runtimeparameters,sameTRAIN/truth/time/seed2026100479/6000steps/draws/loss/C2qualitygates;CPU1≤600sfit+120sscore. Verifyprojection/Q8only/oldunits beforeoptimizer. No threshold/window/seed search;failure stops beforePTQ/DEVTEST/export/USB/flash. Sameoldfailureskept/notcausalfieldclaim. wake-gru64-fit-ux501/plan.


### UX501 terminal — 2026-10-04T05:00:18.908250+00:00
{"stage": "UX501", "terminal": true, "classification": "progress", "UTC": "2026-10-04T05:00:18.908250+00:00", "fits": 1, "driver_exit": 0, "fit_complete": true, "quality_passed": false, "TRAIN_only": true, "independent_generalization_proven": false, "field_false_wake_rate_proven": false, "false_wake_fixed": false, "adopted": false, "goal_complete": false, "exports": 0, "PTQ": 0, "DEV_TEST_reads": 0, "new_acoustic_USB_frames": 0, "Flash_writes": 0, "cloud_calls": 0, "fit_seconds": 353.625, "scoring_seconds": 10.908, "runtime_parameters": 20482, "host_parameters": 21067, "final_step": 6000, "checkpoint_sha256": "cd8bf1c7ed5df9aab76a49af1edfd9e64e418060e9e2a9e3b236baa513e8d228", "same_visit_counts": true, "visit_counts_sha256": "6e45002afc7f4132adb084085d6c804e6af9add86b41357a3512c0a96ca93b0a", "optimizer_draws": 180000, "positive": {"zh": {"correct": 3234, "total": 3637, "recall": 0.8891943909815783}, "yue": {"correct": 2327, "total": 2658, "recall": 0.8754702784048156}}, "negative_triggered": 194, "known_near_triggered": 34, "natural_triggered": 62, "early_positive": 12, "gates": {"recall_each_language": false, "negative_rows_at_most18": false, "near_zero": false, "natural_zero": false, "early_zero": false, "literal_blank": true}, "projection_max_abs_error": 2.86102294921875e-06, "old32": {"positive": {"zh": {"correct": 2982, "total": 3637, "recall": 0.8199065163596371}, "yue": {"correct": 2085, "total": 2658, "recall": 0.7844243792325056}}, "negative_triggered": 321, "known_near_triggered": 25, "natural_triggered": 103, "early_positive": 36, "quality_passed": false}, "original_firmware_unchanged": true, "context_unchanged": true, "USB_released": true, "only_USB_queries": 4, "serial_firmware_verified": "0.11.72-summary", "execution_program_sha256": "1a7d1cd2e043e63dbfd59411c9c02b829bcd41f0fa78c72786f19513584a0950"}


### UX502 arithmetic explanation corrected — 2026-10-04T05:04:25.278690+00:00
64/Q6 tightaffinebound65535,resetproduct2147450880,roundoffset2147467264<INTMAX;earlier65536overflowclaimwastooloose. Preserveoldplans/source;currentcommentcorrectedonly,executabletokensidentical,noAPI/math/modelchange,0fit/USB/cloud/Flash.64/Q8scopekept and501qualityfailurestillrejected. docs/WAKE_RECURRENT64_REPORT plusclosureproof.


### UX503 registered — 2026-10-04T05:13:29.175025+00:00
Previous goal turnprogress:500 actual64 admission+501 onlyQ8 fitfailure+502 tighterbounds. Full bilingual/ASRfirst/VAD/streaming/rewake/3dialogue/topicalack scope remains. New64/Q6 typedtag,numericalcontractonly;weights1/64/range[-2,127/64],biasQ8/hiddenQ15,same40features/20872B. Typednestedarrayviews sharedmath preserveoldthreeformats exact.180s validation,object≤4096B/ownstack≤256B;no fit/data/USB/cloud/Flash/defaultCMake. Pass then separateactualresourcephase beforetraining,failstop/nogatesrelaxed. wake-gru64-q6-contract-ux503/plan.


### UX503 close / UX504 register — 2026-10-04T05:18:03.768895+00:00
StrictC11 nestedarrayqualifier implicitvoid conversion failed before numericalsteps; retain503logs/source.504onlyexplicitreadonlyarraycasts,allwarnings andgatesretained. No fit/data/USB/cloud/flash;one bounded contractcontinuation.


### UX504 contract closed — 2026-10-04T05:20:24.580814+00:00
18Python/oldthreeformat1024beforeexact/newQ6INT64 2560/nativeextrema2560 includingboth resetproductextrema passed;Q8native2048/32ind2056. C364twoformat2374B/240B ownstack,32twoformat2344B/240B;noheap/float symbols. Explicitreadonly nestedarrayviews fixed503 strictcompilefailure retained. Same20872B/tagseparate/noCMakechange.0fit/data/USB/cloud/Flash/adoption,notC3runtime/qualityproof;501failurekept. NextfullAgentresourcephase. docs/WAKE_RECURRENT64_REPORT andclosure504.


### UX505 registered — 2026-10-04T05:21:50.947267+00:00
One fullAgent64/Q6 random/alwaysfalse resource phase,512USB exact252Btrace plus60smic;32msmax/16msP99/SDK48KiB/largest24KiB;normal1540096appbudget/context2MiB/history204800/448KiBclipheld. Beforefitfresh4MiBguard,app-only,alwaysrestoreoriginal72/off/off/allnonapptrialbytespreserved/serialrelease. Preserveoldthreeformats;no fit/DEVTEST/cloud/stimulus/privateaudio. Failurestop,no additionaldevicegrouporgatelowering. wake-gru64-q6-resource-ux505/plan.


### UX505 资源验证闭合 — 2026-10-04T05:35:48.514282+00:00
FullAgent random64/Q6 alwaysfalse passed:512 USB exact/max5502us;60s/1881512blocks max8006us/P99upper8000us;deviceworkspace4928/native4936;SDKmin75128/largest69632;DMA/reset/action0. App1505664B/spare34432B,2MiB/history204800 preserved. Freshfullflash guards/app-only and entiretrial allnonappbytes identical;original72/off/off restored,ctx1383/939380B/next4610,USBreleased.0fit/DEVTEST/cloud/stimulus/privateaudio,not quality/FAR/wholevoiceheap proof. New64 OFF;original32 frontend512trace unchanged. docs/WAKE_RECURRENT64_REPORT and closure/resource evidence. Goalactive,miswake notfixed,next separatelybounded64training.


### UX506 registered — 2026-10-04T05:36:48.893393+00:00
One fresh64 named9/event2 Q6 projected fit. Same architecture, initialization seed, TRAIN/targets/draws/6000steps/loss/C2/gates as501; only matrix projection range changes. New tagged Q6 runtime independently passed504/505. No seed or threshold sweep.
Before-code snapshot cb5ba303836a61ba034b234c27a0d52980b64bf43ebfca265f7e4dbf13425501; only3 declared trainer/test paths. One fit600s/one score120s, CPU1, fixed6000steps/seed2026100479, knownTRAIN only. Old gates and failures stay. No DEVTEST, cloud, private audio, Flash or acoustic USB. Four readonly closing USB queries allowed; goal active/miswake not fixed.


### UX506 terminal — 2026-10-04T05:47:05.424722+00:00
{"stage": "UX506", "terminal": true, "classification": "progress", "UTC": "2026-10-04T05:47:05.424722+00:00", "matrix_q": 6, "fits": 1, "driver_exit": 0, "fit_complete": true, "quality_passed": false, "TRAIN_only": true, "false_wake_fixed": false, "goal_complete": false, "adopted": false, "exports": 0, "PTQ": 0, "DEV_TEST_reads": 0, "new_acoustic_USB_frames": 0, "Flash_writes": 0, "cloud_calls": 0, "independent_generalization_proven": false, "field_false_wake_rate_proven": false, "fit_seconds": 359.731, "scoring_seconds": 9.973, "runtime_parameters": 20482, "host_parameters": 21067, "final_step": 6000, "checkpoint_sha256": "f31c9b36dda10e8cf5876519918ff70fbd7756c414523209fd8588d9314c6a75", "same_visit_counts": true, "visit_counts_sha256": "6e45002afc7f4132adb084085d6c804e6af9add86b41357a3512c0a96ca93b0a", "optimizer_draws": 180000, "positive": {"zh": {"correct": 3270, "total": 3637, "recall": 0.8990926587847127}, "yue": {"correct": 2345, "total": 2658, "recall": 0.882242287434161}}, "negative_triggered": 139, "known_near_triggered": 56, "natural_triggered": 29, "early_positive": 20, "gates": {"recall_each_language": false, "negative_rows_at_most18": false, "near_zero": false, "natural_zero": false, "early_zero": false, "literal_blank": true}, "projection_max_abs_error": 3.814697265625e-06, "old64Q8": {"positive": {"zh": {"correct": 3234, "total": 3637, "recall": 0.8891943909815783}, "yue": {"correct": 2327, "total": 2658, "recall": 0.8754702784048156}}, "negative_triggered": 194, "known_near_triggered": 34, "natural_triggered": 62, "early_positive": 12, "quality_passed": false}, "original_firmware_unchanged": true, "context_unchanged": true, "USB_released": true, "only_USB_queries": 4, "serial_firmware_verified": "0.11.72-summary", "execution_program_sha256": "2ee259da0f398dadafba1320838b3d8df81f4b912feb549e4ba15388c5813cc3"}


### UX507 registered — 2026-10-04T05:54:18.334240+00:00
One cached all-TRAIN audit of501 and506 outputs: unchanged actualC2 re-score against stored counts/first samples, independent probabilities of original timed whole-event target and unchanged8-frame decision, failure categories and high-confidence near errors. No model forward, fit, export, candidate selection or new head/threshold/window rule.
One60s cached-only audit, no source/model changes, 0fit/DEVTEST/cloud/Flash/acoustic USB, fourclosing readonly queries; previous turn progress, goal active.


### UX507 terminal — 2026-10-04T06:00:52.680958+00:00
{"stage": "UX507", "terminal": true, "passed": true, "classification": "progress", "UTC": "2026-10-04T06:00:52.680958+00:00", "seconds": 2.168, "fits": 0, "acoustic_forwards": 0, "DEV_TEST": 0, "cloud_calls": 0, "Flash_writes": 0, "no_runtime_rule_changed": true, "existing_failures_retained": true, "candidates_replayed": 2, "actual_C_replay_exact": true, "USB_queries": 4, "USB_released": true, "original_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "context_unchanged": true, "false_wake_fixed": false, "goal_complete": false, "adopted": false, "independent_generalization_proven": false, "whole_goal_still_active": true}


### UX508 registered — 2026-10-04T06:04:08.286279+00:00
One frozen506 TRAIN-only full11-head forward over15791 original rows plus blank. Compare projected2 logits to frozen predictions. Diagnose independent event2 and named9 semantics at original actualC2 first events. One declared coupling: wake=named1, no_wake=logsumexp(named0,2..8); replay unchangedC2 for diagnosis, not an adopted rescue of a failed model. No argmax/threshold/window/head selection or training sweep.


### UX508 terminal — 2026-10-04T06:10:57.931210+00:00
{"stage": "UX508", "terminal": true, "passed": true, "classification": "progress", "UTC": "2026-10-04T06:10:57.931210+00:00", "fits": 0, "acoustic_forward_passes": 1, "DEV_TEST": 0, "PTQ": 0, "exports": 0, "cloud_calls": 0, "Flash_writes": 0, "source_modified": false, "checkpoint_changed": false, "diagnostic_only": true, "original_candidate_still_rejected": true, "original_C2_replay_exact": true, "maximum_logit_difference": 7.62939453125e-06, "original_false_with_named_peak_non_target": 24, "near_false_with_named_peak_true_near": 8, "new_negative_previous_clear": 22, "USB_queries": 4, "USB_released": true, "original_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "context_unchanged": true, "adopted": false, "false_wake_fixed": false, "goal_complete": false, "independent_generalization_proven": false}


### UX509 registered — 2026-10-04T06:47:28.287875+00:00
One fresh64/Q6 named9/event2 fit with a predeclared cosine optimizer schedule .003 to .0003 over the SAME6000steps. Same model/seed/data/labels/time/draws/loss/C2/gates as506. Old constant-LR candidate remains rejected. No checkpoint continuation, extra epoch, loss/head/threshold/window/seed/model search.
506 final15 recorded minibatches loss mean .2799735988/std .1188980108 at constant .003 suggest residual stochastic optimizer motion, not a proven cause. A single fixed decay tests whether stable final optimization improves classification.
Onefit600s/score120s CPU1, no extra steps/new seed/selection;0DEVTEST/PTQ/cloud/Flash. Only3 declared host trainer/test paths, snapshot 72717f68ed560d6ec3c29d408e6fd95d2347f9039f53119c9781bb8c2274a78e.


### UX509 terminal — 2026-10-04T06:57:39.725633+00:00
{"stage": "UX509", "terminal": true, "classification": "progress", "UTC": "2026-10-04T06:57:39.725633+00:00", "matrix_q": 6, "learning_rate_schedule": "cosine_v1", "learning_rate_initial": 0.003, "learning_rate_final": 0.0003, "fits": 1, "driver_exit": 0, "fit_complete": true, "quality_passed": false, "TRAIN_only": true, "false_wake_fixed": false, "goal_complete": false, "adopted": false, "exports": 0, "PTQ": 0, "DEV_TEST_reads": 0, "new_acoustic_USB_frames": 0, "Flash_writes": 0, "cloud_calls": 0, "independent_generalization_proven": false, "field_false_wake_rate_proven": false, "fit_seconds": 353.989, "scoring_seconds": 10.091, "runtime_parameters": 20482, "host_parameters": 21067, "final_step": 6000, "checkpoint_sha256": "4bc47b7ad9ed84e23ae7bf6c438af00a2aee4910e7ffb6636814a0339e238eaa", "same_visit_counts": true, "visit_counts_sha256": "6e45002afc7f4132adb084085d6c804e6af9add86b41357a3512c0a96ca93b0a", "optimizer_draws": 180000, "positive": {"zh": {"correct": 3290, "total": 3637, "recall": 0.9045916964531207}, "yue": {"correct": 2340, "total": 2658, "recall": 0.8803611738148984}}, "negative_triggered": 118, "known_near_triggered": 23, "natural_triggered": 40, "early_positive": 19, "gates": {"recall_each_language": false, "negative_rows_at_most18": false, "near_zero": false, "natural_zero": false, "early_zero": false, "literal_blank": true}, "projection_max_abs_error": 4.76837158203125e-06, "old_constant_Q6": {"positive": {"zh": {"correct": 3270, "total": 3637, "recall": 0.8990926587847127}, "yue": {"correct": 2345, "total": 2658, "recall": 0.882242287434161}}, "negative_triggered": 139, "known_near_triggered": 56, "natural_triggered": 29, "early_positive": 20, "quality_passed": false}, "original_firmware_unchanged": true, "context_unchanged": true, "USB_released": true, "only_USB_queries": 4, "serial_firmware_verified": "0.11.72-summary", "execution_program_sha256": "3a8088fed3845e40ecd27d894a217788589497e65bad7c92f841611beae16ab8"}


### UX510 registered — 2026-10-04T07:21:00.179214+00:00
491 whole-lifetime SDK minimum dropped49204 to46464 after the group checkpoint; per-job hooks were OFF and existing diagnostic end excludes subsequent voice-off/idle restoration.
Extend existing bounded diagnostic hooks with explicit session ownership through three turns and closing commands. Record simultaneous free and SDK historical minimum separately; mark voice-off and allocating workspace restoration. No allocator, wake or timing rule change.
One bounded diagnostic group only; no new model or altered wake/timing/allocator gates. Normal app budget1540096/context2MiB/history204800/clip448KiB retained; always restore72/off/off.


### UX511 registered — 2026-10-04T07:39:59.026576+00:00
A dedicated noinline diagnostic USB sink with512B buffer and the existing @voice JSON contract. Bypass candidate/stage bookkeeping; preserve the independent immutable row times.
Same510 SDK/models/partitions/stacks. One linked-scope check then one off/off USB-only export; zero microphone/playback/cloud/dialogue retries. All old failures retained; always restore72/off/off.


### UX512 causal input/truth audit registered — 2026-10-04T07:59:58.942328+00:00
Hash original TRAIN causal prefixes at the same512-sample decision clock, then confirm every positive/negative match byte-for-byte. A complete-array duplicate audit cannot detect shared past inputs whose future differs.
Distinct from464 full-array duplicates; original legal512-clock bands and strict near/natural zero-event categories. One data-only60s audit;0NN/fit/DEVTEST/cloud/Flash/acousticUSB. Only exact past-byte matches count; no truth/gate changes, four final readonly USB queries.

UX510–512阶段结果：诊断对话0/3完整，结束导出栈溢出及45208B整轮最低堆失败保持；UX511仅修好专用诊断导出，560B链接帧／main余1836B，未增加任务栈。UX512未发现原合法判断时刻的因果输入冲突，不能据此接受失败模型。原72/off/off恢复，1385事件／940428B／next4612和2MiB上下文保持。普通话＋粤语、ASR优先、VAD、LLM流式、连续三轮、话题应答及响应速度的完整目标继续保留，不将诊断通过当作完成。


### UX513 bounded acoustic-chain calibration registered — 2026-10-04T08:20:03.123565+00:00
UX510 failed waveform attribution could reflect the current acoustic replay/capture chain. First check the same frozen source without dialogue, board output or network ASR.
Reuse frozen471 KEYWORD_PCM diagnostic; no new build or model. One640-frame actual input stream, two copies of frozen510 prompt, fixed gain .35 and explicit Misiom-Shooter output.
One20.48s raw keyword input, two existing3.68s prompts; no speech recognition, tools or history/clip writes. Original frozen waveform gates retained; diagnosis only. Fresh guarded application install/restore, context2MiB/history204800/clip448KiB preserved, terminal72/off/off/USBreleased. No unchanged dialogue retry or candidate adoption.


### UX513 acoustic-chain diagnostic closed — 2026-10-04T08:27:27.834283+00:00
{"stage": "UX513", "UTC": "2026-10-04T08:27:27.834283+00:00", "terminal": true, "classification": "progress", "recorded_frames": 640, "source_playbacks": 2, "attribution_passed": false, "input_attribution_passed": false, "external_attribution_passed": true, "original_firmware": "0.11.72-summary", "original_app_sha256": "22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07", "persistent_data_byte_exact": {"ctx": true, "clip": true}, "context_events": 1385, "history_budget": 204800, "USB_released": true, "wake": "off", "voice": "off", "builds": 0, "training": 0, "cloud_calls": 0, "dialogue_groups": 0, "failed_old_evidence_retained": true, "model_adopted": false, "false_wake_fixed": false, "goal_complete": false, "one_second_certified": false, "whole_voice_48KiB_certified": false, "no_unique_acoustic_cause_proven": true}


### UX514 fixed ADC-clock diagnosis registered — 2026-10-04T08:41:30.562271+00:00
The hardware integer trigger interval makes nominal16k PCM about16025.64Hz. This can weaken long waveform attribution without discarding samples; compare exactly the source-derived625/624 clock ratio, not a fitted acoustic ratio.
One cached60s comparison only: localAPB80MHz/divider16/interval78/decimate2 gives625/624. Verify640 timestamps and unchanged nominal fitting; one clock-derived retimed reference, not a quality rescue. Zero new audio/model/Flash/cloud; keep all failed evidence and full goal.


### UX514 fixed-clock diagnostic closed — 2026-10-04T08:48:32.214577+00:00
{"stage": "UX514", "UTC": "2026-10-04T08:48:32.214577+00:00", "terminal": true, "classification": "progress", "seconds": 0.3119999999180436, "nominal_reproduced": true, "fixed_ratio": "625/624", "predicted_hz": 16025.641025641025, "observed_OLS_hz": 16025.649456339659, "fixed_reference_attribution": true, "old_nominal_attribution_still_failed": true, "acoustic_rate_search": false, "original_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_events": 1385, "context_used": 940428, "context_next_record": 4612, "partition_bytes": 2097152, "history_budget": 204800, "context_unchanged": true, "model_forwards": 0, "fits": 0, "new_audio_capture": 0, "Flash_writes": 0, "cloud_calls": 0, "source_unchanged": true, "adopted": false, "false_wake_fixed": false, "full_goal_retained": true, "goal_complete": false, "blocked_consecutive_turns": 0}


### UX515 default-off exact ADC-clock implementation registered — 2026-10-04T08:59:58.846124+00:00
Correct actual ADC sampling clock to32k, so the existing2:1 decimator produces16k. No assertion that a0.16percent error caused neural false wakes.
Default-OFF AGENT_MIC_EXACT_CLOCK, C3/full-audio only. Link wrapper delegates original adc_hal_digi_controller_config then sets divider15,a1,b39 before conversions start. Only nominalAPB80MHz and requested16/32k accepted; status/reset use one atomic32-bit state. Caller fails safely if correction was not applied.
One bounded native proof and one C3 build; correction limited toAPB80M/16or32k. No new model/timing/stack/context changes or acoustic/Flash/cloud operations here. Preserve all earlier quality/latency/whole-heap failures and full goal; hardware verification separately registered only after gates.


### UX515 build failure retained; UX516 scoped compile repair — 2026-10-04T09:14:45.787642+00:00
{"stage": "UX515", "UTC": "2026-10-04T09:14:45.787642+00:00", "terminal": true, "native_proof_passed": true, "C3_build_passed": false, "failure": "Strict C11 disables SDK typeof spelling in hal/misc.h", "builds": 1, "Flash_writes": 0, "new_audio_capture": 0, "cloud_calls": 0, "original_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_unchanged": true, "adopted": false, "false_wake_fixed": false, "goal_complete": false}
{"stage": "UX516", "UTC": "2026-10-04T09:14:45.787642+00:00", "purpose": "Repair evidenced SDK typeof compatibility only, then finish the same linked ADC-clock proof.", "source_baseline": "artifacts\\voice-fast\\adc-exact-clock-ux515\\source-manifest.json", "allowed_source_changes": ["main/CMakeLists.txt"], "repair": "Source-local compile definition typeof=__typeof__, preserving -std=c11 and all SDK sources.", "build_directory": "build-adc-exact-clock-ux515", "builds_max": 1, "timeout_seconds": 120, "options_unchanged": true, "clock_ratio_changes": 0, "model_changes": 0, "threshold_changes": 0, "VAD_changes": 0, "stack_changes": 0, "Flash_writes": 0, "new_audio_capture": 0, "cloud_calls": 0, "app_budget": 1540096, "default_enabled": false, "adopted": false, "false_wake_fixed": false, "goal_complete": false, "stop": "Retain failure and stop on a new compiler/link/resource failure; no parameter or ratio sweep."}


### UX516 linked proof closed; UX517 one device calibration registered — 2026-10-04T09:18:07.879933+00:00
{"stage": "UX516", "UTC": "2026-10-04T09:18:07.879933+00:00", "terminal": true, "native_passed": true, "linked_C3_passed": true, "application_bytes": 1537872, "application_sha256": "96b6a7f3b36ae4a3541081aaa16a098d30b467a6e6ef3ff6671cc06be501a4a1", "state_bytes": 4, "default_enabled": false, "Flash_writes": 0, "adopted": false, "false_wake_fixed": false, "goal_complete": false}
Correct the actual ADC rate to32k before the unchanged2:1 decimator; verify live divider and original16k waveform attribution.
One640-block diagnostic, unchangedsource/gain.35/two plays/attribution gates, one live clock read and500ppm post-DMA timing check. Freshguard install+restore, ctx2MiB/history204800/clip448KiB byte-exact, original72/off/off/USBreleased. No model/threshold/VAD/stack/DEVTEST/cloud/dialogue changes; quality and full goal still open.


### UX517 acoustic-chain diagnostic closed — 2026-10-04T09:22:49.117815+00:00
{"stage": "UX517", "UTC": "2026-10-04T09:22:49.117815+00:00", "terminal": true, "classification": "progress", "recorded_frames": 640, "source_playbacks": 2, "attribution_passed": true, "hardware_calibration_passed": true, "clock": {"live": {"clock_applied": true, "active": true, "source_hz": 80000000, "requested_hz": 32000, "div_num": 15, "div_a": 1, "div_b": 39, "interval": 78}, "nominal_hz": 16000, "estimated_post_DMA_hz": 16000.141038574871, "estimated_ppm": 8.814910929411113, "residual_P95_ms": 4.170532769557758, "rate_passed": true, "reference_retimed": false}, "input_attribution_passed": true, "external_attribution_passed": true, "original_firmware": "0.11.72-summary", "original_app_sha256": "22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07", "persistent_data_byte_exact": {"ctx": true, "clip": true}, "context_events": 1385, "history_budget": 204800, "USB_released": true, "wake": "off", "voice": "off", "builds": 0, "training": 0, "cloud_calls": 0, "dialogue_groups": 0, "failed_old_evidence_retained": true, "model_adopted": false, "false_wake_fixed": false, "goal_complete": false, "one_second_certified": false, "whole_voice_48KiB_certified": false, "no_unique_acoustic_cause_proven": true}


### UX518 bounded new calibrated-candidate classification pair — 2026-10-04T09:26:21.017809+00:00
Test whether the calibrated candidate improves current actual false triggers; not a claim that clock error caused them.
Current72 has originalEL3 backend; candidate also contains the existing48-channel verifier. The earlier492 pair did not improve false words, but this pair alone is not an isolated clock-only experiment.
Only one current72 group18 then one calibrated515 group18,ZH4/Yue4/N10; frozen492 revealedsources/RMS.14/gain.35/threshold740/input1/tails. Zero training/builds/cloud, no dialogue/latency/field-FAR claim. Fresh4MiB pre-test andapp-only verified install/restore, existingclip backedup and new publicdiagnosticclip allowed; preservehistory/summary/2MiB andall oldfailures, end72/off/off/USBreleased. No automatic adoption.


### UX518 stopped before candidate; partial baseline retained — 2026-10-04T09:35:35.293850+00:00
{"stage": "UX518", "UTC": "2026-10-04T09:35:35.293850+00:00", "terminal": true, "classification": "progress: current baseline reproduces a controlled non-target and an unattributed inter-trial trigger; paired comparison aborted.", "baseline_trials_completed": 12, "planned_per_group": 18, "candidate_trials": 0, "partial_languages": {"zh": {"total": 4, "valid_hits": 3}, "yue": {"total": 4, "valid_hits": 4}}, "partial_negatives": {"total": 4, "triggers": 1}, "controlled_non_targets": [{"clip_id": "sapi-kangkang-12", "text": "嗨乐鑫", "language": "zh"}], "warmup_trigger": {"prior_wakes": 8, "new_wakes": 9, "state": "recording", "prior_detected_ms": 73152, "detected_ms": 76603, "prior_cancelled_at_ms": 78165, "next_source_not_played": "dataset-yue-zf_xiaoxiao-0-001-bounded1", "no_environment_or_self_echo_cause_proven": true}, "SDK_min_heap": 65688, "DMA_lost": 0, "current_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_events": 1385, "context_used": 940428, "context_next": 4612, "context_unchanged": true, "clip_before_recoverable": true, "current_clip_ready": false, "backup_directory": "C:\\Users\\PC\\Documents\\ChatGPT\\cogd\\backups\\kws-voice-flow-20261004-092623", "Flash_writes": 0, "training": 0, "cloud_calls": 0, "new_builds": 0, "clock_candidate_quality_not_measured": true, "false_wake_fixed": false, "adopted": false, "goal_complete": false, "blocked_consecutive_turns": 0, "field_FAR_claim": false}


### UX519 previously unexecuted calibrated candidate registered — 2026-10-04T09:44:36.416408+00:00
Measure the frozen, calibrated515 candidate that was never reached in518; preserve the failed baseline and do not rerun it.
One frozen515 group18, same492 selection/gain.35/RMS.14/threshold740/input1/tails;0baseline retries/builds/training/cloud. Live clock checked. Stop on pre-playback trigger or fault, no trial replay. Fresh4MiBguard app-only install+restore,ctx2MiB/history204800/summary kept andclip backedup. Endoriginal72/off/off/USBreleased; partial518 not pairedacceptance or fieldFAR proof. Preserve all quality/latency/wholevoice failures and fullgoal.


### UX519 candidate measured and rejected — 2026-10-04T09:57:30.219470+00:00
{"stage": "UX519", "UTC": "2026-10-04T09:57:30.219470+00:00", "terminal": true, "classification": "progress: full fixed candidate measured; known near words still trigger and one518 valid Yue source lost. Do not adopt.", "baseline_retries": 0, "candidate_groups": 1, "candidate_trials": 18, "source_attempts": 1, "languages": {"zh": {"total": 4, "triggers": 3, "valid_hits": 3}, "yue": {"total": 4, "triggers": 3, "valid_hits": 3}}, "negatives": {"total": 10, "triggers": 2}, "controlled_false_words": [{"clip_id": "dataset-yue-zf_xiaoxiao-0-001-bounded1", "language": "yue", "text": "你好小燕"}, {"clip_id": "sapi-kangkang-08", "language": "zh", "text": "你好小燕"}], "descriptive_common12": [{"clip_id": "sapi-huihui-02", "label": 1, "language": "zh", "old_triggered": true, "new_triggered": true, "old_valid_hit": true, "new_valid_hit": true}, {"clip_id": "sapi-kangkang-03", "label": 1, "language": "zh", "old_triggered": false, "new_triggered": false, "old_valid_hit": false, "new_valid_hit": false}, {"clip_id": "sapi-huihui-03", "label": 1, "language": "zh", "old_triggered": true, "new_triggered": true, "old_valid_hit": true, "new_valid_hit": true}, {"clip_id": "sapi-huihui-05", "label": 1, "language": "zh", "old_triggered": true, "new_triggered": true, "old_valid_hit": true, "new_valid_hit": true}, {"clip_id": "dataset-yue-zf_xiaoxiao-1-020-bounded1", "label": 1, "language": "yue", "old_triggered": true, "new_triggered": true, "old_valid_hit": true, "new_valid_hit": true}, {"clip_id": "dataset-yue-zm_yunxia-1-015-bounded1", "label": 1, "language": "yue", "old_triggered": true, "new_triggered": false, "old_valid_hit": true, "new_valid_hit": false}, {"clip_id": "dataset-yue-zm_yunxia-1-016-bounded1", "label": 1, "language": "yue", "old_triggered": true, "new_triggered": true, "old_valid_hit": true, "new_valid_hit": true}, {"clip_id": "dataset-yue-zm_yunxia-1-028-bounded1", "label": 1, "language": "yue", "old_triggered": true, "new_triggered": true, "old_valid_hit": true, "new_valid_hit": true}, {"clip_id": "dataset-yue-zf_xiaoxiao-0-006-bounded1", "label": 0, "language": "yue", "old_triggered": false, "new_triggered": false, "old_valid_hit": false, "new_valid_hit": false}, {"clip_id": "sapi-huihui-06", "label": 0, "language": "zh", "old_triggered": false, "new_triggered": false, "old_valid_hit": false, "new_valid_hit": false}, {"clip_id": "sapi-huihui-07", "label": 0, "language": "zh", "old_triggered": false, "new_triggered": false, "old_valid_hit": false, "new_valid_hit": false}, {"clip_id": "sapi-kangkang-12", "label": 0, "language": "zh", "old_triggered": true, "new_triggered": false, "old_valid_hit": false, "new_valid_hit": false}], "paired_acceptance_allowed": false, "common_valid_hits_preserved": false, "resources": {"SDK_min_heap": 63768, "largest_listening_block": 57344, "passed": true}, "max_us": 14203, "p99_us": 14500, "current_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_events": 1385, "context_used": 940428, "context_next": 4612, "context_unchanged": true, "history_budget": 204800, "partition_bytes": 2097152, "original_clip_recoverable": true, "model_fits": 0, "new_builds": 0, "cloud_calls": 0, "adopted": false, "false_wake_fixed": false, "goal_complete": false, "blocked_consecutive_turns": 0, "no_unique_clock_or_room_cause_claim": true, "whole_voice_heap_or_latency_certified": false}


### UX520 exact corrected-clock near-word input diagnostic registered — 2026-10-04T10:02:44.428616+00:00
Capture exact current clock-corrected C3 inputs for the two still-admitted near words and two valid bilingual controls; require waveform attribution before interpreting neural confusion.
471 used old nominal clock and stopped on source attribution; this uses the physically corrected clock, frozen519 leveled WAVs and repeated three-band waveform gates. No new candidate or threshold.
Four384-frame streams, frozen519 two nearfalse/two bilingualvalid controls, eachtwo copiesat64/224/gain.35 for joint waveform attribution. One20s external recorder perstream; live clock/CRC/512/armed/3heads verified. No newsource/build/model/fits/cloud/DEVTEST, actions suppressed andnoFlashdatawrites. Fresh4MiBguard install+restore, byteexactctx/clip,original72/off/off/USBreleased. Not ordinary3dialogue,quality/FAR/latency or whole48KiB acceptance.


### UX520 source-attributed Mandarin confusion; Yue limitation retained — 2026-10-04T10:16:59.750191+00:00
{"stage": "UX520", "UTC": "2026-10-04T10:16:59.750191+00:00", "terminal": true, "classification": "progress: corrected-clock source-attributed Mandarin near word still falsely accepted, all1536 new C3/native frames exact; Yue attribution failed and no improvement claimed.", "streams": 4, "source_playbacks": 8, "frames": 1536, "native_parity_mismatches": 0, "cached_native_dependencies_unchanged": 25, "old_source_or_parity_failure_retained": true, "all_waveform_attribution_passed": false, "Mandarin_negative_attributed_events": 2, "Mandarin_control_attributed_events": 1, "Yue_evidence_unqualified": true, "max_inference_us": 14136, "all_live_clocks_match": true, "all_post_DMA_clock_checks_passed": true, "persistent_data_byte_exact": {"ctx": true, "clip": true}, "original_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_events": 1385, "context_used": 940428, "context_next": 4612, "partition_bytes": 2097152, "history_budget": 204800, "model_changes": 0, "new_builds": 0, "fits": 0, "cloud_calls": 0, "new_reference_compilations": 0, "adopted": false, "false_wake_fixed": false, "goal_complete": false, "blocked_consecutive_turns": 0, "next_action": "Work on learned complete-word distinction using the qualified Mandarin hard-negative input; no more unchanged clock/threshold replay, and do not train or certify from failed Yue attribution.", "whole_voice_heap_or_one_second_or_three_dialogues_certified": false}


### UX521 已归因普通话输入的冻结48特征有限诊断 — 2026-10-04T10:39:33.537936+00:00
仅使用UX520原归因通过的普通话目标/小燕两流，粤语失败保持。C原前端和48核验器导出1536个16ms帧，读回头逐帧核对。一次30秒上限L1线性规划只改48个输出权重，bias=-109/shift2/阈值268/256ms规则固定；原有效目标证据保持，近词仅原WAV内标签，环境尾部不标负。整数化后完整原C因果重放并独立INT64核对；不扫参数或重试。两个已揭盲且不同声音的开发源，可能分声音不能证明分词；不得当作独立准确率或部署依据。无TRAIN/TEST读取、split更改、NN训练、C编译、采放、云或Flash；仅有限诊断优化一次。完整goal未完成/阻塞0。证据artifacts/voice-fast/wake-attributed-head-ux521/。


### UX521 末层整数化有限诊断拒绝 — 2026-10-04T10:43:05.410906+00:00
1536个16ms前端/48末层特征与实际核验头一致；原模型clone+naive prime的768个32ms块和C3全等。一次浮点L1规划可行，但48系数整数化丢失负例约束；独立INT64与原C提案全等，普通话目标1→2/2，近词2→1/2仍错，明确拒绝。不是现场改善或真人准确率；原模型/规则/程序依赖25项保持，无NN训练/整库预测/云/采放/Flash，不能放宽门槛后重试。已四项USB核实原72/off/off、1385事件/940428B/next4612、2MiB/204800预算保持，串口释放。原521结果和全部520粤语归因失败保留。整体goal未完成/阻塞0。


### UX522 原冻结输入的整数末层与整库保留 — 2026-10-04T11:05:54.905936+00:00
前turn为progress：已定位末层取整失败。唯一30秒上限INT8 MILP，48输出权重/固定bias−109 shift2/原521约束余量2/阈值268/256ms均保持，仅改直接整数求解。一次原C重放对齐后，15791条完整TRAIN与30条同声音慧慧/康康原WAV检查：每种语言原有效交集99%、零新增负误或提前；绝对质量失败与同源/原盲测限制分别保持，非降标准或完整准入。CPU4线程/batch128/整库600秒上限、真实128零PCM归一化prime，原完整时界+5120/恰1事件不变。不同声音退化不能拿两流改善掩盖；无阈值/余量/模型/solver扫描或失败重试，无新采放/云/Flash/NN训练。提案学习一次不计为独立验证；独立发布验证排除两个已揭盲声音。失败拒绝保存，成功只允许另登记资源与物理3轮检查。goal完整保留/阻塞0。证据artifacts/voice-fast/wake-integer-head-ux522/。


### UX522 整库与同声音保留失败，拒绝局部整数头 — 2026-10-04T11:20:43.797592+00:00
命令：wsl.exe --cd /mnt/c/Users/PC/Documents/ChatGPT/cogd -e env OMP_NUM_THREADS=4 MKL_NUM_THREADS=4 /home/chalmers/.venvs/cogd-kws/bin/python artifacts/voice-fast/wake-integer-head-ux522/run.py；session68220已poll确认exit0，唯一整数求解0.057秒/L1=375/改6权重/整数约束全满足。整库15791条一次39.52秒，原中3568/3637、粤2548/2658、负误17/9496；候选中2010/3637、粤489/2658、负误6但新增5，近误1→4、自然7→0，新增提前1。原有效交集中56.33%、粤19.15%，同声音慧慧5→4/6、康康6→5/6，明确拒绝；当前两流目标1→2/2、近词2→0/2只证明局部约束有效，不以漏醒抵消误醒或当现场改善。2304实际头/73728末层值数值前置通过，原/提案4个C确认器对照及整库已保存事件统计审计通过，无第二优化或神经重跑。未改变原模型/网络/规则/源码/数据；无DEV独立TEST/新采放/云/Flash/C编译。四项USB实核原72/off/off、1385事件/940428B/next4612、2MiB/204800预算保持，串口释放。下一步须把整库保留约束纳入优化，不能重试522/放宽门槛；不据此证明所有末层不可能。完整goal仍未完成/阻塞0。


### UX523 整库保留的冻结末层有限可行性 — 2026-10-04T12:11:59.641850+00:00
一次原C对齐特征提取覆盖15791条TRAIN及30条已揭盲同声音输入，选原有效事件最强支持对，并在原E/L潜在确认范围内约束负例和原低分位；不根据提案换锚点。原521实际两流约束保持。一次30秒最小公共松弛LP，只有最优零松弛才允许一次30秒INT8 L1 MILP；无NN训练、阈值/窗口/网络/种子扫描。必要仿射约束保留定点舍入界限与不溢出；固定支持及逐低分位保留比事件保留更强，无解只否定此登记合同。候选仍须原C整库99%交集/零新增误醒提前和同声音/实际两流检查，不自动上板。无新采放/云/Flash/盲TEST；原72、2MiB上下文/204800历史/448KiB录音保持，完整goal及旧失败继续保留。证据artifacts/voice-fast/wake-retained-head-ux523/。


### UX523 整库保留可行性检查结束 — 2026-10-04T12:15:41.295795+00:00
{"LP_runs": 1, "MILP_runs": 0, "TRAIN_rows": 15791, "same_voice_rows": 30, "feasibility": {"status": 0, "success": true, "message": "Optimization terminated successfully. (HiGHS Status 7: Optimal)", "seconds": 3.3652098999999964, "minimum_slack": 839.7788873208739, "runs": 1}, "affine_contract_feasible": false, "quality_passed": false, "conservative_fixed_selection": true, "new_neural_training": 0, "Flash_writes": 0, "current_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_events": 1385, "context_used": 940428, "context_next": 4612, "goal_complete": false}
审计仅重算保存LP原始/对偶约束，无再求解或神经前向。终止冻结末层微调路线；本合同保留固定支持对/逐低分位，比最终事件保留更强，不能宣称所有末层或网络均无解。原阈值/规则/声学模型/源码保持，未标记修复。整体双语、异步ASR、VAD、流式、连续3轮和生动衔接目标及此前延迟/内存失败继续保留。没有开始下一轮训练或采放。


### UX524 补齐声源与标签的板上同通道对照 — 2026-10-04T12:31:04.615366+00:00
上一turn523为progress：结束无可用提案的冻结末层微调，阻塞0。静态检查静音预热已存在，不再重写/重复验证。仅新增康康目标03和慧慧小燕08两条384块原输入，各2次固定回放，补原520慧慧目标/康康近词的两缺失单元，避免学习把声音当标签；非重跑旧分类验收。固定.14RMS/.35播放/740阈值/16k物理校正时钟/原波形门槛，先双麦归因和原C逐帧数值核对，失败原样保存且不用训练；0训练/模型/阈值/源码构建/云/独立TEST。fresh4MiB/app-only install+restore，ctx2MiB/history204800/clip448KiB字节保持，结束原72/off/off/USB释放。完整双语、异步ASR、VAD、流式、3连续对话、生动衔接及旧质量/速度/资源失败继续保留；这不是3轮普通对话验收。证据artifacts/voice-fast/wake-voice-balance-ux524/。


### UX524 同通道双声音对照采集结束 — 2026-10-04T12:38:24.995193+00:00
{"stage": "UX524", "UTC": "2026-10-04T12:38:24.995193+00:00", "terminal": true, "classification": "progress: acquired missing voice-by-label cells and checked whether the new physical input can support balanced representation learning; this is not a new classifier or ordinary-dialogue acceptance.", "streams": 2, "source_playbacks": 4, "frames": 768, "all_new_inputs_C_exact": true, "waveform_attribution": false, "balanced_input_admitted": false, "total_balanced_head_values": 0, "native_dependencies_unchanged": 25, "persistent_data_byte_exact": {"ctx": true, "clip": true}, "original_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_events": 1385, "context_used": 940428, "context_next": 4612, "partition_bytes": 2097152, "history_budget": 204800, "neural_training_runs": 0, "new_models": 0, "new_builds": 0, "cloud_calls": 0, "ordinary_dialogue_groups": 0, "independent_TEST_reads": 0, "split_changes": 0, "adopted": false, "false_wake_fixed": false, "goal_complete": false, "blocked_consecutive_turns": 0, "old_Yue_attribution_failures_retained": true, "one_second_or_whole_voice_heap_or_three_dialogues_certified": false, "next_action": "Stop this capture group with the failed attribution retained. No voice-confounded learning or unchanged retry; a new acquisition/data path needs independent evidence."}


### UX525 同声音对照的有限非线性表示学习 — 2026-10-04T13:00:47.153176+00:00
上一turn524为progress，已补齐一组归因合格的慧慧目标/近词，康康目标失败及整个4单元矩阵拒绝保持。不重试冻结输出头/阈值；只学习原48x48 PW/ReLU权重和bias，输出头/其它层/shift/算子/预算固定。一次特征测量提取layer9有符号输入，原C/PyTorch/INT64前置对齐。只用合格慧慧实际正负对照、全部TRAIN和30已揭盲同声音WAV保留；不以不同声源单标签拟合，不学习康康失败正例或未知环境尾部。一次Adam固定seed/LR.01/2000steps/120秒，确定性分层完整访问记录，final仅1候选，禁止择checkpoint或超时重跑；完整原C事件门槛/分语言99%交集/零新增误醒提前/同声音保留/实际近词零均须通过。host成功仍需另登记新seed+板上资源及至少3完整连续对话。0新采放/云/Flash/C3构建/独立TEST，原2MiB上下文/204800历史/448KiB录音及全goal、旧失败保持。证据artifacts/voice-fast/wake-nonlinear-representation-ux525/。


### UX525 有限非线性表示学习收尾 — 2026-10-04T13:12:54.327389+00:00
一次2000步final训练及15791例完整评估完成，原执行末尾stdout NumPy int64序列化失败，exit1原样保留；仅审计已保存结果，不重跑训练/推理。冻结E/L24+48基线中粤3568/3637、2548/2658完全保留，负例17→14但新增1例；实际慧慧目标1→1、近词2→1，康康已揭盲近词2→2。实际近词零误触及零新增负例未通过，候选拒绝，不烧录，不声称解决误醒/真人泛化/每小时FAR。当前小试收尾，不追加不变路线或参数扫描。
{"stage": "UX525", "UTC": "2026-10-04T13:12:54.327389+00:00", "terminal": true, "finite_pilot_closed": true, "candidate_rejected": true, "original_firmware": "0.11.72-summary", "firmware_sources_unchanged": true, "wake": "off", "voice": "off", "USB_released": true, "context_events": 1385, "context_used": 940428, "context_next": 4612, "partition_bytes": 2097152, "history_budget": 204800, "context_counters_unchanged": true, "flash_writes": 0, "new_recordings": 0, "cloud_calls": 0, "additional_training_runs": 0, "original_training_runs": 1, "original_steps": 2000, "original_execution_exit_code": 1, "stdout_failure_preserved": true, "numerical_results_audited": true, "independent_validation": false, "adopted": false, "false_wake_fixed": false, "three_dialogues_or_one_second_response_certified": false, "goal_complete": false, "blocked_consecutive_turns": 0, "next_action": "Close this pilot and deliver the negative result. No automatic unchanged retries, parameter sweeps, or candidate deployment. A further route needs new evidence and an explicit finite scope."}


### UX526 已验证采样时钟接入三轮业务 — 2026-10-04T13:25:16.597868+00:00
上一轮525为progress：有限非线性训练拒绝并收尾，不重新打开训练。只把已测516精确ADC时钟接入既有511诊断/流式ASR/LLM/TTS/快速应答，模型/阈值/初态/VAD/栈/2MiB上下文/204800历史不变。配置一处ON；一次增量构建240秒和一次中粤中连续三轮各1次唤醒260秒，不重试、不等待ready。heap会话包含关闭监听/工作区恢复，保持原波形门槛，诊断不得当一秒或FAR验收。fresh4MiB/app-only安装及恢复，保留所有失败和新增历史，结束原72/off/off/USB释放；0训练/新模型/DEV或TEST。证据artifacts/voice-fast/voice-clock-dialogues-ux526/。


### UX526 精确时钟三轮业务集成结束 — 2026-10-04T13:42:06.193384+00:00
一次配置集成构建1537456B/1540096B；单组中/粤/中首次唤醒3/3、记忆任务3/3，严格输入2/3，第三轮尾部多“呢”原失败保持，group exit1不重跑。外部三源共同对齐及最后设备录音原高两频段门槛通过；本地SenseVoice末词为“小星系”，不称人工语音/唯一ASR根因证明。heap完整导出：当前观察低点51728B、SDK历史和46396B<49152，104跳过/34覆盖保持；关闭监听及工作区恢复前已到46396，后未再下降，不能把关闭恢复当本轮SDK新低点。诊断不作一秒验收。fresh4MiB/app-only安装及恢复原72/off/off，新增6事件保留；0训练/新模型，有限pilot结束，goal未达成。
{"stage": "UX526", "UTC": "2026-10-04T13:42:06.193384+00:00", "terminal": true, "classification": "progress", "builds": 1, "training_runs": 0, "new_models": 0, "dialogue_groups": 1, "first_attempt_wakes": 3, "core_tasks": 3, "strict_full_inputs": 2, "diagnostic_group_passed": false, "original_failure_preserved": true, "exact_clock_integrated_not_adopted": true, "external_source_alignment_passed": true, "last_device_clip_source_attributed": true, "original_SDK_minimum": 46396, "observed_simultaneous_minimum": 51728, "allocation_skipped": 104, "closing_caused_additional_SDK_drop": false, "quality_passed": false, "normal_speed_acceptance": false, "original_firmware": "0.11.72-summary", "wake": "off", "voice": "off", "USB_released": true, "context_events": 1391, "context_used": 943184, "context_next": 4618, "context_partition": 2097152, "history_budget": 204800, "new_history_preserved": true, "original_clip_recoverable_from_guard_backup": true, "false_wake_fixed": false, "adopted": false, "goal_complete": false, "blocked_consecutive_turns": 0, "next_action": "No repeat of this group or ended KWS fitting. Inspect the observed in-dialogue resource low point and transcription discrepancy from these saved inputs; closing restoration is not the new low point in this run. Normal release still needs resource, bilingual/false-wake and latency proof."}


### 唤醒交付收尾边界 — 2026-10-04T14:17:02.895740+00:00
本轮训练与集成实验收尾，不追加同路线循环，不将误唤醒或整体语音目标标为完成。保留设备当前0.11.72-summary及完整原源码/配置，修正旧一键安装包。发布清单须accepted=false，误唤醒/真人双语/一秒语音及整轮资源的未通过记录保持；不烧录失败候选。本次仅四项只读USB核验与本机包校验，0训练/录音/云调用/构建/Flash写入。设备wake/voice off、Wi-Fi在线、上下文1391事件943184B、2MiB分区/204800B历史预算，结束释放COM5。整体目标继续未达成，可靠模型无有证据的完成日期。结论docs/WAKE_CLOSEOUT_REPORT.md。


## 2026-10-05 接手收尾：0.12.0-rc1 用户测试版

本轮以用户最新要求为准：接手原会话、停止扩展安全诊断和长期训练，交付一版可直接测试的固件。此前双语真人泛化、误唤醒率与一秒有效回答的未通过记录保留；测试版交付不冒充这些指标已修复。

交付内容：从当前源码构建独立、可复现的普通语音测试配置；固化已有24/24/48唤醒模型及静音状态；保留USB文本、硬件工具、普通话/粤语唤醒、流式ASR/LLM/TTS、已有提前准备与应答衔接。无需新增账号权限或安全密钥。使用SDK常规TLS和证书校验，不开展越界复现、不启用实验TLS算术/协作/小分片改动；设备分区、凭据及历史容量保持。

收尾验收：构建及应用容量校验；相关主机功能回归；新鲜全Flash备份后仅更新应用并验证数据区；设备版本、联网和历史核对；一组至少三轮连续语音业务及USB业务检查；明确记录失败，不循环训练/扫参。保留原0.11.72回滚包，发布新安装包、对应源码与简短测试说明。交付后停止自动实验，由用户进行体验测试。
