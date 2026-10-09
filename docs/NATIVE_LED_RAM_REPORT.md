# ESP32-C3 原生机器码灯光实验记录

日期：2026-10-09。设备：ESP-Hi / ESP32-C3 / COM5 / 40 MHz 晶振。

**结果：独立原生程序在 RAM 中执行完 60 秒测试，发送和定时验证通过；完整 Flash 逐字节一致，原固件已恢复。用户要求复跑后，在 run-03 期间确认四颗灯的颜色与切换节奏正常。**

## 现有软件已记录并备份

| 项目 | 记录 |
|---|---|
| 原固件版本 | `0.12.0-rc2`，原发布状态仍为用户测试候选 |
| 当前应用大小 | 1,531,568 B |
| 应用 SHA-256 | `5a1e82db96bea8072f44415c1c747d0050c7f6785aa7d49e49e9bb66cb35946b` |
| 完整 Flash | 4,194,304 B；已执行设备端 `verify-flash` 校验 |
| Flash SHA-256 | `29ee7e802191d49ea96683430648772b747561620df8d9abb327f39817558c81` |
| 对应发布源码归档 SHA-256 | `91fae1f36154b38fd583325ae552a45a660d864306feccb71e30123a3f04401d` |
| 测试前灯光 | RGB(0,0,0) |
| 测试前语音 | 启用，处于监听 |

版本与应用二进制匹配现有 `firmware/releases/0.12.0-rc2/manifest.json`。

备份与证据位于 `artifacts/native-led-20261009/`：

- `firmware-before.json`：版本、分区、应用与源码哈希、原状态。
- `flash-before.bin`：原设备完整 Flash，保存在本机。
- `application-before.bin`：从完整备份提取的当前应用。
- `baseline/`：原状态串口查询记录。
- `backup.log`：读取与设备校验记录。

## 实际执行方式

用独立 C 文件和少量启动汇编生成 RV32IMC 原生指令，通过乐鑫 ROM 下载器的 `load-ram` 装载后，从 `0x40380000` 执行。没有设备端解释器，没有链接 FreeRTOS、ESP-IDF 或 C 标准运行库；唯一 ROM 调用是关闭下载看门狗的 `disable_default_watchdog`。

GPIO8 经 GPIO Matrix 连接 RMT CH0，直接操作寄存器驱动四颗 WS2812。四灯同步红→绿→蓝循环，亮度 64/255，共 20 个各 3 秒的区间。第一个区间从红色开始，第 20 个区间为绿色，60 秒到达后熄灯。SYSTIMER 按绝对时间安排各帧，RMT 负责脉冲时序。

| 程序资源 | 大小 |
|---|---:|
| 装载镜像 `native_led.bin` | 2,624 B |
| IRAM 指令段（含对齐填充） | 2,128 B |
| DRAM 常量/初始化数据 | 408 B |
| BSS | 4 B |
| 保留栈空间 | 8,192 B，未测量峰值 |
| 指令、数据、BSS 与保留栈合计 | 10,732 B |
| 堆 | 0 B |
| 新增 Flash 写入 | 0 B |

链接脚本分别为指令、数据和栈安排 16/8/8 KiB 地址区间；上表合计为实际段大小加保留栈，不把未使用的地址空隙视作有效程序内容。

成功镜像 SHA-256：`d1a13b3e4ecf918850426986ae716c5421c9a449077684a40daed891b50eae1f`。

## 实机验证

| 检查 | 结果 |
|---|---|
| 原生程序 READY、主机 GO 握手 | PASS |
| 20 个区间的顺序与颜色数据 | PASS |
| RMT 发送完成次数 | 22：初始熄灯、20 个颜色、最终熄灯 |
| 最短/最长区间 | 2.999998188 / 3.000001250 秒 |
| 首个颜色帧完成至熄灯帧完成 | 59.999995688 秒 |
| 主机接收日志的相应跨度 | 59.953 秒，受 USB 缓冲与轮询影响 |
| 全 Flash 测试前后逐字节比较 | PASS；与最初备份也相同 |
| 原应用哈希与版本恢复 | PASS；`0.12.0-rc2` |
| 恢复语音与灯光 | PASS；`voice_enabled=true`、`stage=listening`、RGB(0,0,0) |
| 实物灯光观察 | run-02 未观察；run-03 用户确认颜色、四灯同步与每 3 秒切换节奏正常；未进行亮度测量 |

芯片时长按标称 16 MHz SYSTIMER 计数换算，未用外部仪器校准晶振。日志字段 `visible_ticks` 表示首个颜色帧与熄灯帧的 **RMT 发送完成时间差**，不是光传感器读数。

首次尝试 `run-01` 在入口 `csrw mie,zero` 触发非法指令：`MCAUSE=2`、`MTVAL=0x30401073`。C3 未实现通用 `mie` CSR；改为仅通过 `mstatus.MIE` 禁用中断，并按 C3 要求把异常入口对齐到 256 B。第一次失败的镜像、源文件、串口日志和恢复结果全部保留，未改判为通过。

第二次 `run-02` 完成全部自动检查，退出码 0。每轮均读取执行前后全 Flash，并在结束时复位恢复原应用；未执行 Flash 擦写、密钥配置或 eFuse 操作。

### 用户要求复跑：run-03

2026-10-09 15:50:14（UTC+8）开始新一轮记录，使用与 run-02 完全相同的镜像。20 个颜色区间及最终熄灯帧全部完成，间隔 2.999998625 至 3.000001375 秒，首个颜色帧至熄灯帧 59.999997625 秒；自动检查 PASS，退出码 0。

用户在程序运行期间明确回复：“看到了，颜色和节奏正常”，对应问题为四颗灯是否按红→绿→蓝、每 3 秒同步换色。该人工观察保存在 `run-03/user-observation.json`，与代理未使用相机观察的事实分别记录。用户回复时程序尚未结束，因此没有将该回复扩大为对最终熄灯的人工确认；最终熄灯有 RMT 完成和恢复后 RGB(0,0,0) 的设备证据。

本轮前后完整 Flash 逐字节一致，并与初始备份相同。已恢复 `0.12.0-rc2`、语音启用且处于监听，串口已释放。

## 文件与复现

- `experiments/native_led_ram/`：规格、源文件、链接脚本、构建与有界实机执行脚本。
- `artifacts/native-led-20261009/build/`：成功构建、ELF、RAM 镜像、纯指令段 `instructions.bin`、带源码的反汇编、内存映射与哈希。
- `artifacts/native-led-20261009/run-01/`：第一次失败证据、源码与构建快照。
- `artifacts/native-led-20261009/run-02/`：成功执行结果、20 次切换记录、原始串口、前后 Flash 与恢复查询。
- `artifacts/native-led-20261009/run-03/`：用户要求的复跑结果、相同镜像与源码快照、人工观察确认及恢复记录。

使用项目已有 Python 环境运行：

```powershell
& .toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe experiments/native_led_ram/build.py
& .toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe experiments/native_led_ram/run_test.py --run-name run-新的唯一名称
```

运行脚本会短暂复位 COM5 并执行一次 60 秒测试。同名证据目录已存在时拒绝覆盖。本次没有发布新固件，也没有改变现有 `latest` 安装包。

依据：[乐鑫文档入口](https://documentation.espressif.com/zh/documentList?eol=false)、[ESP32-C3 TRM v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)、[esptool RAM 装载说明](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/esptool/advanced-commands.html#load-a-binary-to-ram-load-ram)。寄存器和 ROM 地址同时与项目已有乐鑫头文件交叉核对。


### 2026-10-09 16:38 用户要求再次运行：run-04

使用相同机器码镜像，完成20个颜色区间及最终熄灯帧。芯片计时 59.999996188 秒，间隔 2.999997437 至 3.000002000 秒，主机记录60.000秒，自动验证PASS，退出码0。本轮新鲜4MiB Flash快照与运行后快照逐字节一致（SHA256 bf759bab85989c8b429a6b69b4b32939830d47437cce9b5276e20b97f6cf22ec），应用哈希保持。已恢复0.12.0-rc2、语音监听及RGB(0,0,0)，COM5已释放。本轮未收到新的人工观察反馈，不沿用run-03的人工确认作为本轮观察。证据：artifacts/native-led-20261009/run-04/result.json。
