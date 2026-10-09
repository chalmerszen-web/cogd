# 本地优先预取：UX186 主机结果

实现默认关闭的AGENT_LOCAL_FIRST_PREFETCH策略。旧候选API保持；新增本地优先入口，简单/未完成灯控不提名多余语音。完整final灯控语法抽为共享纯识别函数，预览不返回RGB或执行权限；未知复合后缀转入原有THINK话题机制，最终请求/取消/改口/UTF8和GPIO权限仍由原路径校验。无新的常驻缓冲、堆分配或任务，不改VAD、声音采样及上下文。

73项完整主机检查通过，包括8项固定SDK检查和2项新增ASan/UBSan测试。普通话/粤语连续partial、颜色半字、完整修正、不完整修正、额外屏幕/音乐/条件动作、取消和输入错误均覆盖。新adapter测试证明简单灯控没有response.create和PCM页，完整final才能执行一次本地作用；复杂灯控仍有受源话题约束的过程语句及明确ack_join，问候/常识快答/历史话题保留旧行为。

初次共享fixture的main重命名暴露隐式返回规则，补显式return0；新复杂应答fixture遗漏ack_join而失败，修正fixture生命周期，未更改程序取消/缓冲释放规则。两份失败日志保留。静态模块测试只能准入一组设备试验，不能证明实机省多少内存、声音更好或1秒有效回应。

源代码与证据：plugins/speech/{intent,candidate}.{c,h}，platform/espidf/voice_candidate.c，host_tests/test_{candidate,voice}_local_first.c；artifacts/voice-fast/local-first-ux186/{plan.json,closure.json,host-full-tests.log,host-new-tests.log}。本阶段没有USB、云、录音或上下文变更。
