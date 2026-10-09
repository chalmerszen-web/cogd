#ifndef STUDY_SOURCE_BOUND_H
#define STUDY_SOURCE_BOUND_H
#include "agent.h"
typedef struct { unsigned elapsed_ms,last_continuation_ms,onset_bits,possible,threshold,target_samples,end_grace_ms; } source_bound_t;
agent_err_t source_bound_init(source_bound_t *,unsigned noise);
/* Independent original filtered mean-absolute level, once per20ms. Target is
 * acquisition only; no neural/endpoint result, cancellation or cue is emitted. */
agent_err_t source_bound_feed(source_bound_t *,unsigned level);
#endif
