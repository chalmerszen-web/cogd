# P4 AOQ continuation — 2026-11-10

Status: **Candidate 131 PASSES the requested finite, fixed-source physical
qualification.** This resumes the interrupted `确认 cogd 的芯片支持` chat.
Earlier failures remain recorded below. Human acceptance and broad noise/voice
coverage are not claimed.

## Current verified result — candidate 131

The installed native P4 AOQ application is 6,269,120 bytes, SHA-256
`e8ed5e6a3d87b313bcbba6cb2ac862648e65746c40238dd80ee5de929c3a67ca`.
All 215 frozen firmware input hashes match the tested source. Guarded
application-only installation preserved the protected regions. A fresh final
application readback after all tests matches this hash. The board was reset
and checked with voice enabled and wake state `listening`.

| Required physical check | Result |
| --- | --- |
| Ordinary short answers | 10/10 PASS, complete tested prefix, no unsolicited interruption |
| Story interruption and arithmetic follow-up | 10/10 PASS; exactly one scheduled cut per round; answers 12 then 13; all three source utterances complete after only punctuation/numeric notation normalization |
| Long expanded speech | 3/3 PASS, no unscheduled cut or extra user turn |
| Context retention/update | 5/5 pairs PASS |
| Full photo tutoring | All 8 steps PASS: capture, select, confirm, read, explain, step two, resume, actual done |
| Quick progress and task states | 19/19 queries PASS across cancelled, reading, selection, confirmation, solving and completed/reading-solution states |
| Cancellation and completion ownership | Actual in-flight cancellation PASS; no late result revival; accepted completion announced once |
| Native camera video | PASS; 15 uploaded frames, scene description matches paper, equations and yellow fastener; actual LCD preview inspected |

The camera now captures an actual native 1920x1080 question photo, with an
upper-field sensor window that retains all three original equations. No image
synthesis or upscaling is used. Three sequential photos in this qualification
correctly preserve `12-x=9`, `1/2-2x=1/3` and `1.6×2-3x=40%`.
The exact tutoring JPEG is 783685 bytes, SHA-256
`6845b44d88482a6e45e8ad19c87e8372804fedbba8c8cf81ee21bb84b039b5ea`.
It and all three tutoring LCD photographs were exported and inspected. Capture
windows were 1811/1827 ms; each deferred two camera samples for audio reserve,
which is not lost voice audio. Both report `camera_released=true`. The second
step and resumed reading screen retain the same position. Question text may
still display simple LaTeX escape strings; typography and arbitrary generated
solution correctness are not certified by these workflow checks. Video scene
validation does not certify incidental background lettering.

| Reply type | Count | Median | Minimum–maximum |
| --- | ---: | ---: | ---: |
| Ordinary short answer | 10 | 2.367 s | 2.265–3.297 s |
| Answer after interruption | 10 | 4.360 s | 3.938–5.547 s |
| `那再加一呢` follow-up | 10 | 2.750 s | 2.563–5.016 s |
| `好了没` across all tested states | 19 | 1.469 s | 1.375–2.250 s |

These are source-WAV completion to actual reply speaker-start observations,
excluding the endpoint cue, not server-only timing or human acoustic P95.
Photo admission's cached receipt was 1.437 s. Cancellation acknowledgement was
4.094 s including input recognition. The tutoring solve was substantially
slower: result receipt 61.906 s and completion speech 63.484 s after the user
finished speaking (job execution interval 57.562 s). This is explicitly not
included in the ordinary short-answer median. No general claim that all
answers now arrive in one or two seconds is made.

Native AOQ, AOQ without diagnostic probes, WSS, and C3 builds PASS; all 114 host
tests PASS. The unchanged AOQ session/media/conversation suites previously
passed on candidate 127. C3/WSS source and symbol isolation is checked; their
physical hardware operation was NOT RUN. P4 retains native transport, the
600 ms local endpoint, frontend profile 3/gain 18 and 20 ms upload pacing.
The final cumulative capture audit covers 52,964 frames, maximum processing
17,242 us, zero frames over 32 ms and zero receive overflows. Every phase
reports zero stream underruns and transmit-deadline misses.

Raw diagnostic limitation: third-party direct stdout still spliced 14
noncritical records (10 idle-ASR-discard events and 4 transport-statistics
records). Original logs are retained unchanged. Removing only the exact
literal debug strings reconstructs all 14 records without inventing fields;
all user/answer/interruption/speaker/job receipt sequences exactly match the
original reports. This is documented rather than claimed as zero raw-log
corruption. SDK logging itself uses the common USB queue.

Private local evidence lives under `.local/aoq-native-20261107/`:
`qualify131-live/report.json`, each `qualify131-live-*/report.json`,
`metrics131.json`, `serial-audit131.json`, `postflight131/binding.json`,
`postflight131/phase-audit.json`, `candidate131/install/receipt.json`, and
`candidate131-build/p4-native-build-inputs.json`. Captured photos/room recordings
are local evidence and are not included in the source publication.

## Historical failures and intermediate experiments

## Bound firmware and fresh tests

Candidate 117: SHA-256
`11291e502d3d98923bed7df3c821fe7750cdc24c2f18c4693505dd0c549e1e74`.
All 215 frozen source hashes matched at resumption. A fresh COM3 application
readback matched before testing. Settings: P4 native AOQ, video off, volume 80,
600 ms local endpoint, 20 ms upload pacing, frontend profile 3 / gain 18.
Tests use the selected Misiom-Shooter output and an independent Insta360 recording.

| Fresh run | Result |
| --- | --- |
| `resume117-barge` | Round 1 passed. Round 2 transcribed `那再加一呢` as `那再加`; stopped. Answering 13 did not count as complete recognition. |
| `resume117-follow-capture` | Independent repeat: round 1 passed, round 2 reproduced the same missing ending. Raw/linear/AEC/accepted PCM and frame clocks retained. |
| `resume117-progress` | Cached `好了没` replies: 890, 1625, 2688 ms. Third exceeded the existing 2500 ms gate; photo phase not run. |
| `resume117-ordinary-isolated` | After restoring 117: ten full-input ordinary rounds passed, no unscheduled cuts. Reply times 1828, 1937, 2609, 1891, 1703, 1797, 2829, 1891, 1719, 1671 ms; median 1859.5 ms. This independently tests ordinary questions, not the failed interruption gate. |
| `gain119-barge` | Same 117 image, runtime frontend gain 24 instead of 18: first follow-up again lost `一呢`; stopped and restored gain 18. No gain change accepted. |

Latency means test WAV playback completion to the board's actual reply speaker
start, excluding the endpoint cue. It is an automated acoustic-fixture measure,
not broad human/noise acceptance or a network-only duration.

Candidate 118: SHA-256
`d41eb7bb4fcfe59950cdae88032704cd19a7de1808298707bbe605a1866ea7a9`.
The sole experiment waited 200 ms after the final accepted PCM frame before
Data-track commit, keeping the 600 ms endpoint. Installation, protected-region
verification and readback passed.

| Fresh run | Result |
| --- | --- |
| `qualify118-barge` | Three full interruption/follow-up rounds passed; reply times 3266, 3812, 3172 ms. Diagnostic only. |
| `qualify118-progress-live` | Cancelled-state replies 1407, 1281, 1156 ms; busy reading reply 1578 ms. Fresh camera task reached selection and announced completion exactly once. |
| `qualify118-full-barge` | Requested ten-round batch failed on round 2: `等于多少` became `等于多`. Later qualification phases not run. |

The initial `qualify118-progress` invocation selected the wrong fixture manifest
and failed before an acoustic test. It is a harness setup failure, not a device
result. The separate `-live` path preserves the corrected run.

The 200 ms experiment did not resolve missing endings and was reverted. All 215
source hashes again match candidate 117. A protected application-only restore
completed with application readback, protected-region and reset checks passing;
see `restore117-after118/install/receipt.json` in the private evidence directory.

## Evidence narrowing the missing-ending failure

The failed 117 follow-up capture has uninterrupted 20 ms accepted timestamps and
no skipped diagnostic frames. Offline Whisper recognizes the full phrase in the
actual accepted PCM. Zero declared lost datagrams is not a server receipt or
proof of recognition quality.

Waveform alignment shows a weak region around source 1.1–1.3 seconds already in
the raw P4 microphone signal. The linear channel retains that signal at four
times its amplitude with the expected 32 ms block delay. The independent
Insta360 recording has the same weak region. This does not support blaming an
AEC truncation for this captured example.

A fresh WASAPI loopback of the unchanged fixture matches its 100 ms RMS blocks
and has correlation 0.988. The host playback output did not lose that ending.
Separate left/right playback did not clearly remove the weakness. These findings
localize attenuation to the acoustic path but do not identify a particular
headphone, placement, microphone or cloud-recognizer defect.

The user confirmed that a single earcup is close to the P4 microphone and that
both test phrases sound clear and complete when listening directly. The gain
comparison preserved this placement. Do not equate acoustic attenuation measured
in the recordings with the human reporting an inaudible or clipped source.

## Checks and remaining gates

Fresh host tests: 112/112 passed. AOQ session/adapter tests with sanitizers passed;
candidate 118's AOQ build without diagnostic probes compiled. These checks do
not turn either failed acoustic batch into a pass.

Candidate 120 was a bounded differential: stop priming the remote
input buffer during the board's own playback, while retaining quiet live input,
the local microphone ring, complete replay after confirmed interruption and
same-connection cancellation. The relationship between that recent latency
optimization and omitted follow-up endings is a hypothesis, not an established
root cause. Endpoint stayed 600 ms and frontend 3 / gain 18. Its SHA-256 is
`1ef5524e895dce6208a563f4ffd16468cc740b3958a54e5fcc36e4574dbe5410`.
Guarded installation/readback passed. The first three full interruption rounds
passed, but reply times were 4297, 5703, 5703 ms. Round 4 failed on its initial
story request (`故事` became `故`), before the scheduled interruption. The suite
stopped and all subsequent phases are NOT RUN. This candidate is not a fix.

A separate first-utterance test of `那再加一呢` passed three fresh sessions on 120
(`fresh120-follow-b`). An earlier isolated run's second transcript was the
complete `那再加1呢`; its literal-only assertion falsely rejected the digit.
Keep that original harness failure and its `oracle-review.json`; the corrected
run uses the same digit equivalence as the original interruption tests.

C3, WSS and AOQ without diagnostic probes compiled for 120. C3/WSS source and
symbol isolation checks passed. Neither result claims C3 or WSS hardware testing.

Candidate 121 compares the complete onset-only upload path:
also remove quiet speculative upload while keeping local pre-roll, same-connection
cancellation, cached status replies and ordered response submission. Maximum ten
interruption/follow-up rounds, stop at the first failure. Recognition and actual
response latency both remain acceptance requirements.

Candidate 121 SHA-256:
`72fd0e0b2c4659d94dd6a6adc049c0ff1d37e452bbe2191bbe13f8b8b1ac109b`.
Guarded installation, protected regions and readback passed. Its ten consecutive
full interruption/follow-up rounds passed without extra cuts. Reply times were
3219, 4110, 4203, 4328, 3844, 4781, 3938, 4047, 4125, 4172 ms: median 4117.5 ms,
versus the earlier same-condition baseline of about 5220 ms.

Overall qualification still **FAIL**: the subsequent status test answered its
first query in 1438 ms, but the second committed 27,200 samples and received an
empty cloud transcription, so no cached reply started. The suite stopped;
ordinary, long, context, tutor and video phases on 121 remain NOT RUN. The
separate earlier ordinary pass belongs to 117, not 121.

A bounded comparison observed the existing automatic input-buffer clear
between local cached replies. A 22-second pause was diagnostic only, never an
acceptable response delay. Its first attempt failed the original speed gate
before reaching the pause. The separate `progress121-after-clear-b` repeat
returned both replies (1453 and 2000 ms), with the clear acknowledgement between
them. This comparison alone cannot establish a root cause or speed acceptance.

Candidate 122 retains onset-only upload and explicitly clears the resolved input
after a cached local reply or empty transcription. Further uploads wait for the
existing clear acknowledgement; the microphone ring remains intact. No extra
endpoint delay was added. SHA-256:
`d7ab681d2cb0d9a80db4a07a88e69bea9f9d602549528b7edbe4338f4e41922f`.
Guarded installation, protected regions and application readback passed.

Its first strict status batch passed ten consecutive queries: 1406, 1954, 1609,
1454, 1485, 1516, 1485, 1656, 1531, 1610 ms. A fresh onboard-photo task answered
the overlapping progress query in 1531 ms and delivered its completion notice
exactly once. Every local reply had a matching input-clear acknowledgement and
did not invoke model inference or suffer an extra interruption. These results
are in `qualify122-live-progress/report.json`.

The same image then passed ten consecutive full interruption/follow-up rounds
and ten ordinary rounds. All interruption follow-ups retained `那再加一呢`,
including the final particle (independent transcript review in addition to the
runner's semantic check). No extra cuts, receive overflows, playback underruns
or transmission deadlines were reported.

| 122 fresh batch | Reply start after fixture ends, ms | Median, ms |
| --- | --- | --- |
| Interruption 10/10 | 4344, 4047, 3782, 5156, 4109, 3875, 5516, 4234, 4266, 5313 | 4250 |
| Ordinary 10/10 | 2282, 2234, 2640, 2297, 2360, 2547, 2453, 2375, 3110, 2328 | 2367.5 |

Three long answers also passed (67,262 / 67,230 / 64,049 ms actual playback;
331 / 332 / 289 transcript characters), as did five consecutive context pairs.

The full batch stopped at the tutor gate: its actual camera image was read as
`16 × 2 - 3x = 40%` rather than the expected `1.6 × 2 - 3x = 40%`. The spoken
selection was transcribed completely, including `1.6`, but retained-photo
refinement still returned `16`. The test refused to confirm or solve the wrong
question. `qualify122-live-tutor/report.json` remains FAIL; later teaching steps
and that batch's video phase are NOT RUN. The failing photo's bytes were cleared
by the runner's standard cancellation cleanup; its capture ID and SHA remain.

A separately labelled fresh onboard capture (`camera122-real/photo.jpg`, SHA
`471d3202ea033efdd3ac2dbf282b5f349eab3c371231bbc2ef558bf4268c2dae`)
again returned `16`. Visual inspection showed small, faint handwritten problems
and a blue/cyan fastener. It is not the original failed frame. The user has been
asked to enlarge the paper in the camera view and avoid glare before repeating
the third-question gate. This does not establish an OCR firmware fix.

The independent video check initially used that photo's blue/cyan fastener as
its expectation and failed: the model described a yellow ball. Reviewing its
independent `video-screen.jpg` showed that the live preview also displayed a
yellow fastener. This exposed an invalid cross-path color expectation, rather
than evidence of a video hallucination. The original report remains unchanged.

Source inspection found the tutor JPEG supplying RGB24 bytes to ESP-IDF's
`JPEG_ENCODE_IN_FORMAT_RGB888`, whose installed official example documents
BGR24 byte storage. The live video encoder already uses BGR24. Candidate 123
changes only the tutor conversion's first/third bytes to match that documented
order. It is a confirmed encoding discrepancy; its relationship to the missed
decimal is not established by a single capture.

Candidate 123 SHA-256:
`d9768b44845e2226ccdf79eda845a62f64691b6ebb68b136f8f94a0611c4a227`.
Guarded installation, protected regions and readback passed. Comparing the 215
frozen source inputs with 122 shows only `p4_vision.cpp` changed. WSS and AOQ
without probes compiled; C3's unchanged-source build and isolation checks also
remain valid. No C3/WSS hardware claim is made.

The first camera script sent its status query before the post-installation boot
console was ready; it timed out before capture. Preserve `camera123-real` as a
startup harness failure. The separate `camera123-ready` capture completed and
its JPEG (SHA `7bf2b92d98baf8c4d06e98b7553505402666321ba608988a698919e3eac11258`)
now matches the live preview's yellow fastener. This fresh capture correctly
reads `1.6 × 2 - 3x = 40%` without a paper-placement change. The user was told
to keep the current placement while full qualification is repeated. The
unchanged 122 failures remain retained.

Additional 122 task-state checks used the clearly identifiable first equation
and passed: cancelled / reading / selection / confirmation / solving / complete
/ reading-solution / cancelled-final replies took 1469 / 1438 / 1438 / 1500 /
1437 / 1406 / 1485 / 1625 ms. The solve completion was announced exactly once,
reading position was unchanged, and cancelling a real running photo acquisition
did not revive a stale completion (`qualify122-states-first`). They cannot
substitute for the third-question tutoring gate. Full 123 qualification now
starts with that original complete tutor regression; Git publication remains
blocked until all required phases pass.

The AOQ build without diagnostic probes, C3 and WSS builds, and three targeted
host checks passed for 122. Fresh C3/WSS compilation-source and symbol isolation
also passed (`compat122.json`); neither was tested on hardware. All 215 frozen
source hashes still match the tested 122 image.

Pending: a qualified final image with ten consecutive ordinary and interruption
rounds, long responses, context retention,
complete tutoring with verified steps/rendered position, camera video, additional
progress/completion/cancellation races, compatibility checks and final readback.

Fresh 123 full qualification failed in its first tutoring phase: the photograph
again read `1.6` correctly, but two unsolicited local VAD interruptions stopped
the photo-result speech. All later phases of that run are NOT RUN. The exact
failed JPEG is retained in `qualify123-live-tutor`, SHA-256
`cbb6c3180d9e75ae2afb428c96621e414a2ac6a817f979c8766514ba53e5fcbf`.
The separate `probe123-photo` diagnostic reproduced one unsolicited cut and
saved eight seconds of raw mic, DAC reference, linear and NLP PCM with zero
capture skips. Exhaustive fine/signed-lag correlation did not recover an echo
match; widening the lag search is not supported by this evidence. Several
confirming frames instead contain a narrow roughly 2 kHz periodic residue.
An offline averaged-band experiment rejected this captured false confirmation
and retained the first confirmation time of eight recorded real interruptions;
it is a hypothesis pending fresh hardware qualification, not a passing release.

The user requested clearer actual camera capture. Candidate 124 compares the
official OV5647 1280x960 RAW10 binned and 1920x1080 RAW10 modes with the existing
800x640 mode. Only question captures select these modes. The experiment also
uses direct RGB565 JPEG input to avoid an RGB24 intermediate copy. Default
capture remains 800x640 until physical detail, color, framing and voice-load
evidence establishes a suitable mode. No upscaled image is an acceptance result.

Candidate 124 SHA-256:
`4fc02bfca0ba65149d3e15b2e094139caff4f45df3136f9903571eedb37da449`.
Build, guarded installation, protected-region verification and readback passed.
Its fresh 800x640 direct-RGB565 JPEG retains the yellow fastener and all three
equations; the reader returns the third coefficient as `1.6`
(`camera124-mode0`). The independent higher-mode captures are retained in
`camera124-mode1` and `camera124-mode2`. Mode 1 produces a wider 1280x960 image
without larger question characters and misreads the second denominator as 5
and the third coefficient as 18. Mode 2 produces genuine larger characters at
1920x1080 and reads `1.6`, but its central sensor window cuts off the first
question and part of the second. Both capture operations succeeded; neither
passes the complete-question acceptance check (`camera124-comparison.json`).

Candidate 125 keeps that HD format's dimensions, clock, blanking and sampling,
and tests an upper-field window by changing only the four verified OV5647
vertical-start/end bytes: rows 434..1521 become 0..1087. This is a sensor readout
shift, with no enlargement or reconstructed text. Mode 3 is the proposed AOQ
question default; WSS and live video keep the existing default. It is still
pending actual framing, color and full-equation checks. Format/register storage
also remains alive until camera deinitialization, as required by the sensor's
retained format pointer. The managed sensor driver is unchanged.

Candidate 125 SHA-256:
`b9a6928e3ccead2e7ad7c1378bd3e5011eb831dbf4feeb212dca66233ea39ea0`.
Guarded installation/readback and C3/WSS/AOQ-without-probes builds passed.
`camera125-mode3` verifies an actual 1920x1080 upper-field photo: all three
equations fit, yellow/red colors match the physical preview, and all three
expressions are transcribed correctly. Its JPEG SHA is
`779a14414b9a5eb9674647211acc0a1e6d06d8672dc331a7ba6f31fb61d8b6cd`.
This standalone capture had voice off and is not simultaneous voice acceptance.

The fresh full 125 qualification failed in the first photo with voice active.
The exact serial diagnostic is `jpeg.encoder: no mem for 2097152 bytes encode
buffer`; the job returns `jpeg_encode_failed` without a captured JPEG. This is
a memory-allocation failure, not a new OCR or acoustic failure. All remaining
125 phases are NOT RUN. The failed report is retained in `qualify125-live-tutor`.

Candidate 126 releases the two live camera buffers after the chosen photo and
LCD preview are frozen, before JPEG output allocation. The selected RGB565
copy remains owned by the encoder caller. Camera deinitialization is awaited
with a bounded timeout and recorded as `camera_released` in capture status;
new visual generations and video still restart normally. JPEG resolution and
quality are unchanged. Fresh full qualification will additionally audit this
release after the successful tutoring and progress-over-photo phases.

The prospective 125 strong-vote change now passes all 113 host checks and the
15-capture replay. The new captured regression fails against the original
predicate and passes with the new predicate under Address/UndefinedBehavior
sanitizers. Eight positive interruption captures retain their first confirmation
times, and seven negative captures produce no confirmation. This remains
offline evidence until installed and exercised on the device.

Private evidence and resumable state:
`.local/aoq-native-20261107/resume-state-20261110.json`.

### Candidate 127: execute explicit capture requests

Candidate 126 installed with verified readback, but its fresh qualification
stopped before capture: the complete utterance `请用摄像头重新拍一下当前题目。`
received `好的，我重新拍一下。` without any tool call or background job.
The missing action is retained as a failure, not counted as a camera pass;
the other seven phases were NOT RUN (`qualify126-live-tutor`).

Candidate 127 admits a small exact set of complete capture commands into the
existing background `device_tutor` queue after accepted input identity is
established. Negations, quoted phrases, conditional requests and combined
questions remain under normal language handling. The admission checks tool
constraints, retains the real user/session identity, and has no fabricated
provider function-call ID. Actual completion uses the existing identity-checked
and once-only job notification path. An acknowledgement is still insufficient
for acceptance. A generation guard also protects camera-release status when a
capture is superseded.

Candidate 127 SHA-256:
`4478c7f535a63cf43c07c60b387c938e68da8bacfbdc5b06f258fd691c428b8c`.
All 113 host checks, the AOQ session/media/conversation suites, WSS and AOQ
without probes build successfully. C3 source/symbol isolation remains verified
against its unchanged candidate-125 build. C3 and WSS hardware were not tested.
Guarded installation, protected regions and readback passed. Fresh physical
qualification confirms HD capture with active voice, all three correct source
equations (simple LaTeX notation), selection of the original third question,
solve completion, reading, full explanation, step two, and return to the same
rendered step. Actual `reading.jpg`, `step-two.jpg` and `resumed-reading.jpg`
were inspected. There was no unscheduled interruption in these passed steps.
The selected JPEG SHA is
`611e2b7134b0b3786290bc7df7550f3baf03bbcdc094ddd5af91dc062f679cd3`.

The final `我已经懂了，这道题讲完了。` turn failed: the full utterance received a
spoken acknowledgement, but no operation changed teaching state from reading
to done. The full tutor phase is FAIL, and all subsequent phases are NOT RUN.
Audio and trace evidence are retained in `qualify127-live-tutor`. Failed-photo
export timed out after 656384 of 800805 bytes, so there is no complete JPEG
artifact from this run; its on-device photo identity/hash remains recorded.
The cleanup deadline masks the original missing-done error in the top-level
report; the step records and serial history preserve the actual failed state.
Workflow checks do not certify arbitrary generated solution prose; this run's
step-three display also contains inconsistent verbal fraction ordering.

Candidate 128 adds explicit whole-lesson completion phrases to the existing
local tutor control route. Status questions such as `这道题讲完了吗`, negations,
quotations and compound commands do not match. The same question/revision
checks and actual `done` operation apply, including a remembered done choice
if solving is still in progress. Targeted intent/state checks pass. Candidate
128 SHA-256 is `0226282bab1a3a88dffb976b22764c7f6edb4dc61595014e8363887c3afffd44`.
Guarded installation, protected data and readback passed; all 215 frozen firmware
inputs match. Fresh physical qualification is running; no Git push has occurred.

The photo audit now accepts exact equivalent plain-text/simple-LaTeX notation
without changing any numbers. Tests still reject the original wrong `16`, `18`
and denominator `5` readings. The physical tutor harness additionally requires
the real local `done` receipt to match the current question and revision.

Candidate 128 completed all eight physical tutoring steps, including actual
`done` with question 2/revision 1. No unscheduled interruption occurred.
The three LCD snapshots were inspected. Its camera audit reports 1920x1080,
`camera_released=true`, zero audio skips and a 1823 ms capture window.
The exact photo was fully exported and viewed: `qualify128-live-tutor/captured-photo.jpg`,
776987 bytes, SHA `2d1b5089a43dd1a7ece25aae8630a7b6f8a283c88d8f50c19f14823b885b18d9`.
All three original equations are correct in the camera result. The capture
window excludes upload/OCR; debug USB image export is not product latency.

The following progress phase passed ten cached replies at 1406..1594 ms
(median 1507.5 ms), then failed the subsequent photo acknowledgement's
20-second deadline. An independent unchanged-gate diagnostic reproduced this:
the camera and reader were already ready with all three correct equations,
but the first model response arrived about 28 seconds after admission and
incorrectly said the camera had failed. The actual background completion
notification arrived later. The buffer-duration error during that wait is
also retained in the trace. These failures are not counted as passes, and the
remaining six phases were NOT RUN on 128.

Candidate 129 changes only the conversation control source from 128. An accepted
explicit photo job uses the existing cached reading receipt and input rearm;
it does not request a speculative model acknowledgement. The actual completed
job still goes through the existing identity-checked, once-only notice path.
The new qualification starts with the original ten-status-to-photo regression
and additionally verifies a real cached photo receipt without an intervening
model request. It then runs tutoring, all task states, interruption, ordinary
questions, long speech, context and video. Installation and qualification are
pending; no Git push has occurred.


Candidate 129 installed with protected ranges and readback verified, SHA
`dee7dbc7f7b2f9e0c04e973e219d8afc05961485a88fb36c0b890a5a7b7c1a19`.
Host checks 113/113, WSS and AOQ without probes PASS; unchanged C3 build isolation
PASS. Physical progress and full tutoring PASS. Eleven cached progress replies
were 1406..1593 ms (median 1468 ms); photo admission was 1594 ms.
Both actual HD captures preserved all three equations. The tutor source JPEG,
767055 bytes, SHA `a8dddc735601f51d0229ba1b0d6026c4e443a94a2f96360a21f9c7d7333e3b52`,
was fully exported and inspected along with the three LCD photographs.

The following states phase FAILS: a third sequential capture could not allocate
its 4,147,200-byte input copy. Two live RGB565 buffers still occupied 8.3 MB;
total free memory remained above the request but the largest block was too
small. The original harness first reported a missing cached receipt because
camera logs interleaved inside two USB JSON records. The actual capture failure
is independently recorded by the JPEG allocator and tutor capture_error state;
it is not waived as a diagnostic problem. Remaining five phases NOT RUN.
Whole candidate 129 remains NOT QUALIFIED; no Git publication.

Candidate 130 releases the old retained JPEG allocation before fresh capture,
and on cancellation (string clear previously retained capacity). Terminal
acquisition failure freezes/releases the camera buffers while retaining the
actual failed result. SDK logs now use the same queued USB output path as
structured events, avoiding a separate stdout path inserting bytes into queued
records. Native build PASS; compatibility, guarded install and physical repeat
are pending. No audio threshold, gain, capture resolution or acceptance gate
was relaxed.


Candidate 130 SHA `f4a4b211198f34d04ec4ff909dfe741e2d7f289c0af210b171cae0adb8abcc26`
installed with guarded preservation/readback PASS. Native, WSS, AOQ without
probes and a fresh C3 build PASS; host 113/113 PASS. Progress PASS: eleven
cached replies 1438..1922 ms, median 1562 ms. Both HD photos in qualification
preserved all three equations. Tutor FAIL at explanation: unscheduled local
interrupt at device 310975 ms. The full eight-second rolling capture has no
raw/sent skips. At the confirming frames, a roughly 1.84 kHz residual plus
lower noise passed the previous voiced-evidence checks. Headphone loopback is
zero after the fixture. Independent room/mic/reference offline ASR contains
the device's own explanation and identifies no extra speech; this is machine
analysis, not human acoustic acceptance. Exact failed JPEG exported:
`qualify130-live-tutor/failed-photo.jpg`, 780465 bytes, SHA
`a0ea45d41af760864cef852e178cc1f16da18a4661a041a9b79074f2225bcab8`.

An independent same-image states regression subsequently completed third and
fourth HD acquisitions without the prior allocation failure. Selection,
confirmation, solving, complete and reading-progress checks PASS. Final
cancellation FAIL: the complete request was transcribed, the model said it
cancelled, but no operation executed and the photo result arrived later.
Original qualification and independent failure are retained; no publication.

Candidate 131 rejects short-period high-frequency evidence in whichever
channel supplies a strong duplex vote. It leaves actual uplink PCM, ordinary
votes, endpoint and thresholds unchanged. Replays of 16 independent captures
retain eight previously confirming real interruptions (seven unchanged, one
28 ms later). Eight other traces remain nonconfirming, but one is the retained
old missed-interruption run `batch100-cold-barge`, not a negative control.
That limitation is explicitly retained; replay does not prove full acceptance. The new captured regression fails on the
130 predicate and passes on the new predicate; earlier recorded failures remain
in the suite. A low-band periodicity alternative was rejected because it lost
three real interruptions. Explicit, scoped whole-utterance tutor cancellation
now executes the regular cancel operation and checks its actual result before
playing the cached cancelled receipt. Negations, quotations and other combined
requests stay outside this exact local route. Build/install/qualification pending.


Replay scope correction: these predicate replays hold the original eligibility
flags from each recording's older frontend. The historical batch100 missed
interruption is retained; its nonconfirmation in this partial replay neither
makes it a negative control nor proves a current full-frontend reproduction.
The complete fresh physical suite is still required. A tone-only alternative
also left this trace nonconfirming and was not adopted.
