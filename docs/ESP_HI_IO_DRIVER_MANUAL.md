# ESP-HI 接口、GPIO 与 ESP-IDF 驱动手册

版本：2026-10-10。对象：本项目已连接的 ESP-HI 头部板，ESP32-C3 rev0.4、4 MiB Flash、40 MHz 晶振、原生 USB Serial/JTAG。本轮起始固件为 **0.12.0-rc2**，最终时钟固件为 **0.12.4-clock，实物显示已由摄像头验证**。本文是“设备提出需求 → Codex 修改程序 → 编译 → 烧录 → 实机验证”的硬件依据。

## 1. 使用范围与证据

接线依据来自当前板级源码、既有实机测试和固定版本 ESP-HI BSP 摘录；芯片能力以 Espressif 手册及本项目 SDK 为准。**源码中的 GPIO 定义不能代替电路原理图。** PCB 版本、麦克风/功放型号、喇叭阻抗、电源额定电流、连接器针序及 LCD 实际控制芯片均未完成器件鉴定。

| 资料 | 用途 | 当前依据 |
|---|---|---|
| 本项目驱动 | 软件实际配置、引脚与资源占用 | boards/esp_hi、plugins/display、platform/espidf |
| 实机 USB 查询 | 固件、硬件映射、当前模式 | artifacts/clock-20261010/01-baseline.json |
| ESP-IDF | 可编译的头文件与函数签名 | 本地 .toolchains/esp-idf，v6.1，提交 fff9895c82d744c7237be8847347bdd1b07c6643 |
| ESP-HI 参考 BSP | LCD 初始化表与原始信号名称 | xiaozhi-esp32 提交 ac6deed3d8e75348475364bf40ad953c6cd48054，项目保留的摘录及许可证 |
| 芯片资料 | 电气、启动、外设、寄存器 | 本文第 11 节的官方 PDF 与 IDF 文档 |

本轮下载的官方文件及 SHA-256 记录在 [资料索引](reference/esp32c3/sources.json)。板载接线与芯片任意引脚复用能力分开理解：芯片允许复用，不代表已经焊接外设的板载引脚可以随意接其它电路。

## 2. GPIO0～21 完整分配

| GPIO | ESP-HI 接线/功能 | 当前驱动 | 软件使用边界 |
|---:|---|---|---|
| 0 | MOVE_WAKE / 顶部按钮；芯片另有 ADC1_CH0 | gpio_get_level | 只读输入，内部上拉；按钮机械位置沿用 BSP 名称 |
| 1 | AUDIO_WAKE / 底部按钮；ADC1_CH1 | gpio_get_level | 只读输入，内部上拉 |
| 2 | 模拟麦克风，ADC1_CH2 | adc_continuous | 音频驱动独占；同时是启动配置引脚 |
| 3 | 功放 PA 使能；ADC1_CH3 | gpio_set_level | 音频驱动管理，播放前后控制电平 |
| 4 | LCD MOSI；ADC1_CH4 | SPI2 / esp_lcd | 屏幕开启时不可作普通 GPIO |
| 5 | LCD SCLK；ADC2_CH0 | SPI2 / esp_lcd | 屏幕开启时不可作普通 GPIO；0.12.3 输入闲置时下拉 |
| 6 | 扬声器 PDM 正向数据 | I2S0 PDM TX | 音频驱动管理 |
| 7 | 同一路 PDM 的反向输出 | GPIO Matrix | 音频驱动管理，不是另一独立声道 |
| 8 | 四颗串联 WS2812 数据线 | led_strip / RMT | 灯光驱动独占；同时是启动配置引脚 |
| 9 | BOOT 按钮 | gpio_get_level | 只读输入、上拉；同时参与下载启动选择 |
| 10 | LCD DC（命令/数据） | 板级 gpio_set_level | 与 GPIO4/5 作为一组申请和释放；0.12.2 起持续驱动 |
| 11 | 板上用途未核实；芯片有 VDD_SPI 相关限制 | 禁止通用访问 | 不依据“编号存在”推断可用 |
| 12 | Flash 接口保留 | SPI0/1 / 启动系统 | 禁止重新配置 |
| 13 | Flash 接口保留 | SPI0/1 / 启动系统 | 禁止重新配置 |
| 14 | Flash 接口保留 | SPI0/1 / 启动系统 | 禁止重新配置 |
| 15 | Flash 接口保留 | SPI0/1 / 启动系统 | 禁止重新配置 |
| 16 | Flash 接口保留 | SPI0/1 / 启动系统 | 禁止重新配置 |
| 17 | Flash 接口保留 | SPI0/1 / 启动系统 | 禁止重新配置 |
| 18 | USB D−；参考身体 BR 信号与其重叠 | USB Serial/JTAG | 当前 USB 链路保留，不能同时作身体输出 |
| 19 | USB D+；参考身体 FR 信号与其重叠 | USB Serial/JTAG | 当前 USB 链路保留，不能同时作身体输出 |
| 20 | 参考身体 BL；芯片默认 UART0 RX 复用能力 | 通用 GPIO/LEDC | 板上不是已证实的空闲焊盘；未启用舵机协议 |
| 21 | 参考身体 FL；芯片默认 UART0 TX 复用能力 | 通用 GPIO/LEDC | 板上不是已证实的空闲焊盘；未启用舵机协议 |

GPIO2/8/9 在复位时参与启动配置。普通运行阶段能使用这些外设，不代表复位期间可以任意强拉电平。电平、电源和驱动能力要依据芯片数据手册与板级电路；不要把 USB 的 5 V 电源当成 GPIO 信号电平。

芯片引脚限制与复用说明：[ESP-IDF v6.1 GPIO](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/gpio.html)。板级原始清单：[pinmap.csv](pinmap.csv)、[硬件审计](HARDWARE_AUDIT.md)。

## 3. 板载外设与接口

| 外设 | 接口参数 | 本项目实现 | 验证边界 |
|---|---|---|---|
| USB 串口/调试 | GPIO18/19，Windows COM5，303A:1001 | driver/usb_serial_jtag.h | 已收发真实中文；USB 数据不自动成为 Codex 消息 |
| RGB 灯 | GPIO8，4 颗 WS2812，GRB 线上顺序 | boards/esp_hi/board.c | 既有实机颜色与节奏人工确认；API 参数仍为 RGB |
| 三个按钮 | GPIO0/1/9 | boards/esp_hi/control_board.c | 当前只读，上拉；需实物核对按钮丝印 |
| 麦克风 | GPIO2 / ADC1 CH2 / 12 bit | boards/esp_hi/audio_board.c | 交付配置按 32 kHz 采样后处理为 16 kHz；电路增益非标定值 |
| 扬声器 | GPIO6 PDM、GPIO7 反相、GPIO3 PA | audio_board.c | 通常 24 kHz PCM；功放芯片、阻抗未知 |
| LCD | 160×80、SPI2、8 MHz、mode 0、RGB565 | display_board.c + plugins/display/display.c | MOSI4/SCLK5/DC10；无软件 CS/RST/MISO/背光控制 |
| 身体输出 | BSP 为 18/19/20/21 | 当前只开放 20/21 普通 GPIO/PWM | 未启用身体驱动；18/19 与 USB 冲突 |
| Flash | 4 MiB | esp_partition、NVS、项目 WAL | 固件、凭据、历史和录音分区必须区分 |
| Wi-Fi 与校时 | ESP32-C3 内置 Wi-Fi | esp_wifi、esp_netif_sntp | 使用设备已有配置；系统时间有效后显示实际时钟 |

板上没有被核实的独立 RTC、I2C 传感器、SD 卡、以太网或外接 UART 设备。ESP32-C3 的 I2C、UART、TWAI、BLE、温度传感器、GPTimer 等芯片能力仍可查官方手册，但不能把这些能力写成板上已经存在的器件。

## 4. 驱动 GPIO 的三个层次

### 4.1 当前 Agent 的 USB 命令

这是使用现有固件最直接的方式。使用项目根目录“打开设备对话.cmd”，或 tools/usb_command.py。下列命令不需要模型推断：

```text
agent hardware
agent gpio get {"pin":9}
agent display status
agent light get
```

确认 GPIO20 的外接负载适合所需电平后，可使用：

```text
agent gpio set {"pin":20,"mode":"output","value":0}
agent gpio set {"pin":20,"mode":"pwm","hz":1000,"duty":250}
agent gpio set {"pin":20,"mode":"input"}
```

本项目 PWM duty 的单位是千分比（0～1000），250 表示 25%；允许频率 10～5000 Hz，当前分配两个 LEDC 通道，10 bit 硬件分辨率。这是本项目的接口范围，不是芯片全部极限。GPIO0/1/9 只允许输入。屏幕持有 4/5/10 时通用写入返回 busy。

### 4.2 ESP-IDF C 驱动

GPIO 输入用 gpio_config 配置后读取 gpio_get_level；输出用 gpio_set_level；PWM、SPI、I2S、RMT 由各自外设驱动配置 IO MUX/GPIO Matrix。不要在外设运行时再用 gpio_config 覆盖它的配置。

下面是独立固件中的按钮输入示例；在当前 Agent 内应复用已经初始化的资源：

```c
#include "driver/gpio.h"
#include "esp_err.h"

void button_init(void)
{
    const gpio_config_t c = {
        .pin_bit_mask = 1ULL << GPIO_NUM_9,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&c));
}
/* 按键按下的电平仍需按本板接线确认。 */
int button_level(void) { return gpio_get_level(GPIO_NUM_9); }
```

通用输出的顺序：确认引脚可用 → 申请项目资源 → 停止原外设 → 预设初始输出电平 → gpio_config 为输出 → 更新输出 → 结束时恢复适合该电路的输入/上下拉并释放资源。若需要读回输出脚，启用 INPUT_OUTPUT；单纯读到输出寄存器或管脚电平，不等于外部负载动作已成功。

**gpio_reset_pin 并非完全无电气副作用的复位。** 本地 IDF v6.1 实现会开启内部上拉。0.12.3 对 LCD SCLK GPIO5 跳过这一步，直接使用 gpio_config，并在输入闲置时下拉，避免人为制造 SCLK 上升沿。没有软件 CS/RST 控制时，不应依赖 DC 翻转来重新对齐字节。最终组合已通过实物时钟验收，但没有单独证明这项修正是最初空白的唯一原因。

中断方式：gpio_set_intr_type / gpio_install_isr_service / gpio_isr_handler_add，ISR 内只做最小通知，把去抖和业务放到任务中。软件按钮去抖与硬件短毛刺滤波是不同问题。

### 4.3 寄存器/原生机器码方式

此前原生 RAM 灯光实验已用 RV32IMC 程序直接操作 GPIO Matrix、RMT 和 USB。入口、链接地址、ROM 辅助函数、USB FIFO 与异常向量都由独立实验负责。这种方式不包含 IDF 初始化和资源仲裁，不能直接混入当前 Agent 的多任务驱动。

| C3 寄存器/模块 | 典型位置或 SDK 宏 | 用途 |
|---|---|---|
| GPIO 输出置位/清位 | GPIO_OUT_W1TS_REG / GPIO_OUT_W1TC_REG，GPIO 基址 0x60004000 | 原子设定输出锁存 |
| GPIO 输出使能 | GPIO_ENABLE_W1TS_REG / GPIO_ENABLE_W1TC_REG | 允许/禁止输出驱动 |
| IO MUX | soc/io_mux_reg.h | 管脚功能、上下拉和驱动能力 |
| GPIO Matrix | GPIO_FUNCn_OUT_SEL_CFG_REG、soc/gpio_sig_map.h | 把硬件外设信号连接到管脚 |
| USB Serial/JTAG | FIFO 0x60043000，EP1 配置 0x60043004 | 原生实验的 USB 字节收发 |
| SYSTIMER | 基址 0x60023000 | 原生实验计时；读取需要更新/有效握手 |
| RMT | 基址 0x60016000 | 原生实验生成 WS2812 波形 |

上述地址只针对本 ESP32-C3。完整位域以本地 SDK 的 soc/esp32c3/register/soc、include/soc 和 TRM 为准；C3 不支持照搬通用 RISC-V 的 mie CSR 用法，原生入口按本项目已验证的 start.S 设置 mstatus 与 256 字节对齐 mtvec。参考 [原生 RAM 实验源码](../experiments/native_led_ram/native_led.c) 与 [实验报告](NATIVE_LED_RAM_REPORT.md)。

## 5. 外设驱动调用速查

| 功能 | 头文件 / CMake 依赖 | 调用顺序与本项目要点 |
|---|---|---|
| GPIO | driver/gpio.h / esp_driver_gpio | gpio_config → gpio_get_level / gpio_set_level；释放时按电路设置输入/上下拉，注意 gpio_reset_pin 的上拉副作用 |
| PWM | driver/ledc.h / esp_driver_ledc | ledc_timer_config → ledc_channel_config → ledc_set_duty + ledc_update_duty；停止 ledc_stop；C3 用 LOW_SPEED |
| WS2812 | led_strip.h / espressif__led_strip | led_strip_new_rmt_device → set_pixel → refresh；本项目 RMT 10 MHz，四颗同步赋色 |
| 原生 RMT | driver/rmt_tx.h / esp_driver_rmt | rmt_new_tx_channel → encoder → rmt_enable → rmt_transmit → 等发送完成 |
| 麦克风 | esp_adc/adc_continuous.h / esp_adc | new_handle → config → register_event_callbacks → start → read/parse_data → stop/deinit |
| PDM 扬声器 | driver/i2s_pdm.h / esp_driver_i2s | i2s_new_channel → init_pdm_tx_mode → preload → enable → write → disable/del_channel |
| LCD SPI | driver/spi_master.h / esp_driver_spi | spi_bus_initialize(SPI2_HOST) → esp_lcd_new_panel_io_spi → tx_param → panel_io_del → spi_bus_free |
| LCD panel IO | esp_lcd_panel_io.h / esp_lcd | 当前按轮询小块传输；0.12.2 起 DC 由板级锁保护并持续驱动，panel IO 配 dc_gpio_num=-1 |
| USB | driver/usb_serial_jtag.h / esp_driver_usb_serial_jtag | driver_install → read_bytes/write_bytes；处理部分读写、超时和行边界 |
| 单调计时 | esp_timer.h / esp_timer | esp_timer_get_time，微秒；用于超时，不是日历时间 |
| 日历/校时 | esp_netif_sntp.h、time.h / esp_netif | esp_netif_sntp_init/start；time(NULL) 取得系统 epoch |
| 配置 | nvs_flash.h、nvs.h / nvs_flash | nvs_flash_init → nvs_open → get/set → nvs_commit；密钥不进串口日志 |
| 分区存储 | esp_partition.h / esp_partition | 按标签查分区；仅允许明确的分区范围，保留布局 |

这些调用与当前工程 main/CMakeLists.txt 的依赖对应。SPI0/1 留给 Flash；LCD 使用 SPI2。GPIO Matrix 反相 GPIO7 是本项目已有 PDM 差分式接线适配；PDM 的 DAC 命名不代表 ESP32-C3 有通用模拟 DAC 输出引脚。

## 6. LCD 的具体驱动流程

当前板级实现：[display_board.c](../boards/esp_hi/display_board.c)。纯 C 像素生成：[display.c](../plugins/display/display.c)。

1. 作为 owner 5 一次申请 GPIO4/5/10；与普通 GPIO/控制计划互斥。
2. SPI2 初始化：MOSI=4，SCLK=5，MISO=-1；不使用 DMA，当前传输块 64 B。
3. panel IO：8 MHz、mode 0、dc_gpio_num=-1、CS=-1，命令和参数均 8 bit。GPIO10 单独配置为 INPUT_OUTPUT；发命令时拉低，参数/像素时拉高，并保持两次传输之间的电平。tx_param(io,-1,...) 只发参数，不增加命令字节。
4. 软件复位 0x01 后等待 150 ms；退出休眠 0x11 后等待 120 ms。
5. 发送项目保留的 ESP-HI BSP 初始化表，包括 RGB565 格式 0x3A=0x05、方向 0x36=0x68、开显示 0x29。0.12.4 根据摄像头确认的实际安装方向翻转原0xA8的两轴，使USB插口在上方时文字正向。
6. 每行设置 0x2A/0x2B 地址窗口，再用 0x2C 写入 160×2 字节 RGB565 大端像素；可见窗口采用 Espressif Board Manager 的偏移 (1,26)：x=1～160、y=26～105。
7. 已有控制任务每约 10 ms 调用 display_tick，每次最多四行；用 320 B 行缓冲，不增加 25,600 B 的全屏缓冲。
8. 关闭时发送 0x28，删除 panel IO，释放 SPI 总线和引脚组。

初始化表历史名称存在 ST7789、ILI9341 和 ST7735 混用。本项目明确使用 **esp_hi_st7735_bsp 兼容配置**，不宣称已经读出实物芯片 ID。MISO 未接入，当前软件不能通过该接口读回屏幕 ID 或像素。

ready=true、frames 递增、sdk_error=0 只证明软件传输路径；亮屏、方向、字符位置和秒跳动要通过实物观察确认。

2026-10-10：0.12.2及RAM八种字节对齐测试未见字符/色带；0.12.3经用户实际断电10秒后首次出现倒置时钟；0.12.4修正方向与窗口，连续照片确认大字HH:MM与右下小字SS，软件重启后也恢复。摄像头原始照片和Flash保持证据见 [LCD 排查报告](CLOCK_LCD_REPAIR_REPORT.md)。新核对的 Espressif Board Manager ESP-HI 定义同样使用MOSI4/SCLK5/DC10和相同初始化表；本项目保留已验证的反色关闭。未单独证明DC、SCLK或冷启动中的哪一项是先前空白的唯一原因。

## 7. 时钟需求到实现的对应关系

本次需求原文：**“我要变成一个时钟，24小时制，显示时、分，右下角小字显示秒。”**

| 要求 | 实现约定 | 验证 |
|---|---|---|
| 24 小时制 | HH 范围 00～23，包含前导零 | 主机检查 00:00、12:00、23:59 与跨天 |
| 显示时、分 | 主区域大字 HH:MM | 从实际 C 渲染函数生成预览并检查实物 |
| 右下角小字秒 | SS 范围 00～59，独立的小字区域 | 主机检查像素范围及只变秒时的变化区域 |
| 每秒更新 | 依据系统 epoch 刷新，整帧使用同一次时间快照 | 实机帧计数、时间差与连续刷新 |
| 日常可用 | 本次专用构建开机自动开启时钟，默认 UTC+8 | 烧录后的自动启动及重启验证 |
| 尚未校时 | 主时间 --:--，秒 -- | 不显示伪造的有效时间 |
| 断网 | 本次开机已校时后由系统时钟继续走时 | 不宣称外置 RTC 或断电保时 |

时间来自设备既有 SNTP 配置；时区用每次渲染的分钟偏移转换，不修改进程级 TZ。首次上电尚未校时以及断电重启不能依赖上次屏幕上的数值。普通秒刷新无需联网模型、Codex 或 USB 常驻连接。

## 8. 设备如何给 Codex 发需求

USB Serial/JTAG 是收发字节的接口。电脑端必须读取、组帧，再把需求交给指定聊天；不是 USB 键盘模拟，也不是 MCU 自己运行 Codex。

本次验证已使用一个独立的 RAM 请求程序：需求文字编译进 MCU 镜像，电脑只发送 GO 触发；MCU 通过 USB 发出带 id/type/text 的 JSON 行。电脑核对 id、精确文本和完整换行，然后把真实串口内容转发到当前聊天。该 RAM 程序结束后恢复原固件，再按收到的需求编译并烧录时钟版本。

```json
{"type":"codex_request","id":"clock-20261010-01","text":"我要变成一个时钟，24小时制，显示时、分，右下角小字显示秒。"}
```

一次验收保留：RAM 镜像哈希、实际 RX 字节、原文匹配、聊天转发回执、需求到修改的映射、编译日志、应用写入与读回校验、屏幕观察。相同 id 不重复触发烧录；不把普通调试日志当作开发命令。

上一轮仅使用现有模型回传并由助手转发；本轮精确文本由芯片内的确定性程序产生，避免模型改写。常驻桥接及任意外部需求的自动烧录不属于本次有限验证。

## 9. 编译、烧录与回滚

本项目工具链入口 tools/idf.ps1 设置私有 ESP-IDF v6.1 环境。基础语音构建沿用 tools/build_voice_test.ps1；时钟构建使用本次新增的明确开关，记录完整 CMake 配置。采用独立 build 目录，保存原始 release。

| 分区 | 偏移 | 大小 | 本次处理 |
|---|---|---|---|
| 启动/分区表区域 | 0x000000～0x008FFF | 按现有镜像 | 保留 |
| nvs | 0x009000 | 0x006000 | 保留凭据和配置 |
| phy_init | 0x00F000 | 0x001000 | 保留 |
| factory 应用 | 0x010000 | 0x180000 | 仅更新此区域内应用 |
| ctx 历史 | 0x190000 | 0x200000 | 保留 |
| clip 录音 | 0x390000 | 0x070000 | 保留 |

应用槽为 1536 KiB，项目正常应用预算为 1504 KiB（1,540,096 B）。烧录工具 tools/kws/flash_guard.py 会先读取并设备校验完整 4 MiB，确认分区布局，再只写 0x10000；读回应用并比较非应用区，最后复位。全 Flash 备份含私人配置，仅留本机，不打入公共资料包。

普通运行会更新历史或射频校准等数据，因此“烧录时非应用区保持”与“跨多次运行 Flash 永远逐字节相同”不是同一个结论。回滚应使用本次新鲜保存的 application-before.bin 或已验证的旧 release，不能全片擦除。

## 10. 常见问题定位

| 现象 | 首先检查 |
|---|---|
| COM5 无法打开 | 是否被串口窗口占用，设备是否重新枚举；重新检测，不假定端口永久不变 |
| USB 失联 | 是否改了 GPIO18/19 或进入下载/复位状态；当前项目禁止复用 USB 引脚 |
| GPIO 返回 forbidden | 当前板级许可表与专用驱动占用，不只看芯片 GPIO 范围 |
| GPIO 返回 busy | LCD owner5、音频、控制计划或存储资源互斥 |
| LCD ready 但没字 | 实物屏幕、供电、初始化配置、方向/偏移；不能用帧数代替观察 |
| 时钟为 --:-- | Wi-Fi/SNTP 和 time_valid；不向模型询问当前时间当硬件时基 |
| 秒不跳或慢 | 比较系统 epoch、frames、max_tick_us；检查任务阻塞和当前设备 busy |
| PWM 数值异常 | 本项目 duty 为千分比；硬件为 10 bit，两通道限制 |
| 扬声器无声/噪声 | PDM6/反相7、PA3、采样率、音量和资源拥有者；不把它当标准 I2S 三线 DAC |
| 烧录被尺寸/布局校验拒绝 | 保留拒绝结果；检查应用预算和分区，不能为通过而擦除数据或压缩历史 |

## 11. 官方资料与本地副本

- [ESP32-C3 数据手册（本地 PDF）](reference/esp32c3/esp32-c3_datasheet_en.pdf)：引脚、启动配置、供电、电气及外设概要。[官方来源](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)。
- [ESP32-C3 技术参考手册 TRM（本地 PDF）](reference/esp32c3/esp32-c3_technical_reference_manual_en.pdf)：IO MUX/GPIO Matrix、时钟复位、内存、USB、SPI、RMT 等寄存器。[官方来源](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)。
- [Sitronix ST7735S v1.1（本地 PDF）](reference/esp32c3/ST7735S_V1.1_20111121.pdf)：BSP 配置所对应的控制器参考，不等于确认实物型号。印刷页 36～40 说明串行位采样及 CS/复位恢复协议；[Waveshare 托管的厂商手册](https://files.waveshare.com/upload/e/e2/ST7735S_V1.1_20111121.pdf)。
- [Espressif Board Manager ESP-HI 定义](https://github.com/espressif/esp-board-manager/tree/cbee842b9eb75cf86b731f1933f71adee9e41d7f/esp_friends_boards/esp_hi)：用于第二来源核对引脚和初始化，不覆盖实物器件鉴定。
- [GPIO API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/gpio.html)；[LEDC API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/ledc.html)；[RMT API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/rmt.html)。
- [ADC 连续采样](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/adc/adc_continuous.html)；[I2S/PDM](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/i2s.html)。
- [SPI Master](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/spi_master.html)；[LCD 驱动](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/peripherals/lcd/index.html)。
- [USB Serial/JTAG 控制台](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-guides/usb-serial-jtag-console.html)；[USB 驱动 API](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_driver_usb_serial_jtag/include/driver/usb_serial_jtag.h)。
- [System Time/SNTP](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/system/system_time.html)；[esp_timer](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/system/esp_timer.html)；[NVS](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/storage/nvs_flash.html)。

SDK 内可直接检索 components/esp_driver_gpio、esp_driver_ledc、esp_driver_spi、esp_driver_i2s、esp_driver_usb_serial_jtag、esp_adc、esp_lcd、esp_netif、soc/esp32c3，以及 managed_components/espressif__led_strip。联网文档与本地版本不一致时，以本次实际编译的头文件、配置和构建结果记录差异。

## 12. 本次验收记录入口

手册完成后已执行设备需求回传，随后实现时钟。分阶段要求见 [时钟验证规格](../experiments/usb_clock/SPEC.md)，初轮结果见 [USB 时钟报告](USB_CLOCK_VALIDATION_REPORT.md)，最终 0.12.4 实物通过及先前失败记录见 [LCD 验收报告](CLOCK_LCD_REPAIR_REPORT.md)。
