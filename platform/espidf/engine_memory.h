#ifndef ESP_AGENT_ENGINE_MEMORY_H
#define ESP_AGENT_ENGINE_MEMORY_H
#include "engine.h"

/* Dynamic platform owner only: never pass caller-owned/static bound storage.
 * Caller holds the work lock and has joined capture/network borrowers. The
 * engine additionally refuses active sinks/history selection. Failed restore
 * keeps a preexisting request-only buffer, or leaves a detached engine. No
 * request or tool operation is retried here. */
agent_err_t esp_agent_engine_memory_restore(agent_engine_t *,bool contiguous);
agent_err_t esp_agent_engine_memory_release(agent_engine_t *);
/* Allocate only request/context scratch. A later restore(false) keeps this
 * exact allocation while adding state after the capture owner has joined. */
agent_err_t esp_agent_engine_memory_prepare(agent_engine_t *);
#if AGENT_REQUEST_SCRATCH_COMPACT
/* Preparation-only bound, from validated recent record/canonical lengths and
 * worst-case user quoting. Large/unknown records or possible compaction keep
 * full storage. restore/adopt replace small scratch only after owner joins. */
agent_err_t esp_agent_engine_memory_prepare_input(agent_engine_t *,const char *);
/* Transfer a known heap-owned split state back to capture. Caller holds the
 * work lock and joins all borrowers. Only success transfers ownership; no
 * allocation, zeroing or submitted work occurs here. NOT_FOUND leaves the
 * engine untouched for the original release/calloc fallback. */
agent_err_t esp_agent_engine_memory_take_capture(agent_engine_t *,size_t,void **);
#endif
/* Transfer an idle, heap-allocated capture arena directly to engine state.
 * Success takes ownership of memory and zeroes only sizeof(workspace). Failure
 * leaves the arena/bytes with the caller. A full request buffer is retained;
 * explicitly smaller preparation scratch is retired after the caller's join;
 * otherwise only that buffer is allocated. No replacement state allocation. */
agent_err_t esp_agent_engine_memory_adopt(agent_engine_t *,void *,size_t);
#endif
