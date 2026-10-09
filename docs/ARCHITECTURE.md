# Current Agent architecture

The project uses plugin ABI major 5. Project-owned
firmware is C11. `core/` provides the static registry, typed operations,
lifecycle, events, error contract and resource limits. It contains no ESP-IDF
or FreeRTOS includes. Board/SDK details stay in `platform/` and `boards/`.

## Execution and ownership

USB runs the main command loop. One network worker owns each conversation and
context mutation, admitted through a single-entry queue. Status and cancel use
local/atomic state. Background synchronization yields to conversation admission.
A work mutex also protects provisioning and temporary workspace reuse.

The executor's transient messages, reply and SSE live in caller-owned
`agent_engine_workspace_t` (29836 bytes on C3), separately from its unchanged
24577-byte request buffer. The engine retains configuration, turn identity,
circuit state and binding pointers outside those borrowable bytes. A caller
may use contiguous `agent_engine_storage_t` (54416 bytes) or bind disjoint
blocks with `agent_engine_bind_parts`. Both move the context serialization
alias. Active answer/progress sinks or locked history selection reject a
rebind; a detached engine rejects new turns before effects.

Default builds still use static contiguous storage. The opt-in text-candidate
build uses a 28216-byte capture arena and allocates its bounded24KiB IMA cache
in pages as PCM arrives. After all borrowers join, restoration allocates state
and request buffers separately, each below32KiB; partial failure frees both.
Legacy capture explicitly requests its full contiguous arena before borrowing
it. No input, history or request capacity changes with storage ownership.
See `docs/VOICE_SPLIT_WORKSPACE_REPORT.md` for the experimental resource and
device results; ordinary interaction and whole-flow acceptance remain open.

The LLM executor persists a user event, selects complete-turn history, builds a
request, validates each complete tool batch, executes through typed operations,
and returns original call IDs. Only completed turns become normal history;
failed/partial turns remain audit events. JSON and SSE are bounded, including
UTF-8, fragmentation, malformed input and four tools/four continuation rounds.
SSE borrows the request scratch after the HTTP body has been sent. Nested scores
are parsed directly from JSON nodes without reserialization buffers.

DIRECT requests stream selected CRC-checked Flash records through at most
4096-byte writes. The exact body length is measured first. The selection is
locked for the attempt, including safe retry, and released on every exit.
A 128-KiB history budget does not allocate a 128-KiB RAM body. Gateway uses a
separately bounded buffered path. Full limits are in `SPEC.md`.

A transient transport error permits at most one retry with jitter before any
text or tool side effect. Three consecutive transient failed turns open a
30-second circuit. Wi-Fi reconnect has capped backoff. TLS uses the configured
CA or certificate bundle; there is no insecure fallback. USB cancel returns
promptly, while an SDK connection attempt may require its bounded timeout.

## Persistent context

The 2-MiB raw context partition has two 1-MiB banks. Version, sequence, length,
CRC and commit markers govern recovery. Compaction preserves retained/pending
events and commits the replacement bank before reclaiming the old one.
Tail damage and middle corruption remain distinct errors. LOCAL/CLOUD/HYBRID,
Lamport merge, deterministic LWW, tombstones, cursors and durable sequence
reservations are unchanged. History/summary content is data, not instructions.

Recall scans with a ten-second deadline and returns at most three excerpts.
Summary provenance refers to retained source sequence numbers. Network and
context allocation do not consume a larger history budget as a resident buffer.

## Hardware facts and display

DIRECT system messages include the board's constant hardware prompt before history.
The typed tool table carries a hardware query and display operations. The pure-C
display plugin parses configurations and renders a 320-byte RGB565 row; SPI and
wall time stay in the ESP-HI adapter. The existing control task advances LCD reset,
initialization and four rows per tick. LCD owns GPIO4/5/10 as a group (owner 5)
until stopped, then releases them as inputs. No extra task, full framebuffer or
per-second cloud request is needed. See DISPLAY_SPEC.md for the pinned register
profile, fixed UTC offset, asynchronous status and limits of visual verification.

## GPIO and action plans

The trusted board table exposes input/output and two PWM channels on
GPIO4/5/10/20/21, with buttons0/1/9 read-only. USB, Flash and managed audio/light
pins keep their existing ownership. A model cannot alter these capabilities.
Direct settings persist; plans restore their prior state on finish/cancel.

`plugins/control` preflights at most32 steps,8 repetitions and60 seconds, claims
resources as a group, then advances with a monotonic clock. The control worker
uses2048B at priority4. Direct operations return busy while a plan is active.
Claims remain held during asynchronous cleanup until DMA stops and state is
restored. Deferred cleanup does not replay a successful driver action.

## Audio, music and wake recording

One4096B audio worker at priority24 owns ADC/I2S lifecycle. Lazy DMA resources
are released on stop/end. A 10240B ADC pool covers80ms at32kHz; input is reduced
to16kHz. PDM DAC output uses24kHz mono. ADC/I2S interrupts remain available during
small Flash writes; whole-bank erasure coordinates with audio resource claims.

Songs use a710B phrase/sequence structure, two admission slots and a208B
renderer. Integer phase accumulators, Flash tables, four voices, velocity/gate,
decay, release, headroom and a final fade avoid render-time allocation. There
is no full-song PCM buffer. A bounded CRC/offset/timeout USB upload leases idle
request scratch and commits only a complete valid score.

The default build adopts the tested B wake chain: WakeNet9s hilexin plus the
fixed ADC8 keyword check, source/tonal/endpoint rules, and the Flash-backed TEN
worker. Keyword detection uses512-sample16-kHz blocks. The extra verifier uses
6144B at priority4, reads only published Flash sample/byte prefixes, and must
join before clip trimming/commit/cache reuse. Its model arena leases disposable
engine storage; speech and network roles do not race on that memory.

Wake is off at boot, pauses for dialogue/playback and records at most10seconds.
A successful CRC-checked recording precedes the completion cue. Packed v3 is
lossless; old v1/v2/v3 recordings remain readable. Manual recording writesv1.
See `WAKE_VAD_SPEC.md` and `AUDIO_CLIP_FORMAT.md`. There is no transcription or
microphone upload. The user accepted an experimental baseline; historical
strict voice targets are not newly certified.

## Resource evidence

Main/audio/control/network stacks are6144/4096/2048/15360B. Stack watermarks,
free/minimum/largest heap, ADC loss, render time and worker CPU/wall/source
progress remain distinct measurements. The current source is independent of
old build/research directories. Frozen dependencies and licenses live under
`third_party/`; integration results and hashes are in `BASELINE.md`.
