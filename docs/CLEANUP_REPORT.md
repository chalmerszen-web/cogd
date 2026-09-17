# 基线清理完成记录

2026-09-15，按用户“只保留当前基线相关材料”要求实际删除，并完成清理后的全新构建、主机测试及设备应用更新。当前为0.6.0-upgrade，详情见 [BASELINE](BASELINE.md)。

## 已删除

共 **456个文件/目录目标、305,335个文件、33,163,806,690B（33.16GB / 30.89GiB）**。包括：

- 175个构建目录（含清理前的验证构建），现在仅重新生成build-agent与build-host。
- 原参考仓库 `_ref/`、研究硬件工程 `hardware_tests/`；当前所需源码、模型和许可证已先迁出并校验。
- 废弃唤醒/VAD候选、训练数据和训练/诊断环境、旧音乐采集与调优中间件、旧日志和重复备份。
- 过时研究报告、一次性训练/声学搜索脚本和Python缓存。当前依赖闭包保留，原对比源码也有归档。

先校验当前来源、两个固件构建、24/12组主机测试、A/B镜像和回滚备份，再执行删除。所有目标是本工作区内已核对的绝对路径；使用PowerShell原生逐项删除，先排除重解析链接。每个目标删除后验证不存在并记录时间。清理后重新生成的两个构建目录和当前工具运行缓存单独标明，不冒充未删除。

删除前可用磁盘空间52.14GB；清理及重新构建后的观测为91.62GB，净增加39.48GB。这是全盘瞬时值，会受压缩、系统及其他任务写入影响；删除逻辑字节数与实际磁盘增量分开记录。

## 还剩什么

本次目录快照为 **7.28GB（6.78GiB）**，包含工具链和证据；正常业务源码只是其中一小部分。目录大小是文件逻辑长度，不是NTFS分配占用；快照后的报告更新有少量差异。

| 目录 | 文件数 | 逻辑大小 | 保留原因 |
|---|---:|---:|---|
| `.toolchains/` | 52,438 | 5523.25 MB | ESP-IDF 6.1、RISC-V编译器和项目Python，继续构建所需 |
| `artifacts/` | 10,757 | 1378.91 MB | 当前基线/回滚材料、1000次完整证据、本次清理与验证记录 |
| `build-agent/` | 1,556 | 223.24 MB | 清理后新建的唯一完整固件构建 |
| `build-host/` | 112 | 30.31 MB | 清理后新建的主机测试构建 |
| `third_party/` | 91 | 11.52 MB | 当前语音模型、C3库、TEN/RVfplib/cJSON源及许可证 |
| `managed_components/` | 554 | 2.89 MB | 当前ESP-IDF固定依赖 |
| `.local/` | 3,190 | 81.92 MB | 仅loopback-python录音/分析环境和mock证书/数据库 |
| `components/` | 35 | 0.15 MB | 当前关键词和TEN确认组件 |
| `plugins/` | 54 | 0.24 MB | LLM、上下文、工具、控制、音频 |
| `boards/` | 12 | 0.10 MB | ESP-HI板级驱动 |
| `platform/` | 8 | 0.06 MB | ESP-IDF与POSIX适配 |
| `core/` | 10 | 0.02 MB | C11插件/消息/策略内核 |
| `main/` | 4 | 0.00 MB | 固件入口 |
| `host_tests/` | 31 | 0.16 MB | 24/12组主机测试及当前音频渲染辅助 |
| `tools/` | 33 | 0.14 MB | 30个必要工具文件，其中22个Python脚本 |
| `docs/` | 24 | 0.15 MB | 24份当前规格、协议、硬件事实和必要历史报告 |
| `protocol/` | 6 | 0.02 MB | JSON协议schema |
| `examples/` | 1 | 0.00 MB | 一份一分钟乐曲示例 |
| `.git/` | 2,079 | 28.94 MB | 本地Git元数据，未提交或推送 |

根目录还保留README、SPEC、ACTIONLOG、原始需求报告、CMake/依赖锁、当前分区表和配置。ACTIONLOG按时间保留历史，不把过去失败改写为通过；其旧中间产物路径已不保证存在。正式历史报告已加此说明。

`artifacts/`现在仅有三个目录：

- `baseline/`：当前固件/ELF/map/config/source及manifest；原A/B、接受B来源、基准源文件归档和一份lean完整回滚镜像。
- `voice-ab-1000-v1/`：1000个正式结果、500配对、录音无损归档、两次无效采集及完整审计。直接支持所选基线，完整保留。
- `baseline-cleanup-v1/`：本次来源迁移、删除计划/逐文件清单/执行记录、清理前源码、更新前后Flash、构建和设备验证。

原始录音、备份、构建和凭据继续排除Git；未提交或推送。未删除设备上下文或凭据，未更改分区布局。

## 验证和审计入口

- `artifacts/baseline-cleanup-v1/deletion-plan.json`、`deletion-files.jsonl`：批准范围内的完整计划和逐文件尺寸。
- `deletion-supplement-plan.json`、`deletion-supplement.json`：收尾发现的单份旧M4状态快照及其删除记录，已计入总数。
- `deleted.jsonl`、`deletion-result.json`：实际完成记录；`inventory-after.json`：剩余目录快照。
- `promotion.json`、`third_party/manifest.json`：迁入来源及固定依赖；89个哈希校验通过。
- `build-after-cleanup.log`、`host-after-cleanup-02.log`：不存在旧目录时重新构建及24/24测试通过。音频关闭构建和12/12测试也有保留结果。
- `source-audit.json`：C11及边界/私有产物检查；`flash-validation.json`与`nvs-validation.json`：应用正确、原数据不变。
- `device-smoke/report.json`、`device-final.json`：56条有界USB检查与最终设备状态。

本次清理和基线整理已收尾；没有重新展开训练、长时间声学调优或新一轮1000次测试。
