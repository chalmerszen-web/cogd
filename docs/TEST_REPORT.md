> 历史阶段记录：安装状态和产物路径描述当时的结果。2026-09-15 已采纳调优语音链路并清理旧中间材料；当前入口见 [BASELINE.md](BASELINE.md) 与 [CLEANUP_REPORT.md](CLEANUP_REPORT.md)。1000次正式证据完整保留，其数据不等于合并版的新1000次测试。

# Test report

M0 status: **IMPLEMENTED, HARDWARE ACCEPTANCE BLOCKED** (2026-09-10). Complete M0 has not been declared. The user confirmed they currently cannot adjust the network or unplug USB; dependent tests remain outstanding.

| Area | Result | Evidence |
|---|---|---|
| Host C11 / ASan / UBSan | PASS, 8 suites | `artifacts/logs/final-host.log` |
| Registry / lifecycle / budgets | PASS | core, JSON, LLM and tools suites |
| SSE | PASS, fixed sizes + 200 seeded random fragmentations; interleaved calls, UTF-8, line endings, truncation | SSE suite, phase F log |
| WAL | PASS, append cut points and 4250 bank-rotation interruption boundaries; CRC/tail/middle/full | WAL suite |
| Context | PASS, reserved IDs after restart, out-of-order dedup, LWW/tombstone, checkpoint rollback, complete-turn history | context suite |
| POSIX file persistence | PASS, fsync + close/reopen | POSIX Flash suite |
| Mock verified HTTPS | PASS, 4 host suites: auth/CA rejection, dedup, scoped cursor, restart, four-tool continuation and fault matrix | `artifacts/logs/final-gateway-host.log` |
| Native USB / Wi-Fi / NVS | PASS on COM5 | phases B/D and subsequent status records |
| Real DeepSeek nonstream | PASS, 1.859 s | `phase-e-deepseek.json` |
| Real DeepSeek stream | PASS, 1.844 s | `phase-f-deepseek.json` |
| Natural-language LED | PASS, model/tool/final; user observed all four red | `phase-g-tool.json`, ACTIONLOG |
| Final C11 nonstream / all four tools | PASS, four tools with original call IDs followed by C11_FOUR_TOOLS_OK; 3.656 s | `release-c11-nonstream-tools.json` |
| Software reboot history | PASS, remembered BLUE_PANDA_731 | `phase-h-history-before.json`, `phase-h-history-after.json` |
| Final C11 modes / Flash compaction / restart | PASS, LOCAL/CLOUD/HYBRID, bank generation 1 to 2, 824 events and pending entries preserved, persistent memory and fresh sequence after reboot | `release-flash-recovery.json` |
| Invalid TLS / DNS / cancel / input/RGB bounds | PASS on device | `phase-j-faults.json` |
| Wi-Fi loss during a turn and recovery | PASS, actual device radio disconnection; new DeepSeek request succeeds | `phase-j-faults.json` |
| Final C11 100 real DeepSeek turns | PASS, 100/100; post-warmup median change 0 B, slope -1.119 B/turn | `release-c11-soak.json` |
| Physical USB cut | BLOCKED; earlier writing window completed without disconnection and is not a power-loss pass | `phase-h-power-cut.json`, user confirmation |
| Device-to-mock HTTPS and hybrid sync | BLOCKED by separate LANs | `phase-i-network-diagnostic.json` |
| Device HTTP 401/403/429/5xx, server timeout, malformed/truncated mock stream | Pending reachable Gateway | fault matrix script prepared |
| M0 source / documentation audit | PASS, 14 owned firmware units compile as C11, no platform headers in portable modules, generated/private files ignored, required documents present | `m0-source-audit.json` |
| Final device configuration | PASS, DIRECT + HYBRID, 1032 pending events retained, Wi-Fi and NVS ready | `final-device-state.json` |

The device is 192.168.1.178/24 via 192.168.1.1; the PC is 192.168.9.109/24 via 192.168.9.1. Direct cloud TLS succeeds; local mock TCP connection times out. No network/firewall bypass was introduced. The existing PC firewall was already off and was left unchanged.

Final strict-C11 soak values after ten warmup turns: free heap 124512–125316 B; last/first ten-sample median difference 0 B; fitted slope -1.119 B/turn. Lifetime minimum heap was 89936 B, worker stack remaining 5028 B, and the final largest free block 98304 B. These pass the documented -8 B/turn and -1024 B median thresholds; 100 turns do not establish unlimited endurance. The earlier independent 100/100 run is retained in `phase-j-soak.json` (median change -84 B, slope -1.927 B/turn).

Actual compaction copied all 824 then-pending events before bank commit and reduced occupied bytes from 317372 to 315396. Software reboot retained generation 2 and every pending event; the next local sequence advanced from 1538 to 1666, and DeepSeek returned the persisted memory C11_READY_20260910. This is separate evidence from a physical cut.

The final nonstream four-tool call set RGB 255/0/0 and completed successfully. The earlier physical observation remains the recorded human LED evidence. After restoring DIRECT + HYBRID, free heap was 127064 B, current task watermark 5136 B, worker watermark 5028 B. Context contained 1032 pending test events occupying 406712 of 524288 bytes in the active bank. The temporary Gateway URL is cleared and the task-owned mock server stopped, so missing Gateway configuration is reported without discarding pending events. No storage format was performed during cleanup. Final build/size figures are in BUILD_REPORT.

Reference firmware compilation was attempted but did not complete on this Windows host because its generated command exceeded Windows process command-line limits near 2014/2108 steps. The independent pure-C target built and ran; reference-build failure is recorded rather than relabeled as success.

## Subsequent audio extension, 0.2.0

The M0 rows above describe the preserved baseline. Audio has separate evidence and does not resolve the two external M0 blockers.

| Area | Result | Evidence under artifacts/logs/ |
|---|---|---|
| Audio-enabled host C11/ASan/UBSan | PASS, 9 suites | audio-host-final.log |
| Score and synthesis | PASS, invalid/extra/duplicate parameters, pitch, duration, rests, envelope, fragmented rendering, amplitude bounds and local meter | audio host suite |
| Audio-disabled target and host | PASS, 8 suites and no project audio ELF symbols | audio-disabled-build.log, audio-disabled-host.log, audio-disabled-symbols.log |
| Gateway audio capabilities | PASS, deterministic score and original-ID continuation with the enlarged registry | audio-gateway-host.log |
| PDM playback | PASS, complete sample progress; user confirmed melody normal and volume appropriate | audio-bringup.json, audio-listening-replay.json, ACTIONLOG |
| ADC control | PASS, capture counter advances while enabled and stops when disabled, zero DMA pool overruns in exercised cases | audio-hardware.json |
| Concurrent audio / ADC / Wi-Fi / LED tool | PASS, speaker still playing before and after actual DeepSeek LED call | audio-hardware.json |
| Cancellation / bounds | PASS, end-to-end USB cancellation 0.406 s; invalid note/volume causes no new playback job | audio-hardware.json |
| 20 real DMA lifecycle cycles | PASS, free-heap median change 0 bytes, audio stack remaining 5104 bytes | audio-hardware.json |
| DeepSeek-generated music, streaming | PASS, volume and score calls followed by completion, 4.500 s | audio-deepseek-score.json |
| Nonstreaming generated music | PASS, triangle-wave score and concise Chinese completion, 5.094 s | audio-release-deepseek.json |
| Final firmware stop followed by immediate play | PASS, both commands in one USB packet; cancellation 0.407 s including host round trips | audio-release-controls.json |
| Microphone-only level window | PASS, replayed 30-second test at the user's requested start time; RMS 937–7574 with speaker off, no waveform recording | audio-mic-retest.json |

During speaker playback, the nearby ADC microphone showed significant clipping. The quiet speaker-off window had no clipping. The user-requested external-sound retest recorded 5298 clipped samples and zero DMA pool overruns, so this establishes signal response without claiming clean recorded speech. These are relative signal measurements. The current feature provides microphone control and levels, not speech recognition or acoustic echo cancellation.

The first microphone window (audio-mic-levels.json, RMS 440–1016) ran while the user was away. At their request the window was repeated immediately; the later range increased by about eightfold. The report does not infer what was spoken from level values.

Final USB state is idle, volume 20, microphone off, DIRECT + HYBRID, with all 1081 pending context events preserved (438588 bytes in generation 2). Evidence: audio-final-device.json. The last firmware adjustment waits up to 200 ms for stop acknowledgment so an immediately following play is accepted; the score renderer and ADC path are the same ones exercised in the concurrent/endurance tests.

## M2 control, context and recording

M0/M1 tables above remain historical evidence. Current firmware is 0.3.0-dev. M2 results are separate from the unresolved M0 LAN/power-cut conditions.

| Area | Result | Evidence under artifacts/logs/ |
|---|---|---|
| Portable C11/ASan/UBSan | PASS, 13 suites; prompt order, 64/128 KiB framing, CRC/cancel, tools, WAL, plans, clip and fixed-point DSP | m2-final-host.log |
| Audio-disabled host | PASS, 10 suites | m2-noaudio-host-final.log |
| Audio-disabled ESP32-C3 target | PASS, 963472-byte binary; no project audio/voice/clip/replay symbols | m2-noaudio-build-final.log, m2-noaudio-symbols-final.log |
| Verified HTTPS mock | PASS, 6 suites including all 19 capabilities, summary validation/idempotency and original tool IDs | m2-gateway-host-final.log |
| Flash layout migration | PASS, all 1130 then-pending events preserved; full image/staging readback and actual boot | m2-migration-complete.json, M2_MIGRATION.md |
| Real long-context recall | PASS, exact-ID tool continuation after fixing the two-second budget; full-bank search 3.062 s | m2-context-acceptance.json |
| Summary, compaction and reboot | PASS, source ID retained, 1374 pending events preserved, generation advances, nonstream summary use and unique post-reboot sequence | m2-context-acceptance.json |
| Invalid GPIO / ownership / cancel | PASS, USB18 rejected without prefix effects, direct conflict BUSY, repeat/restoration, button wait timeout and cancellation | m2-hardware-basic.json |
| LLM action-plan composition | PASS, capability discovery followed by a score/light plan, complete sample count and plan done | m2-deepseek-music-plan.json |
| Level trigger / maximum plan | PASS, fresh RMS triggers green light; 32 steps × 8 cycles; large two-score schema validates | m2-extended-hardware.json |
| Recording bounds / stop | PASS, invalid duration rejected; cancelled partial clip not playable; cancellation 0.406 s | m2-extended-hardware.json |
| Maximum real capture/replay | PASS, 10 seconds, 160000 input / 240000 output samples; reported overflow 0 | m2-extended-hardware.json |
| Context erase versus DMA | PASS, bank compaction returns BUSY during active microphone and sampling remains valid | m2-extended-hardware.json |
| Audible voice replay | PASS for the saved clip: after DAC-mode correction at volume 80, user reports clear voice and less hiss | m2-human-replay-dac-volume80.json, ACTIONLOG |
| Audible generated melody | PASS, user reports normal melody and appropriate volume with the new DAC configuration | m2-deepseek-music-plan.json, ACTIONLOG |
| Archival preservation | PASS, user-approved 106 filler pairs archived; exact current readback, every retained event byte and source boundary verified | m2-test-archive-applied.json |
| New fact versus old summary | PASS on targeted recovery; the preceding reused-key failure remains recorded | m2-newer-fact-recovery.json, m2-long-soak-final.json |
| Final 100 real concurrent turns | PASS, 100/100 including exact-ID recall on 100; zero reported ADC overflow and fresh sampled levels | m2-long-soak-release.json, m2-soak-summary.json |
| Post-test archival and final boot | PASS, 98 further filler pairs archived under the confirmed test-only rule; source retained and fresh M2_READY reply | m2-post-soak-archive-applied.json, m2-final-device.json |

Audio diagnosis retained failed pilots. Increasing the ADC pool alone did not prevent 360–534 ms worker stalls. IRAM-safe interrupts plus a bounded higher-priority audio worker corrected freshness/overflow in later runs. One early pilot that only checked counters was revoked when stale samples were discovered. The first full candidate completed 99 turns before the local search-budget error; it is not labeled 100/100. The next six-turn reused-key recall failure is also retained. Numerical denoising gains did not establish acoustic success: PC-versus-board listening and the PDM DAC-mode replay provided the eventual evidence.

The maximum-duration capture measured 145 clipped ADC samples out of 160000. The tested replay is intelligible; the microphone/amp are not calibrated, no complete noise removal or echo cancellation is claimed, and no recording was uploaded or transcribed. Original human voice PCM was restored after automated clip tests.

The final 100-turn run took 510.078 seconds. History reached 65519 bytes at 64 KiB and 131019 bytes at 128 KiB; the largest measured request was 144781 bytes. After the 125-second TCP warmup, 67 comparable music/mic-active samples ranged from 83404 to 83608 free-heap bytes. First/last ten-sample median change was +40 bytes; slope +0.284 bytes/turn. Lifetime minimum heap was 49400 bytes, sampled minimum largest free block 43008 bytes, maximum sampled mic polling gap 16 ms. Remaining stack watermarks were network 5680, main 5428, control 2540 and audio 5116 bytes. The independent maximum-clip test reached audio watermark 4804 bytes. No crash or reset occurred during the final soak; this finite run is not an unlimited endurance guarantee.

After the approved test-only archival, the final fresh boot/DeepSeek smoke test retained 1190 events, all still pending, at 519768 of 1048576 active-bank bytes, generation 6. Budget is 128 KiB, route DIRECT and mode LOCAL. The five-second accepted recording is ready; volume 80, microphone/speaker/plans idle, COM5 released. `m2-final-device.json` records this rebooted state; its reset lifetime minimum is distinct from the 49400-byte soak minimum. M0 LAN and true-power-cut blockers remain unchanged.

## M3 0.4.0-dev

Current requirements and complete acoustic/candidate evidence are in [MUSIC_REPORT.md](MUSIC_REPORT.md). Fifteen enabled C suites, ten disabled suites, six mock HTTPS tests and eight host migration/archive tests pass. Forty simulated reboot/reservation cycles cover the local dedup-range exhaustion found during real music tuning. Full old local range state advances without deleting history; remote out-of-order dedup remains exact.

The same flashed binary completed 100 validated real DeepSeek rounds in two explicitly recorded segments after a host observer timeout. The late unsampled original round 38 is retained as a failure and excluded from the 100 validated rounds; firmware network deadlines were not relaxed. Final retrieval by original event ID succeeded. Minimum heap 49968 bytes, maximum music render block 1689 us, ADC overruns 0, maximum microphone polling gap 16 ms. Fifty-four comparable post-warmup samples show -8 bytes median change and -0.208 bytes/turn slope. Maximum history 131067 bytes; maximum request 148021 bytes. Remaining stacks: main 5380, network 3496, audio 4904, control 2640 bytes. No task-stack reduction or history-capacity reduction was used.

The final calibration shows >=5.15 dB velocity separation for all three instruments, natural decay and no full-scale capture clipping. At least five complete device-generated candidate performances are retained; selected candidate-05c corrects accompaniment gaps and root-bass/arpeggio mismatches, and has a tonic ending. Subjective M3 listening remains pending; previous M2 human feedback does not certify M3. Physical readback and final three playback evidence are maintained in the music report and release manifest. Existing M0 external blockers remain unchanged.
