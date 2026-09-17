#include "factor_q30.h"
#include "voice.h"
#include <string.h>

/* RBJ biquads, 16 kHz, coefficients normalized by a0 and quantized to Q30.
 * 180-Hz high-pass, 100/200-Hz notches (Q=8), 3400-Hz low-pass.
 * Design equations: https://www.w3.org/TR/audio-eq-cookbook/ .
 * Q8 sample state reduces fixed-point limit cycles; all sums fit int64_t. */
static const int32_t coefficients[4][5]={
    {1021391994,-2042783987,1021391994,-2040230175,971595976},
    {1071113591,-2140575603,1071113591,-2140575603,1068485359},
    {1068502217,-2130416762,1068502217,-2130416762,1063262609},
    {243866057,487732114,243866057,-297066368,198788771}
};
void agent_voice_init(agent_voice_t *v) { memset(v,0,sizeof(*v)); }
static int32_t biquad_sample(agent_biquad_t *s,const int32_t *c,int32_t value)
{ return factor_q30(s,c,value); }
int16_t agent_voice_filter(agent_voice_t *v,int16_t input)
{
    int32_t value=(int32_t)input*256;
    for(unsigned i=0;i<4;++i) value=biquad_sample(&v->stage[i],coefficients[i],value);
    value/=256;
    return (int16_t)(value>32767?32767:value< -32768?-32768:value);
}
int16_t agent_voice_reject_cue(agent_biquad_t *state,int16_t input)
{
    /* 1320-Hz notch, Q=8, 16 kHz. Used only for VAD decisions: the raw clip
     * and keyword input retain every sample, including immediate speech. */
    static const int32_t c[5]={1041490845,-1809343540,1041490845,-1809343540,1009239866};
    int32_t value=biquad_sample(state,c,(int32_t)input*256)/256;
    return (int16_t)(value>32767?32767:value< -32768?-32768:value);
}
static agent_err_t next_sample(agent_replay_t *r,int16_t *out)
{
    while(r->used==r->available) {
        if(r->source==r->clip->samples && !r->previous_count) { *out=r->next; return AGENT_OK; }
        size_t n=r->clip->samples-r->source; if(n>64) n=64;
        if(n) { agent_err_t e=agent_clip_read(r->clip,r->source,r->cache,n); if(e) return e; }
        for(size_t i=0;i<n;++i) r->cache[i]=agent_voice_filter(&r->voice,r->cache[i]);
        memset(r->cache+n,0,(64-n)*sizeof(*r->cache));
        agent_noise_process(&r->noise,r->cache,r->cache);
        r->source+=n; r->used=0; r->available=r->previous_count; r->previous_count=n;
    }
    *out=r->cache[r->used++]; return AGENT_OK;
}
agent_err_t agent_replay_init(agent_replay_t *r,agent_clip_t *clip)
{
    if(!r || !clip || !clip->ready || !clip->samples) return AGENT_ERR_NOT_FOUND;
    memset(r,0,sizeof(*r)); r->clip=clip; r->total=clip->samples*3/2;
    agent_noise_init(&r->noise); return AGENT_OK;
}
agent_err_t agent_replay_prepare(agent_replay_t *r,bool *ready)
{
    if(!r || !r->clip || !ready) return AGENT_ERR_ARGUMENT;
    *ready=r->prepared; if(*ready) return AGENT_OK;
    size_t n=r->clip->samples-r->source; if(n>128) n=128;
    agent_err_t e=agent_clip_read(r->clip,r->source,r->cache,n); if(e) return e;
    for(size_t i=0;i<n;++i) r->cache[i]=agent_voice_filter(&r->voice,r->cache[i]);
    memset(r->cache+n,0,(128-n)*sizeof(*r->cache));
    agent_noise_profile(&r->noise,r->cache); r->source+=n;
    if(r->source==r->clip->samples) {
        agent_noise_ready(&r->noise); agent_voice_init(&r->voice); r->source=0;
        e=next_sample(r,&r->previous);
        if(!e) { r->next=r->previous; e=next_sample(r,&r->next); }
        if(!e) *ready=r->prepared=true;
    }
    return e;
}
agent_err_t agent_replay_render(agent_replay_t *r,int16_t *pcm,size_t cap,unsigned volume,size_t *count)
{
    if(!r || !pcm || !count || !r->clip || !r->prepared || volume>100) return AGENT_ERR_ARGUMENT;
    *count=0;
    while(*count<cap && r->rendered<r->total) {
        int32_t value=((int32_t)r->previous*(3-(int32_t)r->phase)+(int32_t)r->next*(int32_t)r->phase)/3;
        size_t envelope=r->rendered; if(r->total-r->rendered-1<envelope) envelope=r->total-r->rendered-1;
        if(envelope>120) envelope=120;
        pcm[(*count)++]=(int16_t)(value*(int32_t)volume/100*(int32_t)envelope/120);
        ++r->rendered; r->phase+=2;
        if(r->phase>=3) {
            r->phase-=3; r->previous=r->next;
            agent_err_t e=next_sample(r,&r->next); if(e) return e;
        }
    }
    return AGENT_OK;
}
