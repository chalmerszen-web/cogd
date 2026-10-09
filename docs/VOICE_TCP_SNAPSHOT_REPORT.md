# 并行停收的TCP队列证据：UX164

2026-09-29。202诊断版的三次请求均重现停收，但首次直接观察到两条连接
保留接收对象的状态。尚未修好，不将本轮诊断当作语音速度验收。

## 观测

每个作业在最后WS数据后约400/1500/4000ms各取一次。TCPIP同步回调内只读
实际SDK PCB和netconn队列，不修改TCP或读取任何响应；HTTP描述符仅在
on_sent等待候选退出时发布。快照只保存数值，候选退出后打印。两端口在
active PCB中必须唯一，netconn还检查PCB与socket关联。三次快照耗时按毫秒
分辨率记录；不是零成本。正常和无音频构建不含这段代码。

|作业|WS乱序pbuf数|HTTP接收邮箱消息数|两者对象数下界|最后完整协议事件|
|---|---:|---:|---:|---|
|1|4|4|8|response.audio.delta|
|2|3|5|8|response.audio.delta|
|3|4|4|8|response.audio.delta|

逐作业的三份快照中，WS下个期望序号与HTTP接收状态不变；WS接收窗口仍为
5760，接收邮箱空，尚未出现response.done。其他详细计数在tcp-evidence.json。
这些不是TLS解析器里未取走的已排序音频。WS有乱序数据，说明也不能把此前
空轮询直接解读为网络没有新包。HTTP邮箱消息可能包含pbuf链，上表不是精确
Wi-Fi驱动内存分配计数；没有抓包证明全部丢包位置。

FIONREAD均返回失败，以-1保存，不能当0字节。固定SDK的SO_RCVBUF和
FIONREAD_LINUXMODE关闭，sockets.c没有编译该命令分支；实际队列证据来自
TCPIP回调。未为诊断打开这些生产配置。

动态Wi-Fi RX上限8；未开启L2_TO_L3_COPY时，wlanif_input让pbuf引用原L2
缓冲，直到上层释放。观察支持“HTTP邮箱与WS乱序队列共同留住L2缓冲”的
解释，但尚不能只凭这些计数认定根因。下一步只验证SDK已有L2拷贝选项：
以堆pbuf承接数据，尽快释放L2缓冲，检查是否解除停顿及其RAM代价。

## 验证与限制

70/70 ASan/UBSan主机检查通过，含实际loopback FIONREAD不消费数据、TCPIP
回调边界、重复PCB拒绝、无效句柄/回调错误、三次上限、独立连接和关闭清理。
主机mock结构不冒充SDK ABI测试。首次主机失败是新增fixture打开了错误的
握手优先级路径，修正后全量通过；首次诊断构建因SDK裸asm不兼容严格C11，
按既有适配方式仅对诊断文件映射__asm__后通过。失败日志保留。

普通/诊断/无音频应用分别1501968/1506576/1014096B；
诊断源码407份C/H/CMake已与冻结包逐字节核对。普通包在诊断专用CMake映射
修正前冻结，正常构建路径不受该差异影响，未烧录普通包。两份sdkconfig与
默认配置不变，70项用例44.54秒。未更改网络预算、缓存、期限或输入策略。

三flow全部真实body重叠，随后cancelled，无完整业务回答，SDK最低堆
42004B，网络栈最小
1688B。有播放underrun/timeout，
未观察到WDT/panic/复位或灯效。未满足前置门槛，跳过普通版声学组。该入口
绕过ASR，不能验证完整输入、连续真人对话、粤语泛化或一秒有效回复。

## 恢复

guard备份整片4MiB、应用读回及非应用区比较通过；已恢复72的fast/capture/
reuse开/prefetch关/监听状态，新增日志保留，分区、200KiB历史与录音槽不变。
202诊断代码保留为下轮归因工具，未推广；默认安装包不变。

证据：artifacts/voice-fast/tcp-snapshot-ux164/中的tcp-evidence.json、trial-summary.json、
原始serial.log与speaker.wav、主机/构建/guard日志、closure.json。
诊断应用SHA256：be9623c796a42afe37ceab6421e7915c96d3e670fdd8a822f57d363bc3dc3e32。
