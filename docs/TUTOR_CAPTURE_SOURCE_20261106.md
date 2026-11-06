# P4 重新拍题的照片来源修复（0.9.4，完整语音验收由用户延期）

2026-11-06。用户确认重新拍题后设备仍回答旧题，本轮修复将“重新采集”和“同图选题”分开。当前候选为 `0.9.4-p4-tutor`，**拍题来源修复在本轮范围验证通过，按用户要求交付开发候选；不能视为完整语音验收通过**（candidate_not_full_voice_acceptance）：构建、安装读回及相机来源验证通过，用户确认新拍题正常（USER_OBSERVED）；“重新读题”首次误路由仍为 FAIL/已知问题。完整语音验收为 DEFERRED_BY_USER，最终读回及正常监听核验 PASS，外部屏幕视觉为 NOT_RUN。机器可读摘要见 [tutor-source-20261106.json](evidence/tutor-source-20261106.json)。

旧实现存在两条可复用旧题的路径：等待确认/选题时，start 直接返回当前状态；同阶段 refine 继承旧照片、fixture 和候选题号。对话提示又没有区分重新拍摄与同图纠正，当前教学状态和历史可以继续影响回答。用户故障作为真实问题保留；这不是将旧回放结果误认成新拍摄的问题。

本轮约束：

- start 每次请求新采集，包括已有待确认题目时的重拍；语音侧“重拍、重新拍照、再拍一张、换题”均明确使用 start。
- refine 只在同一张保留照片上选题或纠正题干，须带当前 question_id/revision；过期或无有效待确认照片时返回 not_executed，不偷偷改为新采集。
- 语音工具 schema 不公开 test_image_url，执行入口也按字段存在性拒绝该参数。仅直接授权的 USB start 保留固定图片测试入口，refine 不接受测试 URL。
- 回执提供 photo_source、capture_id、photo_sha256、captured_at_ms。新拍摄未返回当前摘要前，不把旧历史、题干或解法当成新结果；保留真实对话历史，不加入 ASR 自动确认。
- 新增 P4 专用只读照片分块导出命令，核对实际送入识别的保留 JPEG；导出不请求再次拍照。

## 镜像与软件验证

| 项目 | 结果 |
| --- | --- |
| P4 构建 | PASS，应用 5,781,712 B；SHA256 `9a245df9a4731a2cb32e6b7db19a92a7141918a66ce3bafa769c069708cac3c8` |
| 安装/保护区/读回 | PASS，candidate01 收据 installed/protected/readback 均 true；只写应用，assets_written=false |
| 字体资产 | 沿用 1,349,792 B；SHA256 `275652fb54ddc6ed664fe5286c90ede050d4dcff90c23092fcaf8f3b30f06bcc` |
| 分区表 | 沿用 SHA256 `7626d3676f91dc219f4ac56116edc5e747ee5ac18c26e2097fa6bfd1166cffc6` |
| 共享主机/定向回归 | 105/105 PASS；响应归属、教学契约和文档状态 3/3 PASS |
| 最终实测后读回及设备状态 | PASS；应用、资产、分区实读回均匹配，正常监听独立复核通过 |

最终只读读回于 2026-11-06 09:03:32 UTC 完成，`flash_written=false`，应用、资产和分区分别与上表哈希完全一致。第一次 `final-normal-state` 检查因启动 Wi-Fi 日志插入首个 agent status JSON，导致解析超时，收据 valid=false；保留该失败，不将其当成固件复现。未修改源码、未再次复位，启动稳定后独立运行 `final-normal-state-02`，于 09:04:36 UTC 通过：0.9.4、Wi-Fi/时间有效、voice_enabled=true、mode=natural、官方“你好小智”/wn10_nihaoxiaozhi 为 listening、error=ok、audio_ready=true、音量80、串口已释放。

## physical01：旧 fixture → 新镜头 → 同图选题

此轮由 USB 发出工具请求，不能替代自然语音入口验收。旧 A 为明确的 fixture：qid1/capture1、needs_selection，照片 SHA256 `806b030d111f01fa6739ccf6a274b134bb5af1d5054292a0382aead7ee0fcc6c`。

不带 URL 的 start 得到新相机照片 B：qid2/capture2、photo_source=camera、captured_at_ms=155801，SHA256 `0d1bf684a9e80584c378c6ad3d97a5f81a60ce475fd20776646a7fe63f7e505b`。只读导出收到 114,768 B，JPEG 为 800×640，导出前后身份一致且哈希匹配。主控实际查看原图，内容为参考答案解析一(1) `lim_{x→0} ln(cos x)/x² = -1/2`，与该次识别摘要相符；这只是该张照片的对应关系，不是数学正确性或广泛识别率认证。

随后以当前 qid2/revision0 refine，进入 qid3/awaiting_confirmation；capture2、照片哈希和拍摄时间完全不变，证明同图选题未重拍。再用旧 qid1 refine 得到 `not_executed / refine_requires_current_pending_photo`，qid3 内容保持。以上来源与过期请求检查为 PASS。

导出收据自身标明 `visual_question_match=NOT_REVIEWED`、`firmware_binding_verified_here=false`，它只负责字节/JPEG 完整性；上述人工看图和镜像绑定分别来自 physical01/manual-review.json、physical01/firmware-binding.json 与安装读回收据，不能把导出工具的结果改写为全项验收。

## 自然语音片段：首轮误路由与明确重拍分开记录

`physical01/events.jsonl` 中，623115/629239 ms 的用户转写为“重新读题。”；该阶段实际调用 status、read、explain 和 confirm，没有新采集。这一入口歧义/误路由保留为 **FAIL/已知问题**，不能以随后成功改写为通过。

用户随后明确说“不对不对，你打开摄像头重新读题，你摄像头黑了。”；642727 ms 实际调用 start，得到新相机 C：qid4/capture5、camera、captured_at_ms=659612、SHA256 `8dc16cd908f5c2681be78538f5037d299f27b54a326e5bb477f2f83e3b6005fd`。用户选第三道题后，714624 ms 首次 refine 错带旧 qid3，被拒绝；随后查询 status，719172 ms 才以当前 qid4 refine 成功，进入 qid5，照片来源和身份不变，语音复述当前图上的 `∫ (ln ln x)/x dx`。只读导出 C 的 128,082 B、800×640 JPEG 通过字节/哈希检查。主控实际看图确认是(3)的该不定积分及换元 t=ln x、结果 ln x ln ln x − ln x + C，下方为(4)渐近线解析，与本次识别和口述对应，且区别于 A/B；复核记录见 physical01/manual-review.json。这不证明屏幕显示或广泛数学正确性。

用户明确反馈“现在新拍题是正常了”，记为 **USER_OBSERVED**。同时用户表示当前无法安排语音对话环境，要求先交付拍题修复、稍后完整测试；因此完整语音验收标为 **DEFERRED_BY_USER**，不继续追加语音测试，不称完整声学流程通过。

| 尚未关闭的项目 | 状态与边界 |
| --- | --- |
| 完整语音交互 | DEFERRED_BY_USER；首轮“重新读题”误路由 FAIL 保留，明确打开摄像头后新采集成功及用户确认单独记录 |
| LCD 实际画面 | NOT_RUN；外部 Insta 画面只见桌面，不能用串口状态代替屏幕视觉 |
| 实手指、真人自由交互、广泛题型识别 | NOT_RUN |

## C3 与发布边界

C3 使用现有 ESP-IDF v6.1 和 build-c3-xiaozhi 缓存，构建 PASS；92 个仓库编译单元无 P4 单元，runtime 的 AGENT_BOARD_P4=0。应用仍为 1,569,184 B，仅余 3,680 B；分区 SHA256 `03135fff90b8a4983a4a7a758018626dc296756ac79578e5df9c7b8eb64b3179` 与上一轮相同。应用本次为 `ad14f3cf28700cc2e241f195c78d64326820d80a047e58043b2aaea2f7d023f6`，不同于上一轮 `addfc5b39223bea84b0a2f18c10bb716a99f6a07f012ef3cf225caaa72b97193`。未找到可核对的上一轮应用快照，**不能证明差异仅为日期、ELF SHA 或尾校验，也不能称字节一致**。C3 未烧录、未实测，S3 仍未实现。

[0.9.3 七项修复报告](TUTOR_FIXES_20261106.md)保留其原镜像和固定回放范围，不算本候选重跑。视频并发、误打断、远场/噪声、500 ms 回复和耐久等既有未通过项不因本轮来源修复自动关闭。

原图、录音、录像、详细串口和完整 Flash 只保存在本地 `.local/tutor-source-20261106/`。公开 JSON 仅保留状态、镜像/照片身份及原始证据哈希；原始文件不随 Git 发布。
