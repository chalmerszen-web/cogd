#ifndef AGENT_VOICE_HISTORY_H
#define AGENT_VOICE_HISTORY_H
#include "engine.h"

enum { AGENT_VOICE_RECORD_CANDIDATE=1u, AGENT_VOICE_RECORD_PARTIAL=2u,
       AGENT_VOICE_RECORD_EFFECTS=4u };
/* All speech workers and PCM readers must have joined. messages already holds
 * the actual input, tool results and completed answer. Reuses reply as JSON
 * scratch; only successful turns get an assistant record. Preserve the first
 * execution error over a later logging error; prepare_error prevents writing
 * an invalid message list. No retry of effects or speech on any return. */
agent_err_t agent_voice_history(agent_engine_t *,const char *input,
    agent_err_t result,agent_err_t prepare_error,unsigned flags);
#endif
