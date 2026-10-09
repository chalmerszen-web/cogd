# ESP-HI 设备请求到时钟固件验证

日期：2026-10-10，Asia/Shanghai。设备：COM5 / ESP32-C3 rev0.4 / 4 MiB。

**设备发信、Codex接收、实现、编译、应用烧录与实物时钟验收现已通过，最终固件0.12.4-clock。** 摄像头确认正向大字HH:MM、右下小字SS变化及重启自动恢复，见 [LCD验收报告](CLOCK_LCD_REPAIR_REPORT.md)。下文保留初轮0.12.1-clock的软件证据与“屏幕没有显示时钟”的失败记录，不把后续成功回填为旧版通过。

## 手册先行

[IO 与驱动手册](ESP_HI_IO_DRIVER_MANUAL.md)已在执行设备发信实验前落盘。覆盖 GPIO0～21、按钮、WS2812、模拟麦克风、PDM/PA、LCD SPI、身体接口、USB、Flash、校时、资源冲突、驱动方式及 IDF 调用。附官方数据手册和 TRM PDF、本地 ESP-IDF v6.1 索引；明确未知 PCB/BOM 和 LCD 芯片身份。

## 设备实际发出的原文

```json
{"type":"codex_request","id":"clock-20261010-01","text":"我要变成一个时钟，24小时制，显示时、分，右下角小字显示秒。"}
```

原文在 MCU RAM 程序中，主机只发送 GO 换行触发。镜像 1200 B，SHA-256 5d4549c3ca6a9df0e00037cc410b79428ea56d082b17f8b01160a3a3f4d682f9。实际 USB 字节与原文逐字匹配；不使用设备模型生成需求。前后完整 4 MiB Flash 一致，恢复原 0.12.0-rc2，随后把请求通过聊天转发工具送入本聊天并收到同编号消息。

这证明本轮主机触发、设备发信、电脑转发的有限流程；没有安装脱离当前助手的常驻自动烧录程序。测试源：[request.c](../experiments/usb_clock/request.c)、[接收与恢复](../experiments/usb_clock/run_request.py)。

## 实现

- plugins/display/display.c：大字 HH:MM 位于 x7～151、y16～50；小字秒位于 x134～155、y62～75，距屏幕右边和底部均 4 像素。RGB565 白字黑底，沿用 320 B 行缓冲。
- 同一帧共用一个时间快照；内部 hms 保留秒供渲染。24 小时制、前导零、跨天、未校时占位均保留明确行为。
- AGENT_CLOCK_BOOT 专用构建开机开启屏幕，UTC+8；不需要每秒调用联网模型。重启后无需电脑再次发送 display set。
- 复用 GPIO4/5/10 和既有 esp_lcd SPI 初始化；未添加新的 GPIO 接线、时钟芯片或任务。
- tools/build_clock.ps1 生成独立 build-clock/，版本为 0.12.1-clock。

下图来自实际固件 C 渲染函数，不是实物照片：

![实际 C 渲染器输出](../artifacts/clock-20261010/clock-235958-preview.png)

## 验证结果

| 检查 | 结果 | 说明 |
|---|---|---|
| 官方资料落盘 | PASS | 两份 PDF、有效 API/头文件快照、来源哈希 |
| MCU USB 需求原文 | PASS | 带唯一 id，RX 原始字节及 JSON 保留 |
| RAM 过程数据保持 | PASS | 前后 4 MiB 逐字节相同，恢复 rc2 |
| 当前聊天接收 | PASS | 聊天转发工具成功，当前会话收到设备原文 |
| 主机回归 | PASS 4/4 | display、control、devices、tools；ASan/UBSan 构建 |
| 时间和像素 | PASS | 午夜/中午/跨天、秒变动仅影响右下区域、RGB565 缓冲边界 |
| 三种渲染预览 | PASS | 23:59:58、00:00:00、未校时占位；实际 C 函数导出 |
| IDF 编译 | PASS | ESP-IDF v6.1，应用 1,531,728 B，1504 KiB预算余8,368 B |
| 应用烧录/读回 | PASS | 写入地址0x10000；应用逐字节读回匹配 |
| 非应用区保持 | PASS | 启动、分区、NVS、PHY、ctx、clip 烧录前后逐字节相同 |
| 开机与连续刷新 | PASS | 自动clock，time_valid，6次时间持续前进，帧数3→8 |
| 重启自动恢复 | PASS | 实际reboot后自动clock，未发送display set |
| 时钟对照 | PASS | 采样屏幕时间与电脑UTC+8相差1～2秒；不是外部授时精度标定 |
| 历史保持 | PASS | 本次显示检查前后统计一致；1472事件、990348B、2MiB分区 |
| 实物字符/位置/跳秒 | FAIL | 用户反馈“屏幕没有显示时钟”；继续调查 LCD 显示链路 |

设备状态中的 visual_verified=false 是驱动本身无法看见屏幕的声明；人工确认单独记录，不改成伪造的硬件读回。

## 固件与恢复

新应用 SHA-256：aa2fb920d7ba9fd40e198e7173e6d1c45bb5619a152d9dda23e0b9295e9f7f64。

烧录前新鲜完整备份：backups/kws-clock-20261009-190056/（目录名为工具记录的 UTC 时间，实际本地时间是2026-10-10）。旧应用 application-before.bin 的 SHA-256 为 5a1e82db96bea8072f44415c1c747d0050c7f6785aa7d49e49e9bb66cb35946b，对应0.12.0-rc2。完整 Flash 包含私人配置，仅留本机。

有限测试结束：0.12.1-clock、busy=false、Wi-Fi在线、wake listening，COM5已关闭。最低堆64,908B，控制任务栈水位1196B。既有语音缺陷不属于本次修复或验收。

证据目录：artifacts/clock-20261010/，包括原始USB、RAM前后镜像、聊天回执、主机/编译日志、Flash结果和device-test/result.json。发布包保留独立版本及对应源码；原有0.12.0-rc2发布目录保留。
