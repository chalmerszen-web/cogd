#ifndef AGENT_TLS_COOPERATE_H
#define AGENT_TLS_COOPERATE_H
#include <stdbool.h>

/* A synchronous connect scope; no callback retains a borrowed arena. */
bool esp_agent_tls_cooperate_begin(void);
void esp_agent_tls_cooperate_end(void);
bool esp_agent_tls_cooperate_current(void);
/* Called by the pinned SDK outer loop, never from inside crypto arithmetic. */
void agent_tls_cooperate_step(int finished);
void esp_agent_tls_cooperate_stats(unsigned *steps, unsigned *max_step_ms);
#endif
