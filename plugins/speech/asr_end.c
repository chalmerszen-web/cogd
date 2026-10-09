#include "asr_end.h"
#include <string.h>

void agent_asr_end_reset(agent_asr_end_t *s)
{ if(s) {memset(s,0,sizeof(*s));s->source_valid=true;} }

void agent_asr_end_source(agent_asr_end_t *s,unsigned ms,bool speech)
{
    if(!s || !s->source_valid)return;
    if(ms>AGENT_ASR_END_MAX_MS || ms!=s->source_ms+AGENT_ASR_END_FRAME_MS) {
        s->source_valid=false;s->candidate_id=0;return;
    }
    s->source_ms=ms;
    s->onset_bits=(uint8_t)((s->onset_bits<<1)|(speech?1u:0u));
    unsigned dense=0;
    for(unsigned bits=s->onset_bits;bits;bits>>=1)dense+=bits&1u;
    if(speech && dense>=4)s->dense_end_ms=ms;
    if(speech)s->quiet_ms=0;
    else if(s->quiet_ms<AGENT_ASR_END_QUIET_MS)s->quiet_ms+=AGENT_ASR_END_FRAME_MS;
    if(s->quiet_ms>=AGENT_ASR_END_QUIET_MS) {
        s->resume_armed=true;s->onset_bits=0;
    }
    if(s->resume_armed && speech) {
        unsigned votes=0,oldest=0;
        for(unsigned bits=s->onset_bits,age=0;bits;bits>>=1,++age)
            if(bits&1u) {++votes;oldest=age;}
        if(votes>=4) {
            /* The onset is the oldest contributing frame's beginning, not
             * the fourth-vote receipt time near an original word's tail. */
            s->resume_ms=ms-(oldest+1u)*AGENT_ASR_END_FRAME_MS;
            s->resume_armed=false;
            if(s->candidate_id && s->resume_ms>s->candidate_end_ms)s->candidate_id=0;
        }
    }
}

void agent_asr_end_sentence(agent_asr_end_t *s,uint32_t id,unsigned end,
    bool final,bool nonempty,bool timed,unsigned now)
{
    if(!s || !id || id<s->newest_id)return;
    if(id>s->newest_id) {s->newest_id=id;s->candidate_id=0;}
    if(!final)return; /* A new partial already invalidated the old candidate. */
    if(!nonempty)s->candidate_id=0;
    if(id<=s->finalized_id)return;
    s->finalized_id=id;
    if(!nonempty || !timed || !end || end>AGENT_ASR_END_MAX_MS ||
       !s->source_valid || s->resume_ms>end)return;
    s->candidate_id=id;s->candidate_end_ms=end;s->candidate_at_ms=now;
}

bool agent_asr_end_ready(const agent_asr_end_t *s,bool confirmed,unsigned now)
{
    return s && confirmed && s->source_valid && s->candidate_id &&
        s->source_ms>=s->candidate_end_ms &&
        (uint32_t)(now-s->candidate_at_ms)>=AGENT_ASR_END_OBSERVE_MS;
}
bool agent_asr_end_ready_after(const agent_asr_end_t *s,bool confirmed,unsigned now,unsigned hold)
{
    return hold<=2000 && agent_asr_end_ready(s,confirmed,now) &&
        (!hold || s->dense_end_ms<=s->candidate_end_ms) &&
        s->source_ms-s->candidate_end_ms>=hold;
}
