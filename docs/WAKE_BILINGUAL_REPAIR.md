# 双语唤醒有限修复（2026-09-22）

用户反馈真人普通话无法唤醒、粤语能够唤醒。当前 E/K3 没有语言开关，
固定阈值为 740、增益为 1。历史合成声源通过不能解释为真人普通话通过。

## 冻结的诊断与一次训练范围

先用本机 SAPI 声音进行独立引擎数字输入诊断，不播放声音、不访问 COM5。
45 个请求中发现 Yaoyao 与 Kangkang 的 15 个 WAV 逐字节重复，因此去重后只有
30 个不同 WAV、两个有效声音。Huihui 正例 4/6，两个未命中的正例都是较快的
无逗号“你好小言”；三个带逗号正例均命中。Kangkang 正例 6/6，负例“小燕”
误触发 1/9。SAPI 仅用于诊断，不加入训练，也不作为独立真人验证。

旧 TTS 生成器固定用“你好，小言”作为正例，存在学习中间停顿模式的可能。
这只是待验证假设，不是对真人失败原因的确证。使用相同且已有许可的本机
CosyVoice2，新增无逗号完整短语：四个原 TRAIN 声音、两个原 VALIDATION 声音，
每声音 8 个请求。自动 ASR 与固定端点质量筛选后保留 23 TRAIN、16 VALIDATION；
9 个失败保留原因，不能人工改作通过。补充 621 个 TRAIN 例（包括半词负例、
TRAIN 背景混合）和 16 VALIDATION 例；旧数据保持原数组前缀，归一化和 TEST
文件逐字节保留。没有用 SAPI 声音训练或选择阈值。

只做一次 2000 步适应：从 K-best 初始化，学习率 0.00005，batch32，seed202609221，
旧的双语、近似词和设备域采样保留；对同类补充例使用 0.5 的有限抽样概率。
固定 E 成员和设备阈值 740，不做模型权重搜索或阈值搜索；只允许验证集选最优
训练 checkpoint。旧 E/K 权重不覆盖。候选先做主机数值对齐和双语/负例回归，再
交主任务安排声学实测；主机改善不等于真人能力已修复。

当前证据目录：`artifacts/kws-bilingual/`。原始录音、生成音频和训练产物保持
本地，不进入 Git。第一次补充生成因相对路径元数据失败，保存原失败日志和目录；
修复路径解析后使用新目录完成，未覆盖失败证据。

## 结果：候选有局部改善，尚不能替换默认模型

唯一 L 训练耗时 27.35 秒，验证损失选择 checkpoint；没有第二次训练或阈值搜索。
两者均固定 E 成员、三块均值、740 门槛。模型运行结构、Flash 权重规模、工作区
和上下文预算不变，默认 E/K 文件尚未覆盖。

| 同一份固定输入 | 旧 E/K | 候选 E/L |
|---|---:|---:|
| 旧验证普通话正例 | 44/45 | 45/45 |
| 旧验证粤语正例 | 33/33 | 33/33 |
| 设备录音子集正例（普通话/粤语） | 8/8、8/8 | 8/8、8/8 |
| 设备录音子集负例触发 | 0/72 | 0/72 |
| 旧验证全部负例触发 | 4/636 | **5/636** |
| 新增无停顿验证正例 | 15/16 | 15/16 |
| 独立 SAPI 普通话正例，去重后 | 10/12 | 11/12 |
| 独立 SAPI 负例，去重后 | 1/18 | 1/18 |

E/L 恢复 Huihui 正常语速、无停顿短语；稍快无停顿短语仍未命中。新增误报来自
FLEURS 粤语自然句 `fleurs-yue_hant_hk-test-1192969723645644630`，不能用端点
不确定解释。原半词错误数、提前事件数未增加。因为严格的负例不回退关卡失败，
**E/L 只保留为明确标注风险的比较候选，不自动替换当前固件，不宣称真人双语
已修复。** 两个 SAPI 声音及原合成声音均不能替代用户真人失败录音的诊断。

候选权重 C 和 Python/Torch 独立整数 oracle 在 4096 特征帧、512 PCM 帧上全部
一致；2 项 C ASan/UBSan 和 51 项 Python 测试通过。融合验证脚本先精确复现旧
E/K 全部保存分数，再比较 E/L，避免把解释器差异误计为模型改善。候选随后已完成
固定 14 条声学回放：E/L 在普通话正例上为 4/4 trigger、4/4 valid；粤语为
4/4 trigger、3/4 valid；6 条负例中有 2/6 trigger，且两条候选结果相同。该
清单已揭盲，来源为 SAPI/CosyVoice2 或数据集合成音频，不能证明真人双语泛化，
也不能替代用户真人失败录音。结果见 `artifacts/voice-fast/wake-comparison.json`、
`wake-ek.log` 与 `wake-el.log`；没有在此子任务中使用 COM5 或电脑扬声器。

固定 14 条声学清单已完成一次回放：4 普通话（包含两个原无停顿失败）、4 粤语、
6 个近似词/半词/旧词负例；E/L 结果为普通话 4/4 valid、粤语 4/4 trigger
但 3/4 valid、负例 2/6 trigger。路径 `artifacts/kws-bilingual/replay-selection.jsonl`，
SHA256 `624cca185fc1698d6896e476b32f7ac4c3c9ca62908d713de37653f3fdba82b1`。
这是已揭盲的诊断比较源；不得把该回放或后续重复回放计为独立真人或新盲测。

### 产物与复现

- 诊断：`sapi-probe-02/baseline-deduplicated.json`、`candidate-el.json`，保留重复
  声音标记与逐条结果；`probe_bilingual.py` 不把启动预热中的高分算作词段峰值。
- 训练：`compact-tts-02`、`compact-asr.json`、`features-compact`、`adapt-l`，记录
  数据来源、拒绝例、划分、参数、源码快照、checkpoint 和 hashes。
- 候选：`adapt-l-int8/model.json`、`model.c`、`model_secondary.c`；融合主机库
  `build-kws-fusion-el-host/libkws.so`。旧 E/K 可原样回滚。
- 检查：`evaluation.json`、`changed-trigger-clips.json`、`parity-l/host-parity.json`、
  `tests-all.log`、`host-build.log`。

在既有 WSL KWS 环境下运行 `training/kws/prepare_compact.py`、`train.py`、
`calibrate.py`、`tools/kws/evaluate_compact.py`、`parity.py`；完成目录拒绝覆盖，
复现应换独立输出位置。确切训练参数保存在 `adapt-l/config.json`。SAPI 生成器
在 PowerShell 7 运行，本机 Windows PowerShell 5 没有同一组声音，失败记录保留；
生成 WAV 不打开扬声器。

## 独立构建与同板比较入口

根据主任务要求，继续提供 E/L 可编译比较版本；不把上述自定的负例零回退门槛
作为停止工作的条件。新增 `AGENT_KWS_FUSION_PAIR=el`，状态名为
`xiaoyan_ds_tcn24_el3`。默认仍选 E/K，旧 E/K 和 E/F 的源代码全部保留。
`tools/build_kws.ps1 -Mode trained -Fusion -FusionPair el -WakeThreshold 740`
创建独立 `build-kws-fusion-el`，不会烧录。

模型 C/JSON 位于 `components/kws_c11/generated/fusion_secondary_l.*`；训练配置、
数据来源、筛选结果、分数摘要和 hashes 位于 `training/kws/releases/compact-l`。
这些文件足以重新编译并重现 C 导出。完全重复训练仍需要 manifest 指定的本地
原始 WAV、旧特征和 K checkpoint；没有声称只凭 hash 可以恢复缺失材料。

`tools/kws/bilingual_replay.py` 实现固定 14 条比较；`--check` 只检查输入，不访问
任何硬件。正式入口检查所烧模型、备份原录音槽，暂时关闭云语音，固定门槛及
源增益后执行，最后恢复原来的语音/唤醒状态、阈值和输入增益。比较器拒绝不同
主板、端点、源文件、参数或不完整运行。新增匹配/不匹配证据测试和模型导出
校验。发布位置 C 源已单独编译，2 项 C sanitizer 与 61 项 Python 测试通过。

本子任务没有执行 USB、扬声器、麦克风操作或烧录；当前用户真人测试不会被该
脚本自动打断。后续同板结果由主任务另行记录，不将计划中的测试记作通过。

### 真实接口契约复核

根据 `artifacts/voice-flow/human-ready/20260922-134053/report.json` 修复恢复路径：
实际字段是 `input_gain`，此前错误地读成 `gain`。现在在任何改动前先提取并验证
原设置；恢复覆盖语音开启、单独唤醒、全部关闭三种状态，且只在监听释放后改
输入增益。停止等待还检查录音、播放和网络任务已退出，避免导出时资源仍被占用。

USB 查询新增 `kws profile → trained`、`audio clip read → offset` 类型标识，
防止较晚的普通 status JSON 被误作下一条回复。保存真实报告的非凭据字段作为
测试 fixture，使用模拟 USB/声卡完整执行 14 条旧 replay 引擎，覆盖所有 args、
当前音量/状态字段和原录音导出，不打开真实硬件。比较器同时校验脚本 hash。
最新 68 项 KWS Python 测试、7 项 USB 回复解析测试通过；这些是接口仿真验证，
不冒充同板声学验收。
