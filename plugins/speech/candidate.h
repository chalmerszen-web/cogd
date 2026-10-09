#ifndef AGENT_SPEECH_CANDIDATE_H
#define AGENT_SPEECH_CANDIDATE_H
#include "intent.h"
#include "agent.h"

/* The ASR owner alone writes revision/kind/revoked. The candidate worker only
 * reads request and cancelled atomically. A nonzero request is immutable for
 * this utterance; later evidence can revoke it, never overwrite or replay it.
 * Parameter-free LIGHT/REMEMBER receipts survive ASR sentence revisions, but
 * still require matching complete final intent/language and generic output.
 * THINK binds a source topic and requires that whole source prefix in final
 * ASR; it grants no factual or tool authority. Explicit repairs revoke these
 * receipts; lamp repairs need the full grammar. ANSWER instead requires the
 * entire final question to match (only outside punctuation/whitespace trim).
 * ASR revisions never grant authority and extra clauses discard the answer. */
typedef struct {
    atomic_uint request;
    unsigned revision,bound_revision;
    atomic_bool cancelled;
    bool revoked;
    /* Written before release-publishing request, then immutable until join.
     * Independent ASR revisions never overwrite the worker's borrowed text. */
    char *source;
} agent_candidate_gate_t;
enum { AGENT_CANDIDATE_YUE=16u, AGENT_CANDIDATE_MEMORY=32u, AGENT_CANDIDATE_KIND=15u };
enum { AGENT_CANDIDATE_SOURCE_BYTES=65 };
/* Optional caller-owned source must hold65 bytes and survive the worker join. */
void agent_candidate_gate_init(agent_candidate_gate_t *,char *source);
void agent_candidate_revision(agent_candidate_gate_t *,unsigned);
bool agent_candidate_preview(agent_candidate_gate_t *,const char *);
/* Optional policy: defer literal/incomplete lamps to the final local path;
 * richer lamp requests may nominate the existing grounded THINK receipt.
 * Memory waits for informative content and binds a grounded THINK receipt;
 * a flag requires complete memory intent plus that content in final ASR.
 * The original preview API and final-only tool authority stay unchanged. */
bool agent_candidate_preview_local_first(agent_candidate_gate_t *,const char *);
bool agent_candidate_final(const agent_candidate_gate_t *,const char *,bool capture_ok);
bool agent_candidate_cantonese(const char *);
const char *agent_candidate_prompt(unsigned request);
const char *agent_candidate_text(const agent_candidate_gate_t *);
bool agent_candidate_reply(const agent_candidate_gate_t *,const char *text);
/* Optional neutral-topic prediction: full successful capture first, including
 * immutable source/intent/language agreement. Every predicted topic word must
 * be grounded in this final input; tool/factual/answer authority is unchanged.
 * ASR-owned fields and final bytes must be immutable for the entire call. */
bool agent_candidate_reply_final(const agent_candidate_gate_t *,const char *final,const char *text);
#endif
