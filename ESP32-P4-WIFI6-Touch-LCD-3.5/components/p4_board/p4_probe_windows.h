#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { P4_PROBE_FRAME_MS=32, P4_PROBE_SPANS=6, P4_PROBE_LONG_MS=96000,
       P4_PROBE_LONG_RAW_MS=32000, P4_PROBE_SPAN_MS=6400, P4_PROBE_WATCH_MS=160000,
       P4_PROBE_WATCH_RING=256, P4_PROBE_WATCH_SIDE=64, P4_PROBE_WATCH_COPY_BATCH=4 };
typedef struct {
    unsigned start_frame,frames,raw_frame;
    uint32_t trigger_ms,candidate_ms,candidate_cursor;
    unsigned pre_frames;
} p4_probe_span_t;
typedef struct {
    p4_probe_span_t spans[P4_PROBE_SPANS];
    unsigned used,raw_frames,pending;
} p4_probe_windows_t;

static inline bool p4_probe_reserve(p4_probe_windows_t *w,unsigned frames,unsigned frame,unsigned target)
{
    if(w->pending || w->used>=P4_PROBE_SPANS || frame>target || frames>target-frame ||
       frames>P4_PROBE_LONG_RAW_MS/P4_PROBE_FRAME_MS-w->raw_frames)return false;
    w->spans[w->used++]=(p4_probe_span_t){.start_frame=frame,.raw_frame=w->raw_frames};
    w->pending=frames;
    return true;
}

static inline bool p4_probe_mark(p4_probe_windows_t *w,unsigned ms,unsigned frame,unsigned target)
{
    return ms>=P4_PROBE_FRAME_MS && ms<=P4_PROBE_SPAN_MS && ms%P4_PROBE_FRAME_MS==0 &&
        p4_probe_reserve(w,ms/P4_PROBE_FRAME_MS,frame,target);
}

static inline bool p4_probe_watch_mark(p4_probe_windows_t *w,unsigned center,unsigned written,
    unsigned target,uint32_t trigger_ms,uint32_t candidate_ms,uint32_t cursor)
{
    if(!trigger_ms || center<P4_PROBE_WATCH_SIDE || center>=written)return false;
    unsigned begin=center-P4_PROBE_WATCH_SIDE;
    /* Reserve one further ring write before the first batch is copied. */
    if(written-begin>=P4_PROBE_WATCH_RING ||
       !p4_probe_reserve(w,2*P4_PROBE_WATCH_SIDE+1,begin,target))return false;
    p4_probe_span_t *span=&w->spans[w->used-1];
    span->trigger_ms=trigger_ms;
    span->candidate_ms=candidate_ms;
    span->candidate_cursor=cursor;
    span->pre_frames=P4_PROBE_WATCH_SIDE;
    return true;
}

/* One call per valid timeline frame while the capture lock is held. */
static inline bool p4_probe_take(p4_probe_windows_t *w,unsigned *raw_frame)
{
    if(!w->pending)return false;
    --w->pending;
    ++w->spans[w->used-1].frames;
    *raw_frame=w->raw_frames++;
    return true;
}
