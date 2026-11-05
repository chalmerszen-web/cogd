# P4 独立 H.264 解码基准

这是临时测量固件，不是播放器。测完必须恢复 COGD app；不要用它覆盖 bootloader、分区表或 NVS。实际测量见 `../../evidence/vision-agent-20261102/video-native-probe/`。

固定 200 MHz，`esp_h264 1.4.1` 软件解码，双任务、辅助任务在 core 1。每次解码调用计时累计，帧间让出一个 RTOS tick；统计不含帧间等待、LCD、联网、音频、MP4 解复用和颜色转换。每项结束关闭解码器并释放样本缓冲。

`wan3-high-head.h264` 虽保留早期文件名，实际内容是原 MP4 中复制出的 **8 个完整访问单元**，不是随意截断的文件头。该样本在 SPS 中因 `profile_idc` 不支持而失败。另三个 Baseline 样本在电脑上离线转换，只用来测兼容格式的解码上限；不作为满足“P4 独立播放 WAN3”的方案。

从项目根目录准备样本的命令（原 MP4 已保存在证据目录）：

```powershell
ffmpeg -i evidence/vision-agent-20261102/wan3-native/wan3-original.mp4 -an -frames:v 8 -c:v copy -bsf:v h264_mp4toannexb -f h264 experiments/video-native-probe/main/samples/wan3-high-head.h264
ffmpeg -i evidence/vision-agent-20261102/wan3-native/wan3-original.mp4 -an -t 2 -vf scale=320:480,fps=15 -c:v libx264 -profile:v baseline -preset veryfast -crf 28 -f h264 experiments/video-native-probe/main/samples/320x480.h264
# 另两项分别使用 scale=480:864、scale=720:1280。
```

编译：在本目录运行 `../../../tools/idf.ps1 --no-ccache -B build build`。只写 `build/cogd_video_probe.bin` 到 app 偏移 `0x10000`。启动后等待 5 秒自动测量，串口 115200 波特输出四条 `@video_probe` 和最终 `@video_probe_done`。后者出现后，恢复经过确认的 COGD app，并独立读回核对。

输入文件 SHA256、实际媒体属性、原始串口和结构化测量都已保存在证据目录。每个 Baseline 样本 30 帧，是短时性能基准，不是长时间稳定性测试。
