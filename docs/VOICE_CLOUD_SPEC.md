# Device voice conversation

Authorized objective: keep the current C11 Agent and bilingual Xiaoyan wake
backend; add device-side VocalignTech ASR/TTS around the existing DeepSeek
conversation/tool engine. Test with synthetic speech replayed by the computer.
Human recordings and real-distance claims are outside this development stage.

## Requirements and implementation order

| ID | Requirement | Evidence required |
|---|---|---|
| VC-01 | Wake -> endpointed microphone clip -> ASR -> DeepSeek -> audible TTS -> rearm | Serial timestamps, device clip and external recording of multiple conversations |
| VC-02 | Credentials provisioned over USB from the user-named local key file | Only configured booleans in status; keys absent from logs/source |
| VC-03 | 200 KiB selected conversation history, streamed from Flash | Persisted budget=204800, request and heap telemetry; existing partition and records preserved |
| VC-04 | Bounded memory and exclusive audio ownership | No whole PCM allocation; shared idle engine workspace; repeated heap/stack/DMA checks |
| VC-05 | Errors and cancellation release TLS/audio/clip ownership | Host failure injection plus board cancel/offline recovery |
| VC-06 | Future streaming ASR/TTS provider interfaces | Explicit capability flags and open/feed/finish/cancel contracts; unavailable operations reported as unsupported |
| VC-07 | Reproducible build, backup and rollback | Source/build hashes, full Flash before, app-only write, non-app byte comparison |

The current platform endpoints are asynchronous file ASR and non-streaming model
TTS. Downloading WAV incrementally is transport streaming, not model streaming.
Do not claim otherwise. Use authenticated platform HTTPS only for platform APIs;
temporary object upload and result download never receive the platform key.

One network worker owns the complete speech turn. The audio task publishes a
committed clip and holds listening; ASR streams 16 kHz PCM from that clip as WAV.
The same worker passes recognized text through the ordinary persisted Agent
conversation, then feeds validated WAV PCM into a bounded speaker queue. It
rearms after playback or an explicit error, with no acoustic self-trigger loop.
USB status/cancel remains available. Never automatically resubmit a paid POST
after an uncertain response; safe ASR polling can retry within a fixed deadline.

200 KiB refers to the history selected for an LLM turn, not a new RAM buffer.
The 2 MiB physical context partition and existing records remain intact for this
application-only upgrade. Status/report must distinguish these capacities.

First verify provider formats with one synthetic phrase, then host protocol and
WAV tests, build and backup/flash. Next run a small acoustic end-to-end batch,
repair observed faults, then a bounded multi-turn stability batch. Retain all
failures. Do not start another wake-model training sweep in this integration.
Keep the earlier E/F and E/K measurements and compileable 0.6.3 rollback.

Authoritative provider documentation, fetched 2026-09-22:
- https://platform.vocaligntech.com/docs/qwen-asr.html
- https://platform.vocaligntech.com/docs/openai-tts.html
Snapshots are in ignored artifacts/voice-cloud/docs. Local recordings,
credentials, backups and provider responses remain outside Git.
