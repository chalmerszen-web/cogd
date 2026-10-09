/* Original trusted outputs and state exact under adversarial PCM and reset. */
#include "periodicity.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t random_state=(uint32_t)20261003410ull;
static uint32_t next_random(void) {
    random_state=random_state*1664525u+1013904223u;return random_state;
}
int main(void) {
    kws_periodicity_t current,other;
    kws_pitch_t original;
    int16_t pcm[256];
    kws_periodicity_reset(&current);kws_periodicity_reset(&other);kws_pitch_reset(&original);
    unsigned uncertain_nonzero=0;
    for(unsigned frame=0;frame<4096;++frame) {
        if(frame%127==0) {
            kws_periodicity_reset(&current);kws_pitch_reset(&original);
        }
        for(unsigned i=0;i<256;++i) {
            switch((frame/128)%8) {
            case 0:pcm[i]=(int16_t)((int)(next_random()>>16)-32768);break;
            case 1:pcm[i]=i%2?32767:-32768;break;
            case 2:pcm[i]=i/2%2?32767:-32768;break;
            case 3:pcm[i]=-32768;break;
            case 4:pcm[i]=32767;break;
            case 5:pcm[i]=0;break;
            default:pcm[i]=(int16_t)lround(30000*sin((frame*256+i)*6.283185307179586*200/16000));break;
            }
        }
        kws_pitch_features_t a=kws_pitch_block(&original,pcm);
        kws_periodicity_features_t b=kws_periodicity_block(&current,pcm);
        assert(a.frequency_q4==b.trusted_frequency_q4);
        if(a.frequency_q4)assert(a.periodicity_q12==b.strength_q12);
        if(!a.frequency_q4 && b.strength_q12)++uncertain_nonzero;
        assert(b.strength_q12>=0 && b.strength_q12<=4096);
        assert(memcmp(&original,&current,sizeof(original))==0);
    }
    assert(uncertain_nonzero>64 && sizeof(current)==770);
    kws_periodicity_t before=current;
    kws_periodicity_features_t bad=kws_periodicity_block(&current,NULL);
    assert(!bad.trusted_frequency_q4 && !bad.strength_q12 && memcmp(&current,&before,sizeof(current))==0);
    bad=kws_periodicity_block(NULL,pcm);
    assert(!bad.trusted_frequency_q4 && !bad.strength_q12);
    kws_periodicity_reset(&current);kws_periodicity_reset(&other);
    for(unsigned f=0;f<32;++f) {
        for(unsigned i=0;i<256;++i)pcm[i]=(int16_t)(i*13-1200);
        kws_periodicity_features_t a=kws_periodicity_block(&current,pcm),b=kws_periodicity_block(&other,pcm);
        assert(a.trusted_frequency_q4==b.trusted_frequency_q4 && a.strength_q12==b.strength_q12);
        assert(memcmp(&current,&other,sizeof(current))==0);
    }
    printf("{\"complete\":true,\"frames\":4096,\"trusted_outputs_and_state_exact\":true,\"state_bytes\":%zu,\"unknown_nonzero_strength\":%u,\"reset_instance_null_safe\":true}\n",sizeof(current),uncertain_nonzero);
    return 0;
}
