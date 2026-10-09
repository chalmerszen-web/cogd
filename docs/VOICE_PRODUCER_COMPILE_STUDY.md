# 采集生产者编译优化诊断

2026-09-22。用户正在真人测试；本诊断未打开 COM5、未访问声卡、未烧录，也未修改固件构建选项。

## 推荐的最小范围

仅对以下四个现有源文件追加 `-O3`，由主流程构建、比较并验证：

```cmake
set_property(SOURCE
  "../plugins/audio/voice.c" "../plugins/audio/tonal.c"
  "../plugins/audio/source_stream.c" "../plugins/audio/source_bound.c"
  APPEND PROPERTY COMPILE_OPTIONS "-O3")
```

不启用 `-Ofast`、不改变滤波系数、不减少采样、帧或神经推理次数；不修改 VAD 判定。`decimate.c` 已单独使用 `-O3`，不用重复处理。

## 编译证据

使用当前 `build-kws-fusion-ek/compile_commands.json` 中的真实 C3 命令，仅替换优化等级与输出路径，生成独立对象。下面 `.text` 是编译对象汇总，含最终可能被链接器清除的函数及常量；最终固件增加量必须以正式链接结果为准。

| 文件 | `-Os` 字节 | `-O3` 字节 | 增量 |
|---|---:|---:|---:|
| voice.c | 1436 | 2230 | 794 |
| tonal.c | 326 | 488 | 162 |
| source_stream.c | 892 | 974 | 82 |
| source_bound.c | 220 | 230 | 10 |
| **推荐四文件合计** | **2874** | **3922** | **1048** |
| clip_packed.c，暂不纳入 | 2408 | 3932 | 1524 |
| audio_board.c，暂不纳入 | 23839 | 32595 | 8756 |

推荐四文件的 `.data`/`.bss` 均没有增加。C3 编译器静态栈报告显示 `agent_voice_filter` 从 32 B 降到 0 B、`agent_voice_reject_cue` 从 16 B 降到 0 B；整条任务栈仍须实测，不据此直接减栈。

`-Os` 版本 `agent_voice_filter` 每节调用通用 `factor_q30`；`-O3` 将七节滤波的相应内部操作内联、展开并传播系数常量。七节分别是四节 voice filter、一节提示音 notch、两节 tonal notch。原数学已有对称分子因式分解；两版滤波对象均没有 64 位除法 helper，因此不能假设手写右移会带来进一步收益。尤其负数除法需要向零截断，直接算术右移会改变样本及状态。

## 主机检查

`tools/voice/benchmark_producer.c` 调用实际生产者源码；五类确定性输入覆盖满幅随机、低幅随机、DC 翻转、稀疏脉冲和交替满幅。`-Os` 与 `-O3` 交替运行各三轮：

| 测量范围 | 每轮样本 | 两版输出/状态摘要 | x86 中位 ns/样本：Os → O3 |
|---|---:|---|---:|
| 七节 biquad | 6,400,000 | `8b5d18be` 相同 | 42.391 → 18.889 |
| source metadata | 4,096,000 | `cfaf9963` 相同 | 42.742 → 20.435 |

这是 x86 微基准，**不代表 C3 已提升 2 倍**。摘要一致是有限输入的回归证据，不是对所有输入的形式化证明。source metadata 测试以确定性交叉过零回调代替 WebRTC，隔离生产者开销，不包含真实频谱分类器或 Flash。

另在独立 `/tmp/cogd-producer-tests` 中使用 Debug、`-O3`、ASan、UBSan 构建，`voice`、`packed_clip`、`decimate`、`endpoint`、`confirmation`、`publication` **6/6 通过**。其中覆盖原有 CRC/掉电、滤波边界、端点/取消以及一致状态发布。没有修改测试期望让优化通过。

复现入口：

```powershell
python tools/voice/producer_compile_study.py --build-dir build-kws-fusion-ek
```

报告、汇编、静态栈和测试日志位于 `artifacts/voice-flow/producer-study/`。首次误用旧的 `build-agent` 编译数据库因缺少最新 `speech.h` include 失败，改用当前固件数据库后成功；没有对旧构建进行配置修改。

## 如何解释已有设备 CPU 数据

当前候选一轮录音墙钟为 5217.9 ms，生产者任务 CPU 为 2578.4 ms，TEN 前端加推理 CPU 为 1639.7 ms。生产者 CPU 覆盖采样处理、元数据、压缩、Flash 驱动等，不是纯滤波时间，不能据此把全部 2.58 秒归因七节 biquad。

`phase_clock()` 使用 CPU cycle counter，其区间包含其他任务抢占时间。`PH_MIC` 包含多项子工作，`PH_FLUSH` 又包含写入和擦除，不能把所有行相加。`PH_DECIMATE`、`PH_METER`、`PH_SOURCE` 在当前粗粒度诊断中明确未测，零值不代表没有开销。`io.erase` 也包含录音前准备阶段，不能直接从录音生产者 CPU 中相减。

下一次允许设备测试后，用同一输入对照确认时刻、生产者 CPU、TEN CPU/墙钟、DMA 丢失、源样本数、ASR 词尾及最低堆。若 C3 实测收益很小，就保留边界证据并重新定位，不扩大优化到整个板级文件。
