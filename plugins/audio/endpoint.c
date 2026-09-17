#include "endpoint.h"
#include <string.h>

agent_err_t agent_endpoint_init(agent_endpoint_t *e,unsigned end,unsigned wait,unsigned maximum)
{
    if(!e || end<400 || end>2000 || wait<1000 || wait>10000 || maximum<wait || maximum>10000)
        return AGENT_ERR_ARGUMENT;
    memset(e,0,sizeof(*e)); e->end_ms=end; e->wait_ms=wait; e->maximum_ms=maximum;
    return AGENT_OK;
}
agent_endpoint_state_t agent_endpoint_feed(agent_endpoint_t *e,bool speech)
{
    if(!e) return AGENT_EP_CANCELLED;
    if(e->state>=AGENT_EP_DONE) return e->state;
    e->elapsed_ms+=AGENT_VAD_FRAME_MS;
    e->onset_bits=((e->onset_bits<<1)|(speech?1u:0u))&255u;
    unsigned votes=0; for(unsigned bits=e->onset_bits;bits;bits>>=1) votes+=bits&1u;
    if(speech) {
        e->speech_ms+=AGENT_VAD_FRAME_MS;
        if(votes>=5) e->support_at_ms=e->elapsed_ms;
        if(votes>=7) e->state=AGENT_EP_SPEECH;
    }
    /* Isolated spectral/noise impulses must not postpone the endpoint forever.
     * Continuing speech needs four votes in 160 ms; onset needs seven. The
     * retained false-onset clip supplies six votes; sparse three-frame noise
     * bursts must not keep resetting an already active utterance's end timer.
     * This classification guard does not delay or discard captured PCM. */
    if(speech && votes>=4) e->quiet_ms=0;
    else e->quiet_ms+=AGENT_VAD_FRAME_MS;
    if(e->state==AGENT_EP_SPEECH && e->quiet_ms>=e->end_ms && e->speech_ms>=120) e->state=AGENT_EP_DONE;
    else if(e->state==AGENT_EP_WAIT && e->elapsed_ms>=e->wait_ms) e->state=AGENT_EP_NO_SPEECH;
    else if(e->elapsed_ms>=e->maximum_ms) e->state=AGENT_EP_LIMIT;
    return e->state;
}
void agent_endpoint_cancel(agent_endpoint_t *e)
{ if(e && e->state<AGENT_EP_DONE) e->state=AGENT_EP_CANCELLED; }

bool agent_endpoint_support(agent_endpoint_t *e,bool strong)
{
    if(!e || !strong || e->state!=AGENT_EP_WAIT || e->elapsed_ms>=e->wait_ms || e->speech_ms<120) return false;
    /* The model integrates successive feature windows: genuine short-command
     * confirmation can arrive after the original five-vote window has ended.
     * Retain only a bounded candidate, not a latched speech decision. Neither
     * this expiry nor confirmation resets the sample-counted silence timer. */
    if(!e->support_at_ms || e->elapsed_ms-e->support_at_ms>AGENT_EP_SUPPORT_MS) return false;
    e->state=AGENT_EP_SPEECH; return true;
}

void agent_endpoint_verify(agent_endpoint_t *e,bool confirmed)
{
    if(!e || confirmed || e->state==AGENT_EP_CANCELLED || e->state==AGENT_EP_NO_SPEECH) return;
    if(e->elapsed_ms>=e->wait_ms) e->state=AGENT_EP_NO_SPEECH;
    else if(e->state==AGENT_EP_DONE) {
        /* An early unconfirmed burst is not the user's utterance. Keep waiting
         * for later speech, preserving both the PCM and original deadline. */
        e->state=AGENT_EP_WAIT;
        e->speech_ms=e->quiet_ms=e->onset_bits=e->support_at_ms=0;
    }
}

bool agent_activity_feed(agent_activity_t *a,const int16_t *pcm,size_t n,bool learn,bool spectral,bool speaking)
{
    if(!a || !pcm || !n || n>512) return false;
    uint32_t sum=0;
    for(size_t i=0;i<n;++i) { int32_t x=pcm[i]; sum+=(uint32_t)(x<0?-x:x); }
    a->level=sum/(unsigned)n;
    if(learn) {
        a->levels[a->at++%32]=a->level;
        if(a->count<32) ++a->count;
        unsigned low[3]={UINT32_MAX,UINT32_MAX,UINT32_MAX};
        for(unsigned i=0;i<a->count;++i) {
            unsigned v=a->levels[i];
            for(unsigned j=0;j<3;++j) if(v<low[j]) { unsigned old=low[j];low[j]=v;v=old; }
        }
        a->noise=low[a->count<3?a->count-1:2];
    }
    /* Hysteresis preserves quiet syllables within an accepted utterance while
     * keeping the stricter onset guard against stationary microphone noise. */
    unsigned threshold=speaking?a->noise*3u/2u:a->noise*2u;
    if(threshold<240) threshold=240;
    return spectral && a->level>threshold;
}
