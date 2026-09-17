#ifndef AGENT_SYNC_H
#define AGENT_SYNC_H
#include "context.h"
#include "transport.h"
/* One bounded push/pull/ack cycle. Caller schedules retries and gives turns priority. */
agent_err_t agent_context_sync(agent_context_t *,const agent_transport_ops_t *,const atomic_bool *,bool *more);
#endif
