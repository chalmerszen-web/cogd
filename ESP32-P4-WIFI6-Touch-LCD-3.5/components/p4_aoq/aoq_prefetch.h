#pragma once
#include "p4_aoq.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One unused allocation for the current native conversation. No media, QUIC
 * connection, history or tool execution occurs in the background worker.
 * Each start makes at most one bounded HTTP request; no automatic retry.
 * Clear invalidates an in-flight result as well as wiping a ready result. */
int p4_aoq_prefetch_start(p4_aoq_kind_t kind);
bool p4_aoq_prefetch_take(p4_aoq_kind_t kind,p4_aoq_allocation_t *out,unsigned setup_budget_ms);
void p4_aoq_prefetch_clear(void);
#ifdef __cplusplus
}
#endif
