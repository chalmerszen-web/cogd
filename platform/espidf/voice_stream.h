#ifndef ESP_AGENT_VOICE_STREAM_H
#define ESP_AGENT_VOICE_STREAM_H
#include "agent.h"

/* Configure only between turns. Memory is borrowed until end() joins playback.
 * text becomes writable only after the final LLM request has sent its body. */
void esp_agent_voice_stream_prepare(atomic_bool *cancelled,const char *voice,
    char *text,size_t text_capacity,char *scratch,size_t scratch_capacity,
    void (*event)(void *,const char *,const char *));
/* Called after the LLM request body is sent, before response parsing. Waits
 * at most15s for TTS startup to avoid overlapping TLS handshake and HTTP-read
 * allocations; cloud generation remains in flight. A pre-existing song/plan
 * defers startup asynchronously. On any result, end() must join its borrower. */
agent_err_t esp_agent_voice_stream_begin(void *);
agent_err_t esp_agent_voice_stream_write(void *,const char *,size_t);
/* Publish EOF without joining, so a complete short acknowledgement can play
 * while the owning worker receives the planning SSE. end still must join. */
agent_err_t esp_agent_voice_stream_seal(void);
agent_err_t esp_agent_voice_stream_end(void *,agent_err_t);
/* Final answer only, called by the HTTP producer after its request returned.
 * Releases the completed HTTP connection before joining TTS when enabled.
 * Progress speech must use stream_end: its HTTP reader can still be active. */
agent_err_t esp_agent_voice_answer_end(void *,agent_err_t);
unsigned esp_agent_voice_stream_stack(void);
#endif
