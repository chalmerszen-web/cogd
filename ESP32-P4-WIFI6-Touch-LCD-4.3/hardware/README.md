# 嘉立创 EDA 原理图工程

打开 `native/ESP32-P4-WIFI6-Touch-LCD-4.3-Reference/ESP32-P4-WIFI6-Touch-LCD-4.3-Reference.eprj3`。
必须保留其旁边的 `sch/` 目录；这是嘉立创 EDA 专业版原生文件夹工程，已用本机 v4.1.71 导入并导出网表验证。

来源：[Waveshare 官方 4.3 原理图](https://www.waveshare.net/w/upload/b/b8/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf)，PCB Rev1.2。
原 PDF 在 `references/original.pdf`，源网表在 `source-netlist.json`。

- 346 个器件、1,113 个引脚、268 个网络，分为 17 张功能分组图纸。
- 每个器件、引脚、导线、网络名均可编辑。用通用符号重排，未照抄原图的排版。
- 从 EDA 实际导出的 Protel2 网表逐引脚比较：1,113/1,113 一致，无缺失、无额外连接。证据为 `../evidence/native-connectivity-check.json`。
- `previews/` 和 `Waveshare-P4-reference.zip` 为 EDA 原生导出的预览图。

这是电路参考原理图，不是可直接投产的 PCB 工程：封装未绑定；引脚电气类型统一为 Passive，未进行方向性 ERC；20 个源图未明确提取的数值仍标为 `UNSPECIFIED (PDF)`。部分单引脚网络仍有悬空提示。源 PDF 连接一致性通过不等于制造审核通过。

用户最终确认实物是 **3.5**。此目录保留最初要求的 4.3 资料；已连接设备的驱动和声学测试位于相邻 **3.5** 项目。
