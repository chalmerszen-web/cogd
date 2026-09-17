#ifndef STUDY_CONFIRMATION_TAIL_H
#define STUDY_CONFIRMATION_TAIL_H
#include "source_stream.h"
typedef struct {
    const uint8_t *records;
    agent_confirmation_t *owner;
    unsigned noise,last_possible_ms,frames,next_ms;
} confirmation_tail_t;
/* Consumer-only proof over immutable, fully published first4000ms metadata.
 * BUSY never modifies the output. No future observation is inferred from EOF. */
agent_err_t confirmation_tail_open(confirmation_tail_t *,const uint8_t *,unsigned published,unsigned noise);
/* Admission requires the actual unconfirmed endpoint and original neural
 * history already beyond EVERY possible first-confirmation event. */
agent_err_t confirmation_tail_begin(confirmation_tail_t *,agent_confirmation_t *);
/* Advance original endpoint only. Neural counters/history are never forged;
 * caller retains8s wall/cancel checks and storage/workspace ownership. */
agent_err_t confirmation_tail_step(confirmation_tail_t *,agent_confirmation_t *,source_frame_t *);
#endif
