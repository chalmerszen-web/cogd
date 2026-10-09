# Context protocol v1

All modes use immutable `agent.context.event/1` records with `event_id`, `device_id`, `user_id`, `session_id`, `device_seq`, `lamport`, `type`, `actor`, `content`, `policy`, `parents`. See `protocol/context-event.schema.json`.

`event_id` is `<device_id>:<20-digit device_seq>`. Before using a sequence range the ESP adapter durably reserves 128 numbers in NVS. Reboot skips unused reserved numbers. Torn writes and bank compaction cannot reuse an event ID. Integers stay within JSON's exact 53-bit range.

`message` records the incoming user text. `tool_result` records the internal dotted tool name, original call ID, turn ID and JSON result. `turn` is the v1 extension containing all messages of one successfully completed turn, including assistant tool calls and tool replies. This makes complete-turn history recovery and trimming explicit. `error` records a failed turn and whether output/effects occurred. `memory` and `tombstone` carry a key and optional string value. Reasoning tokens are not saved as conversational history.

## Storage

M2 uses a 2 MiB `ctx` partition with two 1 MiB banks; the adapter also understands the older 1 MiB geometry. A 64-byte bank header includes magic, version, generation, record sequence high-water mark, header CRC and commit word at byte 60. A 32-byte record header includes magic, payload length, record sequence, kind, payload CRC, header CRC and commit word at byte 28. All numbers are explicitly little-endian; payloads are JSON, padded to four-byte alignment. CRC-32 uses polynomial 0xEDB88320. Layout migration is a separate verified transaction in `M2_MIGRATION.md`.

Append writes header, payload, then commit word. Recovery validates length, CRCs and commit. An invalid suffix can be discarded only if no valid committed record follows it; later valid data makes this middle corruption and startup reports `corrupt`. A recovered tail forces compaction before further appends. Initialization is an explicit one-time migration, not a corruption fallback.

0.6.2 reserves event space using the actual serialized record size, including its 32-byte header and padding. The previous maximum-request threshold could unnecessarily compact a nearly full bank during music, where the audio arbiter rejects erasure with `busy`, even though the result record fit. Small appends now use remaining space without erasure; required compaction still commits a complete replacement bank before reclaiming the old one. Host tests cover erase-unavailable appends, true full storage, restart preservation and the existing power-loss cases. No retention or partition capacity is reduced.

Compaction first checks that retained records plus a new snapshot fit. It erases only the inactive bank, writes an uncommitted generation header, snapshot and retained events, then commits the new bank last. The prior bank remains intact until a later rotation reuses it. Interrupted erasure/copy/commit leaves the older committed bank authoritative. No filesystem recovery is required.

The snapshot stores Lamport time, pull cursor, local ACK sequence/record watermark, mode, exact seen ranges and mutable-key winners including tombstones. M2 adds optional history-budget and summary fields; old snapshots still load with defaults. Retention keeps all unacknowledged events and recent complete turns; CLOUD keeps one cached turn after compaction, LOCAL/HYBRID up to 128. LOCAL disables sync without discarding pending records. A full retry log returns `full` when retention prevents reclamation.

0.6.3 adds an explicit LOCAL maintenance exception for records already exported to the user's computer. `tools/archive_context.py --apply` reads and CRC-checks the context partition, writes every event and a SHA-256 manifest to `context-archives/`, flushes and verifies the archive, then submits the matching generation and record boundary over USB. An existing verified archive may be supplied with `--archive <directory>`. Close the serial terminal before running this script. No cloud upload or recording capture occurs.

The USB command is `agent context archive {"generation":G,"through_record":R}`. It is not an LLM tool. The snapshot's optional `archived_record` field is independent of cloud ACK and defaults to zero for old snapshots. Compaction can reclaim exported records through R while retaining the recent 128 complete turns, memory/tombstone winners, the summary and its available source turn. All records after R remain protected. A stale generation, locked prompt or non-LOCAL mode is rejected; a previously committed archive boundary is idempotent. The archive watermark and replacement bank commit together; interrupted rotation keeps the old bank valid. This operation is not automatic history eviction: unexported pending records continue to block reclamation when full. Older archived conversations remain on the computer and are no longer available to on-device search.

## Merge and sync

Exact sorted disjoint sequence ranges provide bounded deduplication for remote peers, including out-of-order arrivals. Exhausting remote peer/range/key capacity reports `full`. A newly received remote event updates `lamport = max(local, remote) + 1`; replaying a duplicate does not advance it.

M3 fixes local deduplication after repeated reboots: this device alone allocates its identity's monotonically increasing IDs. Unused NVS reservations are permanently retired, so local seen state collapses to `[1, high_water]` rather than consuming a range for every reboot. Gateway echoes at or below this watermark are idempotent; a gateway cannot mint an unseen future ID for this device (`forbidden`). Foreign devices retain exact sparse ranges. Existing v1 snapshots load unchanged, and the next local append coalesces old local ranges without deleting any event, summary or pending item. Cloned writers must use different device identities. Forty host reboot/reservation cycles, compaction, echo and future-ID tests cover the regression.

Mutable keys choose the maximum `(lamport, device_id, device_seq)`, with lexical ASCII device comparison. The sequence is only the deterministic tie-break for two events from the same origin at equal Lamport time. Tombstones participate in the same order and survive snapshots, preventing an older value from returning.

Live nondeleted memory values are added to the prompt as explicitly labeled data. Deleted values are excluded. Prompt construction preserves the system/data prefix while trimming only complete conversation turns.

One sync cycle pushes at most two pending events, saves the acknowledged sequence, pulls a bounded page, saves each event, commits the pull cursor and sends ACK. A failed cursor commit causes a safe replay. A lost push response causes an idempotent repost. Conversation work preempts sync; transient failures leave retry records in Flash. Missing Gateway configuration leaves pending data visible.

The development protocol uses one configured user/session per device store. Changing that identity while events exist is rejected; multi-account migration is outside M0. History bytes are data, not trusted device instructions; device effects require the current static tool registry and resource policy.

## M2 prompt, recall and summary

DIRECT selects a contiguous suffix of complete turn records from at most 128 descriptors under a 64/128 KiB serialized-history budget. It measures the whole request including current turn, memory/summary and tool definitions; the independent request ceiling is 160 KiB. A selected record cannot move during transmission. Request cancellation and failed transport release the lock; reading detects CRC damage.

`agent.context.search` accepts `query` (1–128 UTF-8 bytes), `before` (optional exclusive WAL sequence) and `limit` (1–3). Query is an exact event ID or phrase (ASCII case-insensitive; other UTF-8 bytes exact). It searches all retained events, including history outside the descriptor window, returning newest-first excerpts of at most 240 bytes with event IDs and `record_seq`. It is not semantic embedding search; reclaimed acknowledged history cannot be found.

Search and summary source scans have a ten-second deadline, checked with cancellation between records. They do not share the two-second budget of immediate device tools: a CRC-checked scan of a nearly full 1 MiB bank took 3.062 seconds on the actual C3. A timeout/error never becomes a successful tool result.

0.11.30 preserves the 240-byte excerpt limit while including a paired answer
when a user message matched: `user: ...` followed by `assistant: ...`. The latter
is the last nonempty assistant text before another user message; tool payloads
and later questions are excluded. Both portions truncate on UTF-8 boundaries.
This fixes repeated questions crowding out their answers in recent search hits.
Exact phrase matching, ordering, pagination, event IDs and retained data are unchanged.

`summary` events contain `text` (1–1536 UTF-8 bytes) and `through_seq`. Local creation verifies that this sequence identifies a completed local turn. Snapshot state keeps summary text, source device/sequence and winner identity. The deterministic winner order is `(lamport, device_id, device_seq)`. Summaries enter prompts as labeled assistant history with provenance; the LLM may call get/set tools but no automatic background summarizer is installed. Original pending events remain retained.
