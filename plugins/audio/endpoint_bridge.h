#ifndef AGENT_ENDPOINT_BRIDGE_H
#define AGENT_ENDPOINT_BRIDGE_H
#include "endpoint.h"

/* Optional fast-ASR coordinator, owned by the same source-frame consumer as
 * endpoint. Four energy and four spectral votes may corroborate a delayed
 * classifier within160ms. At most one bridge per capture; no new onset. */
typedef struct {
    uint16_t candidate_at_ms,resumed_at_ms,bridge_at_ms;
    uint8_t energy_bits,spectral_bits;
    bool pending_seen,active,spent;
} agent_endpoint_bridge_t;
_Static_assert(sizeof(agent_endpoint_bridge_t)<=16,"Bridge state budget");

/* Requires a fresh700/4000/10000ms endpoint. Does not extend it until an actual
 * PENDING notice arrives. Call only at source-frame boundaries, never from a
 * transport callback or concurrently with feed. */
agent_err_t agent_endpoint_bridge_init(agent_endpoint_t *,agent_endpoint_bridge_t *);
void agent_endpoint_bridge_observe(agent_endpoint_t *,agent_endpoint_bridge_t *,unsigned notice);
/* strong is the unchanged double-energy strong test; energy is the unchanged
 * double-energy weak test independent of spectral. One call per20ms frame.
 * End/cancel remain terminal for BOTH objects. No allocation or PCM mutation. */
agent_endpoint_state_t agent_endpoint_bridge_feed(agent_endpoint_t *,agent_endpoint_bridge_t *,
                                                bool strong,bool energy,bool spectral);
#endif
