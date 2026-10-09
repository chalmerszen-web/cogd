#ifndef AGENT_ENGINE_H
#define AGENT_ENGINE_H
#include "context.h"
#include "sse.h"
#include "tools.h"
#include "transport.h"

typedef struct {
    agent_err_t (*build)(const agent_messages_t *,bool,const char *,char *,size_t,size_t *);
    agent_err_t (*parse)(agent_llm_reply_t *,const char *,size_t,bool,agent_emit_fn,void *);
    agent_err_t (*compose)(bool,agent_body_fn,void *,agent_body_fn,void *,agent_write_fn,void *);
    /* Final speech response: tools are disabled by the wire protocol. */
    agent_err_t (*compose_final)(bool,agent_body_fn,void *,agent_write_fn,void *);
} agent_llm_ops_t;
/* Caller-owned transient storage. Keep its capacities independent of storage
 * ownership: changing a lifetime must not change the context/input budgets. */
typedef struct {
    agent_messages_t messages;
    agent_llm_reply_t reply;
    agent_sse_t sse;
} agent_engine_workspace_t;
#define AGENT_ENGINE_BUFFER_SIZE (AGENT_REQUEST_MAX+1u)
/* Optional contiguous layout for a caller that lends the whole arena to a
 * capture worker. Ordinary turns may bind these two blocks independently. */
typedef struct {
    agent_engine_workspace_t work;
    char buffer[AGENT_ENGINE_BUFFER_SIZE];
} agent_engine_storage_t;
#define AGENT_ENGINE_STORAGE_INIT(storage) \
    .workspace=&(storage).work,.buffer=(storage).buffer, \
    .scratch=&(storage),.scratch_capacity=sizeof(storage)
typedef struct {
    agent_core_t *core;
    const agent_transport_ops_t *transport;
    const agent_llm_ops_t *llm;
    const agent_tool_ops_t *tools;
    agent_context_t *context;
    const agent_context_ops_t *context_ops;
    agent_emit_fn emit;
    void *emit_ctx;
    agent_http_trace_fn request_trace;
    void *request_trace_ctx;
    /* Optional DIRECT streaming voice sink. begin runs after the final request
     * body has been sent (or at first text for an adapter without on_sent).
     * An explicitly labelled completed no-call answer can instead open the
     * sink after its response has finished, without another generation.
     * Until end returns, the sink
     * may borrow buffer[AGENT_ENGINE_ANSWER_SCRATCH..]. Planning/tool rounds do
     * not reach this sink. end joins/releases the sink before WAL scratch use.
     * write is all-or-nothing: BUSY means no byte accepted; engine retries with
     * bounded waits and cancellation checks. Other errors abort the turn.
     * begin/end must also bound their waits and observe core->cancelled. */
    agent_err_t (*answer_begin)(void *ctx);
    /* Optional once-per-turn connection preparation after a planning body is
     * sent. No text/PCM, tools, speaker or workspace borrowing is permitted.
     * The caller owns and cleans up unused preparation on every exit. */
    agent_err_t (*answer_prepare)(void *ctx);
    agent_write_fn answer_write;
    agent_err_t (*answer_end)(void *ctx,agent_err_t result);
    void *answer_ctx;
    /* One optional, nonpersistent acknowledgement during the first planning
     * response. begin runs after the request body; end ALWAYS joins before
     * another request, tools or context scratch reuse. It borrows the same
     * tail as the answer sink, never concurrently. No retry after begin. */
    agent_err_t (*progress_begin)(void *ctx);
    agent_err_t (*progress_end)(void *ctx,agent_err_t result);
    void *progress_ctx;
    /* Set only after successful playback; wire-only context for the final
     * response to continue naturally. The caller owns its stable storage. */
    const char *progress_text;
    const char *progress_language; /* Optional validated "zh" or "yue". */
    /* Optional already-drained native acknowledgement for this voice turn.
     * Caller clears these after turn. Prevents replay and duplicate TTS. */
    const char *heard_ack,*heard_ack_language;
    /* Optional bounded join before a validated tool batch takes hardware.
     * Failure aborts without executing that batch. This is progress ownership,
     * not an answer sink; it must not emit/persist a final assistant answer. */
    agent_err_t (*before_tools)(void *ctx);
    void *before_tools_ctx;
    void (*pause_ms)(unsigned);
    uint32_t (*random_u32)(void);
    bool gateway,output_started,effects,voice_mode,answer_prepared;
    /* Caller-owned for one voice turn; bypass tools and ask for missing input. */
    bool clarify_only;
    unsigned failures;
    uint64_t circuit_until;
    char turn_id[64];
    agent_engine_workspace_t *workspace;
    char *buffer;
#if AGENT_REQUEST_SCRATCH_COMPACT
    /* Nonzero only for explicitly bound preparation storage. Full state,
     * response sinks and tools always use the original buffer capacity. */
    size_t preparation_capacity;
    /* Actual capacity tagged only by the dynamic platform owner. Generic
     * binders clear it; unknown/static storage cannot transfer ownership. */
    size_t owned_state_bytes;
#endif
    void *scratch;
    size_t scratch_capacity;
    size_t used,total_text;
    uint64_t history_before;
    bool answer_stream,answer_final,answer_active;
    bool progress_attempted,progress_active;
} agent_engine_t;
#define AGENT_ENGINE_ANSWER_SCRATCH 6152u
#define AGENT_ENGINE_ANSWER_CHUNK 256u
_Static_assert(AGENT_ENGINE_ANSWER_SCRATCH>=AGENT_STREAM_MAX+1,"SSE terminator must precede borrowed speech scratch");
/* Borrow only while the caller owns the turn reservation and work lock.
 * Messages, reply and SSE are initialized afresh by every subsequent turn.
 * Configuration, context indexes and circuit state remain outside this range. */
#define AGENT_ENGINE_SCRATCH_SIZE sizeof(agent_engine_storage_t)
static inline void *agent_engine_scratch(agent_engine_t *engine,size_t *capacity)
{
    *capacity=engine->scratch_capacity;
    return engine->scratch;
}
/* The caller owns the turn/work lock and has joined every borrower. No memory
 * is allocated or freed here. NULL/0 detaches; an absent workspace causes a
 * new turn to fail before network, storage or tool effects. Binding also moves
 * the context serialization scratch, which otherwise aliases this buffer.
 * Active answer/progress sinks or a locked history selection prohibit rebinding
 * even under the work lock. */
agent_err_t agent_engine_bind_workspace(agent_engine_t *,void *,size_t);
/* Two disjoint caller-owned blocks, with unchanged fixed capacities. The
 * borrowable contiguous prefix is only sizeof(agent_engine_workspace_t).
 * Pointer metadata lives in engine, so overwriting borrowed bytes is safe. */
agent_err_t agent_engine_bind_parts(agent_engine_t *,void *,size_t,void *,size_t);
/* Request-only phase. No messages/reply storage is live yet; the caller still
 * owns the buffer and must join all producers before binding full state. */
agent_err_t agent_engine_bind_buffer(agent_engine_t *,void *,size_t);
#if AGENT_REQUEST_SCRATCH_COMPACT
#define AGENT_ENGINE_PREPARATION_MIN 8192u
/* Preparation only: no state/sinks, same input/history limits. The deferred
 * resume must join all borrowers, replace this buffer and bind full storage. */
agent_err_t agent_engine_bind_preparation_buffer(agent_engine_t *,void *,size_t);
#endif
typedef struct {
    /* Called exactly once, including pre-send failure. Join the producer and
     * bind full state, retaining the SAME full-capacity buffer. Only explicitly
     * smaller preparation storage may be replaced after the join. Do not
     * mutate context/input.
     * A nonzero result means abort/join, never retry submitted work. */
    agent_err_t (*resume)(void *ctx,agent_err_t result);
    void *ctx;
    char snapshots[1024];
} agent_engine_deferred_t;
/* Optional native-context DIRECT voice path. Only request scratch is needed
 * until on_sent (or first feed for older adapters). The caller keeps input and
 * deferred storage alive until return. Oversized snapshot/custom adapters join
 * before using the ordinary full-capacity path; no input/history limit changes.
 * The caller owns all allocations. No submitted deferred request is retried. */
agent_err_t agent_engine_turn_deferred(agent_engine_t *,const char *,bool,agent_engine_deferred_t *);
agent_err_t agent_engine_turn(agent_engine_t *,const char *input,bool stream);
#endif
