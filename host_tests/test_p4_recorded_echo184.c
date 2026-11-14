#include "p4_voice_periodicity.h"
#include "p4_near_votes.h"
#include "p4_near_quality.h"
#include "../platform/espidf/p4_adaptive_energy.h"
#include <assert.h>
#include <stdio.h>

/* Retain the failed184 supplementary rule solely to demonstrate the captured
 * regression. The VAD/ordinary-eligibility bits are actual SDK observations. */
static bool previous_evidence(const int16_t a[512],const int16_t l[512],bool av,bool lv,bool power)
{
    return p4_voice_duplex_strong(a,l,av,lv,0) ||
        (power && av && lv && p4_voice_above_low_band(a) &&
         p4_voice_low_component(a) && !p4_voice_high_tone(a));
}
int main(void)
{
    FILE *f=fopen(RECORDED_ECHO184_FIXTURE,"rb");assert(f);
    uint32_t count;assert(fread(&count,4,1,f)==1 && count==61);
    unsigned votes[2]={0},strong[2]={0},first[2]={0},last[2]={0};
    p4_near_onset_t onset[2]={{0}},hint[2]={{0}};
    p4_near_output_tail_t tail={0};unsigned added_tail=0,removed=0;
    for(unsigned i=0;i<count;++i){
        uint32_t at,flags,threshold;int16_t mic[512],a[512],l[512];
        assert(fread(&at,4,1,f)==1 && fread(&flags,4,1,f)==1 && fread(&threshold,4,1,f)==1);
        assert(fread(mic,2,512,f)==512 && fread(a,2,512,f)==512 && fread(l,2,512,f)==512);
        bool near=(flags&64)!=0,av=(flags&2)!=0 && (flags&16384)==0,lv=(flags&8192)!=0;
        bool power=(flags&65536)!=0 && p4_energy_mean_square(a,512)>threshold;
        bool solid[2]={near && previous_evidence(a,l,av,lv,power),
            near && p4_voice_duplex_evidence(a,l,av,lv,0,power)};
        bool dual[2]={(flags&4)!=0,p4_near_output_duplex(&tail,at,(flags&4)!=0,false)};
        if(dual[1] && !dual[0])++added_tail;
        if(solid[0] && !solid[1])++removed;
        for(unsigned j=0;j<2;++j){
            if(p4_near_saturation_guard(dual[j],mic,a,l,&votes[j],&strong[j],&onset[j],&hint[j]))continue;
            votes[j]=(votes[j]<<1)|near;strong[j]=(strong[j]<<1)|solid[j];
            bool confirmed=dual[j]?p4_near_duplex_confirm(near,votes[j],strong[j]):
                near && __builtin_popcount(votes[j]&4095u)>=3;
            if(!j){assert(solid[j]==((flags&128)!=0));assert(confirmed==((flags&256)!=0));}
            if(confirmed){if(!first[j])first[j]=at;last[j]=at;}
        }
    }
    assert(fgetc(f)==EOF);fclose(f);
    assert(first[0]==14957528 && last[0]==14957917 && first[1]==0 && last[1]==0);
    assert(removed==6 && added_tail==1);
    puts("PASS184 recorded echo and idle-rule tail promotion rejected; original failure reproduced");
    return 0;
}
