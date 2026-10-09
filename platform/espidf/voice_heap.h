#ifndef AGENT_VOICE_HEAP_H
#define AGENT_VOICE_HEAP_H
#include <stdbool.h>
/* Diagnostic-only allocation observations. A session also covers idle and
 * closing commands; skipped callbacks remain explicit. Never in release. */
typedef enum {
    VOICE_HEAP_CAPTURE_BEGIN, VOICE_HEAP_ANSWER_END,
    VOICE_HEAP_REARM_BEGIN, VOICE_HEAP_REARM_END,
    VOICE_HEAP_HTTP_RELEASE_BEGIN, VOICE_HEAP_HTTP_RELEASE_END,
    VOICE_HEAP_KWS_ALLOC_BEGIN, VOICE_HEAP_KWS_ALLOC_END,
    VOICE_HEAP_CLEANUP_END, VOICE_HEAP_WORKSPACE_RESTORE_BEGIN,
    VOICE_HEAP_WORKSPACE_RESTORE_END, VOICE_HEAP_VOICE_OFF_BEGIN,
    VOICE_HEAP_VOICE_OFF_END, VOICE_HEAP_PHASE_COUNT
} voice_heap_phase_t;
#ifdef AGENT_VOICE_HEAP_DIAGNOSTICS
void esp_agent_voice_heap_begin(void);
/* Nonblocking metadata only: no allocation, USB, wait or ownership change. */
void esp_agent_voice_heap_mark(voice_heap_phase_t);
void esp_agent_voice_heap_end(void (*event)(void *,const char *,const char *));
/* USB owner: start/end only with voice disabled and no active job. Per-job
 * begin/end leave a session running so later cleanup remains observable. */
bool esp_agent_voice_heap_session_begin(void);
bool esp_agent_voice_heap_session_end(void (*event)(void *,const char *,const char *));
#else
static inline void esp_agent_voice_heap_begin(void) {}
static inline void esp_agent_voice_heap_mark(voice_heap_phase_t phase) { (void)phase; }
static inline void esp_agent_voice_heap_end(void (*event)(void *,const char *,const char *))
{ (void)event; }
static inline bool esp_agent_voice_heap_session_begin(void) { return false; }
static inline bool esp_agent_voice_heap_session_end(void (*event)(void *,const char *,const char *))
{ (void)event;return false; }
#endif
#endif
