# UX190：静音预热与录音提交重叠

2026-09-30，一组固定三轮实机测试结束，已恢复0.11.72-summary，
联网fast/capture监听、reuse开启、prefetch关闭、灯灭，COM5已释放。
候选实现及测试源保存在冻结包，九个临时源码文件按计划恢复。

本地收尾观察到缩短，完整三轮功能未通过：第三轮将最终“绿色”识别为“黑色”，
实际执行黑色。前两轮声学答复候选分别1.067秒、1.389秒，均超过1秒；
第三轮声学输入匹配不通过，延迟保持unknown。没有补轮、改变门槛或推广为默认。

## 实现与实测

停止麦克风、join确认并发布完整上传EOF后，先打开预装零且自动清零的PDM输出，
随后执行原完整录音回读、CRC及头部提交。成功后按真实经过时间补足剩余静音，
再渲染原180ms咻音及零尾。最终回复和工具仍等待成功采集join。
没有改VAD/端点、采样、音量、栈、上下文或分区；CRC/packed存储源码字节同UX189。

| 轮次 | 原UX189收尾(ms) | 本轮提交(ms) | 本轮提示音流程(ms) | 本轮收尾(ms) | 观察差(ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 530 | 175 | 184 | 361 | 169 |
| 2 | 540 | 179 | 181 | 362 | 178 |
| 3 | 536 | 214 | 175 | 391 | 145 |

“收尾”从采集结束算到录音所有者完成；“提交”和“提示音流程”是函数墙钟。
各网络/本地过程重叠，不能相加成总延迟。提示音函数返回不等于最后样本已播放，
175ms函数墙钟不表示180ms扫频被缩短。表中组间差仅为一次固定条件观察，
不是统计性的因果效果，也不能转换为声学响应改善：UX189声学结果均unknown。

| 轮次 | 已经过的静音(ms) | 补零样本/16kHz | 补零(ms) | 计时契约总量(ms) |
| --- | ---: | ---: | ---: | ---: |
| 1 | 175 | 80 | 5 | 180 |
| 2 | 179 | 16 | 1 | 180 |
| 3 | 214 | 0 | 0 | 214 |

补零已排入输出队列，因此计时契约是已过静音加队列零样本，不要求enqueue墙钟已达180ms。
这证明代码时钟与队列下限，没有把它当麦克风测得的静音时长或播放完成时刻。

上传EOF到最终ASR201/205/214ms，最终ASR到首PCM24/23/6018ms。
第三轮全文与灯光最终意图出错，退出本地快速语法，进入两轮DeepSeek和TTS；
6018ms不归因于预热。第三轮收音7903ms，比前两轮6385/6569ms更长，不能凭本组
认定是预热引起识别回归：预热发生在收音停止之后，板上输入及云识别还需独立核查。
该失败仍足以否决本轮完整验收。

## 输入、声学和资源边界

唤醒词源按普通话/粤语/普通话播放，同一普通话蓝改绿命令gain0.25，
idle预连接、reuse/prefetch开启。首次唤醒3/3，三轮均@done；正确绿色作用与读回2/3，
严格全文2/3。第三轮云结果为“把灯调成蓝色。不对，不要蓝色，改成黑色。”，
真实灯读回0/0/0，回复“灯光已关，小星星灭了。”，完整保留。
候选request_ms/pcm_ms/samples/cache_peak均0；空闲预取线程取消不是对话取消。
本阶段未证明复杂问题在用户说完前取得了有效答复包。

原始外录、source、report及首次分析保留。固定来源共同对齐通过，前两份输入各自通过；
第三份中频波形相关0.365，未通过独立输入匹配，不提升其声学成绩。
前两轮咻音检测score均0.958，按原固定规则排除提示音后，独立回复转写均为
“灯光设置好了。”。有效答复的能量/ASR候选起点分别1.067/1.389秒，
咻音后约0.152/0.214秒；这是候选起点，不是音素级标注或主观体验通过。

min_heap49636B，超过49152B门槛484B；DMA丢失0，未观察到协议错误、看门狗或复位。
相较UX189的62212B余量较小，但本轮第三句使用了不同的DeepSeek/TTS回退，
不能把堆差全部归到提前开驱动。驱动分配确实更早，资源重叠需要保留为风险。
没有新增任务或自有堆缓冲；24B旧诊断时钟加16B预热状态，join后一个v2事件，
没有在收音期间新增USB输出。

v2 tuple前11项沿用v1，末四项为preheat_at、cue_enqueue_at、zero_samples、rate。
审计核身份、上传样本、错误、顺序及最低静音量。第三轮缺本地快速路径事件，
并有多个tool_done；这些字段保持缺失，不伪造本地工具耗时，采集时钟独立有效。

## 验证、复现与回滚

73项完整主机检查(ASan/UBSan及8项固定SDK)、10项尾时钟检查通过；
新便携用例核16/24kHz、0–300ms、UINT32_MAX、无效采样率和随机尺寸分块的补零不变量。
正常、无音频、最终诊断构建通过，应用1503536B，在原1540096B预算内。
提前开输出失败曾可能在已有if内部被后续提交覆盖；代码审阅中修正后才冻结、烧录。
没有做板上错误注入，不能将这项审阅说成故障实测。

实际运行命令及结果在各log：

```text
wsl: cmake -S host_tests -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAGENT_SANITIZE=ON -DAGENT_IDF_SOURCE=/mnt/c/Users/PC/Documents/ChatGPT/cogd/.toolchains/esp-idf
wsl: cmake --build build-host && ctest --test-dir build-host --output-on-failure
python -X utf8 tools/voice/continuous_rewake.py --out artifacts/voice-fast/cue-preheat-firmware-ux190/device-three-turn --firmware 0.11.215-protocol-diag --mode fast --prompt-id correction --prompt-gain 0.25 --prompt-manifest artifacts/voice-fast/prefetch-fixtures-01/manifest.json --preconnect idle --prefetch on --reuse on --check-content correction
python -X utf8 tools/voice/analyze_continuous.py artifacts/voice-fast/cue-preheat-firmware-ux190/device-three-turn --output-name offline-cue-analysis-ux190.json
python -X utf8 tools/voice/inspect_tail.py artifacts/voice-fast/cue-preheat-firmware-ux190/device-three-turn --analysis offline-cue-analysis-ux190.json --out artifacts/voice-fast/cue-preheat-firmware-ux190/tail-audit.json
```

三轮脚本因第三轮失败退出1，按计划没有重跑。设备/烧录使用项目IDF Python，
声学分析使用.local/tts-python；实际路径见ACTIONLOG及脚本。
两次flash_guard均全4MiB备份并设备校验，仅写应用、读回，非应用区域全部相同。
上下文1215/860288B→1222/864436B，next_record4442→4449，新事件保留；
恢复后的全部持久化统计相同。prompt_turns128→0、prompt_bytes31505→0、
request_bytes37042→0是三项运行统计重启归零，不能说所有context字段完全相同。

九文件按冻结SHA恢复，407份当前基线代码相同，默认重建通过且SHA同UX189的05fcd2…；
设备实际运行的是保留的72包，不是该默认构建。完整422份候选源码/配置在trial-source.zip，
后续分析实现另存analysis-source.zip，不修改烧录前冻结包。

| 产物 | SHA256 |
| --- | --- |
| 215候选应用 | `1f720b7a37fb57b265c873ccfd433be019a4fd6e88142d51f54cbf6b3623a32e` |
| trial-source.zip | `29ce5831317e8c554dfb3fdd181eee559c19c32cf62e401d41a45575a5447c0b` |
| analysis-source.zip | `875a2425fde09320c78a7e783ae57071e3fe2ee048c9453a9a1b5890541d7fb4` |
| 默认重建应用 | `05fcd2b04adfa7c38a1ab428b5b20da180a46e7332e7ad1c999e0a12b8f9b0ed` |
| 设备已恢复72应用 | `22faca4f0c6bf12030d924db38904d330af9585493ea307e29f18d4da2309c07` |

证据目录：artifacts/voice-fast/cue-preheat-firmware-ux190/，包括plan/package、三构建与
恢复构建日志、host日志、device-three-turn原报告/实录/声学分析、tail-audit、两个flash清单、
source-restored、device-restored和closure。当前有限阶段结束，整体目标仍未完成。
下一步先用已保存输入证据区分第三轮收音/识别及回退等待，不继续扫VAD门槛或追加相同组。
