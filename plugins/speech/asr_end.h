#ifndef AGENT_ASR_END_H
#define AGENT_ASR_END_H
#include "agent.h"

enum { AGENT_ASR_END_FRAME_MS=20, AGENT_ASR_END_OBSERVE_MS=160,
       AGENT_ASR_END_QUIET_MS=300, AGENT_ASR_END_MAX_MS=10000 };
/* One adapter serializes these calls. No allocation, clock reads or locks.
 * Invalid/missing source frames disable cloud endpoint assistance until reset;
 * the independent local VAD/capture continues to own its normal limits. */
typedef struct {
    uint32_t newest_id,finalized_id,source_ms,resume_ms;
    uint32_t candidate_id,candidate_end_ms,candidate_at_ms;
    uint32_t dense_end_ms;
    uint16_t quiet_ms;
    uint8_t onset_bits;
    bool resume_armed,source_valid;
} agent_asr_end_t;
_Static_assert(sizeof(agent_asr_end_t)<=64,"ASR endpoint guard must stay small");

void agent_asr_end_reset(agent_asr_end_t *);
/* Exact source clock: 20,40,...,10000 ms. speech is the existing producer's
 * spectral + level + clean-level decision; this guard does no classification. */
void agent_asr_end_source(agent_asr_end_t *,unsigned source_end_ms,bool speech);
/* end_ms is the provider's audio-source endpoint, never a receipt timestamp.
 * A missing/invalid timestamp may be transcribed but cannot end capture. */
void agent_asr_end_sentence(agent_asr_end_t *,uint32_t id,unsigned end_ms,
    bool final,bool nonempty,bool timed,unsigned now_ms);
bool agent_asr_end_ready(const agent_asr_end_t *,bool locally_confirmed,unsigned now_ms);
/* Provisional generation also preserves a source-counted pause allowance. */
bool agent_asr_end_ready_after(const agent_asr_end_t *,bool,unsigned now_ms,unsigned hold_ms);
#endif
