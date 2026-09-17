> 历史阶段记录：安装状态和产物路径描述当时的结果。2026-09-15 已采纳调优语音链路并清理旧中间材料；当前入口见 [BASELINE.md](BASELINE.md) 与 [CLEANUP_REPORT.md](CLEANUP_REPORT.md)。1000次正式证据完整保留，其数据不等于合并版的新1000次测试。

# Agent 精简版交付记录

2026-09-15，版本 **0.5.1-lean**，ESP-HI / ESP32-C3 / 4 MiB / COM5。
用户将语音阶段改为实验功能收尾，当前重点为 Agent 工具流程、GPIO 与内存。
本文件是当前入口；M4 旧文档中的训练计划和严格验收要求保留为历史。

## 代码和能力

- SSE 帧借用原 24 KiB 请求缓冲，保留 6 KiB 单事件上限。初始化只绑定内存，
  不覆盖尚未发送的 Gateway 正文；发送完成后才可接收响应。每轮/重试重新初始化。
- 嵌套乐谱直接解析 JSON 节点，移除 2 KiB 临时字符串及重复 JSON 解析树。
- 默认构建关闭附加关键词、神经语音及 TEN VAD 校验器和研究诊断；源码/数据
  原件保留。使用官方 WakeNet9s `wn9s_hilexin` 与 ESP-SR 提供的 WebRTC VAD，
  保留 32 kHz ADC → 16 kHz 降采样、24 kHz PDM、音乐及录音回放。
- `device.gpio.get/set` 通过静态表映射 `device_gpio_get/set`。开放原 LCD 的
  GPIO4/5/10、原身体接口20/21，支持输入、持续高低电平、两路10–5000Hz PWM。
  按钮0/1/9只读；18/19 USB、12–17 Flash、未知11、灯8及音频2/3/6/7保留驱动管理。
- 直接 GPIO 设置持续到再次设置/重启；32步、8次重复、60秒控制计划可组合
  GPIO、PWM、输入条件、灯和音频，结束/取消恢复原状态。计划活动时直接 GPIO
  返回busy。临时PWM先释放，再恢复原PWM，避免两通道占满时清理停住。
- 保留22个工具目录、每次回复最多4个调用及最多4轮续调。上下文仍为2 MiB
  双区日志、128轮候选索引、128 KiB历史请求预算；这些数字没有因内存优化而缩小。

官方 WakeNet9s 支持 C3，参见[模型文档](https://docs.espressif.com/projects/esp-sr/en/latest/esp32/wake_word_engine/README.html)。
它是随固件链接的模型，不能称为芯片 ROM 自带唤醒。固定 ESP-SR commit 为
`efa8d907c6d457cd0f99dae6c6b493412d3078d4`。
GPIO限制和输出读回的输入使能依据[官方 GPIO 文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html)。
开放的引脚有原板用途，不代表经过外部电路仪器验证的空闲焊盘。

## 测量

| 项目 | 修改前 | 精简后 | 说明 |
| --- | ---: | ---: | --- |
| C3 `agent_sse_t` | 6176 B | 36 B | 借用既有缓冲，单事件预算不变 |
| C3 `agent_engine_t` | 60696 B | 54552 B | 减6144 B，含结构对齐 |
| 联网、监听关闭时空闲堆 | 96112 B | 110896 B | 两份实机快照，增加14784 B（14.44 KiB，15.38%） |
| 同条件最大连续块 | 86016 B | 98304 B | 增加12288 B |
| 第一轮本地回归后最低堆 | — | 68076 B | 包含唤醒模型启动及随后超时的TLS请求，不是长时间稳定性结果 |
| 上下文物理分区 | 2097152 B | 2097152 B | 每区1048576 B，另一区用于掉电切换 |

C3尺寸由实际交叉编译器和nm测得，不混用此前x86_64的6136 B差值。
没有缩小任务栈；旧版带完整历史的网络请求曾只剩2488 B worker栈，当前有限回归
第一启动周期最低2500 B，不凭开机时的高水位裁栈。静态size与运行时可分配堆不能相加为Flash空闲。

第一版应用1239552 B，相比旧1283808 B少44256 B。随后为定位旧/新共同超时，
加入只含阶段、耗时、字节数及HTTP状态的错误日志；不打印正文、凭据或URL。
最终应用 **1239984 B（1.183 MiB）**，比旧版少43824 B（42.80 KiB，3.41%），
1.5 MiB应用槽剩332880 B（325.08 KiB）。SHA-256：
`a16b5292a3fb2400490077139d7b77cb452e3c16be30749d0524b95ce365d292`。
最终静态size见 `artifacts/agent-lean-v1/size-final.log`；代码/只读段大小不能当作
整个4 MiB的空间分配。最终真实网络调用后free109684 B、minimum77220 B、
最大块73728 B、worker剩2436 B。此值与上一启动周期的68076 B分开记录。

## 验证与已知限制

- 主机 C11 + ASan/UBSan：音频开启21/21，关闭12/12。覆盖原有协议/存储/音频，
  并补充SSE正文共用、容量/分片、JSON节点一致性、GPIO权限/参数/租约/驱动失败、
  清理忙时不重复效果、两路PWM转移后正常结束及取消恢复。
- 实机第一轮：GPIO10高低电平实际读回、1kHz/50% PWM驱动寄存器读回、回到输入，
  保留引脚拒绝、动作链busy/取消和灯恢复、麦克风并行GPIO、音量40旧短谱均通过。
  PWM频率/占空比没有外部示波器测量，不称为外部负载功能验收。
- 官方唤醒模型能进入listening，退出后关闭；无额外keyword/verify对象。
  此次没有声学识别率或主观音质验收，也没有继续训练。
- 修改前的一次非流式资源请求55秒未返回，被主动取消。第一版新固件一次流式
  请求明确返回timeout，约130秒、25次USB状态响应、0次工具；后续非流式未执行。
  请求146294 B，包含125轮/129830 B历史，预算保持131072 B。超时没有被记为通过。
- 加入错误阶段日志后，确认请求完整发出并取得HTTP200；首次接收阶段62477ms
  超时，仅收到56字节，尚无用户可见输出/工具效果，因此按原规则重试一次。
  该轮随后完成流式 `device_status_get` → 返回工具结果 → 中文回复 `@done`。
  证据 `http-phase-probe.json`。不据此断言具体服务端排队原因。
- 单独补齐的流式GPIO用例成功查询能力并设GPIO10高，后续请求超时，没有自动
  重放已有工具效果；测试清理恢复input。`device-cloud-v2/report.json`保持失败，
  没有将部分执行写成整轮成功。
- 独立非流式用例完整通过：DeepSeek调用 `device_control_run` 提交GPIO10高→
  等待100ms计划并回复 `@done`；设备状态为done、active=false、error=ok，
  GPIO10读回input。证据 `nonstream-plan.json`。两种协议已有真实成功闭环，
  但云端长上下文延迟仍会超时；本轮不宣称所有云调用可靠或长时间无泄漏。
- 收尾时上下文使用996144 B，当前1 MiB区剩52432 B（51.20 KiB），另一1 MiB区
  用于安全切换。126轮/130952 B历史参与请求，128 KiB预算保持；原日志未删除。
- M0跨网段Gateway和真实USB断电、M3独立听感，以及原M4稳定性缺项仍保留。

语音实验没有同条件的“官方原版 vs 当前优化版”受控配对，不能给出可靠的总体提升
百分比。现有神经候选既有短词改善，也有误收/漏检回归；未证明优于官方，不部署它们。
按用户修订，以官方路线加现有录音流程作为实验入口收尾，不再为旧门槛扩大训练。

## 使用、构建和回滚

USB普通文本仍是DeepSeek对话；唤醒只启动本地录音流程，不转写或上传人声。

```text
agent control capabilities
agent gpio get {"pin":10}
agent gpio set {"pin":10,"mode":"output","value":1}
agent gpio set {"pin":10,"mode":"pwm","hz":1000,"duty":500}
agent gpio set {"pin":10,"mode":"input"}
agent cancel
```

也可直接输入：“查询GPIO能力，让GPIO10拉高200毫秒，灯光同步闪一下，然后恢复。”
新构建入口为 `tools/build_agent.ps1`，使用独立配置并关闭研究开关；已有本工程布局时
用 `tools/idf.ps1 -B build-agent-lean -p COM5 app-flash` 只更新应用。

原始源码和证据位于忽略目录 `artifacts/agent-lean-v1/`：
`source-before/`、`c3-size-report.json`、`gpio-host-tests-pwm-restore.json`、
`device-before.json`、`device-after-boot.json`、`device-regression-v1/report.json`。
`flash-before.bin`完整4 MiB SHA为
`200339bdc02b2c85457d238f3987e9278564bb1ef7c5de8ae4c6f3bf3d3aa8e7`。
首次升级回读证实boot/table、PHY、上下文、录音逐字节不变；68个有效NVS记录保持。
最终回读 `flash-final.bin` SHA为
`fb81aafd1ceacf83f6d0bf239214c782ee5b46a673fd8dabf5b8a40edc311a1e`。
`final-image.json`核验最终应用、boot/table、PHY及录音；旧活动区全部987280 B前缀
逐字节保持，新诊断对话仅追加，当前1553个日志记录CRC全部有效。
`nvs-final.json`核验68个受保护记录不变，序号预留6784→7040，未重用旧事件编号。
收尾时监听、麦克风和播放均关闭，GPIO10为input，USB已释放，可继续使用串口。
最终软件复位后空闲堆110916 B，活动区使用996144 B且tail_recovered=false，
见`device-final.json`；复位清零的水位/请求计数不替代上文实际带负载测量。
回滚应用分区为`rollback-app-partition.bin`，SHA
`1307dfca813f3dd4cfd583fb5ca5f9d09a1a16602135f1c277c940ea90a01b47`。
需要回滚时只写该应用分区到0x10000，保存后来新增的上下文；不要覆盖旧完整Flash。
备份含私有配置，继续留在本机忽略目录，未加入Git。
