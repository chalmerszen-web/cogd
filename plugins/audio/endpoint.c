#include "endpoint.h"
#include <string.h>

agent_err_t agent_endpoint_init(agent_endpoint_t *e,unsigned end,unsigned wait,unsigned maximum)
{
    if(!e || end<400 || end>2000 || wait<1000 || wait>10000 || maximum<wait || maximum>10000)
        return AGENT_ERR_ARGUMENT;
    memset(e,0,sizeof(*e)); e->end_ms=end; e->wait_ms=wait; e->maximum_ms=maximum;
    return AGENT_OK;
}
static agent_endpoint_state_t feed(agent_endpoint_t *e,bool speech,bool resume,bool weak)
{
    if(!e) return AGENT_EP_CANCELLED;
    if(e->state>=AGENT_EP_DONE) return e->state;
    e->elapsed_ms+=AGENT_VAD_FRAME_MS;
    e->onset_bits=((e->onset_bits<<1)|(speech?1u:0u))&255u;
    e->weak_bits=((e->weak_bits<<1)|((speech || weak)?1u:0u))&255u;
    unsigned votes=0; for(unsigned bits=e->onset_bits;bits;bits>>=1) votes+=bits&1u;
    unsigned support=0;for(unsigned bits=e->weak_bits;bits;bits>>=1)support+=bits&1u;
    if(speech) {
        e->speech_ms+=AGENT_VAD_FRAME_MS;
        if(votes>=5) e->support_at_ms=e->elapsed_ms;
        if(votes>=7) {e->state=AGENT_EP_SPEECH;e->local_onset=true;}
    }
    /* Isolated spectral/noise impulses must not postpone the endpoint forever.
     * Strict continuing speech needs four votes in160ms; onset needs seven.
     * Optional weaker votes can support two current strong votes only after
     * onset. In the fast path, a current transcript or an ASR-admitted pending
     * phrase can corroborate those two strong votes too. Both expire after the same strict-speech
     * window; neither can renew it. Weak votes alone never reset silence. The
     * retained false-onset clip supplies six votes; sparse three-frame noise
     * bursts must not keep resetting an already active utterance's end timer.
     * This classification guard does not delay or discard captured PCM. */
    if(speech && votes>=4) {e->quiet_ms=0;e->dense_at_ms=e->elapsed_ms;}
    else if(speech && e->state==AGENT_EP_SPEECH && votes>=2 &&
            (support>=4 || (resume && ((e->pending && e->transcribed_ms) ||
                                      (e->asr_notice&AGENT_EP_TEXT)))) &&
            e->dense_at_ms && e->elapsed_ms-e->dense_at_ms<=AGENT_EP_WEAK_SUPPORT_MS)
        e->quiet_ms=0;
    else e->quiet_ms+=AGENT_VAD_FRAME_MS;
    bool accepted=e->state==AGENT_EP_SPEECH && e->speech_ms>=120;
    bool local=accepted && e->quiet_ms>=e->end_ms;
    bool cloud=accepted && e->remote_ms && e->elapsed_ms>=e->remote_ms;
    bool ending=local || cloud;
    /* A late first transcript must not inherit an already expired silence
     * deadline and finish on the next frame. Spend the existing shared quota
     * while observing this ASR-only admission, never reset silence or renew
     * admission on repeated text. Strict onset and a guarded cloud end suffice. */
    bool fresh=resume && !cloud && !e->local_onset && e->transcribed_ms &&
        e->elapsed_ms-e->transcribed_ms<e->end_ms;
    /* A question may continue after a sentence pause. Spend at most400ms in
     * the WHOLE turn, out of the existing1000ms pending/fresh quota. Repeated
     * drafts and subsequent pauses cannot refill either allowance. Short
     * commands, absent text and the classic path receive no new delay. */
    unsigned hold=(e->pending || fresh)?AGENT_EP_PENDING_MS/AGENT_VAD_FRAME_MS:
        resume && (e->asr_notice&AGENT_EP_PHRASE)?AGENT_EP_PHRASE_MS/AGENT_VAD_FRAME_MS:0;
    /* A resumed syllable needs four votes before it can reset quiet_ms. Do
     * not cut its first 1..3 frames exactly at the deadline. A recent vote
     * allows only three extra frames total; sparse noise cannot renew that
     * allowance or reset the timer. Neither onset nor energy guards change. */
    /* A just-starting contiguous strong run can straddle the expiry of
     * the pending quota too. Exactly1/3/7 imply1..3 consecutive current votes;
     * a gap terminates, and the fourth strong frame uses the unchanged strict
     * continuation rule. No quota refill, weak-only grace or new state. */
    /* Three votes anywhere in the existing160ms window may need just one
     * more frame. Preserve them across a short gap using the SAME60ms grace;
     * otherwise a fourth valid vote20ms later arrives after terminal capture. */
    bool defer=local && resume && ((e->onset_bits&7u) || votes>=3) && (e->quiet_ms<e->end_ms+AGENT_EP_RESUME_MS ||
        (hold && e->held_frames==hold &&
         e->onset_bits<8u && !(e->onset_bits&(e->onset_bits+1u))));
    if(defer)++e->resume_frames;
    if(ending && !defer && e->held_frames<hold) {
        if(e->elapsed_ms>=e->maximum_ms)return e->state=AGENT_EP_LIMIT;
        ++e->held_frames;defer=true;
    }
    if(ending && !defer) e->state=AGENT_EP_DONE;
    else if(e->state==AGENT_EP_WAIT && e->elapsed_ms>=e->wait_ms) e->state=AGENT_EP_NO_SPEECH;
    else if(e->elapsed_ms>=e->maximum_ms) e->state=AGENT_EP_LIMIT;
    return e->state;
}
agent_endpoint_state_t agent_endpoint_feed(agent_endpoint_t *e,bool speech)
{ return feed(e,speech,false,speech); }
agent_endpoint_state_t agent_endpoint_feed_resume(agent_endpoint_t *e,bool speech)
{ return feed(e,speech,true,speech); }
agent_endpoint_state_t agent_endpoint_feed_supported(agent_endpoint_t *e,bool strong,bool weak)
{ return feed(e,strong,true,weak); }
void agent_endpoint_hint(agent_endpoint_t *e,bool pending,unsigned remote)
{
    if(e && e->state<AGENT_EP_DONE) {
        e->pending=pending;
        e->remote_ms=remote<=e->maximum_ms && !(remote%AGENT_VAD_FRAME_MS)?remote:0;
    }
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
    e->state=AGENT_EP_SPEECH;e->local_onset=true;return true;
}

bool agent_endpoint_transcribed(agent_endpoint_t *e)
{
    if(!e || e->state!=AGENT_EP_WAIT || e->elapsed_ms>=e->wait_ms ||
       e->speech_ms-e->asr_floor_ms<120)return false;
    e->state=AGENT_EP_SPEECH;e->transcribed_ms=e->elapsed_ms;return true;
}

void agent_endpoint_observe(agent_endpoint_t *e,unsigned notice)
{
    if(!e || e->state>=AGENT_EP_DONE)return;
    if(((notice^e->asr_notice)&~(AGENT_EP_GENERATION-1u)) && !e->local_onset &&
       (e->transcribed_ms || e->asr_floor_ms)) {
        e->asr_floor_ms=(uint16_t)e->speech_ms;
        e->transcribed_ms=0;e->state=AGENT_EP_WAIT;
    }
    e->asr_notice=notice;
    e->pending=(notice&AGENT_EP_PENDING)!=0;
    /* The mailbox encodes whole frames, so division/revalidation of alignment
     * is unnecessary here. Still reject a proposal beyond this capture's cap. */
    unsigned remote=((notice&AGENT_EP_REMOTE_MASK)>>AGENT_EP_REMOTE_SHIFT)*20u;
    e->remote_ms=remote<=e->maximum_ms?remote:0;
    if(notice&AGENT_EP_TEXT)(void)agent_endpoint_transcribed(e);
}

void agent_endpoint_verify(agent_endpoint_t *e,bool confirmed)
{
    if(!e || confirmed || e->state==AGENT_EP_CANCELLED || e->state==AGENT_EP_NO_SPEECH) return;
    if(e->elapsed_ms>=e->wait_ms) e->state=AGENT_EP_NO_SPEECH;
    else if(e->state==AGENT_EP_DONE) {
        /* An early unconfirmed burst is not the user's utterance. Keep waiting
         * for later speech, preserving both the PCM and original deadline. */
        e->state=AGENT_EP_WAIT;
        e->speech_ms=e->quiet_ms=e->onset_bits=e->support_at_ms=e->weak_bits=e->dense_at_ms=0;
        e->local_onset=false;e->transcribed_ms=e->asr_floor_ms=0;
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
