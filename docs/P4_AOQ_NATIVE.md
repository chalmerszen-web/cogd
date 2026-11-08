# P4 native AOQ development

The transport connects the ESP32-P4 directly to the Qianwen AOQ relay using UDP/QUIC. There is no computer media relay. The single-turn native speech/video diagnostic and the integrated conversation backend are separate acceptance targets. The integrated backend is under development; passing a diagnostic does not qualify it for release.

Current qualification (2026-11-09): candidate 111 binds eight board reports to 201 unchanged inputs and an independent application readback. Its finite functional regression passed three complete interruptions/follow-ups, two long answers, three complete ordinary inputs, five context pairs, a fresh-photo tutoring workflow and real-camera dialogue. Actual screen positions and camera objects were independently inspected. Standalone ASR/TTS protocol checks and one accelerated-upload regression also passed; the default remains 20 ms. Human acceptance and latency qualification remain open: ordinary replies measured 2.3–2.9 seconds after fixture playback, and interruptions 4.4–5.3 seconds. No matched WSS speed advantage is claimed. C3, WSS and no-probe AOQ were built but not physically tested. See the [hardware record](P4_AOQ_VALIDATION_20261108.md) and [trial guide](P4_AOQ_TRIAL_GUIDE.md).

Candidate 111 requires a temporally close pair of strong interruption votes and separately retains an early linear-voice onset hint. The hint cannot stop playback or admit a turn. It preserves captured samples and bounded replay; no speculative weak-onset input is uploaded. Earlier failed candidates remain documented below and do not describe the currently installed state.

Native conversation reception uses a fixed 512-packet SPSC UDP queue. Its receive task does not decrypt packets or execute model callbacks; the session owner remains responsible for QUIC and callbacks, and joins the receiver before releasing the socket or queue. Each compressed receive stream has a 512 KiB credit window backed by a mirrored data ring plus presence map (1.5 MiB per active stream, at most eight streams). With 32 or more queued datagrams the owner drains up to 64 packets/12 ms and decodes at most two media objects; the normal budgets remain 16 packets/4 ms and eight objects. PCM backpressure never discards received speech or fabricates end-of-stream. Queue peaks, waits and transport progress are diagnostics, not proof of audible completion.

## Compile-time communication selection

`tools/build.ps1 -CommunicationMode WSS` in the P4 project selects the existing conversation transport (the default). `-CommunicationMode AOQ` selects the native backend for the shared natural-conversation owner and labels the image `0.10.0-p4-aoq-dev`. At C level these map to `COGD_COMM_MODE=COGD_COMM_WSS` and `COGD_COMM_AOQ` in `platform/espidf/communication_mode.h`. Unsupported values fail compilation. AOQ is rejected for non-P4 targets; the C3 build continues to use its existing transport and has no AOQ dependency.

The shared owner retains the original tool definitions, tutoring state, history, audio owner and official wake word. The adapter only owns native transport and the association between model response events and media. Normal mode is `agent voice mode natural`; `aoq-test` and `aoq-video-test` remain explicitly single-turn diagnostics when `-AoqProbe` is also enabled. These diagnostic switches do not select the production conversation backend.

| Build | Runtime selection | Transport and scope |
| --- | --- | --- |
| WSS (default) | `natural`, `classic`, `fast` | Original transport and behavior retained |
| AOQ | `natural`, `video off` | Native Qwen Audio conversation with the shared history/tool/audio owner |
| AOQ | `natural`, `video on` | Native Omni conversation plus current board-camera JPEG frames; hardware acceptance pending |
| AOQ | `classic` | Independent native ASR and TTS adapters; fixed-input ASR and TTS playback/cancel/reopen passed on P4; acoustic conversation acceptance pending. Text-model requests retain HTTP |
| AOQ | `fast` | Rejected explicitly; the original speculative fast engine remains available in the WSS build |
| Either P4 build with `-AoqProbe` | `aoq-test`, `aoq-video-test` | Bounded single-turn interoperability diagnostics |

There is no silent WSS fallback for AOQ speech. Allocation still uses authenticated HTTPS. Full-resolution photo recognition and other existing HTTP-only tool APIs retain their original request path; 320x256 live video is not a replacement for the tutoring photo source.

The current audio-only AOQ development build uses manual cloud turns (`turn_detection:null`) and capture-clock local VAD with 600 ms silence. It retains onset pre-roll, commits only after the endpoint PCM is accepted, and requests a response only after nonempty transcription. Empty transcription does not play the upload cue or spend a response. The audio threshold control is retained for diagnostics but does not change this local detector. WSS retains its original default. Change `agent voice endpoint`, `agent voice vad-threshold`, and `agent voice video on|off` only while voice is off and the owner is idle. Video still uses its separate server-VAD profile (900 ms, threshold 0.1, explicit transcription); the complete camera conversation remains an acceptance gate.

Candidate 91 advances the 20 ms uplink deadline from its prior deadline instead of adding another full period after processing finishes. It rebases after a whole missed period and enforces a half-period minimum gap to avoid bursts. Endpoint silence and capture timestamps are unchanged. The experimental weaker-syllable detector from candidate 90 failed physical qualification and was withdrawn; the detector again matches candidate 89. Natural interruption and full workflow qualification remain open.

Candidate 92 retains that pacing and supplements native interruption confirmation with six admitted frames in a 16-frame window, requiring four strong NLP frames. The original 12-frame path remains; individual energy, VAD and correlation gates are unchanged. A separate bounded weak-onset hint retains earlier microphone samples but cannot admit an input turn: the existing strong confirmation is still required, and committed boundaries remain protected. These changes passed targeted host regression and remain subject to physical qualification; they do not erase previous failures.

Candidate 94 aligns the raw microphone and DAC reference with the measured 512-sample FD AEC output delay. The detector had been combining current raw echo with a previous residual syllable, rejecting a real onset in candidate 93's recorded prefix-loss case. Raw probe PCM retains its original sample clock; correlation and envelope checks use the aligned frame plus 96 ms of preceding reference history. No admission thresholds, confirmation counts, onset-chain gaps, or replay caps were relaxed. The first physical candidate-94 barge-in still fails with zero strong NLP votes, and its subsequent playback underruns remain open. It is not a qualified build. Strict quiet-input replay passes five complete utterances, including the previously lost prefix.

Candidate 84 selected the supported single-buffer SDIO receive path. Its requested 128-entry UDP/TCPIP mailboxes were subsequently found to be outside ESP-IDF's active 6..64 ranges: frozen configurations through candidate 88 actually compiled UDP=6 and TCPIP=32. The earlier claim that 128 had taken effect was incorrect. The defaults and build script now request 64 for AOQ and verify the generated compiler header after every build. WSS restores double buffering and 32-entry mailboxes. The previous double-buffer driver was observed freeing receive buffers when its copy task had not finished; this correction is not yet evidence that all network loss has been eliminated.

Manual input-buffer maintenance waits until inference and speaker playout are idle and never clears uncommitted input. A matched clear acknowledgement admits the next upload. The exact 30-second empty-input-limit error is treated as recoverable only after a fully committed and transcribed turn with no outstanding input; other errors remain failures. Candidate 83 acknowledged a clear during output and subsequently reset an unfinished Audio stream; that causal relationship remains unproven. With the generated mailbox values actually corrected, candidate 89 completed two physical long-answer replays (73.34 and 64.62 seconds), three complete interruption/follow-up cases, and five changing-context pairs without underruns or extra interruptions. These finite regressions do not qualify the still-pending complete tutoring and real-camera workflow; see the [current hardware record](P4_AOQ_VALIDATION_20261108.md).

The independent speech adapter preserves the existing speech-provider interface but opens a native AOQ session. Standalone ASR sends PCM as documented base64 Data append objects on reliable QUIC; conversation microphone audio uses 20 ms Audio DATAGRAMs. TTS receives 24 kHz PCM Audio objects, permits audio after the Data task-finished event, and delays completion until the audio terminal or the bounded receive-drain policy below. Partial reads and queue limits are explicit. Its host tests do not substitute for testing the integrated provider on the board.

The integrated receive path holds completed responses until their matching audio segment ends, buffers bounded early audio, and rejects ambiguous overlapping responses. It does not interpret transport group IDs or opaque media userdata as model response IDs. Accepted interruption retires the connection before replaying captured onset into a fresh native session. The caller renews the bounded connection at an idle boundary; cancellation and deadline failures remain visible.

The initial integrated tests exposed two independent problems: acoustic endpoint settings still lost the tail of a question, while semantic endpointing retained it with a long wait; a stale playback timestamp could also cause a false interruption on the next turn. Those failed cases remain release gates. Temporary `agent voice endpoint` and AOQ-only `agent voice vad-threshold` controls permit comparisons while voice is off. They are volatile diagnostic settings, not a claim that lower thresholds or shorter silence are universally better.

## Build and initial diagnostic

Use the P4 build script with `-CommunicationMode AOQ -AoqProbe -BuildDir build-aoq-native` for the integrated `0.10.0-p4-aoq-dev` image plus diagnostics. `-CommunicationMode WSS -AoqProbe` retains the original backend and labels the diagnostic image `0.9.8-p4-aoq-probe`. The probe option defaults to off. Neither development label represents completed hardware acceptance.

With the diagnostic image installed and the voice worker disabled (`agent voice off`), the existing serial worker accepts:

```
agent aoq probe audio
agent aoq probe asr
agent aoq probe tts
```

Each command performs a fresh authenticated HTTPS allocation on the P4, then a bounded native QUIC connection and AOQ setup exchange. It sends no microphone audio, plays no sound and captures no video. The result explicitly includes `media_sent:false`. Credentials and payloads are excluded from diagnostic output. A successful setup is not a speech, ASR, TTS or speed result.

The media diagnostic commands are separate:

```
agent aoq tts probe
agent aoq tts play <generation returned by a successful TTS probe>
agent aoq asr probe <the same TTS generation>
agent aoq dialogue probe <the same TTS generation>
agent aoq dialogue play <generation returned by a successful dialogue probe>
agent aoq video red <successful TTS generation>
agent aoq video blue <successful TTS generation>
```

The TTS trial requests only the fixed phrase “原生语音测试成功。”, uses a fresh task and native session, and retains PCM only after matching model completion plus an audio terminal or the bounded receive-drain policy below. Playback copies that exact generation into a separate bounded buffer and uses the existing audio owner; its completion counters are not human listening acceptance. ASR copies that same successful generation, converts 24 kHz PCM to 16 kHz on P4, and uses the selected ASR model's documented Data-append path. It requires the expected transcript, not only task-finished. These commands do not use a microphone or a computer media relay.

The bounded dialogue trial uses the same exact PCM generation, resamples it on P4, waits for `session.updated`, and sends paced 20 ms PCM frames on Audio 201 using QUIC DATAGRAM. It permits exactly one input turn and one model response on a fresh connection. The response is retained only after the expected model events and bounded audio drain; playback is a separate explicit command. This is not the normal microphone conversation, a reusable response-identity implementation, or a speed comparison. `agent cancel` cancels entered media trials and joins any existing playback before freeing its buffers.

The three allocation kinds now declare their documented audio directions: dialogue has both Audio directions; standalone TTS publishes Data and subscribes to Audio/Data; standalone ASR publishes Audio/Data and subscribes to Data, with its fixed trial carrying PCM only on Data. The SDK-matched profiles contain 6/5/5 tracks respectively, including command tracks. A native media trial using the earlier six-track TTS offer received GoAway after accepted setup; this is retained as a failure, not classified as a successful TTS connection.

The initial probe accepts literal-IP relay endpoints and tests the first endpoint only. DNS resolution and relay fallback are not yet implemented. Failure remains visible; there is no silent WSS fallback in this diagnostic.

The explicit `agent voice mode aoq-test` entry (set with voice off, then enable
voice again) uses the existing official wake word and existing P4 microphone/
speaker owners. It opens a fresh AOQ session for one short spoken question and
streams a short answer. It does not implement tools, history, follow-up or
barge-in; `mode natural` selects the shared conversation owner with the compile-time transport. After completion,
disable voice and query `agent aoq live result` for the retained JSON receipt.
This avoids ESP-SR's direct startup logger interleaving a long completion report.
The mode is volatile; it is an explicit interoperability/speed-test entry, not a
completed replacement for the tutoring system.

The fixed video commands allocate `qwen3.8-omni-flash-realtime` with voice Tina,
publish JPEG Video 101 at 320x256/1 fps, and send the same known speech fixture
on Audio 201. Each JPEG owns one group and reliable stream with explicit EOG
and FIN. Only one copied frame can be pending; Data/audio take priority. The
two artificial fixtures contain a red square or blue circle and are labeled
`camera_used:false`. Correct color/shape, actual audio response and at least
two sent frames are required. Camera capture is a separate gate.

For live board-camera input, select `agent voice mode aoq-video-test` while
voice is off, then turn voice on. The official “你好小智” wake word starts one
question with native Omni audio and 320x256 JPEG frames at up to 1 fps. The
existing camera worker supplies current frames; the computer supplies no images.
Microphone upload starts first. Video waits for the cloud `speech_started`
event, which confirms that audio reached the model; socket acceptance of a
DATAGRAM alone is not delivery confirmation. Frames older than 1.5 seconds,
repeated sequence numbers, and obsolete camera generations are
rejected. Ending the turn stops camera admission and restores the visual owner.
The persistent bounded encoder/cache storage is board-owned, so a camera worker
finishing an encode cannot use freed dialogue memory. This low-resolution video
mode is for scene questions, not a replacement for full-resolution photo tutoring.

The live camera mode requests `server_vad`, threshold 0.1 and 900 ms silence,
plus explicit `qwen3-asr-flash-realtime` input transcription. These parameters
use the [documented Omni controls](https://help.aliyun.com/zh/model-studio/client-events).
Candidate 22's cloud-confirmed `semantic_vad`/0.5/800 configuration produced no
speech-start event with either a clear synthesized camera question or the same
acoustic fixture that passed the audio-only path. Those trials remain failures;
documented semantic-VAD support is not presented as verified interoperability.
It verifies the returned VAD type, threshold, silence duration and transcription model in
`session.updated` before admitting media, and records `camera_config_confirmed`.
The audio-only and fixed-fixture settings remain separate. Second turns are
still rejected; this setting does not implement follow-up or interruption.

Live microphone upload now uses the post-cue capture clock, before network
allocation. The previous scheduler started a new real-time clock only after
`session.updated`, so audio collected during the connection remained delayed
by approximately the connection time. The live sender now catches up at no
more than one 20-ms frame every 5 ms, subject to QUIC send credit and actual
capture availability. It preserves every sample and original timestamp, never
sends future timestamps, and resumes producer-paced transmission when caught
up. It does not trim initial speech or shorten the model's VAD silence setting.
Current camera images wait until uploaded audio covers their capture time.
The receipt exposes `initial_capture_lag_ms` and `last_capture_lag_ms`; these
measure client-side PCM age, not network/model or end-to-end response time.

The missing endpoint cue was a separate live-entry regression: it opened the
uncued speaker stream only after response PCM arrived. The recovery opens the
existing cued audio owner at the first matching cloud speech-stop. Amplifier
warmup and the original 180-ms sweep run concurrently with model inference;
network polling continues and reply PCM is not consumed as a cue. Duplicate
speech-stop events cannot replay it or move the latency origin. Microphone and
camera admission end after opening the stream, preserving its 80-ms prebuffer
policy. Cancellation/model timeout still joins that owner before releasing its
ring. `endpoint_cue_start/end` events record the board cue render timestamps,
separately from `aoq_playback_started`, which measures response PCM progress.
This restores feedback; it is not evidence of faster end-to-end answering.

Serial mode selection (each line is a separate command):

```
agent voice off
agent voice mode aoq-test
agent voice on
```

Use `aoq-video-test` instead of `aoq-test` for camera questions, or `natural`
to restore normal tutoring. Both native modes return to wake listening after
one short answer; wake again for another question. They do not carry conversation
history across wakes. Reboot also restores the default normal mode. Serial is
only configuration/observation, not a media relay.

## Protocol sources and separation

Allocation uses the [official token API](https://platform.qianwenai.com/docs/api-reference/realtime-api/token-auth) and the existing device authentication configuration. The adapter authenticates HTTPS normally and pins the QUIC relay leaf certificate to the returned SHA-256 fingerprint. It does not disable certificate or handshake signature verification.

The AOQ setup codec was compared byte-for-byte with pure serialization functions from the official Windows SDK v1.4.0. The SDK is a development reference; its binary is not embedded or distributed with COGD. The observed ALPN is `aoq-quic-01`. AOQ framing is distinct from generic MoQ draft framing. The vendored QUIC foundation and local changes are documented in `components/esp_http3/COGD_PATCHES.md` under the P4 project.

The SDK uses a relay routing prefix in its initial eight-byte connection IDs: a zero byte, the first three raw bytes of the allocated signed SID, and four random bytes. The signed SID remains unchanged; it is not split or hex-decoded. An offline synthetic SDK capture additionally established three private QUIC transport parameters: `0xff0e07` contains client role varint 1, `0xff0e08` contains the full SID bytes, and `0xff0e09` contains the selected endpoint's route-index varint. The SDK sends fallback SNI `localhost`. Initial packet tokens remain empty. These details are centralized in `aoq_transport.h`; certificate identity is still checked against the HTTPS-issued leaf pin.

Initial hardware trials without the private transport parameters received no UDP response, including the trial that corrected CID routing alone. Independent decryption of a captured P4 Initial verified its standard QUIC cryptography and framing. An Initial with the complete AOQ parameters contains the signed SID; the temporary packet-export command was removed before adding them. Do not put raw handshakes or session fields into public diagnostics. Complete native interoperability remains a separate hardware gate.

The AOQ allocation models are `qwen-audio-3.1-realtime-flash`, `qwen-audio-3.1-asr-flash-streaming` and `qwen-audio-3.0-tts-flash`. The older C3 ASR route stays unchanged. Model allocation success is separate from successful native media exchange.

## Segment PCM continuity

LOC field 50 is named `kLocPropertyAlgoInternalSampleCursor` in the SDK. A real P4 TTS run received object 0 followed by object 1, each 480 mono samples at 24 kHz; timestamps advanced by 20000 microseconds while this internal cursor remained zero. Treating that value as a PCM buffer offset incorrectly rejected valid audio. The bounded TTS receiver now checks sequential object IDs on its single reliable stream, negotiated PCM size/duration and task identity. It records internal cursor discontinuities without using them to insert, drop or overlap PCM. Repeated/skipped transport objects still fail.

The observed TTS service emits `task-finished` without closing its reusable audio stream. Requiring FIN or EOG alone incorrectly rejected otherwise complete fixed trials. The alternative completion policy requires model completion, nonempty PCM, no buffered stream fragments or known gaps, and a quiet receive window after the later of the last PCM or model completion. The window is four measured RTTs plus 100 ms, clamped to 250–2000 ms (1000 ms before an RTT estimate). New PCM resets it. Reports preserve `audio_end_group:false` and `audio_end_track:false` when absent. This is an explicit bounded application policy, not a cross-track transport fence or a guarantee against arbitrarily late media.

## Acceptance sequence

### Acoustic tuning in progress, 8 November

The user authorized autonomous tuning with the existing headset, microphone
and Insta360. The acceptance target is repeatable complete input, interruption
and uninterrupted playback, followed by integrated tutoring/video checks.
The migration is still unreleased; a single successful utterance is insufficient.

Candidates 36-38 added an explicitly armed, finite diagnostic capturing raw
microphone/DAC reference/AEC PCM, the exact PCM accepted by native AOQ, local
VAD/energy/correlation decisions and capture/accept timestamps. It runs for at
most eight seconds, allocates outside audio processing and records diagnostic
contention instead of blocking audio. Transport acceptance is not a cloud
delivery acknowledgement. Evidence remains private under `.local`.

| Run | Observed result |
| --- | --- |
| `probe36-original` | FAIL: cloud transcript stops at “我们的小猫叫蓝莓，请指。” The complete latter clause is present in accepted PCM; 243 recorded frames match AEC PCM byte-for-byte at a fixed offset. No diagnostic frame skips. Steady capture-to-accept lag is about 70-80 ms after initial catch-up. |
| `probe37-original` | FAIL: mild NS plus adaptive digital AGC reduced overall recorded level from RMS 415 to 195; no valid cloud turn. This configuration is rejected. |
| `probe38-original` | FAIL: NS plus fixed compression increases level but still loses the latter clause. |
| `batch38-threshold300`, `batch38-threshold400` | FAIL: lower server VAD thresholds introduce an unsolicited empty turn before the test question, without fixing the original full-input regression. |
| `batch38-gain-only` | One complete original-source question and correct answer pass with NS off, fixed digital gain 18 dB, limiter enabled, VAD 0.5 / 600 ms. This is not yet a repeatability pass. |
| `batch38-normal10` | Stops at its first failure: the provider cancels the pending reply just after the endpoint cue. Ten passes were not obtained. |
| `batch39-normal10` | First original-source utterance passes; the second loses the latter clause. Raising gain to 24 dB and switching to semantic endpointing also fail separate bounded comparisons. |
| `probe39-barge` | Full story request and real playback verified, but no interruption. All 188 local frames fail the energy condition; AEC output RMS is 166. The actual accepted uplink is muted as designed. Maximum capture-to-accept lag reaches 2.40 s during the downlink burst. |
| `batch40-echo80` | FAIL: at volume 80, 1,266,720 samples (52.78 s) play but the turn does not complete within the test bound. The peer resets stream 7; the original Session ignored the reset. Playback reports 11 underruns and three DMA deadline misses. No successful long-monologue acceptance is claimed. |
| `batch40-barge35` | FAIL: at the explicitly reduced volume 35, local interruption occurs about 2.72 s after the source begins and stops playback in 5 ms. Only “等于多少？” survives transcription; the arithmetic answer and follow-up gate do not pass. This is not acceptance at volume 80. |
| `batch41-echo80` | FAIL: peer reset remains with the 32-packet mailbox. The failure is now reported as `peer_reset_stream` and the test stops promptly. No playback underrun or DMA deadline miss occurs before the abort; this is not complete-playback acceptance. |
| `batch41-original10` | Stops at the first failure, “我们的小猫叫蓝莓，请指出。” No repeatability pass. |
| `generic-speech41` | PASS within fixed-input scope: native standalone ASR returns the full math question; multipart TTS renders all 551,520 samples with `play_error:ok`. This does not test the microphone or human listening. |
| `batch42-echo80` | One long-answer PASS at volume 80: all 1,329,840 samples play, actual speaker time is about 55.5 s, and the turn completes with zero interruptions, underruns or DMA deadline misses. External room audio contains the ending. This is one run, not the ten-run acceptance target. |
| `probe42-barge35` | FAIL at volume 35: the six-second AEC capture retains the full “停一下，七加五等于多少”, but local confirmation occurs about 2.8 s after onset and the 750 ms reconnect replay loses the first clause. Cloud input is only “等于多少？”. This identifies a separate onset-retention defect; it does not establish acceptance at volume 80. |
| `batch43-barge35` | FAIL: no local-VAD interruption in this fresh run. Extending replay does not itself make the acoustic detector reliable. |
| `batch43-barge25` | FAIL at volume 25: local interruption and native reconnection occur, but no new cloud speech-start/transcript or arithmetic answer follows. This is not an interruption/context pass. |
| `batch43-echo80-10` | Ten-run batch stops on case 2. Case 1 completes about 54.9 s of audio with no interruption/underrun; case 2 is reset by the peer with numeric code 268, 916,800 adapter bytes queued and owner peak only 759,840 samples. The first pass does not close the long-answer regression. |

Code review found the endpoint cue was outside the old upload echo gate,
which began only when assistant PCM started. Candidate 39 adds a bounded cue
capture window and checks capture time before gating assistant playback,
preserving user PCM queued before playback. The audible cue remains enabled.
The default uplink conditioner now uses fixed gain with limiting; NS is off.
Probe builds retain volatile, next-connection profile selection for comparison.
No conditioning changes the capture/KWS or passive local-interruption PCM.

Candidate 40 bounds the conversation receive pump between packets and shares
one eight-object budget across its parse passes. Queued bytes and QUIC credit
are retained. Its mock transport test verifies that a pending microphone frame
can be submitted before the rest of a downlink burst and all 25 ordered objects
eventually drain. Actual Session errors now preserve their cause instead of
being mislabeled `adapter_limit`.

The long-answer regression additionally exposed an ignored peer reset: a
missing segment terminator can never arrive after that reset, leaving the owner
waiting after playback exhausts its buffer. The next candidate propagates
resets of unfinished streams, ignores resets only for fully consumed retired
streams, and reports stop requests on an active outgoing control/data/video
stream. Host regressions and prompt hardware failure reporting pass. P4's default
UDP mailbox grows from six to 32 packets for burst headroom. This is not yet
proof that packet loss or the acoustic input failures have been resolved.

Both long-answer runs reach the playback receive threshold of about 949,200
queued samples. Review found that the old adapter only pumped QUIC when its
message queue was empty, and the owner stopped calling it while its PCM buffer
was full. Candidate 42 separates the network pump from queue reads. Audio
backpressure leaves the object and its receive credit in Session's bounded
window while ACKs, timers and Data continue. The adapter reserves 128 KiB for
Data within its existing 1 MiB queue; it does not remove buffer limits or discard
audio. A host regression blocks Audio, verifies Data progress with unchanged
audio IDs/counts, then releases capacity and receives the original PCM and FIN.
One complete long-answer hardware run passes as recorded above. Candidate 42
is 6,092,688 bytes, SHA-256
`d75f5fe008b1c9d99e78505a3697b7ebda2bf223be7214957a2e6b37db7b60b5`;
application readback, protected ranges and reset pass. Its P4 WSS and AOQ
without-probe builds also pass.

Candidate 43 extends only confirmed local AOQ interruption replay to at most
three seconds, bounded by the beginning of playback muting and the existing
six-second AEC ring. Provider-only cancellation retains its narrower policy;
the WSS replay path is unchanged. Host tests cover the observed late confirmation,
short playback, the cap and unsigned cursor wrap. The adapter also reserves
capacity for early audio across a response boundary while older PCM remains
queued; a host test keeps Data progressing while that audio is backpressured.
These code checks alone do not pass the acoustic interruption gate.

Candidate 43 application readback and protected checks pass: 6,092,688 bytes,
SHA-256 `6d666a5f82c634ee7e4453d161d825b2e7f73067e55418b117e41b012a80beee`.
Its P4 WSS, AOQ without diagnostics and fresh C3 builds pass. C3 is byte-identical
to candidate 41's C3 image, with no AOQ/front-end compilation or symbols;
C3 hardware validation remains NOT RUN.

The second long-answer failure occurs before the owner's capacity is reached.
Review found its receive loop yields after every PCM object, even after the
speaker starts. Candidate 44 keeps the immediate first-playback handoff but
then drains native messages within the existing eight-ms/24-object budget.
The 45-second PCM reservoir and WSS receive policy stay unchanged. Numeric
peer-reset codes are also retained in the error-only diagnostic. Candidate 44
readback/protected checks pass: 6,092,688 bytes, SHA-256
`fa5840b25fe68d327748b39173ea340a0cf56ec737138341835de53e40e68046`.
Its fixed standalone ASR and multipart TTS pass (551,520 of 551,520 samples
rendered). Acoustic validation has **not** passed; neither a larger queue nor
ignored resets qualify as successful completion.

After the user adjusted the headset and then turned off the reported noise
source/monitoring, fresh original-source batches still lose the latter clause.
The corresponding long-answer batch still falsely interrupts; an eventual
answer also repeats `[用户] 讲一个300字的故事。`. Preserve both failures.
Changing the source or clearing history does not close them.

`probe44-noiseoff-echo` disables the PC room recorder and captures only board
diagnostics. With no arranged speech after the story request, local interruption
occurs 1,294 ms after speaker start. All 128,000 raw samples and accepted samples
export successfully with zero **diagnostic** skips, but 8 seconds of capture
spans 8,831 ms of near-detector timestamps (maximum gap 449 ms). Diagnostic skips
do not measure I2S DMA loss. This run contains 17 clipped AEC samples, though the
raw microphone does not clip. Extending reference correlation from 96 ms to
320 ms offline does not identify a delayed echo match for the triggering frame;
that is insufficient evidence to extend the detector window.

Candidate 45 removes repeated invariant energy sums and software-double
arithmetic from the P4 correlation loop, retaining the same lag range, gates,
vote counts and 600 ms cloud endpoint. Sixty-four products of samples shifted
right by three fit signed 32-bit accumulators; cross-energy products use float.
Host checks compare decisions against a wide-precision oracle, including
delayed/reference-only echo, random mixtures and full-scale signed extremes.
The capture owner now exposes processing/near-detector maximum time, frames
exceeding 32 ms and actual RX queue overflow events. These counters are lifetime
values; compare before/after snapshots for each test. Acoustic acceptance is
still required, including tests with diagnostics disabled.

Candidate 45 is installed/read back with protected areas verified: 6,092,688
bytes, SHA-256 `7e2e9c11fe28e1afde72badc66d52dfd6e3afe550ea0919524e8ea539048173a`.
`probe45-original` records 6 seconds in a 5,985 ms trace span. Near-detector
maximum at that point is 1,543 us; no RX overflow is observed. Its dialogue
starts before the scheduled source, so it is not a clean recognition trial.
The user subsequently confirms another human-voice playback source and pauses
it. The smoke harness now rejects any cloud speech/turn/interrupt before its
scheduled test source instead of testing inside an already-started turn.

After that confirmation, the original soft source fails in three fresh
configurations: gain-only omits the last character of `名字`, noise-only loses
the latter clause, and noise plus gain ends at `请只重复它`. Keep these failures
separate from the one passing louder calibrated source. The default remains
gain-only, 600 ms endpoint and threshold 0.5.

`generic-speech45` passes standalone fixed-input ASR and all 551,520 samples of
multipart TTS, with no silent WSS fallback. This is not human-listening or
microphone acceptance. Later lifetime capture counters include four processing
frames over 32 ms (maximum 42,769 us in the AEC path), while near-detector maximum
is 1,560 us and hardware RX overflow remains zero. Do not report all capture
work as permanently below 32 ms.

Candidate 46 adds an **optional**, disabled-by-default 180 Hz high-pass in the
AOQ uplink conditioner (diagnostic profile bit 2; profile 5 means high-pass plus
gain). Quiet board PCM has about 76% of its energy below 250 Hz; offline filtering
reduces its RMS from 452 to 312. Host tests cover DC rejection, the cutoff,
speech-band gain, signed full-scale input and recovery to silence. These are
filter checks, not evidence of improved cloud transcription. The first physical
high-pass comparison still omits the final character of `名字`, and the
volume-80 interruption comparison does not confirm local speech. The optional
filter has not demonstrated an improvement and remains off by default. C3 and
WSS do not compile this conditioner change.

Candidate-46 follow-up evidence, after the user paused other human playback:

| Run | Result and limitation |
| --- | --- |
| `probe46-quiet-original` | FAIL: the original soft fixture is transcribed as `我们的小猫叫蓝莓，请只重复它的名。`. All 300 accepted-PCM frames are captured without diagnostic skips, with contiguous 20 ms capture timestamps. Raw, AEC and accepted PCM retain a recognizable latter clause in independent offline ASR. That does not establish cloud delivery or prove a cloud-model defect. |
| `batch46-original-endpoint800` | FAIL: extending the endpoint from 600 to 800 ms still loses the latter clause (`请指。`). A requested 1200 ms control was rejected before playback because the existing runtime permits only 200–800 ms. |
| `batch46-original-threshold100` | FAIL: lowering the cloud VAD threshold to 0.1 still omits the last character of `名字`; the default remains 0.5 / 600 ms. |
| `batch46-calibrated10` | PASS, 10/10 complete physical questions, correct replies and full playback, using the separately hashed, levelled source. The frozen candidate-46 binary passes the final hash check. This does not replace the failing original soft-source regression. |
| `probe46-barge80` | FAIL: no local interruption; 7 eligible frames, at most 5 votes and 1 strong frame. The raw recording is dominated by the device's story. AEC offline ASR does not recover the complete test question. Raw capture is complete, but 3 accepted-PCM diagnostic copies were skipped, so the diagnostic report also fails. RX overflow remains zero. |
| `batch46-late-barge80` | FAIL: delaying the test interruption until eight seconds into playback produces a local cut about 940 ms after the source starts, but the new connection produces no speech-start/transcript or twelve answer. Reconnection readiness takes about 1.48 s. A repeated diagnostic at the same delay does not confirm a local cut. Neither early nor late interruption is accepted. |

The interval above is host source scheduling to observed UART event, not a
calibrated mouth-to-speaker measurement. No threshold, filter or source change
has closed the original soft-input or full-duplex interruption regressions.

Further isolation preserves two separate failures. `context46-five-pairs`
stops on the first compound phrase, before any recall check: the provider
cancels the reply with `turn_detected`, and audio does not finish. In
`probe46-muted-handoff-control`, volume is explicitly zero to remove speaker
echo; local interruption succeeds, but the new cloud transcript is only `停。`.
This muted control is not full-duplex acoustic acceptance.

Candidate 47 adds a probe-only `agent aoq catchup 5|10|20` control for replay
after connection rotation. Initial catchup and the default remain 5 ms per
20 ms frame. At 20 ms, `probe47-muted-paced20` transcribes the entire math
question, but answers `7+8=15` instead of twelve. It remains FAIL. Capture age
grows to 4.62 seconds and hardware RX overflows rise from zero to 28 during
that run. Slower replay is not accepted as a latency or correctness fix.

Candidate 48 corrects a distinct recovery anchor: a valid cloud `audio_end_ms`
is mapped to the accepted input timeline of that native connection, including
its nonzero rewind origin. Previously the current capture cursor at event
receipt could be about 1.9 seconds past the boundary and omit the next clause.
A mapped boundary also takes precedence over cue muting, which may begin after
that clause was already captured. Out-of-range, fractional and nonfinite clocks
retain the old receipt-time fallback. Recovery remains bounded; WSS is unchanged.
Host sanitizer tests cover the recorded boundary, replay origins, counter wrap,
invalid clocks and the cue precedence case. AOQ with/without probes and WSS
builds pass. The 6,093,504-byte P4 application is installed and read back as
`31d37e48856401f3275a20f536a4c8f37fddf853febb4d41a17e465d316c3b28`, with
protected areas verified. Physical compound-phrase acceptance failed:
`context48-five-pairs` did not start because cold-boot SNTP was not ready within
45 seconds (it later recovered without a host time override);
`context48-five-pairs02` did not start its question because wake was followed by
an empty transcript and unsolicited greeting. A separate wake-only observation
did not reproduce this, but its diagnostic export skipped one accepted-PCM
copy, so its overall report stays FAIL.

`context48-five-pairs03` reaches the original compound question but still fails
to retain the new code. Its mapped provider-cancel recovery recovers the latter
clause `只重复暗号`, while the separate cloud-speech interruption branch still
uses a receipt-time 750 ms fallback and loses the earlier words. A supplemental
natural-TTS source also fails; changing the source is not a fix. The observed
unrelated transcripts are not attributed to room playback without matching
physical recordings. Candidate 49 applies the validated input-clock mapping to
cloud speech-start as well, retaining 100 ms pre-roll within that connection and
a maximum three-second window. The original unmapped fallback remains. Host
tests include the recorded cursor values; AOQ with/without probes and WSS build.
Candidate 49 is not yet accepted by physical repetition.

Candidate 49 is installed/read back as 6,093,920 bytes, SHA-256
`681d5a91cede32844c19316e8b4edad2ebb7e94375a7d8433571f2b3b4638765`, with
protected Flash verified. `context49-five-pairs` passes the first green-umbrella
set/recall pair, then fails the yellow-kite recall: the entire new set phrase and
the immediate yellow-kite reply are correct, but the next recall returns green
umbrella. No new connection occurs between those turns. The original question
and test order are unchanged. A read-only inspection of the prior candidate-48
journal also finds the interrupted green-umbrella user turn persisted, so that
failure is not simply missing durable storage. C3 rebuild/isolation passes with
the same application hash as candidate 47; C3 hardware remains untested.

Candidate 50 removes recent audio-conversation turns from the fixed system
instructions and restores them as bounded user/assistant items using the
[documented Qwen Audio context protocol](https://help.aliyun.com/zh/model-studio/fun-audiochat-client-events).
Saved memory and summary remain labelled data. The latest eight eligible text
messages are bounded to 6500 serialized bytes; no historical function calls are
replayed and no response is requested during restoration. Native audio admission
waits for matching item-created acknowledgements, with duplicate/stale IDs,
role mismatches and timeout handled explicitly. Unexpected inference during
restoration fails before tools can execute. The existing WSS and Omni history
paths remain. Host tests cover acknowledgement isolation and another observed
internal marker. Candidate 50 is installed/read back as 6,096,192 bytes,
SHA-256 `4d482808ce950816583cf1eb28226a9fbf1025101218adaa3d158525c0bef2b7`;
protected Flash and reset pass. AOQ with/without probes and WSS builds pass.
`context50-five-pairs` passes the original five set/recall pairs in 98.02 seconds:
all ten complete input phrases, current answers and completed playback are
verified, without interruption or playback/deadline failures. Eight restored
items are acknowledged in 103 ms before input is admitted. This is fixed
headphone-to-board microphone evidence, not human, soft-input or barge-in
acceptance. The earlier failed reports remain unchanged. Fresh physical readback
in `preflight51/posttest50-binding.json` independently binds these reports to
candidate 50 without changing any pass/failure statuses. File-end to observed
DAC-start in this ten-turn run ranges from 623 to 2032 ms (median 745 ms);
that host/UART measurement is not human end-of-speech or a WSS comparison.

`batch50-barge80` fails its first case: local interruption at about 2.83 seconds
after the source onset stops the story, but the reconnected input is only
“停一下。”; the math answer and subsequent follow-up are not accepted.
`probe50-barge80` separately fails with no local confirmation. Its complete
eight-second raw capture has at most five votes and two strong votes within
the confirmation window; all captured accepted PCM is zero under the reply
echo gate. Three accepted-PCM diagnostic copies are skipped, so its diagnostic
completeness check also fails. Offline AEC transcription retains only an
imperfect math phrase; raw microphone transcription is dominated by the story.
These are distinct runs, not proof that the cloud received a complete question.
The supplemental natural-TTS interruption case is NOT RUN: its preceding,
unchanged story request loses the final word and the guard stops the test.

Candidate 51 adds `agent audio probe linear-start 1000..8000` for a finite
four-channel diagnostic (`mic,dac_reference,aec_scaled,linear_scaled`). It uses
the documented linear-then-NLP sequence only while that diagnostic is active,
retaining the final NLP output for delivery and the original near-speech
thresholds. Ordinary three-channel `start` and inactive operation retain
`aec_process`. A diagnostic started mid-frame reports a skipped raw copy
instead of substituting an old linear residual. AOQ with/without probes and
WSS builds pass; this is isolation instrumentation, not a barge-in fix.
Candidate 51 application readback/protected verification/reset pass at
6,097,296 bytes, SHA-256
`9d0ebfbbae3793ebd984f347f0c59199e88c8f942622af6879f1386145f02679`.

`probe51-linear-barge80` has a complete six-second four-channel raw capture,
zero raw skips/clipping/RX overflows, but no local confirmation and four skipped
accepted-PCM diagnostic copies; the overall result remains FAIL. In the same
recording, offline transcription of the linear residual is “停一下,7加5等於多少”,
while the final NLP output becomes “年底下 新加坡等于多少”. Raw microphone ASR
mostly repeats the device's story. Linear/NLP RMS is approximately 778/380;
offline ASR is diagnostic evidence, not proof of cloud delivery or human quality.

Candidate 52 selects the linear residual for native AOQ capture/near detection
while the conversation monitor is active. It still runs NLP to maintain its
state, and retains the existing energy, sustained-vote and correlation gates.
Inactive wake and WSS select the previous NLP output. The board component's
`AGENT_P4_AOQ_DUPLEX` definition is added only in an AOQ build; C3 does not compile
this board source. Diagnostic near-frame flag 1024 records linear selection;
ordinary three-channel probes capture the delivered signal, while four-channel
probes keep separate NLP and linear outputs. Physical interruption and long
playback-without-input regressions are required before accepting this change.

Candidate 52 is installed/read back at 6,097,296 bytes, SHA-256
`079e6c4251896498ac1bc1def1fcd6eabd70ea795d1d6b377503c511eea5a6f9`;
protected Flash/reset and the three P4 builds pass. Compile commands confirm
linear capture is selected only in the two AOQ configurations, not WSS.
`batch52-barge80` fails its first case with no local confirmation; no later
cases are counted. A counterfactual energy-only analysis of candidate 51's
recorded linear PCM cannot reach six votes under the old DAC/raw-microphone
ratio constraints, even before VAD/correlation. It is an upper bound, not
an on-chip detector replay.

Candidate 53 changes only the native linear duplex energy policy: retain the
absolute mean-PCM floor of 620, require two frames above 900, and retain the
six-of-twelve voice votes and both original correlation vetoes. The former
DAC/raw-microphone envelope ratios still apply to the NLP/WSS path. Those
ratios compared the cancelled near signal with removed speaker energy and
rejected the captured intelligible phrase. This targeted policy change still
requires actual barge-in and echo-only repetition; it is not yet accepted.
Candidate 53 application readback/protected verification/reset pass at
6,097,680 bytes, SHA-256
`cbe63c3d87f554ee6fc65dc1d5af3272211dc2530024a09507fcc8917b28547b`.
AOQ with/without probes and WSS builds pass. Hardware acceptance remains pending.

Candidate 53 fails before the deliberate source: `batch53-barge80` reports
“Story too short” because local VAD cuts the device's own story during the
pre-source wait. `probe53-echo80` independently reproduces that false cut with
no interruption source, complete raw/accepted captures and no diagnostic skips.
The subsequent cloud transcript “们。” is not a user input. Do not describe
either run as a successful fast interruption.

Candidate 54 restores the original strong-frame condition (output above
`200 + raw_envelope * scale / 2`), keeping the linear path's absolute energy
floor and original six-vote/correlation requirements. Re-evaluating the 61
recorded decision frames up to candidate 53's false cut gives zero strong
frames/confirmations under that condition. The captured legitimate linear
phrase can satisfy the stronger condition; its full on-chip acceptance is
still required. This limited counterfactual ends at the original cut, since
speaker behavior after that boundary would differ. The three P4 builds pass.
Candidate 54 application readback/protected verification/reset pass at
6,097,296 bytes, SHA-256
`d3901014ca5fc31064a92d960ed042e91a02640e0459b4f2ae2d182cc877f7ff`.

Candidate 54 `batch54-echo80-one` passes one 71.235-second physical playback
with no interruption; this is not the ten-case gate. `batch54-barge80-one`
fails: playback stops, but the new transcript is “提问一。” and no twelve
answer follows. `probe54-barge80-paced5` independently fails with no new user
transcript after the handoff. Its eight-second raw and accepted-PCM captures
are complete, with no diagnostic skips or I2S RX overflow. The linear AEC
recording and the waveform-matched phrase in accepted PCM both transcribe
offline as “停一下，7 加 5 等於多少”; whole-file transcription of the accepted
capture is misleading because it also contains muted frames and replayed
pre-onset echo. The phrase has not disappeared from the accepted buffer.
Acceptance is not a cloud delivery acknowledgment. Maximum capture-to-accept
lag reaches 4.674 seconds during rewind/reconnection, and the original
five-millisecond catch-up failure remains a failure.

The separate `probe54-barge80-paced10` comparison passes its narrow probe
checks: the cloud transcribes the full question and includes twelve in the
answer. It does **not** pass complete interruption acceptance: the model then
continues the old story. It has complete raw/accepted diagnostics with no copy
skips. File-end to first DAC observation is about 5.016 seconds, not a speed
acceptance result. Ten-millisecond catch-up is a candidate setting, and the
five-millisecond failure remains separate.

Candidate 55 caps recovered-PCM catch-up at twice real time (10 ms per 20 ms
frame), retaining 20 ms pacing at the live edge. An interrupted journal reply
previously lost its marker while its old user request was reinserted as a live
conversation item. Native audio now retains that request only as labelled
historical data and instructs the model not to resume cancelled speech without
an explicit request. User quotations are not classified as interruption
metadata. Host history checks pass under ASan/UBSan; all three P4 builds pass.
The candidate requires fresh full interruption/recall and quiet-playback tests.
Candidate 55 is installed with application readback, protected-Flash verification
and reset passing: 6,097,888 bytes, SHA-256
`1e2cce2f45ede9c3df3498b62c566312ba743be0ee9c99cdf0ef87bb721f0de6`.
C3 builds again with no P4/AOQ compile units or symbols, and its application
remains exactly `f78e24731cc4254940588cc0c9b1c5bde0c6ada45bb9eb8e6ded8d47a5395f28`.
This does not constitute C3 hardware acceptance.

`batch55-barge80-ten` fails its first case; the remaining nine are **NOT RUN**.
After local interruption the fixed rewind first produces an empty cloud turn,
then transcribes only “停一下。” and triggers repeated provider cancellations.
The recovery eventually reaches the capture limit. No twelve/thirteen gate
passes. The pacing comparison alone did not establish a stable fix.

Candidate 56 keeps the detection thresholds unchanged and records the first
eligible frame in a confirmed near-speech chain, retaining gaps up to 640 ms.
Local replay uses that onset plus 500 ms pre-roll, with the existing three-second
maximum, playback boundary and stale-clock fallback. This prevents an automatic
full three-second rewind from unnecessarily preceding the actual input with old
speaker echo. On the recorded candidate-54 failure, the boundary removes 1.258
seconds of old prefix while the retained AEC phrase still transcribes completely
offline. This checks retention only; changed playback and cloud behavior require
a new physical run. Onset/clock/history host tests pass under ASan/UBSan, and all
three P4 builds pass. An initial diagnostic-format compile error was corrected
before creating the frozen candidate; no failed build was installed.
Candidate 56 application readback/protected verification/reset pass at
6,098,224 bytes, SHA-256
`c4ad612d125de85417e27fd6e08d95198a24134cf6fef8e01599887176bb0cee`.
Its `batch56-barge80-ten` fails the first case; later cases are NOT RUN.
The onset was 29,678 ms, only 382 ms after speaker start and before the actual
interruption source. The 2,420 ms replay therefore still included device echo;
the cloud transcript was “欢迎新来。” followed by cancellation/capture limit.

Its separate `batch56-normal-ten` passes ten complete cat-name inputs and
answers with full playback, no unplanned interruption, underrun or TX deadline
miss. `batch56-original-soft-one` passes the original unlevelled full sentence
once; this does not erase the earlier soft-input failures or satisfy the full
soft-input gate. In the normal batch, file-end to UART-observed DAC start ranges
from 1,565 to 3,252 ms (median 2,471.5 ms). File-end to observed cloud speech stop
has a 1,917 ms median; cloud stop to DAC has a 322.5 ms median. These are fixed
acoustic/file timing measurements, not calibrated human timing or a WSS
comparison. The remaining endpoint/transport delay is not accepted as fast.

Candidate 57 uses the already-computed NLP signal for local VAD while keeping
linear PCM for capture/replay and the same energy/correlation/strong gates.
The onset chain retains up to 1,200 ms between eligible frames because the
recorded NLP VAD has a 992 ms dropout inside a complete near-end phrase. This
metadata retention does not change the 600 ms cloud endpoint. Re-evaluating the
candidate-51 recording with its actual NLP VAD and linear gates permits a
confirmation at 2,816 ms, with zero confirmations in that recording's post-source
window. It is a limited recorded-data check, not physical acceptance.

Candidate 57 application-only installation, independent readback, protected-area
verification and reset pass: 6,098,352 bytes, SHA-256
`221c3160705021d71651ea30004c0c3e2efcaf8002f5c03cc136a4664b99c6f6`.
All three P4 configurations build and the onset/recovery/history sanitizer tests
pass. The fresh pre-upgrade readback also binds all three candidate-56 reports
to its installed image, without changing their individual results.

Candidate 57 fails `batch57-barge80-ten` before the interruption source starts:
its own story triggers local VAD after 1,144 ms; the later nine cases are NOT
RUN. Capture processing reaches 412,197 microseconds, with ten over-32-ms
frames and 27 I2S RX overflows. `probe57-early-echo80` starts recording before
the story request and independently repeats the false cut with no external
interruption source. Its diagnostic copies have zero skips, but RX overflows
increase from 27 to 70 and over-32-ms frames from ten to 26. Therefore this is
not continuous loss-free capture. The paired mic/reference frames remain useful
for diagnosis; their 250 processing timestamps include roughly 50–80 ms spacing
around the false cut, rather than the required 32 ms. The earlier candidate-56
results are not evidence that candidate 57 meets the capture deadline.

Candidate 58 tests a 128-byte L2 cache line while retaining the 128 KB cache
capacity and existing internal/DMA reserve. It adds separate linear/NLP/frame
timings to the finite diagnostic trace; speech thresholds and PCM selection are
unchanged. The official [P4 ESP-SR benchmark](https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/benchmark/README.html)
uses 128-byte lines with a 256 KB cache. This trial deliberately retains the
smaller capacity because another 128 KB would consume the current DMA margin.
The configuration is a performance hypothesis requiring a new physical result,
not proof that cache behavior caused the failure. Acoustic batches now reject
any increase in I2S RX overflows, in addition to the existing transcript,
response, interruption, underrun and playback checks.

Candidate 58 native build and application-only install pass, including readback,
protected-area verification and reset: 6,098,128 bytes, SHA-256
`809b74a4bb692dcb7dee19847e015f259dd53d1c1c92f7b202f8058321a78052`.
Its frozen sdkconfig confirms a 128 KB L2 cache with 128-byte lines. Independent
pre-upgrade readback binds both failed candidate-57 reports to that image.

All three P4 build configurations pass with the 128-byte cache line. In
`probe58-early-echo80`, the raw 250-frame span is 7,979 ms versus the nominal
7,968 ms, with zero RX overflows or over-32-ms processing frames from before
wake through the window. During playback, full-frame processing has a median
of 14,358 us and maximum of 29,810 us; linear AEC median is 8,711 us and NLP
median is 4,195 us. No false interruption occurs in this short window. This
supports the cache-line performance change in this run, not long-run immunity.
The overall diagnostic remains **FAIL**: the nonblocking accepted-PCM copy
misses seven frames (raw copies miss zero). It is not a loss-free accepted-PCM
recording and cannot establish a complete upload or a long-story acceptance.

`batch58-barge80-ten` passes its first two full acoustic sequences: stop the
story, transcribe the complete stop/seven-plus-five question, answer twelve
without continuing the old story, then answer thirteen to the spoken followup
and finish playback. Its third case fails to detect local interruption; cases
four through ten are NOT RUN. RX overflows remain zero, although occasional
processing frames reach 36,967 us. This is **FAIL** for the ten-case gate.
The separate `probe58-linear-barge80` subsequently reads the complete math
question and answers twelve, but does not reproduce that missed interruption.
It has one raw-copy skip and six accepted-copy skips, so its overall diagnostic
also remains **FAIL**. Neither result replaces the original third-case failure.

`barge-capture58-diagnostic` completes four full interruption/followup sequences.
Its fifth case is not a valid double-talk trial: the provider's response text is
`[生成中]`, and playback ends before the external interruption source starts.
The new question is fully transcribed and answered twelve, with no playback
left to interrupt. The original script error and ten skipped accepted-PCM
diagnostic copies are retained; this case cannot prove a VAD miss.

The separate `barge-capture58b-diagnostic` reproduces a real miss in case two
after one complete passing sequence. Story playback overlaps the entire
external question, RX overflows remain zero, and all 250 raw frames are copied
without skips. Three accepted-PCM diagnostic copies are skipped, so that copy
is not a complete upload record. The local trace reaches ten independent near
speech votes but zero strong votes. The remaining raw-microphone ratio compares
the cancelled voice against a recent envelope dominated by our own speaker.
For example, frames with linear levels 1,299 and 1,337 have NLP levels 1,197 and
1,284, but both fail that raw-envelope condition.

Candidate 59 replaces only native duplex's strong-frame ratio with energy above
620 in the already computed NLP signal. It retains NLP VAD, linear energy,
raw/output correlation, six-of-twelve near votes, two strong votes and the
playback startup guard. WSS retains its prior decision. On the recorded failed
case this would confirm at board time 1,747,882 ms; the candidate-58 pure-playback
record still has no confirmation. Counterfactual replay is not a physical pass.
The known candidate-57 processing-overflow recording still fails, so meeting
the real-time deadline remains a separate requirement. Diagnostic raw and sent
copies now use independent nonblocking locks, and a capture armed mid-frame
accepts a linear frame already computed for native duplex.

Candidate 59 native build is 6,098,256 bytes, SHA-256
`b8a8be2a15e38e6b7581c878770da855225fe5838a3f88c7bf7e0726a209c8b0`.
Application-only installation, readback, protected areas and verified reset
pass. Native AOQ with/without diagnostic entry points and WSS builds pass.
`batch59-barge80-ten` fails its first case before the interruption source:
local VAD stops the story 921 ms after playback starts. Nine cases are NOT RUN.
The capture path reaches 413,414 us with 25 I2S RX overflows. The separate
`probe59-early-echo80` reproduces the false cut and adds 43 RX overflows. Its
250 raw frames span 8,806 ms instead of 7,968 ms; linear-stage playback median
is 30,994 us, NLP median 13,681 us, and combined median 49,521 us. The raw and
accepted diagnostic copies now both have zero skips, but the actual capture
clock is discontinuous. This is not a valid loss-free recording or acceptance.
The earlier cache-line-only improvement is therefore insufficient.

Candidate 60 tests the smaller `AEC_MODE_FD_LOW_COST` working set for native
AOQ, retaining linear output plus NLP and the 128 KB cache. The WSS AEC mode
is unchanged. This follows the [ESP-SR AEC resource guidance](https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/acoustic_echo_cancellation/README.html),
but still requires the board's own timing and acoustic checks. It also fixes a
separate correctness defect: I2S DMA overflows previously affected only a
diagnostic counter. They now invalidate near-speech confirmation and propagate
the missing sample count to the live session, which fails instead of silently
uploading/replaying compacted audio with a false continuous timestamp.
Candidate 60 is 6,098,592 bytes, SHA-256
`37489745043b2c7f9b85f89aadd54fcb9c2d555a2abb45bd327e3651ff57362b`.
`batch60-barge80-ten` fails the first case after a real local interruption;
the session reports capture capacity loss and closes before the twelve answer.
Nine cases are NOT RUN. Capture reaches 330,236 us and 34 RX overflows. The
smaller AEC profile alone is insufficient. Application/protected-area checks
pass, and an independent post-test readback binds this failed run to candidate
60. C3 build/isolation passes (`f891ba8bdaf5c5b096daf018d2eec726ec540a0aac377732e8c13000b1c98a06`,
1,569,184 bytes), without C3 hardware validation.

Candidate 61 adds a probe-only, synthetic offline AEC benchmark on CPU1. Its
6,103,216-byte application is installed, read back and independently read back
after the benchmark as `6500dd839fc91a1948f79543acc138ecd8f68eb004c452383c305ea5e0d69e40`.
All four profiles complete 48 frames without a 32-ms overrun. High-performance
AEC averages 9.36 ms per frame and low-cost AEC 6.92 ms; explicit cache alignment
does not materially change those synthetic results. No microphone, dialogue or
release acceptance is claimed. The measured CPU frequency is **200 MHz**, matching
the existing stability profile in `board/board.c`; the 400-MHz sdkconfig default
does not represent the running application frequency. Keep that stability
profile while investigating the much larger live-processing delays.

Candidate 62 (`9360199134967ae37ff701b13fe4a07c7765665a50c6bb4ed8b265a0e8e59cb6`,
6,105,632 bytes) adds task runtime snapshots in a probe-only profiling build.
The short `profile62-echo80` window has zero RX overflows or false cuts; it is
not a replacement for the failed repeated interruption tests. The network
worker remains at priority **23** throughout the conversation despite being
created at priority 4; capture runs at 20. Network CPU time reaches 58–82% of
one core during the sampled playback intervals. The pinned FreeRTOS kernel
retains inherited priority until all held mutexes are released, and the worker
holds `work_lock` for the whole turn. This is a concrete scheduling defect,
not proof that every earlier delay had the same cause.

Candidate 63 changes that long-lived workspace admission gate to an initially
available binary semaphore only for native AOQ. Exclusive access is retained,
but unrelated short driver/allocator lock inheritance cannot persist solely
because the workspace gate is held. C3 and WSS retain their mutex. Diagnostic
snapshots now include base priority and core affinity. Task priority, capture
deadlines, full interruption, follow-up and playback need new physical checks.

Candidate 63 is independently bound after all tests to SHA-256
`6cc534edcaf66a219e22372b1417c7390c6b6f5765220af99b40b29032a6d4ad`
(6,105,920 bytes). `profile63-echo80` observes network current/base priority 4,
238 playback AEC frames and a maximum capture-processing time of 27,207 us,
with no RX overflow. The two original interruption batches still fail to read
the complete opening phrase. A separate probe passes the math question, and
`barge-capture63-diagnostic` completes five full interruption/follow-up sequences;
its capture command delays the source by about 200 ms relative to the original
batch, so these results do not replace either failure.

`barge-capture63-cold-matched` restores the original source timing after a cold
reset and fails on case 1; cases 2-3 are NOT RUN. Local interruption arrives
about 2.77 s after the source begins, and no answer twelve follows. RX overflow
and diagnostic skips remain zero. Residual echo predates the arranged source
in the onset chain, while sustained near-speech confirmation arrives late.
After three seconds of retained input and native connection setup, the mapped
provider-cancel replay target is about 5.7 seconds behind the producer. The old
six-second ring's one-second reserve rejects it, closing the conversation with
`limit` before the final question audio is uploaded. The exported 106,240
accepted samples and full 128,000 raw samples preserve this actual failure;
offline ASR is only a diagnostic, not evidence of complete cloud delivery.

Candidate 64 expands only native AOQ's session input ring to twelve seconds
(384,000 bytes in PSRAM), retaining the one-second overwrite reserve, capture
discontinuity checks and three-cancellation budget. WSS keeps six seconds;
C3 does not compile this branch. Rewind failures now identify local, provider
or ordinary replay in capacity telemetry. This addresses the observed recovery
capacity defect, not delayed acoustic confirmation or recognition quality;
both require new physical checks against the frozen candidate.

Candidate 64 is independently read back after its trial as
`8e28dcb0aad98e25d4f9bf7837325ebbec7471aaaa6b702591f33e977db53989`
(6,106,000 bytes). Its first cold-start trial is **not a valid interruption
test**: the complete story request is transcribed, but the model returns only
`[生成中]` and finishes about 967 ms of playback before the arranged barge
source. The source is never played. Preserve the attempt error (`Story ended
before capture`) and wrapper diagnostic-incomplete failure (only 52 frames
before cleanup); the replay-window fix remains physically unverified. C3,
WSS and integrated AOQ builds pass. C3's current image is 1,569,184 bytes,
SHA-256 `58387b48a64ac519f2651cca9f3dbf261883a062741955a3c60884a3b9112a8f`,
with no AOQ/P4 sources or symbols; C3 hardware remains NOT RUN.

The private context snapshot contains an earlier exact `[生成中]` assistant
answer. This does not prove it caused the latest model response. Candidate 65
excludes that whole trimmed placeholder from answer history, preserving user
quotations and actual explanations. A user request paired with an invalid
answer is retained as historical data, not restored as an unanswered command.
History and AOQ recovery host tests pass with ASan/UBSan.

Candidate 65 also tests a stationary native-AOQ microphone path: ADC gain
stays at the playback setting of 30 dB, with the existing factor-four post-AEC
compensation throughout cue, speech and silence. Previously each mute/unmute
switched ADC gain between 42 and 30 dB while the echo filter retained its
state. An explicit output-active flag now determines duplex gating; fixed
digital gain must not imply that the speaker is always active. WSS retains
its old gain transitions. This is a bounded acoustic experiment requiring
fresh official-wake, soft-input, interruption and false-cut checks, not an
assertion that gain switching caused every failure. Neither speech thresholds
nor the 600-ms cloud endpoint were reduced.

Candidate 65 (`3ff56d36f584aa65fb26f4704b48637743cde00dc74f08ebaacb011623e2bd34`,
6,106,400 bytes) installs with protected-range verification and full app
readback. `barge-capture65-cold-matched` fails case 1 for incomplete input;
cases 2-3 are NOT RUN. Official wake succeeds, local interruption occurs about
1.80 s after the arranged source begins, the cloud reads `7+5等于多少？` and
plays the answer twelve, but omits the opening stop phrase. The script does
not proceed to the follow-up after this failure. All 128,000 raw and accepted
samples export, with 250 raw frames, 400 sent frames and zero diagnostic skips.
Capture maximum is 23,004 us, with zero 32-ms overruns or I2S RX overflows.
The provider-cancel window failure is not exercised in this run.

`generic-speech65` separately passes fixed native standalone ASR (46,720
generated PCM samples, complete one-plus-one question, `capture_used:false`)
and multipart TTS (547,680/547,680 rendered samples at 24 kHz, play error OK).
This is not microphone recognition or independent audible-text verification.
The old WSS fast mode is rejected in native AOQ rather than silently selected;
cleanup restores natural voice mode. WSS and AOQ without probes compile.

Candidate 66 restores the original `AEC_MODE_FD_HIGH_PERF` filter for a bounded
physical comparison after the workspace-priority fix. Fixed ADC gain, NLP
normal level, all near-speech gates and the cloud endpoint remain unchanged.
The earlier low-cost experiment did not solve capture priority starvation;
new live timing and complete-interruption checks must justify this restoration.

Candidate 66 (`65f56cd5d5bab3233e308da8985aa21913c26b4139b99e336c620fb5fb319361`,
6,106,400 bytes) is independently read back after testing. The first cold
sequence passes: complete stop/seven-plus-five transcript, answer twelve and
complete playback, then thirteen on follow-up with complete playback. Local
confirmation is still about 1.88 s after source start. RX overflow remains
zero, but two capture frames exceed 32 ms (maximum 32,921 us); keep that timing
observation. Case 2 fails before a valid interruption: despite transcribing
the story request, the model invokes the synthetic `slow_lookup` tool. This
test tool deliberately waits thirty seconds, so the story-start deadline is
missed. Case 3 is NOT RUN and the overall report remains FAIL. This identifies
a separate normal-conversation/tool-exposure defect, not another VAD miss.

Candidate 67 removes the synthetic tool and its prompt from normal dialogue.
Only probe builds can explicitly arm it with `agent voice test-tools on`
while voice is stopped; the volatile flag is false after boot. Normal builds
reject enabling it. Status exposes `diagnostic_tools`, and execution also
rejects an unadvertised/stale `slow_lookup` call before it can enter the job
queue. `agent voice test-tools off` returns to normal behavior. Real tutoring,
camera, media and background jobs remain available. Simple arithmetic and
story requests are directed to the conversational response rather than a
background lookup. New physical repeated-story/interruption validation is
required; compile success is not proof of correct model tool selection.

Candidate 67 (`b534ed997f95f80720990211fd470d9eea0e1943ff3fc8fdbc919fae62fb15e6`,
6,107,120 bytes) passes the physical default-off and explicit on/off diagnostic
tool checks. Its first cold interruption still fails complete input; cases 2
and 3 are NOT RUN. The cloud first transcribes `5天1`, cancels that turn, then
transcribes `7+5等于多少？` and plays twelve. The stop prefix is missing. No
synthetic lookup runs, RX overflow and processing overruns remain zero, and
maximum capture processing is 20,941 us. All eight seconds of raw and accepted
PCM diagnostics export without skips. Independent post-test app readback
matches candidate 67. The original report remains FAIL.

Offline recognition of candidate 67's linear diagnostic channel contains the
complete stop/math request mixed with residual speech; its NLP channel produces
unrelated recognition. Candidate 65 showed the opposite advantage for NLP.
These inconsistent offline results are not delivery evidence. Candidate 68 is
a bounded physical comparison using NLP PCM for upload while retaining the same
linear/NLP local detector, fixed ADC, output volume, cloud endpoint and source
timing. Do not infer better recognition from suppression alone; ordinary and
soft input must also be checked, and the earlier NLP failures remain recorded.

Candidate 68 (`3f9710a0a09c0818cc39a63183b9e2a42d8b341bc565e04c991811b27498b632`,
6,107,120 bytes) is installed with protected-region verification and exact app
readback. At cloud threshold 0.5 its cold interruption fails: local stop is
about 2.71 s after source onset, a fresh session becomes ready, but no new
speech-start or twelve answer arrives. The raw and sent eight-second exports
have no skipped frames; RX overflow is zero, with one 33,629 us processing
frame. The different generated story prevents treating this as a controlled
comparison of identical acoustic mixtures, but the acceptance case still fails.

The same image passes 10/10 levelled ordinary questions at threshold 0.5.
File-end to observed DAC-start median is 3,057.5 ms (2,471–3,252 ms), with
2,237 ms median to observed cloud speech-stop. Original, unlevelled input
passes seven cases then loses the final character in `名字` on case 8; cases
9–10 are NOT RUN. With only cloud threshold changed to 0.3, a new ten-case
original-input batch passes 10/10, median 2,096 ms (1,097–2,643 ms) to observed
DAC-start. The original failed batch stays FAIL. These are fixed acoustic,
UART-observed measurements, not calibrated human latency or a WSS comparison.
They do not yet establish that 0.3 is safe for interruption and idle noise.
The audio endpoint remains 600 ms and the local double-talk detector is unchanged.

At threshold 0.3, candidate 68's warm 10-ms replay comparison passes two full
stop/math/twelve/thirteen cases, then fails case 3 because stop and math are
transcribed as separate turns. A 5-ms replay comparison fails case 1: after
`停一下`, recovery transcribes only `多少？`, yet answers twelve from available
context. The correct answer therefore cannot prove intact input. Lowering
master playback from 80 to 40 also fails the strict single-turn input gate;
no default-volume change is justified by that trial. All original failures
and exports remain intact, with a matching independent post-test app readback.

The 5-ms failure identifies an additional deterministic truncation defect.
At provider cancellation, the reader cursor is 288736 and the validated cloud
endpoint is 236576 (3.26 seconds earlier). `p4_aoq_cancel_replay` clamps even a
mapped endpoint to 35,200 samples (2.2 seconds), choosing 253536 and discarding
16,960 samples (1.06 seconds), including the beginning of the math clause.
Candidate 69 returns a validated mapped endpoint unchanged. The actual ring's
capacity, one-second overwrite reserve and discontinuity check still apply;
expired audio fails explicitly instead of silently becoming partial speech.
Unmapped/cue fallbacks and the three-cancellation budget retain their bounds.
The captured numbers reproduce an assertion failure before this change and
pass after it, including cursor-wrap coverage. New physical results are required.

Candidate 69 (`c14ffe29be9b5f6dfa63c2b4448914a91b9d95d536ceca23a44a9b243b2c5dc5`)
first fails before audio because autonomous SNTP has not synchronized. The
follow-up acoustic run explicitly records a temporary serial UTC setting,
checked against host/clock-tool UTC; it is not an autonomous-clock pass. That
run fails the first math-input gate: local cut takes 1,815 ms, the cloud input
is empty, yet it answers twelve from available context. No provider cancellation
occurs, so this case does not exercise the mapped-endpoint repair. Capture has
zero RX overflows, zero frames over 32 ms, and 27,135 us maximum processing.
Raw and sent diagnostic exports are complete. An independent post-test app
readback matches candidate 69 and preserves both failures.

Candidate 70 (`adf700e32e7bc782b8675b4250abbb51c3a78aa5cd1d4bc80d853a4bef6ac064`,
6,106,992 bytes) retains that repair and restores the linear native uplink.
The NLP signal still supplies local VAD and strong-speech evidence. Local
thresholds, onset retention, ADC gain, 640-ms AEC tail and WSS path are unchanged.
Installation, protected ranges and application readback pass; acoustic results
remain a separate gate. The original 600-ms/threshold-0.3/catchup-5/volume-80
case is repeated, with no source timing, wording or gain change.

Candidate 70's matched cold case fails to trigger local interruption. Capture
has zero RX overflow/over-32-ms frames and 26,873 us maximum processing. The
offline linear signal contains the math phrase; NLP transcription is empty.
These are separate ASR diagnostics, not ground truth or a successful cloud turn.
Before the story begins, six model responses occur without existing per-tool
telemetry. That is evidence of extra rounds, not proof of any particular tool.

Candidate 71 (`7d9e38f720054430a34d74106000c5aa9d43428b50224e292ac13e21a8d23163`,
6,107,296 bytes) adds AOQ-only tool-name and response-completion notices, with
no arguments/context payload logged and no audio-policy change. Installation
and protected readback pass. Its 76,427-ms long-playback case has no false cut
but FAILS with 15 underruns, seven TX deadlines and maximum 624-ms submission
gap. Capture itself has zero RX overflow and maximum 31,437 us processing.
Three subsequent start-only diagnostics have no tools and one response each,
with endpoint-to-speaker times of 387, 412 and 1139 ms. They intentionally stop
playback after four seconds and do not qualify full playback or speed acceptance.

A separately generated Qianwen natural-source comparison keeps the requested
phrases and peak ceiling, with static gain/resampling only. Source ASR verifies
the words independently. It does not replace the original SAPI regression.
At 5-ms replay, candidate 71 recognizes only stop and misses the math answer.
At real-time 20-ms replay, it recognizes stop and the full math clause in two
turns, then answers twelve. The strict single-utterance gate still fails;
follow-up thirteen is not reached. Reconnect input lag grows to about 6.25 s,
so real-time replay is not accepted as the default latency solution.

Candidate 72 (`5c0c52c94ab7f167cc9589706008d855b4da5a64f5a4abca770f56735b1720fe`,
6,107,296 bytes) changes only native local VAD mode 3 to mode 2; WSS remains
mode 3 and the energy, correlation and strong-vote gates are unchanged.
At endpoint 600 ms, cloud threshold 0.1, replay 10 ms and volume 80, the natural
source passes all three complete interruption/math/follow-up cases. Local cut
times are 1800, 1815 and 1737 ms, with zero RX overflow, stream underruns and
TX deadline misses. This is three warm acoustic cases, not general acceptance.
The original SAPI source at the same settings fails its first case: stop and
the complete math are separate turns, twelve plays, and the follow-up has no
cloud speech event. The remaining planned cases are NOT RUN. Its 0.3/5-ms
comparison also fails its first case, with no math answer after a local cut.

The follow-up capture attempt does not reach the intended capture: the complete
story request receives the literal answer `[工具调用]`, without a tool-call
event, and playback ends before interruption. Preserve this additional failure.
A separate quiet math-to-follow-up diagnostic recognizes the complete original
SAPI phrases and answers twelve/thirteen. Its eight-second raw/accepted-PCM
capture has no diagnostic skips. This does not replace the failed barge sequence.
The new history regression rejects the exact assistant placeholder `[工具调用]`
while retaining user quotations and explanations; it is RED before the filter
and GREEN with it under host ASan/UBSan. This source change is not installed in
candidate 72 and is not evidence that the original model output is fixed.

Candidate 72's later matched original-source capture recognizes the complete
stop/math phrase in one turn and completes twelve/thirteen. It is one diagnostic
PASS, not a replacement for the original failed batches. The long timing
trace FAILS: playback stalls for 5362 ms, accumulates seven underruns and three
TX deadlines, then the server resets unfinished Audio stream 7 with code 268.
The application rejects the truncated response. Do not treat reset as EOF.
Independent post-test readback binds all seven candidate-72 reports, preserving
their original PASS/FAIL and diagnostic scopes.

Candidate 73 (`829ba3830644ce03f8e8381576280d0dfa8ae1334c3168d24dbaf45a53a597ee`,
6,110,480 bytes) adds one unused AOQ allocation prefetched on P4 while a native
conversation is ready. A single bounded HTTP worker makes no media connection.
An allocation is taken at most once, only for its model kind, with sufficient
remaining setup lifetime and age at most 60 seconds. Ending the conversation
wipes the ready allocation and invalidates a late in-flight result. Standalone
ASR/TTS and setup diagnostics retain fresh synchronous allocation. Failed or
unavailable prefetch falls back to the existing foreground allocation, without
an automatic background retry. Host ownership/cancel/expiry tests and both
prefetched/fallback Session paths pass ASan/UBSan.

Three P4 setup-only measurements before this change spend 739--780 ms in HTTPS
allocation, with 1204--1276 ms total setup. Candidate 73 uses prefetched
allocations in two physical handoffs: allocation is 0 ms and total native
setup is 477/470 ms. The first original-source acoustic case nevertheless FAILS:
only fragments are recognized and the answer is thirteen instead of twelve.
A separate recorded case recognizes stop and the entire math in separate turns,
then answers twelve and thirteen; its strict whole-input gate remains FAIL.
These results prove the reduced setup wait, not complete end-to-end improvement.
The natural-source three-case batch passes its first two complete interruption
and follow-up cases, then FAILS when local VAD cuts its own twelve-answer playback
without a scheduled new input. The two passing cases measure 3580/4846 ms from
source-file end to observed DAC start; successful cases alone do not establish
a consistent overall latency improvement. Independent post-test readback binds
all three candidate-73 reports without changing their original FAIL results.
WSS and non-probe AOQ builds pass. C3 build/isolation pass with unchanged image
`a5039f4addc2a327fe786a84adf58a6b77002370451a5948274752be694ea3ba`
(1,569,184 bytes); C3 hardware was not exercised here.

A further source correction feeds contiguous 480-sample VAD frames from the
512-sample AEC blocks. The former direct 30-ms VAD call discarded each block's
last 32 samples. Native AOQ now retains that fractional frame; WSS remains on
its previous path. A host regression verifies all 7680 samples across fifteen
AEC blocks become sixteen VAD frames, without gaps or repetitions. This fix
is installed as candidate 74 (6,110,608 bytes,
`dff59760a5a9135dc79f78c5d0d32628d535e0c322ca72eaee453f64a2885da4`).
App readback, protected Flash verification and reset pass; acoustic qualification
is in progress. This does not establish that false cuts or incomplete input
are resolved.

Candidate 74's fresh physical batches use threshold 0.1, endpoint 600 ms,
replay pacing 10 ms, frontend 1/18 and volume 80. Ordinary input passes 10/10;
both original SAPI and natural-source barge sequences pass 3/3 with complete
input, twelve, thirteen and full playback. All completed cases have zero RX
overflow, stream underrun and TX deadline misses. Original-source cut delay is
878/925/894 ms; natural-source delay is 612/1878/1769 ms. Ordinary source-file
end to observed DAC is median 2057.5 ms, range 1080--3096 ms. These are fixture
and UART timings, not human acceptance or a WSS comparison.

The long batch FAILS case one: the model promises a story and ends after
4.25 seconds without telling it. Its remaining two cases are NOT RUN. The
20-case soft-input batch passes two, then FAILS: the full question is transcribed,
but no Audio arrives; after about 13 seconds response.done is cancelled with
turn_detected. The mapped replay is older than the protected ring retention,
so the application closes rather than claiming missing samples are present.
The remaining seventeen cases are NOT RUN. A subsequent eight-second capture
attempt is rejected before the source/capture starts due to unscheduled cloud
input. A separate four-second board-only ambient capture has zero DAC reference
and no raw skips; it is diagnostic evidence, not a passing voice turn.

All 113 host cases pass individually: the full run initially has 112 PASS and
one environment failure (nested test cannot find cmake); adding the bundled
cmake directory to the child PATH makes that single test pass. Preserve the
original failed run. Candidate 75 adds an AOQ-only prompt instruction to
deliver requested long content in the current answer and disables development
FreeRTOS runtime statistics. It does not change the default VAD threshold.
It is installed with app readback, protected Flash verification and reset PASS:
6,108,256 bytes, SHA-256
`c761e87962c2c364b18fd8a04ef572cc56007ba2f735db0d7250640c36b16d3b`.
Its first 0.1-threshold long batch aborts before the planned source because of
unscheduled cloud speech; no long-answer case has run in that batch.
With the startup-default 0.5 threshold, the first story fully plays for
48,714 ms without a false cut, RX overflow, underrun or TX deadline miss.
The second story FAILS: local_vad interrupts at 11,840 ms after DAC start,
without a scheduled interruption source; the third case is NOT RUN. Do not
attribute this local cut to a cloud VAD parameter. A separate delayed eight-
second raw capture covers the comparable interval without reproducing the
cut; it is a diagnostic window, not a replacement long-playback PASS.
Both candidate-75 WSS and native AOQ without diagnostic entry points build
successfully. C3 code is unchanged from its last successful isolated build.
Candidate 75 also FAILS the original soft fixture at both 0.5 and 0.3:
the first case in each batch is incomplete. Each planned 20-case batch stops
there; the remaining nineteen cases are NOT RUN. All five candidate-75 reports
are bound to its image by a fresh post-test Flash readback. The eight-second
diagnostic PASS does not override any failed batch.

Saved candidate-53 false-cut audio exposes a separate correlation defect:
the 8-sample lag grid misses a microphone echo peak at lag 41 (squared
correlation 0.5598 versus coarse maximum 0.4934). Native AOQ now refines the
three strongest coarse peaks on each physical channel by +/-7 samples,
bounded to 84 additional probes, retaining the 0.50/0.25 decision thresholds.
WSS retains the original coarse algorithm. A deterministic 35-sample delayed
signal regression fails before and passes after the change. Six relevant
ASan/UBSan host tests pass; an initial WSL shell-regex invocation fails before
running tests and its log is retained separately. Counterfactual replay removes
candidate 53's confirmation and preserves a recorded candidate-69 real-speech
confirmation. It does not remove candidate 59's separate false confirmation,
and is not physical acceptance. Candidate 76 is built for a new hardware run:
6,108,656 bytes, SHA-256
`6354a3bac203b2648b5705a15da6cd85120458a7ccc3056d1aa6e572aa394f6d`.
Its app-only installation, protected-area verification, app/partition/assets
readbacks and hard reset PASS. WSS and AOQ without diagnostic entry points
also build successfully. Its fresh fixed TTS/ASR/dialogue/synthetic-video chain
passes, as do real-photo reading and a separate same-photo selection, solve,
rendered-step and exact scroll-position restore test. These do not establish
spoken tool selection or real-camera AOQ conversation. Acoustic qualification
does not pass: the original barge source loses its opening words, and other
runs fail before their planned source starts. Gain-only reduction does not
remove the missing final syllable. Mild noise suppression passes three ordinary
turns but splits the interruption into separate user turns, so it is not made
the default. Full results and preserved NOT RUN boundaries are in
[the candidate-76 validation record](P4_AOQ_VALIDATION_20261108.md).
An independent post-test Flash readback matches candidate 76 byte-for-byte
and binds 21 original reports without altering their results. All 190 recorded
production build inputs still match. Image identity passes; qualification
remains false. No Git commit or push was made.

An independent post-test Flash readback binds nine candidate-74 reports to
its immutable image, including the failed batches. Its separate native fixed
chain passes standalone TTS (50,880 samples), ASR (complete fixed sentence),
dialogue playback (19,200 samples), red-square video (67,200 audio samples)
and blue-circle video (65,280 samples). Both video cases use four generated
JPEG frames, not the camera. This establishes native transport for those
fixtures only; it does not pass microphone, interruption or real-camera
acceptance. The board-only ambient recording is diagnostic, not a voice PASS.

The separate candidate-68 fixed-PCM latency diagnostic is FAIL at
`audio_pacing_stall`, after 179 datagrams. It did transcribe the complete fixed
question and receive `收到`, but did not finish its paced input/drain policy.
The first PCM was 5,251 ms after start, configuration ready at 1,638 ms, with
46,720 input samples plus 100 ms leading silence. This partial timing observation
does not establish an integrated microphone speed improvement or prove the
hypothesis that initial buffered audio accounts for persistent cloud delay.

The official [Qwen-Audio AOQ guide](https://help.aliyun.com/zh/model-studio/audio-realtime-aoq-access)
keeps conversation audio on the Audio track. The separate
[ASR buffering example](https://help.aliyun.com/zh/model-studio/voice-input-using-aoq-access-qwen-audio-asr)
uses Data append for its Inference model; it is not evidence that conversation
audio can switch to that path. Neither guide establishes successful delivery
of this implementation's accelerated media replay. Keep actual cloud transcript
and response checks as the gate.

`batch45-quiet-echo80-10` finishes all ten physical cases at volume 80 with
61.5–64.2 seconds of playback each. Every case reports zero interruptions,
underruns and TX deadline misses; no peer reset occurs. RX overflow stays zero.
Capture processing does exceed 32 ms on 27 additional frames (lifetime maximum
43,456 us); near-detector maximum reaches 2,751 us. The test wrapper overall is
**FAIL** because a parallel candidate-46 build overwrote its referenced local
binary before the final file-hash check. Do not rewrite that report into PASS.
The initial readback is bound to candidate 45, and no flash/reset occurred in
the run. A fresh post-test physical readback matches candidate 45 exactly;
`preflight46/posttest45-binding.json` binds the original report hash to that
readback without changing its wrapper failure.
Subsequent smoke runs use a frozen build directory, not the active build output.

The cue/queued-input boundaries, cancellation bound and 32-bit clock wrap pass
ASan/UBSan host tests, together with history filtering and cancellation recovery.
Candidate 38 builds in AOQ with/without diagnostics and P4 WSS. A fresh official
C3 build passes (1,569,184 bytes); compilation and map inspection exclude the
P4 AOQ/front-end sources. No C3 hardware test was performed. Candidate 39's
application readback, protected ranges and reset pass: 6,091,296 bytes,
SHA-256 `4c1bc85b41664720ea6720e77b6911a3a1f09c7ce6a54e8174efa0e8373ae9d3`.
Its first repeated original-source utterance passes, but the second still loses
the latter clause. Full physical acceptance remains open; host checks do not
substitute for it.

### Prior installed candidate 35

Candidate 35 is `0.10.0-p4-aoq-dev`, 6,060,528 bytes, SHA-256
`ea310c8a796906e028c56fcbd4dec5c22f2cc1d91fc53a56e2d448ff1279e66d`.
Application readback, protected ranges/assets and reset passed. All 178 frozen
product inputs match. AOQ with and without diagnostics and P4 WSS compile;
the history regression passes ASan/UBSan. C3 excludes this P4-only owner; its
last C3 build/isolation result remains the candidate-32 check, not a new C3
hardware test. The without-probe image is 6,000,800 bytes, SHA-256
`d467dc8d2451179d6e7f52fb782ce7a0390c5eaf097b94bea96e7be615f6bb4d` (not installed).

The automatic acoustic tests reproduced spoken `[设备通知]` text and found it
persisted in recent assistant history. That spelling was not covered by the
old history exclusions. Candidate 35 excludes assistant answers beginning with
known internal-notice labels from future prompt history and marks a newly
generated invalid answer instead of recycling it as a successful answer.
It retains the user's utterance and existing journal records. A user's question
about a label is not filtered. This prevents history recirculation; it is not a
guarantee that a model can never generate an internal label for the first time.

The user requested autonomous testing with the existing headset, microphone
and Insta360. The computer only plays fixtures and records evidence; the P4
continues to connect directly to Qianwen. All runs below use 600 ms / 0.5.

| Automatic run | Result and boundary |
| --- | --- |
| `acoustic-calibration35`, image 34 | Quiet board capture has no DAC reference and no clipping. Full-level playback reaches both microphones; lower-level playback worsens transcription. The latter clause is weak near the board noise floor. This does not establish a distance-only cause. |
| `selftest35-original`, image 34 | FAIL: original short fixture loses its latter clause; internal-notice text is spoken. Preserved as the unchanged-source regression. |
| `selftest35-calibrated`, image 34 | Full short request and follow-up transcribed and answered correctly after fixture-level compression, without changing firmware/VAD. An extra unscripted transcript occurred while waiting for idle. The story was split/cancelled, so the subsequent attempted interruption is not accepted as a story-barge pass. |
| `selftest35-story`, image 34, volume 80 | Full short story request accepted, actual story playback active before the math interruption. FAIL: no local-VAD interruption. Raw mic/reference/AEC capture retained; an energy/correlation upper-bound analysis reaches at most 3 of the required 6 votes even before the speech classifier. This is not an exact replay of the on-chip VAD. |
| `selftest35-story-volume50`, image 34 | Same deliberate-interruption failure at temporary volume 50; original volume restored. |
| `selftest35-story-volume50-strong`, image 34 | Stronger interruption fixture was prepared, but the unchanged story request was truncated first. Story/barge acceptance NOT RUN; do not count this as a stronger-input barge comparison. |
| `history-regression35`, image 35 | PASS: acoustic wake, complete fresh nonce instruction “请记住暗号是大树49”, complete recall question, correct nonce answers, explicit goodbye and return to listening. Two prior internal notices were filtered; no internal label spoken, no interruption, no playback underrun or DMA deadline miss. This does not validate barge-in or general noisy-room recognition. |

The ordinary smoke runner now requires the complete fixture in one transcribed
user turn before accepting an answer or starting the interruption stage. A
remembered correct answer and fragments spread across separate turns cannot
substitute for full input. Long-utterance endpointing, interruption at normal
volume, video/tutoring integration and overall speed acceptance remain open.
No migration commit or Git push has been made.

### Previous integrated candidate 34

Candidate 34 is `0.10.0-p4-aoq-dev`, 6,060,048 bytes, SHA-256
`83c08c8e7cb987eeeab59a77234e7a1af12c2cd41dc747c5ddd93c1396eb297b`.
Application readback, protected Flash/assets verification and reset passed.
A fresh protected-range snapshot preserves the user's newly recorded dialogue.
All 177 recorded build inputs match. AOQ with/without diagnostic entry points
and WSS compile. The without-probe image is 6,000,320 bytes, SHA-256
`e22f61b7e41da5ae43325a89b48ad627eb87023205bae775db2edde3d8999593`,
and is not installed. This change is P4-only; the C3 build excludes this owner.

Candidate 33's `barge33` reproduced two `provider_cancel` events followed by
generic network recovery and final `server` failure, before the intended
interruption fixture could run. The old branch classified every cancelled
response as a server error and discarded captured input during recovery.
The [provider event reference](https://platform.qianwenai.com/docs/api-reference/qwen-audio-realtime/server-events)
distinguishes cancellation by turn detection/client request from LLM/TTS failure.
Candidate 34 retires the cancelled media connection while retaining bounded
microphone onset and the original capture clock. It does not issue the network
recovery announcement for cancellation. Three consecutive provider-cancel
recoveries without a completed reply are allowed; further cancellation fails
visibly instead of allocating indefinitely. Completion resets that allowance.
ASan/UBSan policy tests cover replay bounds, counter wrap and retry limits.
Hardware interruption acceptance remains pending; this does not establish the
cause of the separate earlier truncated acoustic fixture.

`barge34` observed `realtime_provider_cancel: turn_detected`, native handoff,
a fresh ready session and subsequent completed playback, with zero generic
network recoveries. This verifies the corrected cancellation branch on P4;
the exact replayed onset transcript was not established. The full deliberate
interruption test FAILS because no local-VAD interruption was observed and
transcriptions differed from the fixtures. It also recorded internal-notice
text in spoken output and five playback underruns. These findings remain open
alongside the original failures. Candidate 34 is not a release acceptance.

### Previous integrated candidate 33

Candidate 33 is `0.10.0-p4-aoq-dev`, 6,059,728 bytes, SHA-256
`83fa4a801531b212e4f823d51af14ee02f42bf0c29cceaeb615557874cf763d8`.
Application readback, protected Flash/assets verification and reset passed.
It includes the generic ASR/TTS adapters, continuous-video integration and the
600 ms / 0.5 audio endpoint default. It also publishes accepted interruption
counters before rotating the native connection; the previous loop exit could
otherwise omit that counter. The current mode is natural AOQ with video off.

| Run on 33 | Result | Evidence boundary |
| --- | --- | --- |
| `generic-speech33` | PASS: exact native TTS fixture transcribed as “请直接回答：1+1等于多少？”; multipart generic TTS rendered all 545,760 samples at 24 kHz and released the owner; `fast` correctly rejected | Fixed input, not acoustic recognition or human listening. ASR's 2432 ms is whole task duration, not conversation latency |
| `generic-cancel33b` | PASS: cancel during active TTS stopped playback; a fresh TTS task then completed | Serial cancellation, not voice interruption. The preceding `generic-cancel33` stopped on an interleaved UART status response; its failed receipt is retained. Only read-only query retry changed in the new observer |
| `integrated-voice33` | FAIL: cloud transcription retained “我们的小猫叫蓝莓。” but omitted “请只重复它的名字。” | Official wake and response playback worked. Strict full-input validation stopped the run; recall and voice interruption were NOT RUN |
| `short-aec33` | FAIL: repeat with synchronized microphone/reference/AEC capture still produced incomplete cloud transcription | Independent offline ASR recovered the complete fixture from AEC output. This rules out a completely absent captured tail in this sample; it does not prove delivery of that tail to the cloud or identify the final root cause |
| `late-human33`, recovered after user confirmation | PASS for the two recorded question/follow-up pairs: full passphrase-plus-math question and recall; full cat-name instruction and recall | Durable records 902/903 and 907/908 are newer than the installed-image baseline (898). User reported normal speed/content. Current and preceding observed configuration is 600 ms; the expired 400 ms prompt does not establish a 400 ms run. The timed recorder had ended, so no numerical latency or playback timing is claimed |

The recovered human records also include an interrupted partial passphrase
before the complete question. The retained session status reports one reconnect
and final error `server`. Without the expired event log, its exact server event
cannot be reconstructed. Those remain open diagnostics; the four successful
content records do not qualify the entire session. The passphrase also existed
in earlier history, so this is not a new random-value memory isolation test.

### Earlier integrated candidate 29b

Candidate 29b was `0.10.0-p4-aoq-dev`, 6,063,344 bytes, SHA-256
`593e49ec408dbdbaac8a04900f1e4296af54174703da5aa78f5cf6d11b412fd3`.
Application readback, protected Flash/assets verification and reset passed.
It integrates natural AOQ conversation, restores the endpoint cue, and corrects
a stale playback timestamp that could trigger an interruption on the next turn.
It does not yet contain the subsequent general speech adapter or continuous-video
integration. Candidate 29's preceding install attempt failed its changed-history
baseline check before any write; candidate 29b used a fresh, verified runtime
history snapshot. Both receipts are retained privately.

| Run on 29b | Result | Evidence boundary |
| --- | --- | --- |
| `fixed-math29` | PASS: P4-native TTS generated the full math question; the exact retained audio was fully transcribed over native dialogue, and the answer played | Fixed PCM, independent of the acoustic microphone path |
| `human29` | Full question and answer recovered from fresh durable history; user confirmed the cue but still found the response slow | The short timing logger had ended before the question; no latency claim. Endpoint was semantic at that time |
| `human29b`, server VAD 600 ms / 0.5 | Two math questions and a following “等一下啊。” transcribed and answered; no interruption reported in those turns | User reported improvement but still wanted faster responses; not a context-recall or barge-in acceptance |
| `human29c`, server VAD 400 ms / 0.5 | NOT RUN | The 30-minute logging window recorded no new wake or voice events; the requested long two-clause question and recall have no result. This is not a 400 ms recognition or speed verdict |

For the two math turns in `human29b`, board receipt of the cloud speech-stop
event to first PCM was 1083 / 217 ms, and to speaker start was 1101 / 331 ms.
PCM arrival to speaker start was 18 / 114 ms. These are board clock intervals;
they exclude the acoustic endpoint wait and do not measure the user's complete
speech-end-to-answer delay. The first-turn difference is not assigned solely to
model computation: network and model waiting are both included.

The fixed headphone-to-microphone comparisons at thresholds 0.1 and 0.3 failed
full-question recognition. The synchronized 0.3 capture contained distorted
speech; AEC and microphone PCM were identical and the reference channel was
zero. That specific capture does not demonstrate AEC deleting the sentence.
These failures remain recorded and are not replaced by the successful fixed
digital input or human math turns.

The intermediate installed candidate 32 was 6,059,600 bytes,
SHA-256 `78c67e60e3ae07a2f0b20ef9e99bec641f5993796f6f66a26bfb5326fcd0b793`.
Its generic fixed-input ASR/TTS run passed, and candidate 33 repeated that test
on the currently installed image. This implementation adds the independent
speech adapter and continuous video integration. It also
aligns the raw QUIC record budget with the conversation's video budget: the old
64-record cap would reject a long camera session before its advertised 180-frame
limit. The new bounded profile retains up to 512 records, including final-size
tombstones, with an explicit host regression covering 180 local video streams
and 256 peer streams. Allocation remains incremental and bounded.
P4 AOQ (with and without diagnostic entry points) and WSS builds pass. The
current candidate 33 without-probe image is 6,000,000 bytes, SHA-256
`c430449cb7255dc57ddec4bb15baabed02c9765fb053debdcbef7beba9644cc7`,
and has not been installed. Host ASan/UBSan suites pass: 45 session, 29 fixed
TTS, 26 fixed ASR, 45 fixed dialogue, 20 conversation-adapter and 13 general
speech-adapter cases. Its C3 build is 1,569,184 bytes, SHA-256
`54e8a210bd2071109f6bbf37851c1537e903ea7e90da53e836898d0d55dea59a`.
C3 dependency isolation passes, partition/wake model are unchanged, and binary
comparison has zero unclassified bytes. C3 physical validation is NOT RUN.

In a probe-enabled AOQ build, `agent aoq speech asr <generation>` resamples an
explicitly selected P4-native TTS generation and calls the same generic ASR
provider as classic conversation. It reports the actual transcript and source
generation; the test runner must compare the full expected text. It neither
uses the microphone nor injects PCM into normal conversation. `agent voice say
TEXT` exercises the generic TTS route separately. These fixed-input board tests
passed as listed above; they do not replace acoustic acceptance.

Release remains blocked on the full long-question/speed comparison, acoustic
speech and integrated video hardware runs, context recall, real interruption, and tutoring
state/rendered-position regressions on the final installed image. No Git upload
has been made for this integrated candidate.

### Previous candidate 25 cue recovery

Candidate 25 is `0.9.8-p4-aoq-probe`, 6,051,808 bytes, SHA-256
`777189540e54304c8d3eaafc0e6d33acd3078a3a0c81f818ccf142d0fde3392f`.
Application readback, protected partition/assets verification and reset passed.
It restores the endpoint cue described above. The original 600-ms audio and
900-ms camera server-VAD requests are unchanged. Host tests passed: 45 session,
28 TTS, 26 ASR and 42 dialogue cases, including delayed/no response after cue
ownership begins. C3 incremental build and isolation checks passed; its
1,569,184-byte application SHA-256 is
`863acf38c39c6ce34e89b9812358f60aae7c3755aeef94ce84189e8745f102df`.
No P4/AOQ/QUIC code was linked, and binary differences have no unclassified
bytes. C3 physical validation remains NOT RUN.

All 134 frozen product build inputs still match the installed candidate. Two
new acoustic runs, `endpoint-after25` and `endpoint-camera25`, contain the
original 2600-to-900-Hz endpoint sweep (fixed detector scores 1.0 and 0.792).
Both completed response playback and joined the audio owner. This verifies
restored cue feedback on the installed P4, independently of a cue-request log.
However, the math input was transcribed only as “请直接回答。” and the camera
input only as “请看摄像头画面，告诉我。”. Both full-question checks therefore
FAIL. The camera runner's original PASS used only camera-keyword presence;
`endpoint-validation.json` explicitly adjudicates the full fixture as FAIL
while preserving that raw report. Neither recording passed source alignment,
so no human/source-speech-end-to-answer speed is claimed. Global cue detection
is independent of that failed source-alignment gate, not a relaxed speed gate.
The user has reported no noticeable speed gain. Speed acceptance and full
COGD migration remain incomplete. The P4 is handed back in `aoq-test`, voice
enabled, official wake word unchanged; no PC media relay is involved.

The new candidate 24 control `endpoint-baseline24` failed with
`response_not_completed`, no PCM and no completed input transcript. External
recording also failed the fixed source-alignment gate. This preserves a failed
run, not a zero-latency baseline or evidence that the user did not speak clearly.

### Previous candidate 24 capture-clock fix

Candidate 24 is `0.9.8-p4-aoq-probe`, 6,051,648 bytes, SHA-256
`1e1bd70a54326ba71347face2dcf2b7a0b3b2d5b752e4e2f9c2b0e4dc81516f0`.
Application readback and protected partition/assets checks passed. All 134
recorded build inputs still match the frozen build. The change fixes a second
real-time pacing delay on microphone audio already buffered during connection.

`latency-human01` passed on this image after the user confirmed speaking:
input “1+1等于多少？”, answer “一加一等于二。”, all 36,480 response samples
played and the owner joined. Initial capture lag was 1330 ms and the last
uploaded frame's lag was 221 ms, showing the queue caught up during this turn.
Cloud `speech_stopped` to first PCM was 163 ms, and to first DAC progress 403 ms.
These exclude endpoint detection and are **not** a human speech-end measurement
or a same-condition before/after speed benchmark. The user subsequently reported
no noticeable speed improvement and a missing endpoint cue. Overall latency and
user-experience acceptance therefore remain open; this is not a completed migration.

`latency-human-camera01` then passed after the user confirmed the camera
question: full input transcription, five current JPEG frames (38,625 bytes),
answer “画面里主要是浅蓝色，像白板或墙壁的背景。”, and all 109,440 samples
played and joined. Initial/last capture lag was 1557/2 ms; cloud speech-stop to
first PCM/DAC progress was 617/874 ms. This validates camera/audio transport and
playback after catch-up; the post-turn Insta360 still showed the screen idle,
so it does not independently verify the model's color description or fine text.
The device was restored to `aoq-test` with voice enabled after the regression.

The attempted automated baselines remain failures: `latency-baseline01` and
`03` had incomplete transcription; `02` and `04` did not wake; `05` heard a
different question and answered incorrectly. `latency-after01` also failed to
wake. None is used to claim a response-time improvement. A complete microphone
turn with the user is separate from these failed acoustic fixtures.
`latency-camera01` was also an unsuccessful automatic wake/evidence capture;
the subsequently successful human camera run has its own receipt.

Host tests passed: 45 session, 28 TTS, 26 ASR and 40 dialogue cases. The new
cases cover capture catch-up with exact PCM continuity and Busy retry, and
reject future capture origins. The C3 build/isolation check was rerun and passed;
its 1,569,184-byte application SHA-256 is
`3d8e0e510f6fce1cb78d037659ea7bb1aa5d925502722fa5c6306d0e463b34e0`.
No P4/AOQ/QUIC code was linked; all binary differences are classified build
metadata/checksums or `ESP_ERROR_CHECK` line-number immediates. C3 hardware
validation remains NOT RUN.

### Previous candidate 23 interoperability baseline

Candidate 23 is `0.9.8-p4-aoq-probe`, 6,050,960 bytes, SHA-256
`d9f706ac396b8312f57e2f51bba096288925eea0ddf8c624a0f0894413de9cd0`.
Application readback matched, and partition/assets/protected-range verification
passed. The application-only install did not write the bootloader, partition
table, tutoring context or media assets.

| Run on candidate 23 | Result | Evidence boundary |
| --- | --- | --- |
| `native-speech-chain12`: standalone TTS | PASS: 106 objects, 50,880 samples received and played at 24 kHz; empty playback ring and joined owner | Fixed phrase, DAC progress; human listening pending |
| Same run: independent ASR | PASS: P4 converted the exact TTS generation to 67,840 bytes at 16 kHz; final text exactly “原生语音测试成功。” | Native separate ASR task with fixed PCM; not open-microphone recognition accuracy |
| Same run: audio dialogue | PASS: 191 input DATAGRAMs, model answered “收到”, 19,200 response samples played and joined | Fixed audio; the dialogue transcript used the homophone “原声”, distinct from the exact independent ASR result |
| Same run: red/blue video | PASS: four frames of each distinct synthetic fixture; correct color/shape; 63,360 and 61,440 samples played | `camera_used:false`; this tests transport and model interoperability |
| `live-camera10` | PASS: actual microphone question fully transcribed, five current board-camera JPEGs (36,522 bytes), 144,000 response samples played and joined | P4 camera/microphone direct to Omni; headphone acoustic fixture; no computer media relay |
| `live-microphone05` | Transport/playback PASS: recognized the cat name 蓝莓 and played/joined all 165,120 response samples | The fixture's second clause was only partly transcribed (“请只重”); this is not a full-utterance ASR accuracy pass |
| `live-microphone06-clear` | PASS: the same clear camera question was fully transcribed in audio-only mode; model correctly reported no camera input; all 88,320 samples played and joined | Actual microphone; a separate fresh run, not a replacement for the partial-transcription result above |

The real-camera question was “请看摄像头画面，告诉我主要是什么颜色。”
The answer described a blue background and yellow dot, matching those broad
features in the contemporaneous Insta360 view of the P4 preview. Its additional
claim about red writing on white paper was not independently verified; neither
fine-text accuracy nor full visual/human acceptance is claimed. The source JPEGs
come from the current camera worker, not the fixed video fixtures or photo cache.
Cloud speech-stop to first PCM was 600 ms and to first DAC progress 839 ms in
this camera run. These exclude endpoint detection and microphone buffering and
are not an end-to-end WSS comparison.

The audio-only microphone run measured 345 ms from cloud speech-stop to first
PCM and 587 ms to first DAC progress, with the same latency exclusions. Its
partial final clause remains an observed recognition limitation, separate from
the passing exact fixed-PCM standalone ASR test. The clear-question audio-only
control measured 90 ms to first PCM and 326 ms to first DAC progress after the
cloud speech-stop event. These different replies are not a comparative benchmark.

The full fixed chain reported at least 2964 bytes of free worker stack and no
panic. Host session regressions passed (45 session, 28 TTS, 26 ASR, 37 dialogue),
including rejection of mismatched camera VAD configuration before media input.
The final candidate's shared runtime/main CMake hashes still match the versions
used for the C3 build/isolation check below. C3 hardware validation is NOT RUN.

### Preserved earlier trials

Candidate 13 (`9ddeab346306f1240d0b066dcf67a633710a4250c1ed545b9354cb20a18d3caf`) passed the fixed native TTS receive and separate P4 playback run: 106 PCM objects, 50,880 mono samples at 24 kHz, all samples written and played by the DAC-clock counter, empty ring and joined audio owner. Human listening remains pending. The following ASR command triggered the `agent_net` stack protection fault and rebooted; it did **not** pass. The fix moves trial state and ASR encoding scratch buffers to PSRAM rather than increasing the shared C3 worker stack.

Candidate 14 (`0b434f1b25345a9ac13c918d84e66a801dbd6daee912692fd24c4d1438880c47`) passed the fresh fixed TTS receive/playback and independent ASR chain without a panic. ASR sent 67,840 bytes of resampled PCM over Data, received one final sentence matching “原生语音测试成功。”, and completed its task. The shared network worker's observed minimum free stack was 4292 bytes. Its separate dialogue trial initialized the model and sent 14 Audio DATAGRAM frames, then failed locally because 32-bit `size_t` multiplication overflowed in sample-to-microsecond conversion. The corrected conversion explicitly promotes samples before multiplying and has a regression crossing that 4480-sample boundary. This earlier failed dialogue run is not a passing media result.

Candidate 16 corrected native DATAGRAM congestion accounting: newly acknowledged
in-flight bytes now restore send credit through both ACK paths; duplicate/late
ACKs do not grant duplicate credit, and PTO remains armed for ack-eliciting
packets without retransmittable media. The fixed TTS/playback/ASR/dialogue chain
then passed. Candidate 17's first microphone run produced and played an answer,
but its JSON receipt was corrupted by interleaved ESP-SR startup logging. That
run remains a failed evidence capture. Candidate 18 retains the receipt for an
idle query and suppresses live metadata logging.

Candidate 18 (`25202dd5a2ad1ad895ff251fba0391f54d5c7bc3233d6fb5bc52f61b36f6f588`)
passed the following independent hardware runs:

| Run | Observed result | Scope |
| --- | --- | --- |
| `live-microphone02` | P4 microphone input about a cat named 蓝莓; answer “已记下，您的小猫叫蓝莓。”; 209 Audio DATAGRAMs; all 55,680 response samples played, owner joined | Actual microphone and speaker counters, acoustic headphone fixture, no computer relay; human listening pending |
| `native-speech-chain10` TTS/ASR | 50,880 TTS samples played; P4 resampled 67,840 PCM bytes; independent ASR final exactly “原生语音测试成功。” | Native standalone models and fixed content |
| Same run, audio dialogue | 191 Audio DATAGRAMs; answer “收到”; all 19,200 samples played | Fixed audio, fresh native dialogue session |
| Same run, red video | Four JPEG frames; answer “我看到一个红色的正方形。”; all 55,680 samples played | Synthetic video, `camera_used:false` |
| Same run, blue video | Four JPEG frames; answer “画面里是一个蓝色的圆形。”; all 59,520 samples played | A different synthetic video, `camera_used:false` |

The live microphone run measured 424 ms from the cloud `speech_stopped` event to
first received audio, and 658 ms to first DAC progress. Those intervals exclude
cloud endpoint detection and are not a same-condition WSS speed comparison.
The complete fixed speech/video chain's minimum observed free worker stack was
3476 bytes. No panic was observed in either accepted run.

Candidates 19 and 20's first real-camera runs failed after one JPEG. The camera
path incorrectly waited for the complete video send before admitting microphone
audio. Candidate 20's bounded provider-message vocabulary reported
`image audio before`; its color-layout correction alone did not fix this failure.
Candidate 21 waits for the cloud speech-start event before sending video.
The original failed runs remain distinct from subsequent acceptance.

Candidate 21 (`cf1fc2ecf7996553a6d5508bce389057055af8da68dd76a5f0c82a0c74598460`,
6,050,240 bytes) passed a fresh `native-speech-chain11`: standalone TTS/playback,
exact independent ASR, audio dialogue/playback, and both distinct video fixtures
with full playback. Its `live-microphone03` also passed: the actual microphone
transcript was “我们的小猫叫蓝莓。” and all 122,880 response samples played and
joined. Cloud speech-stop to first PCM was 172 ms and to first DAC progress
421 ms, without a same-condition WSS comparison. The minimum observed free
worker stack after that full chain was 2932 bytes.

Real-camera runs `live-camera03` through `live-camera06` on candidate 21 did not
pass: respectively no speech onset, empty transcription, and repeated early
segmentation of “请告诉我……” into an extra turn. The last two were rejected by
the single-turn guard. Their short completion UART event was not parsed, but
idle retained-result queries recovered the failed receipts; this is not a
passing run. A four-second board capture found nonzero microphone audio and
bit-identical AEC output, with zero DAC reference. Changing the playback file's
sample rate/level did not resolve segmentation. Candidate 22 then verified the
semantic-VAD configuration echo, but `live-camera07`, `live-camera08` and
`live-camera09-control` still failed with no speech onset. The control used the
same fixture as its passing audio-only `live-microphone04` run. The single-turn
guard remains enforced; a failed camera trial is not promoted to a pass merely
because configuration was accepted or microphone packets were sent.

The camera encoder uses BGR24 byte storage for ESP-IDF's `RGB888` input, matching
the [official JPEG driver example](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/jpeg.html).
The red/blue host checks cover this input layout; synthetic JPEG fixtures do
not test the board encoder's color ordering.

The C3 build after the candidate-19 camera/runtime hooks contains no P4/AOQ/QUIC
components, compile units, symbols or strings. It uses the same configuration,
partition table and wake model, and remains 1,569,184 bytes (3680 bytes free in
the application slot). Application SHA-256 is
`0cb9adcf20e59746be3e639b9b1450f41d05b9e3c28acc5affe48e3c36698641`.
Comparison with the baseline explains all binary differences as build metadata,
checksums and `ESP_ERROR_CHECK` line-number immediates; no unclassified bytes
remain. This is build/isolation evidence, not a C3 hardware test.

On 2026-11-07, P4 candidate `fa5d24cf7747611ff0358a2042bfe62afa752ac4f09ef7c8a71966ade8ea30fd` passed native allocation, pinned TLS and accepted AOQ setup for all three listed speech models. Application readback and protected Flash checks passed. Observed allocation-through-setup times were 1205 ms (dialogue), 1224 ms (ASR) and 1202 ms (TTS). These were setup-only trials with no media and no computer relay. They are not speech latency comparisons or successful recognition/playback results.

1. Validate P4 allocation, authenticated QUIC and accepted AOQ setup.
2. Validate independent ASR with known PCM and independent TTS with playback, then the full conversation events and cancellation behavior.
3. Validate user dialogue directly on P4, including wake, interruption, tools and tutoring state.
4. Add supported multimodal input after the speech path works; retain separate photo tutoring and generated-video download behavior.
5. Compare measured end-to-end latency with WSS using equivalent prompts, models, audio and network conditions.

Hardware results must identify the installed application hash. Keep host codec tests, C3 compilation, P4 native setup, fixed media replay and human acceptance as separate results. Do not publish or describe the experimental probe as a completed AOQ migration.
