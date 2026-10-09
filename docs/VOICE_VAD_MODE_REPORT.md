# UX169：同一C3的官方VAD模式配对实验

2026-09-29。用户提出边听边猜意图、提前准备音频。现有实验路径已支持单候选、
异步生成、静默缓存、最终输入核对及改口作废；本次针对仍然提前截断后半句的
输入问题，比较官方VAD MODE2与MODE0。**没有修复三个完整改口样本的结束点，
不将模式变更合入生产代码，不开展模式或阈值扫描。整体语音目标仍未完成。**

## 方法与可信边界

新增独立USB诊断程序 `diagnostics/vad_modes`，版本0.11.207-vad-modes。
官方头文件 `third_party/esp-sr/include/esp32c3/esp_vad.h` 将MODE0标为Normal、
MODE2标为Very Aggressive。程序使用固定C3库的两个独立句柄，每帧先完成相同
生产滤波，再复制PCM分别执行两模式；比较原地修改并分别计算CRC。
库SHA256：`3473d612da471232b8f5ce69470d3ef73aa706d4c4b2372ff626164c9d619d79`。

只重放事先固定的八份本机保存PCM，共2503个20ms帧。MODE2所有元数据与既存
记录逐字节一致，两模式的能量字段也一致；不是电脑模拟官方分类器。
保留700ms静音、相同能量门槛、累计续听额度、ASR通知和全部现有结束代码。
三个UX142八秒诊断录音使用原通知与shadow重放，MODE2结束点与历史实机一致。
其后续通知来自原诊断运行，模式0结果是受控反事实，不能冒充新的网络调度实测。

首三份WAV分别有64、128、64个不足20ms的末尾样本，未补零，明确不参与分类。
数字静音为合成控制，不是房间背景噪声证明。背景尾段使用旧的恒定文字准入
通知，两模式都在文件末尾继续收音；结束未知，不能写成无误触发或拖尾通过。

## 结果

| 保存输入 | MODE2结束/EOF ms | MODE0结束/EOF ms | 差值ms |
|---|---:|---:|---:|
| ux90-r1 | 6680 | 7300 | +620 |
| rf18-greeting-r3 | 2600 | 2600 | +0 |
| gate19-greeting-r3 | 3040 | 3040 | +0 |
| background-tail | 4000 | 4000 | +0 |
| digital-silence | 4000 | 4000 | +0 |
| ux142-r1 | 6220 | 6220 | +0 |
| ux142-r2 | 3300 | 3300 | +0 |
| ux142-r3 | 3240 | 3240 | +0 |

总共647帧的分类位发生变化，但三个完整改口样本的结束点完全未变：
6220、3300、3240ms。旧UX90参考延后620ms，并不能代替当前失败样本通过。
两个短句控制结束点保持；背景尾段两者都未结束，数字静音两者均4000ms无语音。
这说明在此固定输入下，单独降低官方VAD严格度不足以修复输入截断；不把更多
speech位当作识别质量提高，也不把八段录音称为八次新对话。

## 实机资源与协议

12项USB协议检查通过，含帧序、CRC、畸形输入、缺帧、重复帧、取消、10秒空闲
清理及500帧边界。每个句柄实测分配760B，每段结束后空闲堆都回到332660B。
这只是无Wi-Fi、无麦克风的独立程序，不能证明完整Agent的48KiB资源目标。
MODE2滤波加调用最长2100微秒，MODE0单调用最长186微秒；测量范围不同，不计算
两模式速度提升比例。取消记录0.0ms是主机时钟分辨率，不是零物理延迟。

诊断构建132256B，SHA256：`edb0e049f7eecd1698c77c5a470eb7a646bff9b96bb1970cbdfb15c6b300532d`。
主机工程重新编译通过，八份元数据分别执行两个模式的实际C端点，共16次。
本轮未新增对话录音、外录、云请求、阈值搜索或正常固件三轮试验。
未达到预先声明的输入完整性门槛，所以没有进入后续正常固件试验。

## 恢复与复现

只安装诊断应用并恢复72，两次均完整备份4MiB、verify/readback及非应用区核对。
安装前后备份目录分别为 `kws-voice-flow-20260929-044215`、`kws-voice-flow-20260929-044538`。
诊断运行期间整个Flash不变；恢复后的整个Flash与实验前完全一致。
当前设备0.11.72-summary，联网、fast/capture/listening、reuse ON、prefetch OFF，
灯关闭，COM5已释放。1291条/1039044B上下文及所有统计保持，未归档或删除数据；
2MiB上下文、204800B历史预算、448KiB录音分区及默认安装包保持。

保留203生产源码及L2接收释放修复。407份既有C/H/CMake文件与203冻结源码一致，
正常及无音频构建的SHA256也与203一致；仅新增此独立诊断工程和实验文档。
证据、固定清单、配对回放与端点分析脚本位于 `artifacts/voice-fast/vad-modes-ux169/`。
实验包 `artifacts/voice-fast/candidate-0.11.207-vad-modes/` 包含实际sdkconfig和源码。

主要命令：
```powershell
.\tools\idf.ps1 --no-ccache -C diagnostics/vad_modes -B C:/Users/PC/Documents/ChatGPT/cogd/build-vad-modes build
# 以下Python命令使用 .toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe
python tools/kws/flash_guard.py --port COM5 --build artifacts/voice-fast/candidate-0.11.207-vad-modes --phase voice-flow
python artifacts/voice-fast/vad-modes-ux169/replay.py
python artifacts/voice-fast/vad-modes-ux169/analyze.py
python tools/kws/flash_guard.py --port COM5 --build artifacts/voice-fast/candidate-0.11.72 --phase voice-flow
```
两个回放脚本拒绝覆写已有输出。本轮原始失败也保留：prepare第一次因冻结139中
函数排版与预期不同而在断言处退出，当时仅建立空目录；修正替换匹配后才创建
源码、构建和烧录，没有把第一次失败记为成功构建。

下一步需要针对ASR确认、采集结束和可修正候选的协作关系提出新的有界改动，
不能继续依靠放宽VAD模式或延长固定等待来宣称完成。预测提前量与用户说完后
的有效回答起声分别记录；提示音、等待语和已收到的数据包不计作有效回答。
