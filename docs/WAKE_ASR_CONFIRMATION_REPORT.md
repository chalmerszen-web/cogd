# 完整唤醒词的 ASR 二次确认可行性

## 当前结论

UX486–489 的有限检查停止，**本路线尚未满足普通话＋粤语要求，未接入固件**。本地误唤醒问题仍未修复；ASR 不能替代尚未验证的本地判定，更不能把丢失正确唤醒说成减少误触。

输入是旧 24 条公共 TRAIN 实机扬声器回放中的 13 次原 C 触发。沿用已有源时钟，每条保留至原事件后 128 ms 的 2.048 秒窗口；源 WAV 容器全部覆盖，没有重新对齐或调整标签。这是**文件中的理想连续采集**，当前固件在初始化／提示音期间的连续采集与 arena 生命周期尚未实现。

使用当前 `fun-asr-realtime`，800 ms 句间静音、关闭语义标点，验证 TLS；无热词、上下文或提示词。每个会话唯一 ID，保存 started/result/finished 原事件，发送样本 hash 与准备文件一致，无自动重试。协议参照[阿里云客户端事件](https://help.aliyun.com/zh/model-studio/fun-asr-client-events)与[WebSocket 接入说明](https://help.aliyun.com/zh/model-studio/fun-asr-realtime-websocket-api)。

| 阶段 | 实际操作 | 结果及停止原因 |
|---|---|---|
| UX486 | 现有 IMA4 压缩前缀，最多 13 次，首错停止；实际 2 次 | 近词“小燕”拒绝；首条普通话写成“小猿”而误拒，停止剩余 11 次 |
| UX487 | 相同原始 PCM、相同区间／模型／参数，唯一一次对照 | 写成“小言”而通过；只有一对结果，不能证明编码是唯一原因 |
| UX488 | 唯一 μ-law 候选，保留 16 kHz 和原时钟，首错停止；实际 4 次 | 近词拒绝、两条普通话通过，第三条写成同音“小严”，逐字规则失败；原失败保留 |
| UX489 | 复用原有 7 个普通话同音拼写表；已有 4 次缓存不重发，仅执行 4 次新会话 | 六条普通话保留、原近词拒绝；首条粤语正例写成“小饿”而拒绝，停止其余 5 条 |

总计 **11 次新增 ASR 会话**，0 次训练、0 次新录音、0 次烧录；UX489 的四条缓存不重复计费。没有利用来源语言标签作判定，也没有新增“小猿”“小饿”等救援别名。原有词表来自 `training/kws/prepare_compact.py`，本来用于普通话机器筛选；不能把它当粤语发音或独立真人泛化证明。

## 编码与容量边界

μ-law 参照当前 CPython `audioop` 量化，波形误差 SNR 35.91–36.98 dB。这是编码误差指标，不是背景信噪比或听感。这里保留 16 kHz，**不是标准 8 kHz G.711 流**；参照[CPython 实现](https://github.com/python/cpython/blob/v3.11.14/Modules/audioop.c)和[ITU G.711](https://www.itu.int/rec/T-REC-G.711-198811-I/en)。

若使用 512 个单字节样本加 4 B 类型头及 4 B CRC，64 包是 33,280 B，状态预算 64 B。相比当前 29,836 B idle allocation，算术增量上限 3,508 B；但 metadata、ASR scratch、candidate 和 engine 的跨阶段归属仍需新设计。**没有 C3 实际堆／时序证明，未据此分配更大缓存或修改分区。**

UX488 在 13 个文件生成后，因内置 `audioop` 无 `__file__` 而发生来源元数据异常。原程序和错误保留，修复脚本只检查已有文件并补齐元数据，没有重新编码或重新发送会话。

当前结果不足以归因于设备采样、粤语 TTS 发音、云 ASR 或某一种编码；完整 WAV 容器不等于人工音节标注。已停止该确认路线的进一步同类试验，不继续增加字表、扫编码或声称扩大缓存能修复误唤醒。后续本地模型验证须另行登记有限方案，保留已知误触与正确两种语言的对照。

证据分别在 `artifacts/voice-fast/wake-asr-phrase-feasibility-ux486/`、`wake-asr-codec-control-ux487/`、`wake-asr-ulaw-feasibility-ux488/`、`wake-asr-name-normalization-ux489/`：各自 plan、preflight、源码 zip、原会话事件、音频发送 hash、report 和 closure。原设备应用、上下文和录音保持；最后只读状态见 UX489 的 `closing-state/state.json`，串口已释放。
