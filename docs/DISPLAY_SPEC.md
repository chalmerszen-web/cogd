# LCD、硬件提示词与本地时钟

0.6.1-lcd 添加硬件事实提示词和三个工具；用户随后报告实际黑屏。0.6.2-repair 改用 ESP-IDF LCD panel IO，注入每轮实时状态，并保留可见显示未验证标志。上下文、音乐和唤醒容量保持原值。

## 接口来源

依据固定参考 [ESP-HI config.h](https://github.com/78/xiaozhi-esp32/blob/ac6deed3d8e75348475364bf40ad953c6cd48054/main/boards/espressif/esp-hi/config.h) 和同目录 [esp_hi.cc](https://github.com/78/xiaozhi-esp32/blob/ac6deed3d8e75348475364bf40ad953c6cd48054/main/boards/espressif/esp-hi/esp_hi.cc)：

| 外设 | GPIO / 配置 |
|---|---|
| 屏幕 | MOSI 4、SCLK 5、DC 10；SPI2 mode 0；160×80 |
| 屏幕其余接口 | CS、RST 为 NC；没有已知可控 MISO / 背光引脚 |
| 按键输入 | MOVE_WAKE 0、AUDIO_WAKE 1、BOOT 9 |
| 音频 | MIC ADC1 CH2＝2；PA＝3；PDM＝6/7 |
| 四颗 WS2812 | 8 |
| 身体接口 | 20/21 可作一般 GPIO；18/19 已被 USB 使用 |
| 保留 | USB 18/19、Flash 12–17、未核实 11 |

参考代码宏写 ST7789，却把 ST7735 命名的自定义寄存器表交给 ILI9341 API。本实现采用这份实际执行的初始化表，称为 `esp_hi_st7735_bsp`，不据此声称已读出物理芯片型号。保留 MIT 许可证。沿用 gap=(0,24)、mirror=(false,true)、swap_xy=true、RGB565、关闭反色；合并后的 MADCTL=0xa8。SPI 从 BSP 40 MHz 降为 8 MHz，满足低频时钟刷新。

## 对话和 USB

`boards/esp_hi/hardware.c` 的常量提示词由 `plugins/llm/engine.c` 合并进每次 DIRECT 对话的 system 消息，先于历史。沿用空闲请求缓冲，不新增大块常驻内存。此提示词也说明历史对话中的“尚不支持 LCD”已经过时。Gateway 自行组装模型提示，当前不承诺它自动采用该板级提示词。

0.6.2 同时追加本轮开始时的 LCD 状态与设备运行毫秒数，要求模型以当前快照及新工具结果判断现状。历史中的成功调用可能发生在重启前，不能作为“现在已开启”的依据；`ready` / `frames` 也不能让模型声称实物已亮。

- `device.hardware.get`：引脚、外设、来源和能力。
- `device.display.get`：模式、时间、刷新帧数、异步进度、错误。
- `device.display.set`：`clock` / `fill` / `off`；线上名称使用下划线。

例如对话：“请在屏幕显示北京时间，每秒刷新，持续显示。”工具只需设置一次，后续刷新无需网络或 LLM。USB 诊断：

```text
agent hardware
agent display set {"mode":"clock","utc_offset_minutes":480}
agent display status
agent display off
```

`foreground` / `background` 为 RGB565 整数，默认白字黑底；不接受两色相同的时钟。`fill` 用 `foreground` 整屏填色。显示命令完整 schema 位于 `protocol/device-display.schema.json`。

## 执行和资源

显示驱动复用既有控制任务，无新增任务。320 字节 RGB565 行缓冲；字模和初始化表在 Flash，不使用整屏 framebuffer、DMA 或 LVGL。每次控制 tick 最多发送四行，软件复位/退出休眠的等待由状态机完成；每个完整帧采用同一时刻快照。`frames` 增加表示一帧 SPI 写入完成，不能证明肉眼可见、方向或颜色正确。

0.6.2 的 `transport` 为 `esp_lcd_spi`：参数和像素均走 panel IO 同步轮询，每块最多 64 字节；不使用异步像素队列。初次实机候选在大历史读取时发生队列等待停顿，因此保留同步传输。DC 时序由 ESP-IDF 处理。复位/退出休眠的等待从对应命令完成时计起，避免调度延迟消耗等待期。未增加 MISO 读回或改写未知引脚；CS/RST 无可控引脚时不能把未读出控制器 ID 当成识别成功。

启用时整组占用 GPIO4/5/10，资源 owner=5。冲突的普通 GPIO / 动作链返回 busy；其他外设继续工作。关闭/取消先结束 SPI，再恢复输入并释放资源。`agent cancel` 使用原子取消标志，不等待网络请求。重启默认关闭显示；没有往 NVS 持续写入秒数或显示状态。

时钟使用现有 SNTP 和设备系统时间，默认 UTC+08:00，可设 -12:00 至 +14:00 固定偏移。不实现自动夏令时。尚未校时时显示 `--:--:--`；已校时后断网可继续走时，精度取决于本机时钟，完全断电后需重新校时。

## 验证边界

主机检查参数、时区跨日、行缓冲边界、工具无效输入无副作用、资源互斥和大历史下提示词注入；同时保持无音频构建。实机检查 SPI/时钟进度、取消、反复启停、GPIO 冲突和真实 DeepSeek 调用。实际可见像素另列观察状态；软件计数不会自动将 `visual_verified` 改为 true。证据及固件校验值见 `LCD_CLOCK_REPORT.md`。
