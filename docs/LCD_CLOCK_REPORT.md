> 2026-09-19 整理说明：旧录音、实验数据、日志、Flash 备份和旧二进制已列入用户要求的清理范围；自动删除被审批拦截，待执行根目录的清理入口。实际结果见 history/cleanup-result.json。清理后下文旧 artifacts 路径仅记录历史来源。完整源码在 [history](../history/README.md)，当前安装包在 firmware/latest；历史测试结论及未完成验收不变。

# 0.6.1-lcd 屏幕时钟交付记录

后续修正（2026-09-16）：用户明确报告本版本实物黑屏，故可见显示验收失败。下列帧数和工具成功只证明软件路径；不能作为黑屏已修复的证据。后续修复记录见 `LCD_FIX_REPORT.md`。

2026-09-16，COM5 / ESP-HI ESP32-C3 rev0.4。已写入硬件基础提示词、LCD 驱动和工具，完成应用烧录与读回核验。真实 DeepSeek 已识别硬件映射并开启持续北京时间显示。设备留在时钟模式供用户观察；实际可见画面仍待用户测试。

## 完成的检查

| 检查 | 结果 |
|---|---|
| 固定参考 | ac6deed3 的 config.h / esp_hi.cc / README / MIT 许可证；保留来源与哈希 |
| 主机 | C11 + ASan/UBSan：音频开启 25/25；关闭 13/13 |
| 固件 | 音频开启与关闭均构建成功；实际烧录开启版本 |
| 提示词 | 大历史、工具续轮、重试场景验证板级事实留在 system 消息 |
| 显示 | 本机校时时钟跨秒、SPI 帧数递增、纯色填充、关闭/重新启用 |
| 资源 | GPIO4/5/10 与 LCD 冲突返回 busy，冲突动作链不执行；关闭恢复 GPIO10 输入访问；灯光可并行 |
| 无效参数 | 非法时区/相同前背景色拒绝，不改变已有时钟 |
| 取消 | 空闲网络场景，从发取消到读回已关闭状态上界 406 ms（含主机串口读取等待） |
| 五次启停 | 每次关闭后 free_heap 均 94,732 B，无逐次下降 |
| 真实模型 | 流式查询硬件→开启时钟→读取状态；非流式读取持续刷新状态，均 @done |
| 数据保留 | 应用逐字节匹配；启动/分区表/PHY/原录音不变；所有原上下文记录保留；68 条受保护 NVS 记录及 CRC 不变 |

软件显示状态从 frames=12 增至 148；两次真实对话后保持 ready=true、pending=false、error=ok。未通过接口读回面板 ID 或像素，`controller_verified` / `visual_verified` 均为 false。ST7735 命名指令表与实物型号不等同。

## 资源与时间边界

应用 **1,479,184 B**，比 0.6.0 增加 31,600 B；1.5 MiB 应用槽剩 **93,680 B**。静态 DRAM **211,696 B**，比 0.6.0 增加 9,478 B，包含新链接的 SPI 驱动代码/数据。显示自己的像素缓冲为 320 B，无新任务、整屏缓冲或 LVGL。

实机两次云对话后 free_heap=92,976 B，历史最低 49,712 B（48.55 KiB）；控制任务剩余栈 1,308 B，网络 worker 2,424 B。未降低 128 KiB 历史预算、2 MiB 上下文分区或 1 MiB 活动区容量。

普通 LCD 检查中单次 tick 最大 2,881 µs；真实网络会话期间记录到 **832,840 µs 墙钟耗时**，包含调度/阻塞，不能将前一个数字当作负载下上界。空闲时持续逐秒推进，本次不声称联网高负载下硬实时刷新，或所有并发情形取消均低于 500 ms。没有为此次 LCD 功能重跑语音 1000 次实验。

两次真实对话新增 8 条事件：events 1548→1556、使用 996,144→1,005,308 B；活动区剩 43,268 B。历史旧记录逐条一致，序号预留 7040→7168，CRC 均通过，未执行压缩、清空或迁移。

## 证据与回滚

本机证据目录：`artifacts/lcd-clock-v1/`，不进入 Git。

- `host-test.log` / `host-noaudio.log`；`build.log` / `build-noaudio.log`；`source-audit.json`（51 个自有 C11 编译单元、无发现）。
- `device-test.json`；`deepseek-clock-stream.json` / `deepseek-clock-nonstream.json`；`pre-readback-status.json`；`final-status.json`。
- `preflash.json` / `flash.log` / `readback.log` / `flash-validation.json` / `nvs-preserved.json`；`size.json`。
- `reference/` 保存四份固定参考；`release/` 保存当前源码、应用、ELF、map、配置、分区和 manifest。
- `before-full.bin` / `before-app.bin` 是烧录前 0.6.0 的新鲜备份；只回滚应用可保留后续新增对话。全 Flash 备份可能包含私有配置，不用于公开分发。

当前应用 SHA-256：`c2fdad71ff43dc4b73e48ab743164f2124d49982662328be966147aea459fb3e`。

读回全 Flash SHA-256：`4a5f49b28a72ff582b2305ace4c12c9c98f46085c171c4fa8cf70d876ae54b91`。

更新前应用 SHA-256：`41d50cc5e50b487cb47e6ba5dd85553f70000ba34d5c236921c286d8823d5bf5`；全 Flash：`e22bf2ad19dca76f8ee1040fabfacb25e0962e0babf7bd174c91c988b86cceb4`。

显示时钟不改变旧 M0 外网 Gateway / 真实断电项目，以及既有语音、音乐报告的验收边界。
