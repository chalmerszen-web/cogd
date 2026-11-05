# MultiNet7 P4 PCM 诊断工程

此工程是临时的录音输入推理探针，不是唤醒固件：没有麦克风、WiFi、AI 对话或 NVS 写入。实验未通过四组唤醒目标，不能留在用户设备上当作正式版本。

- 实物：3.5 寸 P4，rev 3.2，16 MB Flash，固定 200 MHz、32 MB PSRAM。
- 来源：本项目已下载的 `espressif__esp-sr` 2.6.1，未修改预训练权重。来源与各文件哈希见 `model-provenance.json`。
- `mn7_cn` 和 `fst/commands_cn.txt` 必须一同打包到 `main/models.bin`。没有 FST 命令文件会导致初始化失败。
- P4 的 MultiNet7 `switch_loader_mode` 接口成员为空，调用前必须检查。当前使用其默认加载方式。
- 几个互相依赖的预编译库使用 GNU ld 的 RESCAN 组；额外的 `dl_fft`、`esp_new_jpeg` 目录仅为本实验的依赖别名。
- 六个命令映射为四类。粤语用普通话拼音近似是未经支持保证的实验；中文词表不标声调，不能保证区分“小言／小燕”。

在本目录用 PowerShell 构建：

```powershell
$env:IDF_COMPONENT_MANAGER='0'
../../../tools/idf.ps1 --no-ccache -B build build
```

只有核对 COM 端口、芯片 MAC、分区表相同以及普通固件回退包已保留后，才能写入。仅临时替换 `0x10000` 的应用，不改 bootloader、分区表或 NVS。所有实验镜像和失败日志保存在 `../../wake_training/20261101/wide95/multinet-*`。

UART 为 921600，`PING` 返回 CPU 信息。`BEGIN <frames> <threshold>` 初始化独立推理状态；主机随后每次发送 512 个 little-endian PCM16 样本，设备逐帧返回 FNV-1a 校验、推理耗时、状态及命令结果。主机为固定录音补 2.048 秒静音，记录全部帧，校验传输，不发送网络请求。

运行入口是 `../../tools/wake_multinet_probe.py`。输入必须由 `wake_multinet_prepare.py` 从 TRAIN 或已有 DEVELOPMENT 录音生成，禁止传入封存的最终测试用于选模。帧耗时只覆盖 detect 调用；初始化、主机串口传输及 JSON 输出不包含在其中，不能用整个探针代替实时音频流水线验收。

本次结果：增益 1 和 4 下四条弱音 TRAIN 正例均漏检，四条负例无触发；对应干净数字音频的普通话两条命中、粤语两条漏检。峰值 30,627 µs/32 ms。内部堆增量 20,360 B，PSRAM 堆增量约 2.93 MB。它们都是这个小型探针的测量，不是完整对话固件的额外预算或总体成功率。

结束后恢复 `firmware/experimental-wake4-three-rounds-20261101` 普通固件，并独立读回三个镜像、检查 200 MHz、WiFi、voice on 与 wake listening。当前最终状态以 `../../wake_training/20261101/wide95/result.json` 为准。
