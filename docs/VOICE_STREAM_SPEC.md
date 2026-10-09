# 千问实时语音接入与响应拆解

2026-09-22。用户授权使用 qianwen-model-suite 本机 Key，在当前 ESP-HI
固件上实现流式 ASR/TTS，实机自测并给出各阶段耗时和原因。

## 需求与边界

- VS-01：技能配置的官方 DashScope Key 只在本机读取，经 USB 写入独立 NVS。
  不输出 Key，不替换 DeepSeek/Wi-Fi，不上传私人训练录音。模型会话不自动重放。
- VS-02：Fun-ASR-Realtime 使用 WSS 二进制 PCM 流，在设备录音期间发送并接收
  识别结果；仅最终结果可进入 Agent。保留已有唤醒、VAD、录音和取消。
- VS-03：Qwen-Audio-3.0-TTS-Flash 使用 duplex 协议和匹配的系统音色
  longanhuan_v3.6，PCM16/24 kHz。收到音频即交给有界播放器，不等完整音频文件。
  保留 begin/feed/finish 接口和背压；DeepSeek 工具轮次完成后才朗读最终回答。
- VS-04：沿用 C11、ESP-IDF6.1、现有分区和 200 KiB 历史选择预算。单网络
  worker，服务间关闭连接，避免两条 TLS 同时占内存。不使用电脑中转正常对话。
- VS-05：录音与识别共享已完成写入的 Flash 数据，识别使用独立解码游标；
  VAD 和网络工作区分离、显式移交所有权，失败/取消必须先结束读者再复用。
- VS-06：记录采集开始/结束、连接、ASR 首次/最终结果、DeepSeek/工具、TTS
  连接/首 PCM、扬声器实际提交及播完。统计中位、范围和逐轮组成，区分重叠
  时间与说完后的等待。云排队和历史测试时间差不算纯代码收益。
- VS-07：ASR 连接准备在开始提示音之前，ADC 有意暂停且录音采样时钟尚未开始。
  最多等待 12 秒，失败/取消终止本轮并清理；报告额外的唤醒到录音延迟。
  保持原始采集和本地 VAD 的人声确认，不把 ASR 部分结果当作操作指令。
- VS-08：实时模式下，已完成本地人声确认且收到非空 ASR 最终句子时，可提前
  结束录音；ASR 按 1000 ms 静音断句，关闭语义断句。仅消除无线活动期间本地
  结束判定拖长的问题，不绕过本地人声确认、CRC 提交、取消和长度限制。
  未确认人声的 ASR 文本不能触发对话；旧仅录音模式继续使用原有本地结束判定。

## 有限实施与验收

1. 固定 0.8.1 安装包/源码回滚；核实官方协议，本机各做一次有界 ASR/TTS 调用。
2. 实现便携协议状态机、IDF WSS 适配和录音阶段移交；主机覆盖分片、重复结果、
   错误任务 ID、畸形事件、奇数 PCM、超限、取消和无自动重发。
3. 构建主机/ESP32-C3/关闭音频版本；完整备份 Flash，仅更新应用并读回验证。
4. 用匹配的六条合成指令，经电脑扬声器→板载麦克风→云端→板载扬声器闭环；
   外部麦克风只在测试窗口采集，独立转写回复段。补取消、失败恢复和并发测试。
5. 记录资源最低值、复听证据、耗时拆解和限制；更新最新版安装包、源码快照、
   使用说明及 ACTIONLOG。未达到的项目如实记录，不继承为已通过。

协议依据：
[Fun-ASR 客户端](https://help.aliyun.com/zh/model-studio/fun-asr-client-events)、
[Fun-ASR 服务端](https://help.aliyun.com/zh/model-studio/fun-asr-server-events)、
[TTS WebSocket](https://help.aliyun.com/zh/model-studio/cosyvoice-websocket-api)、
[TTS 客户端](https://help.aliyun.com/zh/model-studio/cosyvoice-client-events)、
[TTS 服务端](https://help.aliyun.com/zh/model-studio/cosyvoice-server-events)。
