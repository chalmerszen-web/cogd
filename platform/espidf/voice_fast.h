#ifndef ESP_AGENT_VOICE_FAST_H
#define ESP_AGENT_VOICE_FAST_H
#include "engine.h"
#ifndef AGENT_CONTEXTUAL_CACHED_ACK
#define AGENT_CONTEXTUAL_CACHED_ACK 0
#endif

/* 29 ms at16kHz. Base64/JSON + masked WS + TLS1.2 CBC-SHA384 overhead
 * and TCP timestamp allowance fit the board's1440-byte MSS. A smaller
 * negotiated path MSS can still split it; this is not a packet guarantee. */
#define ESP_AGENT_FAST_UPLOAD_SAMPLES 464u

/* One network worker owns the session from capture through finish. The final
 * 8 KiB of engine scratch remains outside the live capture/VAD allocation. */
agent_err_t esp_agent_voice_fast_capture(agent_engine_t *,char *,size_t,
    void (*event)(void *,const char *,const char *));
/* Commits the complete flushed input once; final capture validation must join
 * before accepting intent or audio. A manual ASR-only commit may overlap it.
 * Opens the PCM sink before response.create. Ordinary replies close/drain
 * before return. A validated native acknowledgement may remain pending in
 * board-owned Flash; ack_join is then mandatory on every exit. A successful delegate leaves input intact
 * and does not persist it: the ordinary DeepSeek engine owns that turn.
 * A bounded future-tense sentence may stream native PCM before response.done.
 * Later text/calls cannot extend it, and full-response validation remains
 * mandatory before handing off the final ASR. No second TTS connection. */
agent_err_t esp_agent_voice_fast_finish(agent_err_t initial_error,bool *delegate);
/* Complete ASR and capture only. The caller has joined speculative owners,
 * restored the engine arena and released every previous PCM/ack owner.
 * Strict literals reuse ordinary tool policy and success-only confirmation.
 * Every return owns the turn, including failures: the caller must not replay. */
agent_err_t esp_agent_voice_fast_light(agent_engine_t *,const char *,
    void (*event)(void *,const char *,const char *));
/* A successful handoff may request a tools-disabled clarification only. */
bool esp_agent_voice_fast_clarify(void);
#define ESP_AGENT_ACK_MAX 96u
#define ESP_AGENT_ACK_CHARACTERS 32u
typedef struct {
    char text[ESP_AGENT_ACK_MAX+1];
    char language[4];
    bool spoken; /* Native PCM completed and drained; do not synthesize again. */
    bool pending; /* Validated, sealed native PCM; board owns playback. */
} esp_agent_voice_ack_t;
/* Stable until the next warm/capture bind. Empty text denotes a legacy or
 * rejected-route fallback, never a model-generated acknowledgement. */
const esp_agent_voice_ack_t *esp_agent_voice_fast_ack(void);
agent_err_t esp_agent_voice_fast_ack_join(agent_err_t result);
/* Selected the native interim PCM path, even if its drain later failed.
 * This classifies diagnostics only; it does not authorize final-answer reuse. */
bool esp_agent_voice_fast_ack_native(void);
/* Idle calls require work_lock. warm also requires a turn reservation and
 * paused wake inference; no source audio is sent before the next capture. */
agent_err_t esp_agent_voice_fast_warm(agent_engine_t *,char *,size_t,
    void (*event)(void *,const char *,const char *));
agent_err_t esp_agent_voice_fast_poll_idle(void);
void esp_agent_voice_fast_discard(void);
bool esp_agent_voice_fast_ready(void);
/* Volatile, set with voice off and the runtime work/turn locks held. */
void esp_agent_voice_prefetch_set(bool);
bool esp_agent_voice_prefetch_enabled(void);
/* Opt-in, bounded manual conversation reuse; unchanged full-input policy. */
void esp_agent_voice_reuse_set(bool);
bool esp_agent_voice_reuse_enabled(void);

/* A handed-off turn may play a cached progress phrase while DeepSeek runs.
 * The board owns its Flash source and PCM; no engine workspace is borrowed.
 * Zero-initialize per turn, begin at most once, and join on EVERY exit,
 * including a failed begin. join is also an engine before_tools callback. */
typedef struct {
    bool attempted,active;
#if AGENT_CONTEXTUAL_CACHED_ACK
    uint8_t kind;
    bool cantonese,queued,completed;
#endif
} esp_agent_voice_progress_t;
#if AGENT_CONTEXTUAL_CACHED_ACK
enum { ESP_AGENT_PROGRESS_GENERAL,ESP_AGENT_PROGRESS_SEARCH,ESP_AGENT_PROGRESS_MEMORY };
/* Selected queued text, not proof that it was heard. The caller needs a
 * successful terminal join AND the board speaker timestamp for that claim. */
const char *esp_agent_voice_progress_text(const esp_agent_voice_progress_t *);
bool esp_agent_voice_progress_heard(const esp_agent_voice_progress_t *,uint64_t speaker_started,
    agent_engine_t *);
#endif
agent_err_t esp_agent_voice_progress_begin(esp_agent_voice_progress_t *,const char *input,
    const atomic_bool *cancelled);
/* Called after either route settles. Detached/deferred or already supplied
 * receipts retain their owner; an empty admitted route gets one cached cue. */
agent_err_t esp_agent_voice_progress_fallback(esp_agent_voice_progress_t *,
    const esp_agent_voice_ack_t *,bool deferred,bool clarify,const char *input,
    const atomic_bool *cancelled);
agent_err_t esp_agent_voice_progress_join(void *);
#endif
