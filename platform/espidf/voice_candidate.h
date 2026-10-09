#ifndef ESP_AGENT_VOICE_CANDIDATE_H
#define ESP_AGENT_VOICE_CANDIDATE_H
#include "voice_fast.h"
enum { ESP_AGENT_CANDIDATE_PREFIX_BYTES=17024 };
enum { ESP_AGENT_CANDIDATE_COLD, ESP_AGENT_CANDIDATE_WARMING,
       ESP_AGENT_CANDIDATE_READY, ESP_AGENT_CANDIDATE_EXPIRED, ESP_AGENT_CANDIDATE_PARKED };

/* One ASR owner. begin borrows only the board's idle fast-capture prefix.
 * The cooperative transport starts its worker after ASR connects, while
 * capture continues. The original transport starts during ASR connection.
 * Worker owns primary WSS until join returns; no request is sent before the
 * ASR owner supplies a nomination.
 * finish MUST run after capture validation and before any engine reuse, even
 * on failure. It joins the worker; no task or borrowed pointer outlives the arena.
 * A successful direct reply can retain the separately owned primary transport
 * plus value-only protocol state for at most3turns/30s idle. Other owners close it.
 * finish receives the disjoint8192B ASR tail only after its owner has joined. An
 * admitted sentence can stream there. SELF joins the worker and speaker before
 * arena reclamation; task receipts use the live Flash spool, seal after worker
 * join, and release all staging pointers before reclaiming the arena. The
 * caller must ack_join every pending receipt, including on an engine error. */
void esp_agent_voice_candidate_begin(const atomic_bool *,void (*)(void *,const char *,const char *));
/* Coordinator owns the work mutex (or admitted voice turn). Warm borrows the
 * same compact arena before capture. discard joins before any memory reuse.
 * Only idle_state/cancel are safe from another task without that reservation. */
agent_err_t esp_agent_voice_candidate_warm(void *,size_t,const atomic_bool *,
                                          void (*)(void *,const char *,const char *));
unsigned esp_agent_voice_candidate_idle_state(void);
/* Work lock required. Reports an unexpired retained connection; no I/O. */
bool esp_agent_voice_candidate_retained(void);
void esp_agent_voice_candidate_cancel(void);
void esp_agent_voice_candidate_discard(void);
void esp_agent_voice_candidate_ready(void);
/* ASR-owner notices only: select the setup boundary and start at most once.
 * A failed/cancelled ASR still requires finish/discard to release its arena. */
void esp_agent_voice_candidate_asr_notice(const char *);
/* ASR owner's open hook. Claims the untouched idle connection once, or uses
 * the ordinary open path after a failed warm attempt. No session is replayed. */
agent_err_t esp_agent_voice_candidate_asr_open(void *,const atomic_bool *);
void esp_agent_voice_candidate_revision(unsigned);
void esp_agent_voice_candidate_preview(const char *);
agent_err_t esp_agent_voice_candidate_finish(agent_engine_t *,const char *,agent_err_t,
                                            void *pcm,bool *handled);
/* Optional split finish for an admitted task receipt. When deferred() is true
 * the worker and capture arena are STILL LIVE. Only request scratch may be
 * allocated; caller MUST finish again on every exit before any arena reuse.
 * The second finish preserves admission/deadline and leaves persistence to
 * the formal engine. Direct answers, revoked input and local tools stay joined. */
agent_err_t esp_agent_voice_candidate_stage(agent_engine_t *,const char *,agent_err_t,
                                           void *pcm,bool *handled);
bool esp_agent_voice_candidate_deferred(void);
#if AGENT_HANDOFF_PROBE
/* Diagnostic only: bind the real capture allocation without an ASR owner.
 * When hold is true, pause at validated output and after drain until resume.
 * Otherwise publish readiness once and keep receiving under normal scheduling.
 * Caller MUST finish/stage on every result; existing deadlines/cancel apply.
 * inject_failure fails the joined producer after a real request was sent. */
agent_err_t esp_agent_voice_candidate_probe(void *,size_t,const atomic_bool *,
    void (*)(void *,const char *,const char *),const char *preview,bool inject_failure,bool hold);
#endif
const esp_agent_voice_ack_t *esp_agent_voice_candidate_ack(void);
agent_err_t esp_agent_voice_candidate_ack_join(agent_err_t);
#endif
