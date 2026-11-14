#include "p4_voice_periodicity.h"
#include "p4_near_votes.h"
#include "p4_near_quality.h"
#include "../platform/espidf/p4_adaptive_energy.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    FILE *f=fopen(RECORDED_STRUCTURE187_FIXTURE,"rb");assert(f);
    uint32_t count;assert(fread(&count,4,1,f)==1 && count==2);
    for(unsigned c=0;c<count;++c){
        uint32_t id,n;assert(fread(&id,4,1,f)==1 && fread(&n,4,1,f)==1);
        assert((id==42 && n==249) || (id==45 && n==61));
        unsigned votes=0,strong=0,structure=0,first[2]={0},first_onset[2]={0};
        p4_near_onset_t onset={0},hint={0};
        for(unsigned i=0;i<n;++i){
            uint32_t at,flags,threshold;int16_t mic[512],a[512],l[512];
            assert(fread(&at,4,1,f)==1 && fread(&flags,4,1,f)==1 && fread(&threshold,4,1,f)==1);
            assert(fread(mic,2,512,f)==512 && fread(a,2,512,f)==512 && fread(l,2,512,f)==512);
            bool near=(flags&64)!=0,av=(flags&2)!=0 && (flags&16384)==0,lv=(flags&8192)!=0;
            if(p4_near_saturation_guard((flags&4)!=0,mic,a,l,&votes,&strong,&onset,&hint)){structure=0;continue;}
            bool power=(flags&65536)!=0 && p4_energy_mean_square(a,512)>threshold;
            bool solid=near && p4_voice_duplex_evidence(a,l,av,lv,0,power);
            bool voiced=near && p4_voice_duplex_structure(a,l,av,lv);
            votes=((votes<<1)|near)&0xffffu;strong=((strong<<1)|solid)&0xffffu;
            structure=((structure<<1)|voiced)&0xffffu;
            p4_near_onset_observe(&onset,at,near);p4_near_onset_hint(&hint,at,(flags&32768)!=0);
            bool confirmed[2]={p4_near_duplex_confirm(near,votes,strong),p4_near_duplex_corroborated(near,votes,strong,structure)};
            for(unsigned j=0;j<2;++j)if(confirmed[j] && !first[j]){first[j]=at;first_onset[j]=p4_near_onset_select(&onset,&hint,at);}
        }
        if(id==42){assert(first[0]==4918103 && first[1]==first[0]);assert(first_onset[0]==4917758 && first_onset[1]==first_onset[0]);}
        else {assert(first[0]==277823 && first_onset[0]==277658);assert(first[1]==0 && first_onset[1]==0);}
    }
    assert(fgetc(f)==EOF);fclose(f);
    puts("PASS recorded near interruption timing/onset retained and186 unplanned story cut rejected");
    return 0;
}
