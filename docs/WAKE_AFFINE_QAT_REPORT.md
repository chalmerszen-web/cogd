# 可训练声学偏置整数原型（UX235，2026-10-01）

**数值／梯度原型通过；尚未训练、评价识别率或烧录。**

新增 training/kws/affine_pair_qat.py，在原 C11 模型 ABI 下训练卷积权重与 INT32 偏置，固定 shift、归一化、前端、拓扑。偏置用输出单位参数化，乘回固定 2^shift 再整数舍入，与 INT8 权重分开投影；不会错误截成 -128～127。每层含原卷积与跨分支乘积的最坏部分和绝对值不超过 8388607，满足 FP32 整数精确性和 INT32 安全。

12768 个声学权重＋6960 个交互权重＋530 个偏置＝20258 个参数，50 个张量均有有限非零梯度；固定 buffers 保持。初始导出与原模型元数据相同。复用 UX232 预声明的25序列，初始、整数扰动、半整数舍入和两符号偏置极限共4组，15738880 个 FP32/F64/C 完整层值精确。

神经历史仍6252B，推理接口／环形算法不变。估计新增非零深度卷积偏置960B，加少量对齐；Flash、堆、时间仍需 C3 实测，不能用数值通过替代资源证明。没有 optimizer 步骤、识别评分、开发／TEST、新音频、云调用或 Flash 写入。原72/off/off、2MiB上下文、灯、volume80和原录音保留，USB释放。

证据目录：artifacts/voice-fast/wake-affine-qat-ux235/；plan.json、selection.json、probe.json、source-manifest.json、closing-state/status.json、closure.json。冻结 17 文件 SHA256：9b028ef54ea31c6f561a6bfe88c82383dc77eee7d5d896386418d0150f4fcd50。
