#ifndef AGENT_CONFIRMATION_H
#define AGENT_CONFIRMATION_H
#include "endpoint.h"

/* Original16-ms Q15 model scores, joined to20-ms classical decisions by the
 * source clock. A worker may compute one future score while assembling PCM. */
typedef struct {
    agent_endpoint_t endpoint;
    uint16_t recent[5];
    unsigned frames,sum,previous_sum,confirmed_ms;
    bool confirmed;
} agent_confirmation_t;

agent_err_t agent_confirmation_init(agent_confirmation_t *,unsigned end_ms,unsigned wait_ms,unsigned maximum_ms);
agent_err_t agent_confirmation_score(agent_confirmation_t *,unsigned probability_q15);
/* BUSY means the necessary source-time score has not arrived; nothing advances.
 * After joint confirmation, classical decisions need no further neural work. */
agent_err_t agent_confirmation_feed(agent_confirmation_t *,bool classical_speech);
void agent_confirmation_cancel(agent_confirmation_t *);
/* Change only the confirmed utterance's end silence. Waiting/noise admission
 * retain their original deadlines; already accumulated silence is preserved. */
agent_err_t agent_confirmation_set_silence(agent_confirmation_t *,unsigned end_ms);
/* A final ASR sentence may end only an already locally confirmed utterance. */
agent_err_t agent_confirmation_finish(agent_confirmation_t *);
#endif
