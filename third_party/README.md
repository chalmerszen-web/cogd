# 固定运行依赖

本目录只保留当前ESP32-C3基线所需的第三方代码、模型、库与许可证。`manifest.json`记录文件SHA256；在项目Python中执行 `tools/verify_dependencies.py` 校验。

| 目录 | 来源 |
|---|---|
| cjson | 项目固定cJSON，详情见目录内README.agent.md与LICENSE |
| esp-sr | Espressif ESP-SR `efa8d907c6d457cd0f99dae6c6b493412d3078d4` 的C3头文件/库及wn9s_hilexin模型；保留LICENSE |
| ten-vad | TEN-VAD `22a3bcd4509d0faaa8eef4881e8af5f39c178950` 的选定vendor源、固定生成表；保留LICENSE与NOTICES |
| rvfplib | 接受版使用的3个单精度汇编实现；保留LICENSE.txt与LICENSE-EXCEPTION.txt |

TEN vendor/pitch_est.c及项目自有 `components/agent_vad/kernel/` 使用已接受B版的定点/融合/共享缓冲优化，不能称为未经修改的TEN上游。准确文件清单与哈希也在 `components/agent_vad/sources.lock.cmake`。唤醒筛选在 `components/agent_speech/keyword/`。

参考B应用SHA256为 `d9bd6975088b733d7c2f2976a4698f9c744de34a354a5b5929599df0ff68c953`。迁入清单、原来源和接受版源码归档保存在本机 `artifacts/baseline-cleanup-v1/promotion.json` 与 `artifacts/baseline/reference/accepted-source.zip`。不需要原 `_ref/` 或研究构建目录即可构建。
