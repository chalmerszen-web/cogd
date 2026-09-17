# Current audio interfaces

Baseline0.6.0-upgrade keeps short scores, four-voice songs, microphone levels,
local clip capture/replay and the accepted experimental wake chain. Historical
subjective M1/M2 feedback is recorded in ACTIONLOG/TEST_REPORT. M3 objective
music checks and pending subjective acceptance remain separate in MUSIC_REPORT.

## Speaker and score

The board uses PDM DAC onGPIO6/7 with PA GPIO3,24-kHz mono PCM. Default master
volume is80. `device.audio.play_score` accepts BPM40–240, sine/triangle wave,
at most64 `[pitch,ticks]` pairs and30seconds. Pitch0 rests; pitched notes36–96;
ticks1–16 are sixteenth notes. Existing interfaces are unchanged.

`device.audio.play_song` adds a bounded library of two-bar phrases and their
arrangement for melody, bass, pluck and light percussion. At most16 phrases,
128 stored notes,32 arrangement segments and75seconds fit the4-KiB tool
argument limit. Each note has velocity and gate. The shared C renderer uses
Flash tables, integer oscillators, natural decay/release, mix headroom and
limiting, with no allocation during rendering. See MUSIC_SPEC and the protocol
schema for exact fields; `examples/music/sunny_walk.json` is a complete example.

Admission is asynchronous. `playing`, progress/job ID and final error establish
completion; an accepted tool return alone does not prove sound was heard.
`device.audio.get`, `.stop`, `.volume` and `device.mic.set` remain available.
DeepSeek wire names use underscores. Every reply still has at most four tool
calls and a turn at most four continuations. The legacy interfaces remain valid.

## USB operations

```
agent audio status
agent audio volume 80
agent audio play {"bpm":120,"wave":"sine","notes":[[60,4],[64,4],[67,8]]}
agent audio song get
agent audio stop
agent mic on
agent mic status
agent mic off
agent audio capture 5000
agent audio replay
agent wake on
agent wake status
agent wake off
agent cancel
```

The default mic is off. `mic on` reports local RMS/peak and freshness, not a
recording or calibrated sound-pressure level. GPIO2 ADC acquisition runs at
32kHz with12dB attenuation and decimates to16kHz. ADC clipping and DMA overruns
stay visible. Board part identities and calibrated speaker response remain
limited to the hardware facts already established.

Manual capture accepts100–10000ms. Wake recording starts after a confirmed
Hilexin hit and start cue, then uses the accepted B source/TEN/endpoint chain.
Both replace the single448-KiB clip slot. Commit/CRC rejects partial files but
does not transactionally retain the previous recording. Packedv3 wake capture
is lossless, oldv1/v2/v3 are readable, and manual capture writesv1. No recording
is transcribed or uploaded. See AUDIO_CLIP_FORMAT and WAKE_VAD_SPEC.

Replay uses the existing local voice filter/noise reduction; the stored PCM is
not modified. Speaker output can couple into the microphone. Successful local
signal or DMA checks are not a claim of human acoustic quality.

## Concurrency and build

ADC/I2S are owned by the high-priority audio worker. Resource leases coordinate
music, capture/replay, wake, plans and context erasure. Mic levels, USB status,
light control and cancel remain bounded. Cancellation waits for the owner to
stop DMA before reuse. Direct JSON and song upload share admitted workspace;
an incomplete/cancelled/expired upload cannot start playback.

`tools/build_agent.ps1` selects the supported full configuration.
`-BuildDir build-agent-noaudio -NoAudio` excludes audio and speech resources.
Current24/12 host suites and the integration checks are documented in BASELINE.
The1000-case same-device report applies to its frozenB binary, not a new
1000-case certification of the combined project release.
