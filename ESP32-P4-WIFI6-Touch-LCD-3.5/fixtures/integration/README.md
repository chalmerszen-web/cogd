# P4 固定整合测试素材

这些文件是先前固定测试素材的逐字节副本。`manifest.json` 记录来源、用途、大小和 SHA-256；不包含真人录音。运行前校验全部所需素材，校验失败即退出。音频从 Windows 播放端点送入 P4 实体麦克风，没有向设备注入 PCM。

在 cogd 根目录运行（端口和构建目录按现场实际填写）：

```powershell
python tools/p4_voice_smoke.py --port COM3 --build-dir ESP32-P4-WIFI6-Touch-LCD-3.5/build-wakenet10 --out artifacts/integration-voice-01 --record
python tools/p4_voice_smoke.py --port COM3 --build-dir ESP32-P4-WIFI6-Touch-LCD-3.5/build-wakenet10 --out artifacts/integration-media-01 --mode media
```

`--fixtures` 可指定本目录的完整副本。默认播放与录音端点为 Misiom-Shooter；可用 `--output-device` 和 `--record-device` 明确选择实际端点。`--record` 需要 FFmpeg。输出目录必须不存在，任何用例或清理失败均返回非零退出码。

语音用例包含两次独立官方唤醒、普通问答与续问、静默告别、真实播音中插话、明确结束及监听恢复。播放音量默认为 100，结束恢复运行前查询到的音量。主机不设置设备时间，不代替开机自动联网或校时验收。

媒体用例通过到板端实际 IP 的路由推导本机地址，仅临时提供该 MP4 的原始字节；不转码、不生成新视频、不改防火墙。检查本机解码播放及三轮软件触摸事件暂停/继续。该检查不证明手指触摸，也不验证视频与实时语音并发；现有并发误打断失败不得据此改为通过。

脚本记录本地构建 SHA-256，并核对设备版本。版本一致本身不能证明烧录镜像一致：应在运行前后由独立烧录工具读回应用并核对 SHA-256。可用 `--readback` 传入测试前读回文件，长度必须与应用文件相同；测试后读回仍由操作者完成。

固定声学回放通过不等于真人、远场、噪声、长期稳定性或声学 P95 验收。新增测试报告与录音留在独立输出目录，不能覆盖旧证据。

最小教学检查使用新增固定题图及确认、第二步追问、结束音频：

```powershell
python tools/p4_tutor_smoke.py --port COM3 --build-dir ESP32-P4-WIFI6-Touch-LCD-3.5/build-wakenet10 --out artifacts/integration-tutor-01 --record
```

默认总时限 300 秒。该用例明确标记 `test_image_not_camera`：读取题干后通过实体麦克风确认，等待完整解法与讲解方式选择，追问第二步并检查题号及解法版本不变，然后明确结束并恢复监听。它不重新验证五分钟阅读暂停、真人纸张识别、手指触摸、长步骤分页或复核。`--readback` 和端点选择参数与语音脚本一致。
