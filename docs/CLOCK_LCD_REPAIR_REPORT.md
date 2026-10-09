# ESP-HI LCD 修复与时钟实物验收

2026-10-10，COM5，ESP32-C3 rev0.4，4 MiB Flash。

**本次时钟目标已完成，当前安装 0.12.4-clock。** 设备通过真实 USB 发出指定中文请求并转入当前 Codex；已完成编程、编译、烧录。Insta360 Link 原始照片确认：USB 接头在上时画面正向，大字显示 24 小时制 HH:MM，小字秒数位于右下角并持续变化，软件重启后自动恢复。芯片资料与 IO/IDF 调用见 [IO 与驱动手册](ESP_HI_IO_DRIVER_MANUAL.md)。

验收范围是此次设备请求到可见时钟的有限验证。USB 内容经电脑转入 Codex，没有安装常驻自动接收、唤起聊天或自动烧录服务；旧语音质量问题不属于此次通过范围。

## 最终实物结果

用户确认拔电 10 秒并重新摆放后，0.12.3 首次显示真实时钟，但整体旋转 180 度、秒数在左上角；随后软件重启仍可显示。0.12.4 把 MADCTL 从 0xa8 改为 0x68，并使用窗口偏移 (1,26)，修正方向和边缘位置。

| 检查 | 原始观察 |
|---|---|
| 自动开机显示 | boot.jpg：04:16，大字方向正常，右下角 09 |
| 连续秒数 | sample-0.jpg 至 sample-5.jpg：右下角依次 11、13、15、17、19、21 |
| 软件重启自动恢复 | reboot.jpg：04:16，右下角 29；无需再次发送 display set |
| 板上状态 | mode=clock、ready=true、error=ok；6 次时间/帧数采样通过 |
| 回归 | display、control、devices、tools 共 4 项通过 |
| 数据保持 | 应用读回匹配；非应用区逐字节保持；显示测试前后历史统计一致 |

8 张原始照片及采集时间、SHA-256 见 artifacts/clock-20261010/device-test-0.12.4/。独立实物观察结论在 user-observation-0.12.4.json，observer 明确为助手通过摄像头观察。固件自身的 visual_verified=false、controller_verified=false 保持真实含义：MCU 没有独立视觉反馈，也没有读到控制器 ID。

应用 1,532,000 B，SHA-256 917c869e62681c1c0f2197a69311c2233fdffcb086c887ee48a21acbe04a000d。烧录前完整备份为 backups/kws-clock-20261009-201447/（工具使用 UTC 命名），只写 0x10000 应用区域。完整 Flash 备份含私人配置，仅留本机，不进入交付包。

构建入口 tools/build_clock.ps1；设备验证入口 tools/test_clock_device.py --version 0.12.4-clock --require-held-dc --reboot。可选 --camera 指向外部更新的原始 JPEG，让脚本保存与设备检查同阶段的新鲜照片；照片是否满足显示要求仍需单独目视评估。

## 已完成的定位

| 检查 | 结果与边界 |
|---|---|
| 更换 USB 线后重新检测 | COM5 可正常查询、备份、加载 RAM 和烧录 |
| 摄像头 | Insta360 Link；用户双击触摸键回正；最终 Tilt=-22 可看到完整 LCD；图像有反光 |
| 0.12.1 时钟 | 用户确认没有显示；背光亮、无字符 |
| 初版 RAM bitbang | 软件绘制完成且 Flash 不变；用户未见色带。引脚自检有额外 SCLK 边沿，不能排除错位 |
| 0.12.2 DC 持续驱动 | 编译、应用读回、非应用区保持、时间刷新、重启检查通过；GPIO10 输出使能和高电平读回通过；摄像头未见时钟或纯红 |
| RAM 八种字节对齐 | 2384 B 镜像，8 次全 GRAM 四色条；8 张照片均无色条；前后 4 MiB 一致并恢复 0.12.2 |
| 两份板级定义交叉核对 | pinned xiaozhi BSP 与 Espressif Board Manager 均为 MOSI4/SCLK5/DC10，均不使用软件 CS/RST；实际物理连通仍未测量 |
| 0.12.3 冷上电 | 用户实际拔插后有字符，但方向倒置；再软件重启仍有字符 |
| 0.12.4 方向和窗口修正 | 摄像头确认大字 HH:MM、右下小字 SS、连续变化及重启恢复，PASS |

0.12.2 的更改来自本地 IDF v6.1 panel IO 源码：事务结束回调会释放 DC 输出。板级现独立驱动 GPIO10，panel IO 的 dc_gpio_num=-1，命令和参数在同一显示锁内发送。这修正了可确认的电平保持问题，但没有解决观察到的空白屏。

0.12.3 进一步避免 GPIO5 的 gpio_reset_pin 上拉副作用，闲置输入使用下拉。编译、4项相关主机回归、仅应用烧录及读回、开机/软件重启自动时钟检查均通过。首次暖启动照片仍无字符，原 FAIL 保留；用户随后拔插10秒，冷上电出现倒置的真实时钟，冷启动结果更新为字符可见、布局 FAIL。这个版本没有增加随机 GPIO 探测或更换未经核对的 LCD 电压参数。

历史 0.12.3 应用 SHA-256 f71e756a74dc3004e6c7cd191a4fce4175a35ffa1a737bcc97bbff35e8d8b61a，备份 backups/kws-clock-20261009-195431/。DC 保持、SCLK 闲置状态修正与物理断电共同构成成功路径，未逐项隔离验证，不能把最初空白的唯一根因归给其中某一项。

## 原始证据

- artifacts/clock-20261010/camera/clock-0.12.2-observed.jpg：时钟模式实物照片。
- artifacts/clock-20261010/camera/white-fill-0.12.2-observed.jpg、red-fill-0.12.2-observed.jpg：纯色对照。
- artifacts/clock-20261010/lcd-phase-run/phase-0.jpg 至 phase-7.jpg：8 种对齐位置。
- artifacts/clock-20261010/lcd-phase-run/result.json：前后 Flash SHA-256 bb3409a4925482cfd5b05f6161c2e597120969a321449477c12782cb29fdc6ed，逐字节一致，恢复版本 0.12.2-clock。
- artifacts/clock-20261010/device-test-0.12.2/result.json、flash-0.12.2.log：先前固件的软件检查和应用烧录保持证据。
- artifacts/clock-20261010/device-test-0.12.3/result.json、flash-0.12.3.log、host-tests-0.12.3.log：历史固件的软件检查；camera/clock-0.12.3-warm-observed.jpg 为最初重启后的空白屏照片。
- artifacts/clock-20261010/camera/clock-0.12.3-cold-observed.jpg、clock-0.12.3-after-cold-reboot.jpg：物理拔插后及随后软件重启的倒置时钟。
- artifacts/clock-20261010/device-test-0.12.4/result.json、user-observation-0.12.4.json、flash-0.12.4.log、host-tests-0.12.4.log：最终软件、实物、烧录与回归结果。
- artifacts/clock-20261010/request-run/result.json、request.json、usb-rx.bin、02-codex-relay.json：最初精确中文请求、真实 USB 接收与当前聊天转发回执。

RAM 实验源为 experiments/lcd_probe_ram/；摄像头控制脚本 tools/camera_control.ps1 使用 Windows DirectShow 的 IAMCameraControl，未安装驱动或修改设备固件。

## 资料和未确认项

[IO 与驱动手册](ESP_HI_IO_DRIVER_MANUAL.md)覆盖 GPIO0～21、实际接口、IDF API 和下载的芯片资料。ST7735S 数据手册作为现有兼容配置的参考；没有宣称读到屏幕控制器 ID。

第二份板级配置来源为 [Espressif Board Manager 固定提交](https://github.com/espressif/esp-board-manager/tree/cbee842b9eb75cf86b731f1933f71adee9e41d7f/esp_friends_boards/esp_hi)。引脚和寄存器初始化表一致；最终参考其窗口 (1,26)，保留已显示正常的反色设置。早先全 GRAM 实验没有色条，不能据此把窗口差异认定为空白屏唯一根因。开源硬件站返回 HTTP 403/418，未读取到电路原理图。

屏端电平和排线没有示波器/万用表测量，控制器型号仍采用兼容配置而非 ID 读回。物理断电验证对应 0.12.3；最终 0.12.4 的方向/窗口修正经烧录启动和软件重启验证。无独立 RTC 时，完全断电后须联网校时，未校时显示占位。相机画面中固定绿色高光在黑屏及 display-off 时也存在，疑似反射，尚未证实为像素故障；可读字符及秒数验收通过。
