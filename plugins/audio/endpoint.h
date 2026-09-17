#ifndef AGENT_ENDPOINT_H
#define AGENT_ENDPOINT_H
#include "agent.h"

/* The detector supplies one speech decision per 20 ms of PCM. Time is counted
 * in samples, never USB poll times, so scheduling jitter cannot shorten a word. */
#define AGENT_VAD_FRAME_MS 20u
#define AGENT_EP_SUPPORT_MS 320u
typedef enum { AGENT_EP_WAIT, AGENT_EP_SPEECH, AGENT_EP_DONE,
    AGENT_EP_NO_SPEECH, AGENT_EP_LIMIT, AGENT_EP_CANCELLED } agent_endpoint_state_t;
typedef struct {
    agent_endpoint_state_t state;
    unsigned elapsed_ms, speech_ms, quiet_ms;
    unsigned end_ms, wait_ms, maximum_ms;
    unsigned onset_bits, support_at_ms;
} agent_endpoint_t;
typedef struct { unsigned levels[32],at,count,noise,level; } agent_activity_t;
/* Learn a low percentile of recent room levels before wake; freeze during the
 * utterance. The energy guard complements the WebRTC spectral classifier. */
bool agent_activity_feed(agent_activity_t *,const int16_t *,size_t,bool learn,bool spectral_voice,bool speaking);
agent_err_t agent_endpoint_init(agent_endpoint_t *,unsigned end_ms,unsigned wait_ms,unsigned maximum_ms);
agent_endpoint_state_t agent_endpoint_feed(agent_endpoint_t *,bool speech);
/* Optional independent speech confirmation. Unconfirmed spectral bursts may
 * neither commit a clip nor extend the original no-speech waiting deadline. */
void agent_endpoint_verify(agent_endpoint_t *,bool confirmed);
/* Strong independent evidence within 320 ms may support a fragmented onset. This
 * cannot reopen a terminal endpoint, extend waiting, or alter the end timer. */
bool agent_endpoint_support(agent_endpoint_t *,bool strong);
void agent_endpoint_cancel(agent_endpoint_t *);
#endif
