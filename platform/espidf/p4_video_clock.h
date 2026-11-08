#ifndef COGD_P4_VIDEO_CLOCK_H
#define COGD_P4_VIDEO_CLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The JPEG buffer belongs to this pending capture until its audio clock
 * catches up. Replacing it with every newer camera frame can starve video
 * forever when microphone upload lags by more than one camera interval. */
typedef struct { size_t bytes; unsigned sequence; uint64_t stamp; } p4_video_pending_t;
static inline bool p4_video_pending_due(const p4_video_pending_t *p,uint64_t audio_us)
{ return p->bytes && p->stamp<=audio_us; }
static inline bool p4_video_pending_expired(const p4_video_pending_t *p,uint64_t now_us)
{ return p->bytes && now_us>=p->stamp && now_us-p->stamp>5000000; }
#endif
