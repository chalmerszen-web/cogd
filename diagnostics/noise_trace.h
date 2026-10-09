#ifndef AGENT_NOISE_TRACE_H
#define AGENT_NOISE_TRACE_H
#include <stdint.h>

/* Diagnostic only. Audio owner writes before wake, then leaves this immutable
 * until the captured turn has joined. Consumer must finish before rearming.
 * No PCM, allocation or I/O in the listening path. Last5.12s at20ms/frame. */
enum { NOISE_TRACE_CAPACITY=256 };
typedef struct { uint32_t time_ms;uint16_t level,noise; } noise_trace_row_t;
typedef struct {
    noise_trace_row_t rows[NOISE_TRACE_CAPACITY];
    unsigned total;
} noise_trace_t;
static inline void noise_trace_record(noise_trace_t *trace,uint32_t ms,unsigned level,unsigned noise)
{
    trace->rows[trace->total++%NOISE_TRACE_CAPACITY]=(noise_trace_row_t){ms,(uint16_t)level,(uint16_t)noise};
}
static inline unsigned noise_trace_count(const noise_trace_t *trace)
{return trace->total<NOISE_TRACE_CAPACITY?trace->total:NOISE_TRACE_CAPACITY;}
static inline const noise_trace_row_t *noise_trace_row(const noise_trace_t *trace,unsigned index)
{
    unsigned count=noise_trace_count(trace);
    return index<count?&trace->rows[(trace->total-count+index)%NOISE_TRACE_CAPACITY]:0;
}
#endif
