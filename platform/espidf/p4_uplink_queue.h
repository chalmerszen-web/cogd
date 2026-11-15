#ifndef P4_UPLINK_QUEUE_H
#define P4_UPLINK_QUEUE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "p4_probe_stage.h"

/* The capture consumer may examine current speech while the transport sends
 * retained onset PCM at its unchanged 20 ms media cadence. Full queues apply
 * backpressure; they never drop, reorder or recondition an accepted frame. */
enum { P4_UPLINK_FRAME_SAMPLES=320, P4_UPLINK_QUEUE_FRAMES=96 };
typedef struct {
    uint64_t stamp;
    unsigned end_cursor;
    int16_t pcm[P4_UPLINK_FRAME_SAMPLES];
    p4_probe_bound_t probe;
} p4_uplink_frame_t;
typedef struct {
    unsigned read,write;
    p4_uplink_frame_t frames[P4_UPLINK_QUEUE_FRAMES];
} p4_uplink_queue_t;
static inline unsigned p4_uplink_queue_count(const p4_uplink_queue_t *q)
{ return q->write-q->read; }
static inline void p4_uplink_queue_reset(p4_uplink_queue_t *q)
{ q->read=q->write=0; }
static inline p4_uplink_frame_t *p4_uplink_queue_head(p4_uplink_queue_t *q)
{ return q->write==q->read?NULL:&q->frames[q->read%P4_UPLINK_QUEUE_FRAMES]; }
static inline bool p4_uplink_queue_push(p4_uplink_queue_t *q,const int16_t *pcm,
    unsigned end,uint64_t stamp,const p4_probe_bound_t *probe)
{
    if(p4_uplink_queue_count(q)>=P4_UPLINK_QUEUE_FRAMES)return false;
    if(q->write!=q->read && q->frames[(q->write-1)%P4_UPLINK_QUEUE_FRAMES].end_cursor+320u!=end)return false;
    p4_uplink_frame_t *f=&q->frames[q->write%P4_UPLINK_QUEUE_FRAMES];
    f->stamp=stamp;f->end_cursor=end;f->probe=*probe;
    memcpy(f->pcm,pcm,sizeof(f->pcm));++q->write;return true;
}
static inline void p4_uplink_queue_pop(p4_uplink_queue_t *q)
{ if(q->read!=q->write)++q->read; }
#endif
