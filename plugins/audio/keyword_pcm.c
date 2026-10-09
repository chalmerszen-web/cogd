#include "keyword_pcm.h"
#include "crc.h"
#include <limits.h>
#include <stdalign.h>
#include <string.h>

keyword_pcm_reason_t keyword_pcm_reason(const keyword_pcm_t *s)
{ return (keyword_pcm_reason_t)atomic_load_explicit(&s->reason,memory_order_acquire); }
bool keyword_pcm_active(const keyword_pcm_t *s)
{ return s->frames && keyword_pcm_reason(s)==KEYWORD_PCM_NONE; }
void keyword_pcm_halt(keyword_pcm_t *s,keyword_pcm_reason_t reason)
{
    unsigned expected=KEYWORD_PCM_NONE;
    if(reason!=KEYWORD_PCM_NONE)
        atomic_compare_exchange_strong_explicit(&s->reason,&expected,reason,memory_order_release,memory_order_relaxed);
}
bool keyword_pcm_open(keyword_pcm_t *s,void *memory,size_t bytes,unsigned limit)
{
    if(!s || s->frames || !memory || (uintptr_t)memory%alignof(keyword_pcm_frame_t) ||
       bytes/sizeof(keyword_pcm_frame_t)<2 || !limit || limit>KEYWORD_PCM_MAX_FRAMES ||
       s->generation==UINT_MAX || atomic_load(&s->writing))return false;
    size_t slots=bytes/sizeof(keyword_pcm_frame_t);
    s->slots=slots>64?64:(unsigned)slots;
    s->limit=limit;++s->generation;
    atomic_store(&s->produced,0);atomic_store(&s->consumed,0);atomic_store(&s->discarded,0);
    atomic_store(&s->reason,KEYWORD_PCM_NONE);s->frames=memory;return true;
}
keyword_pcm_frame_t *keyword_pcm_begin(keyword_pcm_t *s,const int16_t *pcm,uint32_t time_ms)
{
    if(!pcm || !keyword_pcm_active(s) || atomic_load(&s->writing))return NULL;
    unsigned produced=atomic_load_explicit(&s->produced,memory_order_relaxed);
    unsigned consumed=atomic_load_explicit(&s->consumed,memory_order_acquire);
    if(produced-consumed>=s->slots) {keyword_pcm_halt(s,KEYWORD_PCM_FULL);return NULL;}
    keyword_pcm_frame_t *f=&s->frames[produced%s->slots];
    atomic_store_explicit(&s->writing,true,memory_order_release);
    f->sequence=produced;f->time_ms=time_ms;f->copy_us=0;
    memcpy(f->data,pcm,sizeof(f->data));return f;
}
bool keyword_pcm_commit(keyword_pcm_t *s,keyword_pcm_frame_t *f,unsigned us,unsigned flags,
    uint64_t end,int16_t score,const int16_t scores[2],const int16_t heads[3])
{
    unsigned produced=atomic_load_explicit(&s->produced,memory_order_relaxed);
    if(!s->frames || !f || !scores || !atomic_load(&s->writing) ||
       f!=&s->frames[produced%s->slots] || f->sequence!=produced ||
       ((flags&KEYWORD_PCM_HEADS) && !heads))return false;
    bool publish=keyword_pcm_reason(s)==KEYWORD_PCM_NONE;
    if(publish) {
        f->inference_us=us;f->flags=flags;f->sample_end=end;f->score_q8=score;
        memcpy(f->scores_q8,scores,sizeof(f->scores_q8));f->checksum=agent_crc32(f->data,sizeof(f->data));
        if(heads)memcpy(f->heads_q8,heads,sizeof(f->heads_q8));
        else memset(f->heads_q8,0,sizeof(f->heads_q8));
        atomic_store_explicit(&s->produced,produced+1,memory_order_release);
        if(produced+1==s->limit)keyword_pcm_halt(s,KEYWORD_PCM_COMPLETE);
    } else atomic_fetch_add(&s->discarded,1);
    atomic_store_explicit(&s->writing,false,memory_order_release);return publish;
}
const keyword_pcm_frame_t *keyword_pcm_peek(const keyword_pcm_t *s)
{
    if(!s->frames)return NULL;
    unsigned consumed=atomic_load_explicit(&s->consumed,memory_order_relaxed);
    if(consumed==atomic_load_explicit(&s->produced,memory_order_acquire))return NULL;
    return &s->frames[consumed%s->slots];
}
bool keyword_pcm_pop(keyword_pcm_t *s,unsigned sequence)
{
    const keyword_pcm_frame_t *f=keyword_pcm_peek(s);
    if(!f || f->sequence!=sequence)return false;
    atomic_store_explicit(&s->consumed,sequence+1,memory_order_release);return true;
}
bool keyword_pcm_close(keyword_pcm_t *s,bool discard)
{
    /* Caller first quiesces the audio worker. A bare writing=false snapshot
     * alone cannot authorize reclaiming scratch while a producer is running. */
    if(keyword_pcm_active(s) || atomic_load_explicit(&s->writing,memory_order_acquire))return false;
    unsigned produced=atomic_load(&s->produced),consumed=atomic_load(&s->consumed);
    if(!discard && produced!=consumed)return false;
    if(discard) {atomic_fetch_add(&s->discarded,produced-consumed);atomic_store(&s->consumed,produced);}
    s->frames=NULL;return true;
}
void keyword_pcm_info(const keyword_pcm_t *s,keyword_pcm_info_t *out)
{
    *out=(keyword_pcm_info_t){.active=keyword_pcm_active(s),.writing=atomic_load(&s->writing),
        .generation=s->generation,.slots=s->slots,.limit=s->limit,
        .produced=atomic_load(&s->produced),.consumed=atomic_load(&s->consumed),
        .discarded=atomic_load(&s->discarded),.reason=keyword_pcm_reason(s)};
}
