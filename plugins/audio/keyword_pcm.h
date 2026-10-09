#ifndef AGENT_KEYWORD_PCM_H
#define AGENT_KEYWORD_PCM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/* Explicit diagnostic only. The caller reserves idle scratch, stops the
 * producer before close, and never lends the bytes to recording or a turn. */
#define KEYWORD_PCM_SAMPLES 512u
#define KEYWORD_PCM_MAX_FRAMES 2000u
enum { KEYWORD_PCM_DETECTED=1, KEYWORD_PCM_ACCEPTED=2, KEYWORD_PCM_ERROR=4,
       KEYWORD_PCM_ARMED=8, KEYWORD_PCM_HEADS=16 };
typedef enum {
    KEYWORD_PCM_NONE, KEYWORD_PCM_COMPLETE, KEYWORD_PCM_FULL,
    KEYWORD_PCM_CANCELLED, KEYWORD_PCM_TIMEOUT, KEYWORD_PCM_IO
} keyword_pcm_reason_t;
typedef struct {
    uint64_t sample_end;
    uint32_t sequence,time_ms,inference_us,copy_us,checksum,flags;
    int16_t score_q8,scores_q8[2],heads_q8[3];
    unsigned char data[KEYWORD_PCM_SAMPLES*2];
} keyword_pcm_frame_t;
typedef struct {
    keyword_pcm_frame_t *frames;
    unsigned generation,slots,limit;
    atomic_uint produced,consumed,discarded,reason;
    atomic_bool writing;
} keyword_pcm_t;
typedef struct {
    bool active,writing;
    unsigned generation,produced,consumed,slots,limit,discarded;
    keyword_pcm_reason_t reason;
} keyword_pcm_info_t;

bool keyword_pcm_open(keyword_pcm_t *,void *,size_t,unsigned limit);
bool keyword_pcm_active(const keyword_pcm_t *);
keyword_pcm_frame_t *keyword_pcm_begin(keyword_pcm_t *,const int16_t *,uint32_t time_ms);
bool keyword_pcm_commit(keyword_pcm_t *,keyword_pcm_frame_t *,unsigned inference_us,
    unsigned flags,uint64_t sample_end,int16_t score_q8,const int16_t scores_q8[2],
    const int16_t heads_q8[3]); /* Optional; HEADS is valid only with all three. */
const keyword_pcm_frame_t *keyword_pcm_peek(const keyword_pcm_t *);
bool keyword_pcm_pop(keyword_pcm_t *,unsigned sequence);
void keyword_pcm_halt(keyword_pcm_t *,keyword_pcm_reason_t);
keyword_pcm_reason_t keyword_pcm_reason(const keyword_pcm_t *);
void keyword_pcm_info(const keyword_pcm_t *,keyword_pcm_info_t *);
bool keyword_pcm_close(keyword_pcm_t *,bool discard);
#endif
