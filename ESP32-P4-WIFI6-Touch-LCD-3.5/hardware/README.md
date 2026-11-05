# 实物硬件参考

用户于 2026-10-31 确认板背面型号为 **ESP32-P4-WIFI6-Touch-LCD-3.5**。
原始文件为 [Waveshare 官方 Rev1.2 原理图](https://raw.githubusercontent.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-3.5/main/schematic/ESP32-P4-WIFI6-Touch-LCD-3.5-schematic.pdf)，归档在 `references/original.pdf`。
官方示例仓库版本：`870588e62daaf723f3c52f0060d8dc053665ed30`。

| 功能 | 芯片 / GPIO |
|---|---|
| 双向音频 | ES8311，I2C 地址 0x18 |
| I2C | SDA 7，SCL 8；音频与触摸共用 |
| I2S | MCLK 13，BCLK 12，WS 10，DOUT 9，DIN 11 |
| 扬声器放大器使能 | GPIO 53 |
| 屏幕 | ST7796，320×480，SPI MOSI 20，CLK 21，CS 23，DC 26，RST 27，背光 28 |
| 触摸 | FT5x06，RST 29，INT 50 |
| ESP32-C6 SDIO | CLK 18，CMD 19，D0..D3 14..17，RESET 54 |
| USB TO UART | P4 TX 38，RX 37；本次设备为 COM3 |

最初指定的 4.3 原理图已转换为嘉立创 EDA 原生工程，位于相邻 `ESP32-P4-WIFI6-Touch-LCD-4.3/hardware/native/`。
该工程的 ES7210、MIPI 屏幕和引脚不能作为本 3.5 板的接线图。

## 嘉立创 EDA 专业版工程

入口：[ESP32-P4-WIFI6-Touch-LCD-3.5-Reference.eprj3](native/ESP32-P4-WIFI6-Touch-LCD-3.5-Reference/ESP32-P4-WIFI6-Touch-LCD-3.5-Reference.eprj3)。在嘉立创 EDA 专业版 4.1.71 打开该文件，并保留旁边的 `sch/Reference/*.esch2`；这是包含符号、器件、引脚、导线和网络标签的可编辑原生工程。

- 17 张图纸、246 个器件、861 个引脚、243 个网络；按电源、P4、音频、屏幕等区域重新排版。
- [PNG 预览包](Waveshare-P4-reference.zip)，单页图在 `previews/`。
- [源数据](source-netlist.json)、[嘉立创导出网表](native-export.net)、[原生导入与导出比对](../evidence/native-connectivity-check.json)。实际打开并导出的 861/861 引脚网络与重建数据一致，无缺失或多余引脚。
- 源 PDF SHA-256：`3d8b63f8f2a5a89d00c54eaf9798658f3147774e8d3383a1b0a96d5fb9c82d30`。

3.5 PDF 只有器件/引脚书签，没有 4.3 PDF 中的网络书签。因此本工程的连接由 PDF 矢量导线、连接点和网络/电源标签重建。引脚编号来自书签；861 个引脚均映射到了独立的原图引脚线段。已复核电源网络分离、I2S/I2C/屏幕/SDIO 对应关系，以及旋转/合并文本中的器件值。源图 U7 的 100 脚隐藏标记与 10 脚重复，按可见 XTAL_P 引脚位置修正，保留了几何证据。

**导出比对证明嘉立创导入没有改变重建数据，不等于与厂家原始电气网表完成了独立校验。** 工程使用通用可编辑符号；引脚类型统一为 Passive，封装尚未分配。DRC 留有 246 项未分配封装和 67 项悬空提示；部分原图没有可读的器件型号/数值，标为 `UNSPECIFIED (PDF)`。后续修改为毛巾机或制作 PCB 时，需要逐项复核原 PDF、选择正式符号/封装、设定电气引脚类型并处理这些提示；目前交付为原理图连接参考工程。

重建脚本依次为 `tools/analyze_pdf_pins.py`、`tools/reconstruct_nets.py`、`tools/convert_schematic.py`；原始几何、未绑定文本和导入验证均留在 `evidence/`。不要在已有嘉立创会话打开该工程时重新生成磁盘文件。
