#include "kws.h"
#include <string.h>

void kws_detector_reset(kws_detector_t *d,int16_t threshold_q8)
{ memset(d,0,sizeof(*d)); d->threshold_q8=threshold_q8; }
void kws_detector_stability(kws_detector_t *d,bool stable)
{ d->stable=stable; d->votes=0; }
bool kws_detector_step_armed(kws_detector_t *d,int16_t score,uint64_t sample_end,bool armed)
{
    if(d->blocks && sample_end-d->last_sample!=512) {
        bool stable=d->stable;
        kws_detector_reset(d,d->threshold_q8);
        d->stable=stable;
    }
    d->last_sample=sample_end;
    if(d->blocks<64) ++d->blocks;
    if(!armed) { d->votes=0; return false; }
    unsigned mask=d->stable?15:7;
    d->votes=(uint8_t)(((d->votes<<1)|(score>=d->threshold_q8))&mask);
    unsigned n=(d->votes&1)+((d->votes>>1)&1)+((d->votes>>2)&1);
    bool confirmed=d->stable?d->votes==mask:n>=2;
    if(d->blocks<64 || sample_end<d->cooldown_until || !confirmed) return false;
    d->cooldown_until=sample_end+24000;
    d->votes=0;
    return true;
}
bool kws_detector_step(kws_detector_t *d,int16_t score,uint64_t sample_end)
{ return kws_detector_step_armed(d,score,sample_end,true); }
