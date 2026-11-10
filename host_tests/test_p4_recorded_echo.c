#include "p4_near_correlation.h"
#include "p4_near_votes.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* Independently recorded device-only /shi-er/ echo: coarse/refined search
 * missed the direct-path peaks at 28 and 42 samples. No threshold changes. */
int main(void)
{
    FILE *f=fopen(RECORDED_ECHO_FIXTURE,"rb");assert(f);
    unsigned caught=0;
    for(unsigned i=0;i<2;i++){
        int16_t mic[512],out[512],ref[2048];
        assert(fread(mic,2,512,f)==512);assert(fread(out,2,512,f)==512);assert(fread(ref,2,2048,f)==2048);
        p4_near_correlation_t old=p4_near_correlate(mic,out,ref+1536,ref,false);
        p4_near_correlation_t fixed=p4_near_correlate_refined(mic,out,ref+1536,ref,false);
        assert(!old.mic_echo && !old.output_echo);assert(fixed.mic_echo);++caught;
    }
    assert(fgetc(f)==EOF);fclose(f);assert(caught==2);
    /* The original false confirmation had six votes, of which these two
     * were echo. A real independent six-vote chain is still admitted. */
    assert(!p4_near_duplex_confirm(true,0x3f,0x05));
    assert(p4_near_duplex_confirm(true,0x3f,0x07));
    assert(!p4_near_duplex_confirm(true,0x3f&~0x24u,0x05&~0x24u));
    return 0;
}
