> 历史阶段记录：安装状态和产物路径描述当时的结果。2026-09-15 已采纳调优语音链路并清理旧中间材料；当前入口见 [BASELINE.md](BASELINE.md) 与 [CLEANUP_REPORT.md](CLEANUP_REPORT.md)。1000次正式证据完整保留，其数据不等于合并版的新1000次测试。

# M0 acceptance plan

Status vocabulary: PASS requires evidence; FAIL means an observed mismatch; BLOCKED means an external prerequisite is missing; NOT RUN never means success.

M3 adds the acceptance matrix in `MUSIC_SPEC.md` and evidence in `MUSIC_REPORT.md`. Run `test_host.ps1` and disabled CTest, `hardware_m3.py` for transactional USB and acoustic calibration, `music_candidates.py` for real device-created songs/capture, and `hardware_m3_soak.py` for 100 concurrent calls. Use versioned output directories; the soak observer allows 300 seconds for a turn's two possible bounded HTTP attempts and cancels/drains acknowledgments during cleanup. Resumed reports preserve the failed original, the same firmware hash and explicit segments. Do not label a segmented run uninterrupted. Subjective listening is a separate, currently pending verdict.

| Requirement | Verification | Entry point / evidence |
|---|---|---|
| M0-01 | ABI, duplicate registry, lifecycle rollback, C11 source boundary | `tools/test_host.ps1`, target compile commands |
| M0-02 | USB, GPIO8 LED operation, actual four-LED observation | real DeepSeek light call + user observation |
| M0-03 | Visible local provisioning, NVS reboot, secret-free responses | `tools/provision_secret.py`, phase D logs |
| M0-04 | Real verified TLS, stream and nonstream, invalid CA | `tools/hardware_smoke.py`, `tools/hardware_faults.py` |
| M0-05/06 | Tool allowlist/ranges/IDs, max rounds, JSON escaping/depth/types, fragmented UTF-8/CRLF/DONE | CTest, real tools, Gateway fault matrix |
| M0-07 | CRC, every append/rotation interruption boundary, tail/middle corruption, capacity, restart IDs | CTest WAL/context, POSIX fsync test, physical USB cut |
| M0-08/09 | Repost dedup, LWW/tombstone, lost push/ACK/cursor, LOCAL off, CLOUD/HYBRID | C context/sync tests, Python HTTPS tests, actual LAN integration |
| M0-10 | DNS/TLS/auth/rate/5xx/timeout, malformed/truncated stream, cancellation and Wi-Fi restoration | fault script and local mock fault file |
| M0-11 | Heap/min/largest/stack, 100 turns, final ELF size | soak JSON and IDF size logs |
| M0-12 | Ten documents, schemas, hardware exports, independent reference, no generated secrets in Git | source/artifact audit |

## Reproducible commands

```powershell
.\tools\test_host.ps1
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe host_tests/test_gateway.py
.\tools\idf.ps1 build size size-components
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe tools/hardware_smoke.py --stream
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe tools/hardware_faults.py
.\.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe tools/hardware_soak.py --rounds 100 --route DIRECT
```

For the full Gateway fault matrix, first provision a reachable mock URL and its CA, then add `--gateway` to the fault script. The script temporarily changes only the test Gateway URL/CA, uses LOCAL to avoid background traffic, and restores DIRECT. DeepSeek/Wi-Fi secret values are never read back.

For actual power loss, run `tools/power_cut_test.py`, then physically unplug USB during the bounded writing window, wait three seconds and reconnect. Capture console/reset reason, last committed sequence, recovered stats and the next event ID; confirm prior complete history. A software reset or a completed writing window without a cut is a different result. Exhaustive host injection supplies precise torn-write evidence even when a manual cut falls between two writes.

The soak records every turn and evaluates after ten warmup turns: fitted heap slope must be at least -8 bytes/turn and the last/first ten-sample median change at least -1024 bytes. Inspect the raw series, largest allocation block and task watermarks as well. A finite soak is not proof against every future leak.

Tests do not modify AP/router settings or eFuses. Wi-Fi disconnection uses the device's own diagnostic pause/resume commands. Tests that depend on a human-visible physical change or mutually reachable networks remain explicitly pending until observed.

## Audio extension

`tools/hardware_audio.py` tests real speaker/ADC lifecycle, cancellation, invalid input, simultaneous music/microphone/DeepSeek LED operation and 20 silent DMA allocation/free cycles. It restores master volume and disables microphone/playback after the test. `--no-network` runs the local bring-up subset; `--rounds 0` skips endurance cycles. Record human listening separately from I2S success.

`tools/hardware_mic.py --seconds 30` records level telemetry while the speaker is off and the user speaks or claps. It closes the microphone at the end and saves no waveform. Capture operation and the user's actual participation are separate evidence. The host audio suite checks score rejection without effects, frequency, duration carry, rests, attack/release, amplitude, rendering fragmentation and DC removal/RMS.

Build with `CONFIG_AGENT_AUDIO=n` and run host CMake with `-DAGENT_AUDIO=OFF` to verify the text-only option. Inspect the disabled ELF for absence of `agent_synth`, `agent_score`, `esp_hi_audio` and `audio_task` symbols. The per-response tool-call limit remains four even when the advertised tool registry has nine entries.

## M2 acceptance

- C host: 13 suites with audio and 10 without. Include long prompt framing/order, cancellation and CRC, source recall/summary, full-plan preflight and restoration, PCM commit power cuts, fixed-point voice/noise bounds and exact chunk output.
- `tools/hardware_m2_context.py`: recall the preserved seed event by exact ID, exercise the real tool continuation, persist its source-bound summary, compact all pending records, software reboot and use a nonstreaming request to retrieve it again.
- `tools/hardware_m2.py`: invalid GPIO has no prefix effects; check BUSY ownership, repeat, cancel, button timeout, mic freshness and two-second capture/replay.
- `tools/hardware_m2_extended.py`: fresh-level-triggered LED, 32 steps repeated eight times, maximum score-schema validation, Flash bank erase blocked during mic DMA, cancelled capture remains invalid, maximum ten-second capture/replay. This replaces the clip, so preserve and restore the accepted voice recording separately.
- `tools/hardware_m2_soak.py --rounds 100 --log artifacts/logs/m2-long-soak-release.json`: real verified DeepSeek requests while a muted score/mic/light plan runs. First 50 turns use 64 KiB; later turns use 128 KiB. A unique fact key identifies each run; probes at 6/18/35/67 use visible history, and 100 must use exact-ID recall. No failed attempt is skipped or relabeled.

The soak records all heap samples. Trend evaluation begins after ten turns **and** 125 seconds in a continuous segment: configured TCP MSL is 60000 ms and TIME_WAIT lasts twice that, so earlier connection reclamation transients remain in the raw report but are not steady-state leak estimates. Require at least 20 comparable samples with music and microphone active; first/last ten-sample median difference at least -1024 bytes and fitted slope at least -8 bytes/turn. Every sampled lifetime minimum heap must remain at least 48 KiB. Check fresh ADC data, zero reported pool overruns, released prompt locks and no failed plan. A short or interrupted pilot is not the 100-turn pass.

Record audible voice/music feedback separately. No absolute noise-free output is promised. GPIO writes/PWM are portable contracts only on this board; actual generic pins are input-only 0/1/9. The unavailable physical power cut and device-to-PC Gateway link remain M0 blockers.

Automatically generated filler records may be moved off-device only under the user's explicit archival choice. `tools/prepare_test_archive.py` creates a reviewable draft from an exact context backup and logged complete filler pairs, retains source-referenced turns and every other record byte, and never flashes. It does not change automatic pending-event retention. Administrative offline bank replacement requires verified backup/readback and USB recovery if interrupted; it is distinct from normal atomic runtime compaction.
