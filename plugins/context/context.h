#ifndef AGENT_CONTEXT_H
#define AGENT_CONTEXT_H
#include "wal.h"
#include "llm.h"
#include "json.h"

#define AGENT_PEERS_MAX 8u
#define AGENT_RANGES_MAX 32u
#define AGENT_MEMORY_MAX 8u
#define AGENT_RECENT_MAX 128u
#define AGENT_CONTEXT_BUDGET_DEFAULT (64u * 1024u)
#define AGENT_CONTEXT_BUDGET_MAX (128u * 1024u)
#define AGENT_SUMMARY_MAX 1536u
#define AGENT_CONTEXT_SCAN_MS 10000u
#define AGENT_SEQ_MAX 9007199254740991ULL
typedef enum { AGENT_CONTEXT_LOCAL, AGENT_CONTEXT_CLOUD, AGENT_CONTEXT_HYBRID } agent_context_mode_t;
typedef struct { uint64_t first,last; } agent_range_t;
typedef struct { char device[33]; unsigned count; agent_range_t ranges[AGENT_RANGES_MAX]; } agent_peer_t;
typedef struct { char key[65],value[129],device[33]; uint64_t lamport,seq; bool deleted; } agent_memory_t;
typedef struct {
    char text[AGENT_SUMMARY_MAX+1], device[33];
    uint64_t through,lamport,seq;
} agent_summary_t;
typedef struct {
    agent_wal_t wal;
    const char *device, *user, *session;
    agent_err_t (*reserve)(void *, uint64_t *first, uint64_t *last);
    void *reserve_ctx;
    uint64_t next_seq,seq_end,lamport,cursor,acked,acked_record,last_local;
    agent_context_mode_t mode;
    agent_peer_t peers[AGENT_PEERS_MAX];
    agent_memory_t memory[AGENT_MEMORY_MAX];
    agent_record_t recent[AGENT_RECENT_MAX];
    unsigned recent_count;
    size_t events,pending;
    char *scratch;
    size_t capacity;
    size_t history_budget, prompt_bytes, request_bytes;
    unsigned prompt_start, prompt_count, prompt_candidates;
    uint64_t prompt_generation;
    const atomic_bool *prompt_cancel;
    bool prompt_locked;
    uint64_t last_turn_seq;
    agent_summary_t summary;
    const atomic_bool *cancelled;
    uint64_t (*now_ms)(void *);
    void *clock_ctx;
} agent_context_t;

agent_err_t agent_context_open(agent_context_t *, const agent_flash_ops_t *);
agent_err_t agent_context_compact(agent_context_t *);
agent_err_t agent_context_checkpoint(agent_context_t *, uint64_t cursor, uint64_t ack, agent_context_mode_t);
agent_err_t agent_context_emit(agent_context_t *, const char *type, const char *actor, const char *content_json, char *event_id, size_t);
agent_err_t agent_context_ingest(agent_context_t *, const cJSON *event);
agent_err_t agent_context_history(agent_context_t *, agent_messages_t *, const char *system);
agent_err_t agent_context_prompt(agent_context_t *, agent_messages_t *, const char *system);
agent_err_t agent_context_select(agent_context_t *, uint64_t before, const atomic_bool *cancelled);
agent_err_t agent_context_replay(agent_context_t *, agent_write_fn, void *);
void agent_context_release(agent_context_t *);
agent_err_t agent_context_search(agent_context_t *,const char *query,uint64_t before,unsigned limit,char *,size_t);
agent_err_t agent_context_summary_set(agent_context_t *,const char *text,uint64_t through);
agent_err_t agent_context_summary_get(agent_context_t *,char *,size_t);
agent_err_t agent_context_batch(agent_context_t *, char *output, size_t capacity, uint64_t *last);
agent_err_t agent_context_stats(agent_context_t *, char *, size_t);
const char *agent_context_mode_name(agent_context_mode_t);
typedef struct {
    agent_err_t (*append)(agent_context_t *,const char *,const char *,const char *,char *,size_t);
    agent_err_t (*history)(agent_context_t *,agent_messages_t *,const char *);
    agent_err_t (*stats)(agent_context_t *,char *,size_t);
    agent_err_t (*compact)(agent_context_t *);
    agent_err_t (*prompt)(agent_context_t *,agent_messages_t *,const char *);
    agent_err_t (*select)(agent_context_t *,uint64_t,const atomic_bool *);
    agent_err_t (*replay)(agent_context_t *,agent_write_fn,void *);
    void (*release)(agent_context_t *);
} agent_context_ops_t;
extern const agent_context_ops_t agent_context_ops;
#endif
