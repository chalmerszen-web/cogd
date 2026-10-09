#ifndef AGENT_ENDPOINT_NOTICE_TRACE_H
#define AGENT_ENDPOINT_NOTICE_TRACE_H
#include <stdbool.h>
#include <stdint.h>

/* Diagnostic only. One owner records the mailbox value actually consumed,
 * not the arrival time of a serial log or the producer's current source time.
 * Read only after that owner joins; no allocation or I/O during capture. */
#if AGENT_CAPTURE_PROBE
/* Shadow capture continues after the first terminal; a cloud proposal can
 * change on every frame. Eight seconds contains exactly400 source frames. */
enum { ENDPOINT_NOTICE_CAPACITY=400 };
#else
enum { ENDPOINT_NOTICE_CAPACITY=64 };
#endif
typedef struct { uint32_t before_ms,notice; } endpoint_notice_row_t;
typedef struct {
    endpoint_notice_row_t rows[ENDPOINT_NOTICE_CAPACITY];
    unsigned count;
    bool overflow;
} endpoint_notice_trace_t;
static inline void endpoint_notice_record(endpoint_notice_trace_t *trace,unsigned before_ms,unsigned notice)
{
    if(trace->overflow || (trace->count && trace->rows[trace->count-1].notice==notice))return;
    if(trace->count==ENDPOINT_NOTICE_CAPACITY) {trace->overflow=true;return;}
    trace->rows[trace->count++]=(endpoint_notice_row_t){before_ms,notice};
}
#endif
