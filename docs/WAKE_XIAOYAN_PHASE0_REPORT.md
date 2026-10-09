# “你好，小言”第一步：基线备份与回滚验证

2026-09-21。用户确认先完成Phase 0。本阶段已完成，当前设备仍使用0.6.3-context/嗨乐鑫；没有训练新模型、烧录应用或删除旧唤醒代码。

## 五项交付

| 要求 | 实际结果 | 证据 |
|---|---|---|
| 保存当前源码及未提交修改 | 360个Git跟踪及未忽略源码/历史文件，另554个固定组件文件；ZIP逐文件SHA、CRC读回通过；保存Git HEAD、状态及二进制diff | `backups/kws-phase0-20260921-044018/source-snapshot.json`、`working-source.zip`、`managed-components.zip` |
| 完整设备备份 | 4,194,304 B；设备端verify-flash校验一致，分区MD5与应用内部SHA有效，日志无损坏尾 | 同目录`flash-0.6.3.bin`、`flash-read.log`、`flash-verify.log`、`flash-validation.json` |
| 记录实机基线 | 版本、唤醒、音频、堆、上下文、屏幕及硬件接口；保留已有错误计数，不将其改写为通过 | 同目录`device-before.json` |
| 验证旧版重编译并准备回滚 | 原归档独立解压构建成功；配置、分区表完全相同，应用除构建元数据及其校验字段外逐字节相同；应用级回滚包已校验 | 同目录`rebuild.log`、`size.log`、`rebuild-validation.json`；`firmware/rollback/0.6.3-context/` |
| 更新项目记录 | SPEC列出新方案及资源门槛，ACTIONLOG记录实际操作，history更新0.6.3归档重编译证据 | SPEC、ACTIONLOG、history/manifest.json及validation.json |

备份目录和firmware目录均排除Git，且不在现有清理脚本固定删除列表中。整片备份含NVS凭据、私人上下文和录音数据，只保存在本机。工具链继续保留在.toolchains；完整源码快照不重复打包数GiB已安装SDK，已保存SDK提交、28个子模块状态、编译器版本及组件归档。独立重编译已实际验证这套保留依赖。

## Flash与回滚依据

读取确认ESP32-C3 rev0.4、160 MHz、4 MiB XMC Flash，Secure Boot和Flash Encryption均关闭；本步仅读取安全状态，不写eFuse。

| 分区 | 偏移 | 大小 |
|---|---:|---:|
| NVS | 0x9000 | 24 KiB |
| PHY | 0xf000 | 4 KiB |
| factory（唯一应用分区） | 0x10000 | 1.5 MiB |
| ctx | 0x190000 | 2 MiB |
| clip | 0x390000 | 448 KiB |

当前应用1,487,664 B，应用槽剩85,200 B。设备中应用与`firmware/latest/esp_hi_agent.bin`逐字节相同。回滚包保存此原始、设备核实过的应用，而非此次重编译的新时间戳镜像。

| 文件 | SHA-256 |
|---|---|
| 整片备份 | `4dd86bd01d260568b0b0fd26f7b0ade659e7d9ef81825d5e49e2e8a168fbbb7f` |
| 原应用/回滚应用 | `3999e26330fb7ea49b7bb32dbbaa3ab3bae0662263176d11b9480e734f4ff263` |
| 原0.6.3源码ZIP | `99242ff2bbcb2130efa20e1f186a7f2323926cd922ac5951b23ec807eaa93c69` |
| 重编译应用 | `7f4871802f1c9d6cf929a95036033f9a600c13ecdca4981f9732ed1ac311bb7e` |

回滚入口为`firmware/rollback/0.6.3-context/rollback.py`和同目录CMD；`--check`只检查本地包。入口使用包内冻结的安装脚本，真实执行时先核对设备分区，再仅写0x10000处应用并校验。本步只实际运行了本地`--check`；三项模拟检查证明正常流程仅写一次应用、错误分区和损坏应用在写入前拒绝。模拟中的“installed”输出来自替换过的subprocess，不代表实际烧录。没有为了证明回滚而重复烧录设备。

## 独立重编译

实际执行`tools/build_history.ps1 -Version 0.6.3-context`，归档解压至新的`build-history/0.6.3-context`，使用保存的配置和全部五项语音选项；关闭此前已知有问题的ccache。构建及size、size-components均退出0。

ESP-IDF为v6.1、提交`fff9895c82d744c7237be8847347bdd1b07c6643`，SDK跟踪文件无修改；编译器为riscv32-esp-elf-gcc 15.2.0（esp-15.2.0_20251204）。91个固定第三方文件SHA校验通过。配置文件与归档版本逐字节一致。静态DRAM为211,756 B，应用长度与原包相同。

新旧应用只有72字节不同，全部落在构建日期/时间、ELF哈希、应用校验字节和尾部SHA字段中；其余字节完全一致。该比较实际检查了每一个差异偏移，未仅凭文件大小推断。新的BIN、ELF、MAP、构建参数、编译命令、配置和工具链信息都已另存于备份目录。

## 设备前后状态与限制

| 项目 | 备份前 | 读取备份引起的软件重启、恢复监听后 |
|---|---:|---:|
| 固件/模型 | 0.6.3-context / wn9s_hilexin | 相同 |
| 唤醒状态 | listening | listening |
| 空闲堆 | 53,764 B | 54,196 B |
| 启动以来最低堆 | 41,296 B | 47,044 B |
| 最大连续块 | 36,864 B | 38,912 B |
| 后端分配统计 | 26,812 B | 26,964 B |
| DMA丢帧 | 0 | 0 |
| 上下文事件/已用 | 168 / 207,452 B | 相同 |
| 历史预算 | 131,072 B | 相同 |

备份前的累计推理最大值26,614 μs，不是p99；原音频状态含上次无指令采集timeout，并非本轮新失败。软件重启后计数重新起算且分配时机不同，不能把前后堆差当成优化。屏幕原先关闭，音量80；恢复原先开启的麦克风和唤醒监听后，Wi-Fi、Key配置和时间有效，串口已释放。上下文事件数、占用、代数、游标、归档/同步水位及预算一致。读取Flash期间没有用户态程序运行，设备端对完整备份校验后才退出下载模式。

本阶段不评价新词识别效果，不替代真人测试，也不改变原M0 Gateway/真实断电或屏幕实物未验收状态。

初次源码快照在Windows上对只读文件描述符调用fsync失败；改为读写描述符后重新创建并完整校验成功。首个不完整目录`backups/kws-phase0-20260921-043937`已标注INCOMPLETE，不能作为回滚依据；有效目录为本报告中的044018。
