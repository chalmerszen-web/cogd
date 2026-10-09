#ifndef AGENT_ESP_RUNTIME_H
#define AGENT_ESP_RUNTIME_H
#include "agent.h"
#include "transport.h"

void esp_agent_run(agent_core_t *core);
/* All capture/ASR/candidate owners must have joined. Reclaims a compact
 * capture arena and restores the full engine/context serialization workspace. */
agent_err_t esp_agent_workspace_restore(void);
/* Candidate coordinator only, after joining every borrower and transferring
 * a validated session to independent storage. Retains primary for this restore. */
agent_err_t esp_agent_workspace_restore_retaining_candidate(void);
void esp_agent_write(const char *data, size_t size);
void esp_agent_status(char *output, size_t capacity);
uint64_t esp_agent_now(void);
bool esp_agent_online(void);
agent_err_t esp_agent_http_auth(agent_http_target_t target, char *base, size_t base_size,
                               char *auth, size_t auth_size, const char **ca);
agent_err_t esp_agent_http(void *ctx, const agent_http_request_t *request, agent_http_feed_fn feed, void *feed_ctx);
void esp_agent_http_release(void); /* Worker only; closes sockets, retains a fixed-origin session briefly. */
void esp_agent_http_expire(void); /* Discard an idle platform resumption handle after its short TTL. */
void esp_agent_http_forget(void); /* Work lock required; also invalidates resumption after key changes. */
/* Network worker only. One bounded connect-only preparation, no HTTP bytes.
 * The same authenticated origin/handle is claimed by the next ordinary request. */
agent_err_t esp_agent_http_warm(const atomic_bool *cancelled);
agent_err_t esp_agent_qianwen_auth(char *,size_t);
/* Audio builds; network worker/work lock, with every WS owner joined. */
void esp_agent_speech_ws_expire(bool forget);
/* Sole voice turn owner, before TTS starts. Warm opens only the primary
 * inference transport; no run-task/text/PCM or borrowed workspace. Open
 * adopts it once, within10s. Discard unused warm ownership on every exit. */
agent_err_t esp_agent_speech_ws_warm(const atomic_bool *);
void esp_agent_speech_ws_warm_discard(void);
/* Idle candidate worker only. Opens the isolated ASR transport without
 * consuming its creation event; transfer only after a release/acquire join. */
agent_err_t esp_agent_asr_ws_warm(const atomic_bool *);
#if AGENT_HANDOFF_PROBE
/* Primary owner, or its coordinator after join. Survives close, resets on
 * next successful ownership/open attempt. No live cross-task snapshots. */
void esp_agent_speech_ws_probe(char *,size_t);
bool esp_agent_speech_ws_tcp_probe(unsigned index,unsigned side,char *,size_t);
/* Descriptor published only across the blocking on_sent join; diagnostic
 * candidate owner may query it but never read, change, or close it. */
int esp_agent_http_probe_socket(void);
/* Work-lock owner only; one diagnostic request, reset after its turn. */
void esp_agent_http_probe_headers(bool);
#endif

#endif
