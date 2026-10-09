# ESP32-C3 原生 RAM 灯光实验

用户要求（2026-10-09）：记录现有软件版本，参考乐鑫资料，用机器码让 USB 串口连接的 C3 每 3 秒切换颜色，共运行 60 秒。

## 验收

| 编号 | 要求 | 证据 |
|---|---|---|
| NATIVE-01 | 先查询版本，备份完整 4 MiB Flash，并在设备上校验 | `firmware-before.json`、`backup.log`、`flash-before.bin` |
| NATIVE-02 | 独立 RV32IMC 程序，只含 IRAM/DRAM 段，无 RTOS/SDK 运行库 | ELF、反汇编、`build-report.json`、`image-info.txt` |
| NATIVE-03 | GPIO8 驱动四颗 WS2812，红/绿/蓝循环，20 个各 3 秒的区间，结束熄灭 | 原生串口日志、RMT TX_END、SYSTIMER 时间戳 |
| NATIVE-04 | 每区间误差在 ±2 ms 内，累计 60 秒误差在 ±2 ms 内 | `result.json`，以硬件发送完成时间计算 |
| NATIVE-05 | 执行前后完整 Flash 逐字节一致，随后恢复原固件版本、语音开关和灯光状态 | 每次 `run-*/` 的前后快照、结果和 `restored/state.json` |
| NATIVE-06 | 实物颜色与节奏由用户观察；无观察证据不能标为视觉通过 | 单列用户观察结果 |

## 实现

- `start.S` 设置栈与异常入口，清 BSS；`native_led.c` 直接读写 GPIO、RMT、SYSTIMER、USB Serial/JTAG 寄存器。
- GNU RISC-V 工具链把 C 和汇编转换成真实机器码。C3 执行生成的指令，不在设备上解释 C 或脚本。
- 唯一调用的芯片 ROM 例程为 `disable_default_watchdog`；没有链接 ESP-IDF、FreeRTOS 或 C 标准运行库。
- RMT 采用 XTAL 40 MHz，分频后 10 MHz；CH0 使用 3×48 个硬件符号槽。每帧发送 4×24 个 GRB 数据位和 280 µs 低电平复位。
- 颜色亮度为 64/255；所有四颗灯同步。原状态 RGB(0,0,0)，结束发送全零帧。
- SYSTIMER 为 XTAL/2.5=16 MHz，按绝对时刻启动各帧，避免日志和循环累计延迟。
- 通过 ROM `load-ram` 装载。程序结束后保留在 RAM 中等待主机核验与复位，不持久安装。
- 启动器仅允许运行 `read-flash`、`verify-flash`、`load-ram` 和用于复位的 `read-mem`。
- 先前测试证据按独立目录保留，每轮冻结实际构建和源码。失败也执行 Flash 核验与原固件恢复。

## 资料

- [用户指定的乐鑫文档入口](https://documentation.espressif.com/zh/documentList?eol=false)：页面通过 JavaScript 加载，正文检索使用下列乐鑫原始资料。
- [ESP32-C3 TRM v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)：CPU CSR、内存、GPIO、SYSTIMER、USB Serial/JTAG 和 RMT。
- [esptool load-ram](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/esptool/advanced-commands.html#load-a-binary-to-ram-load-ram)：只装载 IRAM/DRAM，使用 `--no-stub`。
- 本地 Espressif ESP-IDF 6.1 的 ESP32-C3 寄存器头文件、HAL 实现和 ROM 地址表用于交叉核对；它们不参与本实验链接。

复现入口：使用项目已有 Python 环境运行 `build.py`，再运行 `run_test.py --run-name run-唯一标识`。后一命令会复位 COM5 设备并执行 60 秒测试。
