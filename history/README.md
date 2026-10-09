# 历史源码与回滚

**从 0.8 语音版回滚：**旧版无法读取预算为200KiB的检查点。使用当前工程的`tools/voice/rollback.py --apply`，先保存128KiB兼容检查点，再完整备份并仅写0.6.3应用；不要直接使用下面历史版本的烧录命令。重新安装语音版后可设置`agent context budget 200`。详见[语音回滚说明](../docs/VOICE_CLOUD_USAGE.md)。

完整快照：`0.6.0-upgrade`、`0.6.1-lcd`、`0.6.2-repair`、`0.6.3-context`。每个目录只有原始 `source.zip` 和当时的 `build.config`，包含业务源码、模型、固定库、许可证和依赖锁。SHA-256、编译选项与 ESP-IDF 提交记录在 `manifest.json`。

在项目目录的 PowerShell 中重编译，例如：

```powershell
.\tools\build_history.ps1 -Version 0.6.1-lcd
```

脚本校验归档后解压到新的 `build-history/<版本>/`，使用保留的 `.toolchains/` 和固定组件编译，不覆盖当前源码，也不烧录设备。目录已存在时拒绝覆盖；可继续构建该目录：

```powershell
.\tools\idf.ps1 --no-ccache -C build-history/0.6.1-lcd -B build-history/0.6.1-lcd/build build
```

确认要回滚后关闭串口对话，在当前分区布局的设备上仅更新应用：

```powershell
.\tools\idf.ps1 -C build-history/0.6.1-lcd -B build-history/0.6.1-lcd/build -p COM5 app-flash
```

这些版本采用相同分区布局。旧版屏幕等已知问题仍然存在，参见版本报告。重新编译的时间、路径、Git 状态可能影响二进制哈希，不承诺与原镜像逐字节相同。构建验证结果见 `validation.json`。

`legacy/` 保留更早的 0.5.0/0.5.1 实验源码及构建/实验脚本。它们在本次清理之前就已是局部归档，包含失效的绝对路径或缺少当时完整工程，**不能直接作为可编译回滚包**；没有把它们冒充完整版本。可编译回滚采用上面的四个完整快照。实验录音、运行结果和旧二进制列入清理范围；自动删除被拦截，待运行根目录 `清理多余文件.cmd` 后删除。

最新版安装文件在 `firmware/latest/`，根目录双击 `安装最新版.cmd` 即可使用。根目录源码和 `.git/` 继续保留。

0.6.3-context 归档已于2026-09-21独立解压重编译，配置和分区一致，应用除构建元数据及校验字段外逐字节相同。设备核实过的原应用另存于firmware/rollback/0.6.3-context，包含只检查本地文件的`rollback.py --check`入口与实际应用级回滚入口；本次没有实际烧录。见[Phase 0报告](../docs/WAKE_XIAOYAN_PHASE0_REPORT.md)。0.6.0/0.6.1/0.6.2 的归档重编译结果继续保留。个人对话归档另存于context-archives；当前源码及私人Flash备份另存于backups，均不属于Git发布材料或旧清理入口的目标。
