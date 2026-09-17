# Gateway protocol

M2 raises the capability-list bound to 32; firmware advertises 19 tools with audio, 12 without. Each response still permits at most four calls. The mock recognizes music/melody requests and returns a deterministic three-note score followed by a continuation acknowledgment. The same turns endpoint, turn ID and original tool-result IDs remain required. Score and plan schemas describe stricter device-side validation; the mock does not invent access to unavailable hardware.

The development mock uses Python standard-library HTTPS and SQLite with WAL / synchronous FULL. Cryptography is used only to generate a local test CA and short-lived server certificate. Server certificates include the configured LAN IP SAN. The device verifies both trust chain and hostname; there is no insecure flag.

| Endpoint | Request | Response |
|---|---|---|
| POST `/v1/context/events:batch` | device/user/session + 1–4 immutable events | accepted count, acked_seq |
| GET `/v1/context/events?cursor=...&user_id=...&session_id=...&device_id=...` | scoped cursor query | up to two events, durable cursor, more |
| POST `/v1/context/ack` | scoped cursor | ok, cursor |
| POST `/v1/agent/turns` | schema, scope, turn_id, round, stream, current-turn messages, capabilities | compatible chat-completion JSON or SSE |

All calls require `Authorization: Bearer <local test token>`. Request/response bodies are bounded to 24 KiB. The mock stores immutable event IDs uniquely, rejects different data for an existing ID, scopes pulls by user/session and makes ACK monotonic. It stores response rounds under `(user_id, session_id, turn_id, round)` and verifies complete original tool call IDs on continuation. The same request is replayable; changing an existing turn round returns a protocol error.

The device sends only the current turn through the turns endpoint; prompt assembly belongs to a future production Gateway. The mock is deterministic: ordinary input returns MOCK_OK; light requests issue a light tool and then MOCK_LIGHT_OK; `all tools` exercises all four tools. It performs no real model inference. `[loop-tools]` deliberately requests tools repeatedly for the round guard test.

Run `python tools/mock_gateway.py --san <LAN-IP>`. Files under `.local/mock/` include `ca.pem`, `server.pem`, private keys, token and `context.db`. Run host validation with `python host_tests/test_gateway.py`. Only public CA material is sent to the device. If the host IP changes, generate a new certificate directory and provision its public CA.

Fault injection is a **local file**, `.local/mock/fault.json`, never a remotely callable administration endpoint:

```json
{"path":"/v1/agent/turns","status":429}
```

Supported fields: `status` (401/403/429/500/503), `delay` (up to 65 seconds), `mode` (`disconnect`, `malformed`, `truncated`), `fragment` (body slice size). Remove that local file to restore normal responses. For invalid TLS testing, run a second server with an unrelated CA and retain the first CA on the client. Tests must record actual device errors, not infer device behavior from host-only results.

This token is a local test credential. Production identity, user authorization, key rotation, rate limiting, privacy retention, external hosting and a real cloud model backend are outside M0.
