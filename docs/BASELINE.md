# 升级基线记录：0.6.0-upgrade

当前固件已增加 0.6.1-lcd，见 [屏幕时钟报告](LCD_CLOCK_REPORT.md)。本页保留 0.6.0 的原始验收与可回滚镜像记录。

2026-09-15 已安装到 COM5 的 ESP-HI / ESP32-C3 revision v0.4。用户接受同板1000次对比后，采用调优B的语音链路并保留精简Agent的GPIO和内存改进。本项目称为升级版，不代表Espressif官方发布或认证。

## 来源与合并内容

语音来源为冻结的 coarse-profile-v1 B，应用SHA256：
`d9bd6975088b733d7c2f2976a4698f9c744de34a354a5b5929599df0ff68c953`。
板级采集、TEN worker、关键词门、音频源/端点、packed clip、阶段时钟，以及B实际使用的fixed/model/pitch优化均按哈希迁入正式源码。模型、库和许可证位于 `third_party/`，自有内核位于 `components/agent_vad/kernel/`；131个迁入文件有来源清单。

保留0.5.1-lean的GPIO4/5/10/20/21输入输出/PWM、计划资源仲裁、取消恢复、SSE复用请求缓冲和直接解析乐谱。主任务栈保留6144B，B原镜像为5120B；版本/入口和构建路径也改变。因此本次集成镜像与B不是同一个二进制，1000次数据只归属原B。

固定ESP-IDF v6.1；ESP-SR `efa8d907c6d457cd0f99dae6c6b493412d3078d4`，TEN-VAD `22a3bcd4509d0faaa8eef4881e8af5f39c178950`。TEN包含已接受的C3优化，不能称为未经改动的上游。`tools/build_agent.ps1`选择完整语音配置；音频关闭单独构建。

## 本次集成验证

| 检查 | 结果与证据 |
|---|---|
| 音频开启IDF构建 | 通过；清理后从空目录重新构建，`build-after-cleanup.log` |
| 音频关闭IDF构建 | 通过，969,360B，`build-noaudio.log`；此次清理后未重复关闭配置 |
| 主机音频开启 | 24/24，ASan/UBSan；清理后24/24复核，`host-after-cleanup-02.log` |
| 主机音频关闭 | 12/12，ASan/UBSan，`host-noaudio.log` |
| 当前固定依赖 | 89个文件SHA校验；编译输入不再引用旧实验目录 |
| 有界实机检查 | 56条USB命令通过：GPIO/PWM、禁止引脚、计划冲突/取消恢复、麦克风音量、短音播放、监听启停 |
| 应用更新 | 仅app-flash；完整4MiB读回中的应用与构建逐字节一致 |
| 持久化数据 | 启动区/分区表、NVS、PHY、上下文、原录音全部逐字节不变；68个NVS活动记录通过CRC校验，序号预留7040不变 |

本次没有再跑声学1000次或云端长循环；实机检查只覆盖集成和基本行为，没有录下新的完整唤醒指令，TEN完整录音链路的声学结果仍引用原B。首次IDF尝试暴露早期依赖扫描中默认ON的问题，已改为选项默认OFF、构建入口显式ON；失败保留在build.log。清理后主机首次启动使用旧PowerShell被策略拒绝，未进入测试；随后以正常入口运行24组通过，没有更改系统策略。

## 当前空间和状态

| 项目 | 当前值 |
|---|---:|
| 物理Flash | 4,194,304B / 4MiB |
| 应用二进制 | 1,447,584B / 1.381MiB |
| 1.5MiB应用槽剩余 | 125,280B / 122.34KiB |
| 链接静态DRAM已用 | 202,218B |
| 有界检查后的空闲堆 / 最低堆 | 103,896B / 63,556B |
| 最终重启后空闲堆 | 104,388B |
| 上下文分区 / 活动区 | 2,097,152B / 1,048,576B |
| 活动区已用 / 剩余 | 996,144B / 52,432B |
| 事件 / 待同步 / 最近轮次候选 | 1548 / 1548 / 128 |
| 历史请求字节预算 | 131,072B |
| 录音区 / 原录音时长 | 458,752B / 10000ms |

分区内空间不能直接当成可分配RAM。应用比原B增加3,456B，链接静态DRAM比B减少6,114B；不同测试负载的最低堆不作等条件性能比较。2MiB上下文和128KiB历史预算未缩减，也没有删除设备中的任何历史。

设备最终为DIRECT / LOCAL，Wi-Fi连接，音量80，灯熄灭；监听、麦克风、播放和计划关闭，GPIO10恢复输入，串口已释放。

## 验收边界

原1000次实验A/B各500。在已知样本中误唤醒8/100→0/100，无指令误录22/100→2/100，触及10秒上限19→0。唤醒成功率98.25%→98.75%未证明明显改善；录音启动中位数增加251ms。完整限制见 [对比报告](VOICE_AB_1000_REPORT.md)。采纳此实验基线不改写原M0外网Gateway/真实断电未完成，或原M3主观音乐、M4历史未达目标。

## 交付、清理与恢复

当前源代码是仓库正常目录；构建入口为 `tools/build_agent.ps1`。当前产物在 `artifacts/baseline/current/`，其中manifest.json列出应用、ELF、map、配置、分区和源码包哈希。原A/B、B来源归档和原lean完整恢复备份在 `artifacts/baseline/reference/`；备份包含本机私有状态，继续排除Git。恢复时从完整备份提取所需应用区域，按当前布局只更新应用；不要整片覆盖用户数据。

当前应用SHA256：`41d50cc5e50b487cb47e6ba5dd85553f70000ba34d5c236921c286d8823d5bf5`。
更新前完整Flash：`fb81aafd1ceacf83f6d0bf239214c782ee5b46a673fd8dabf5b8a40edc311a1e`。
更新后完整Flash：`e22bf2ad19dca76f8ee1040fabfacb25e0962e0babf7bd174c91c988b86cceb4`。

[清理报告](CLEANUP_REPORT.md)列出实际删除和剩余目录。本次日志根目录为 `artifacts/baseline-cleanup-v1/`，包括promotion.json、删除明细、主机/构建日志、device-smoke/report.json、flash-validation.json、nvs-validation.json及device-final.json。完整1000次正式证据仍位于 `artifacts/voice-ab-1000-v1/`。
