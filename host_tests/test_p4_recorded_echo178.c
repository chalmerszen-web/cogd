#include "p4_voice_periodicity.h"
#include "p4_near_votes.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* Original178 rule, retained only to prove the exact captured regression. */
static bool previous_strong(const int16_t nlp[512],const int16_t linear[512],bool nv,bool lv)
{
    if(!p4_voice_above_low_band(nlp))return false;
    if(nv && p4_voice_periodic(nlp) && p4_voice_low_component(nlp) && !p4_voice_high_tone(nlp))return true;
    return lv && p4_voice_above_low_band(linear) && p4_voice_periodic(linear) &&
        p4_voice_low_component(linear) && !p4_voice_high_tone(linear);
}
int main(void)
{
    FILE *f=fopen(RECORDED_ECHO178_FIXTURE,"rb");assert(f);
    uint32_t cases;assert(fread(&cases,4,1,f)==1 && cases==3);
    for(unsigned c=0;c<cases;++c){
        uint32_t count,old_expected,expected,last_expected;
        assert(fread(&count,4,1,f)==1 && count==12);
        assert(fread(&old_expected,4,1,f)==1 && fread(&expected,4,1,f)==1 && fread(&last_expected,4,1,f)==1);
        unsigned votes=0,old_strong=0,strong=0,old_first=0,first=0,removed=0,at=0;
        for(unsigned frame=0;frame<count;++frame){
            uint32_t flags;int16_t nlp[512],linear[512];
            assert(fread(&at,4,1,f)==1 && fread(&flags,4,1,f)==1);
            assert(fread(nlp,2,512,f)==512 && fread(linear,2,512,f)==512);
            assert(!(flags&262144)); /* These post-reset windows are unclipped. */
            bool near=(flags&64)!=0,nv=(flags&2)!=0 && (flags&16384)==0,lv=(flags&8192)!=0;
            bool prior=near && previous_strong(nlp,linear,nv,lv);
            bool current=near && p4_voice_duplex_strong(nlp,linear,nv,lv,0);
            assert(prior==((flags&128)!=0));
            if(prior && !current){++removed;assert(!nv && lv);}
            votes=(votes<<1)|near;old_strong=(old_strong<<1)|prior;strong=(strong<<1)|current;
            if(!old_first && p4_near_duplex_confirm(near,votes,old_strong))old_first=at;
            if(!first && p4_near_duplex_confirm(near,votes,strong))first=at;
        }
        assert(at==last_expected && old_first==old_expected && first==expected);
        assert(removed==(c==0?3u:0u));
    }
    assert(fgetc(f)==EOF);fclose(f);
    puts("PASS178 linear-only echo chain rejected; two actual output interruption confirmations preserved");
    return 0;
}
