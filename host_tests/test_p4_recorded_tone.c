#include "p4_voice_periodicity.h"
#include "p4_near_votes.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* Replay the original captured failure plus independently captured real
 * interruptions. A new predicate must remove the false confirmation without
 * delaying the first confirmation of the positive controls. */
int main(void)
{
    FILE *f=fopen(RECORDED_TONE_FIXTURE,"rb");assert(f);
    uint32_t cases;assert(fread(&cases,4,1,f)==1 && cases==4);
    for(unsigned c=0;c<cases;++c){
        uint32_t count,old_expected,expected;
        assert(fread(&count,4,1,f)==1 && count>0 && count<=32);
        assert(fread(&old_expected,4,1,f)==1);
        assert(fread(&expected,4,1,f)==1);
        unsigned votes=0,old_strong=0,strong=0,old_first=0,first=0;
        for(unsigned frame=0;frame<count;++frame){
            uint32_t at,flags;int16_t nlp[512],linear[512];
            assert(fread(&at,4,1,f)==1 && fread(&flags,4,1,f)==1);
            assert(fread(nlp,2,512,f)==512 && fread(linear,2,512,f)==512);
            unsigned level=0;for(unsigned i=0;i<512;i++)level+=(unsigned)abs(nlp[i]);level/=512;
            bool near=(flags&64)!=0,linear_voice=(flags&8192)!=0;
            bool original=near && p4_voice_above_low_band(nlp) &&
                ((level>200 && p4_voice_periodic(nlp)) ||
                 (level<=620 && linear_voice && p4_voice_above_low_band(linear) && p4_voice_periodic(linear)));
            votes=(votes<<1)|near;old_strong=(old_strong<<1)|original;
            strong=(strong<<1)|(near && p4_voice_duplex_strong(nlp,linear,linear_voice,level));
            if(!old_first && p4_near_duplex_confirm(near,votes,old_strong))old_first=at;
            if(!first && p4_near_duplex_confirm(near,votes,strong))first=at;
        }
        assert(old_first==old_expected);assert(first==expected);
    }
    assert(fgetc(f)==EOF);fclose(f);
    puts("PASS captured tonal false cut and real interruption timing");
}
