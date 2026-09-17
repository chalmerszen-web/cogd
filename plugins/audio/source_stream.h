#ifndef STUDY_SOURCE_STREAM_H
#define STUDY_SOURCE_STREAM_H
#include "source_bound.h"
#include "voice.h"
#include "tonal.h"
#include "confirmation.h"
enum { SOURCE_FRAME_SAMPLES=320,SOURCE_FRAME_COUNT=500,SOURCE_RECORD_BYTES=6,SOURCE_METADATA_BYTES=SOURCE_FRAME_COUNT*SOURCE_RECORD_BYTES };
typedef bool (*source_spectral_fn)(void *,int16_t *);
typedef struct { unsigned level; bool spectral; unsigned clean_level; } source_frame_t;
typedef struct {
    agent_voice_t voice;
    agent_biquad_t notch;
    tonal_filter_t tonal;
    uint32_t clean_sum;
    source_bound_t bound;
    uint8_t *records;
    int16_t *filtered;
    source_spectral_fn spectral;
    void *ctx;
    unsigned samples,used,frames;
} source_stream_t;
/* Producer owns this object and filtered workspace. Record bytes are immutable
 * after each frame, and remain borrowed until the consumer joins. */
agent_err_t source_stream_init(source_stream_t *,uint8_t *,size_t,int16_t *,unsigned noise,source_spectral_fn,void *);
agent_err_t source_stream_feed(source_stream_t *,int16_t);
bool source_stream_done(const source_stream_t *);
/* Caller acquires a published RAW sample bound. Producer must finish all
 * metadata inside that bound BEFORE publishing it. No consumer reads producer
 * counters or partially written records. Six byte records avoid typed aliases. */
agent_err_t source_stream_read(const uint8_t *,size_t,unsigned published_samples,unsigned frame,source_frame_t *);
/* Caller first supplies original16ms scores, then applies each available20ms
 * frame in source order. The clean-energy veto applies ONLY after an actual
 * original neural confirmation; first confirmation and source bounds ignore it. */
agent_err_t source_stream_confirm(agent_confirmation_t *,const uint8_t *,unsigned published_samples,unsigned noise,source_frame_t *);
#endif
