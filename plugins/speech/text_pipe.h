#ifndef AGENT_TEXT_PIPE_H
#define AGENT_TEXT_PIPE_H
#include "agent.h"

#define AGENT_TEXT_PIPE_CAPACITY 1800u
#define AGENT_TEXT_PIPE_SPAN 256u

/* One producer appends complete UTF-8 strings; one consumer borrows immutable
 * prefixes. No heap, ring wrap or terminating NUL. The owner must join both
 * sides before begin resets the pipe or the borrowed memory is reused. */
typedef struct {
    char *text;
    size_t capacity;
    const atomic_bool *cancelled;
    atomic_size_t published;
    atomic_bool finished;
} agent_text_pipe_t;

agent_err_t agent_text_pipe_begin(agent_text_pipe_t *,char *memory,size_t capacity,const atomic_bool *cancelled);
/* All-or-nothing. LIMIT means the fixed per-turn budget is exhausted; no
 * amount of consumer progress recycles that space. Errors publish nothing. */
agent_err_t agent_text_pipe_write(agent_text_pipe_t *,const char *,size_t);
/* Producer only, after its last write; idempotent even during cancellation. */
void agent_text_pipe_finish(agent_text_pipe_t *);
/* Consumer owns offset and advances it only after consuming a returned span.
 * OK + length=0 + done=false means wait. done=true implies no unread bytes.
 * Spans remain valid until both sides have joined and the owner resets. */
agent_err_t agent_text_pipe_next(const agent_text_pipe_t *,size_t offset,const char **span,size_t *length,bool *done);
#endif
