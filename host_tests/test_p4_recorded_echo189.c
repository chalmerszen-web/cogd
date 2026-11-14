#include "p4_echo_witness.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The retained story189b capture is the exact device-only playback window
 * that caused a local-VAD interruption.  The absolute N/F/MAD bound was too
 * tight for this transfer path; the causal relative residual envelope must
 * reject the coherent echo without suppressing an independent signal. */
int main(void)
{
    FILE *f=fopen(RECORDED_ECHO189_FIXTURE,"rb");assert(f);
    uint32_t frames;assert(fread(&frames,4,1,f)==1 && frames==62);
    p4_echo_witness_t *s=calloc(1,sizeof(*s));assert(s);
    unsigned veto=0,late_veto=0,trained=0;
    for(unsigned i=0;i<frames;++i){
        uint32_t scale,threshold;int16_t mic[512],ref[512];
        assert(fread(&scale,4,1,f)==1 && fread(&threshold,4,1,f)==1);
        assert(scale==4 && threshold==39828);
        assert(fread(mic,2,512,f)==512 && fread(ref,2,512,f)==512);
        p4_echo_witness_result_t r=p4_echo_witness_process(s,mic,ref);
        bool v=p4_echo_witness_veto(r,true,true,scale,threshold);
        if(v)++veto;
        if(i>=46 && v)++late_veto;
        if(r.trained>=8)++trained;
    }
    assert(fgetc(f)==EOF);fclose(f);free(s);
    assert(trained>=54);
    assert(late_veto>=6);
    printf("PASS189 retained playback echo: %u vetoes, %u in final window\n",veto,late_veto);
    return 0;
}
