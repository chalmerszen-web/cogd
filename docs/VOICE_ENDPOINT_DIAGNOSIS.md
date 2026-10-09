# 语音结束判定：基线诊断与有界优化

2026-09-22。本记录只覆盖 VAD 子任务；新的固件实机结果由主流程报告补充。

## 0.9.0 基线证据

输入为 `artifacts/voice-stream/conversations-07/report.json` 的六轮匹配合成回放，全部完成，无 ADC 丢样。可复算：

```powershell
python tools/voice/endpoint_analysis.py artifacts/voice-stream/conversations-07/report.json --output artifacts/voice-flow/endpoint-baseline.json
```

| 项目 | 基线观测 |
|---|---:|
| 输入音频结束至采集结束 | 平均 3063.5 ms，范围 1008–4119 ms |
| 本地神经确认所对应的音频位置 | 700–1160 ms |
| 本地神经确认的墙钟时间 | 2202–3838 ms |
| 确认时相对音频时钟的落后 | 平均 2008.7 ms，范围 1502–2678 ms |
| 每 16 ms 音频的神经处理 CPU 时间 | 平均 22.296 ms |
| 每 16 ms 音频的神经处理墙钟时间 | 平均 48.848 ms |
| 云端 final 到采集结束 | 5/6 轮仅 2–6 ms；另 1 轮本地先结束 1945 ms |

输入结束时间来自电脑播放完成，映射含 USB 接收延迟；它不是精确声学词尾。CPU 和墙钟是设备已有计数器；名称为 `nn_*` 的计数覆盖整个 `esp_hi_vad_process()`，包含 TEN 前端及模型推理，不能解读为纯卷积或 LSTM 时间。5/6 轮的 final 紧邻采集结束，加上该版本的 final 结束采集分支，支持这些轮次由云端协助收尾；不能仅凭汇总计数重建每帧噪声。

结论：约 3 秒收尾既不是纯静音阈值，也不是仅有网络建连。神经确认本身每帧 CPU 已超过对应音频长度；确认后释放模型，剩余本地频谱和能量判定仍常未先于 ASR 收尾。1 ms 的显式任务让步不是主要瓶颈。本轮保留原本的神经和频谱联合确认，不跳模型帧、不删噪声验证。

## 本轮小改

- `agent_confirmation_set_silence()` 只允许已确认且尚未结束的对话调整静音时间。未确认、取消和已结束状态不可改；不会重置累计音频时间和静音时间。
- `vad_worker` 在联合确认成功时，将结束静音由 1000 ms 调为 800 ms。初始等待 4000 ms、录音上限 10000 ms、神经 8000 ms 墙钟上限保持。
- `confirmation_tail` 仍只在未确认阶段使用原 1000 ms 证明；`source_bound` 的 1000 ms 保守采集上界不变。已经确认的结束时间缩短不会放宽任何未确认路径。
- 增加 `quiet_ms` 与 `end_silence_ms` 的 worker 状态，供设备状态输出定位后续真实收尾。不会保存录音或创建新任务。

本地规则最多减少约 200 ms 的静音等待，不能承诺消除全部 3 秒延迟。云端结束时间以及异步 ASR 建连由主流程单独修改、实测。

## 已完成验证

在独立 `/tmp/cogd-endpoint-tests` 构建中，C11、ASan、UBSan 下 `endpoint`、`phase`、`publication`、`confirmation` 共 4/4 通过。命令：

```sh
cmake -S host_tests -B /tmp/cogd-endpoint-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cogd-endpoint-tests --target test_confirmation test_endpoint test_publication test_phase
ctest --test-dir /tmp/cogd-endpoint-tests -R 'confirmation|endpoint|publication|phase' --output-on-failure
```

新增覆盖：600 ms 自然停顿后继续说话仍保留；最后一个有效语音帧后 780 ms 不结束、800 ms 结束；采用当前和前视一帧神经分数的两种供样顺序均通过；修改计时不重置已累计静音；无效参数、未确认、终态及取消不改变状态。

这些是判定逻辑验证，不等价于真人粤语/普通话停顿验证。

## 实机验收建议

1. 同一音源、音量、麦克风位置重放基线命令，分别记录唤醒到采集、输入结束到采集结束、确认源时钟/墙钟、最大积压、ASR final、DMA 丢失。
2. 普通话和粤语各覆盖连续句、短词、句中 300/600 ms 停顿。核查 ASR 最后的语义词是否完整；不要只检查收到非空文本。
3. 单独检查安静录音、短噪声、提示音串入等原有负样本，确认仍需本地证据。若异步握手导致确认超时或丢样，先查 CPU 调度与 Flash 擦写，不能降低确认门限掩盖问题。
4. 服务端 final 可能在句中停顿后迟到；降低云端静音必须验证恢复说话不会被旧 final 截断。若受影响，应保留/合并多个句子并用源时间校核，不能把所有 final 都当整轮终点。

## 进一步算力优化的边界

本地源码已确认启用 `-O3`、禁止浮点重排、fixed8 权重、二进制缩放、定点 FFT、LPC 环形历史、共享池化、融合缩放和定制软件浮点运算。模型权重及状态不在本轮更改，未发现能直接安全消除 22.3 ms/帧限制的一行开关。

减少确认前的音频历史或跳过“静音帧”会改变 TEN 的特征和循环状态，须另行证明，不能视为等价优化。后续若要动内核，应先记录分项 CPU 再做单一运算的主机/设备逐帧数值对齐。本轮不为此开启长期调优。
