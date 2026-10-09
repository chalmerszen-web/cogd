/* Full-range state/output equivalence, including adversarial valid windows. */
#include "pitch.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

void kws_pitch_reference_reset(kws_pitch_t *state);
kws_pitch_features_t kws_pitch_reference_block(kws_pitch_t *state,const int16_t pcm[256]);

static uint32_t random_state = 20261003;
static uint32_t next_random(void) {
    random_state=random_state*1664525u+1013904223u;
    return random_state;
}

int main(void) {
    kws_pitch_t reference,fast;
    kws_pitch_reference_reset(&reference);
    kws_pitch_reset(&fast);
    int16_t pcm[256];
    unsigned voiced=0;
    for(unsigned frame=0;frame<2048;++frame) {
        if(frame%97==0) {
            /* Each constructed window value can result from legal PCM pairs. */
            for(unsigned i=0;i<384;++i)
                reference.window[i]=(int16_t)((int)(next_random()>>20)-2048);
            reference.seen=384;fast=reference;
        }
        for(unsigned i=0;i<256;++i) {
            unsigned sample=frame*256+i;
            switch((frame/128)%8) {
            case 0: pcm[i]=(int16_t)((int)(next_random()>>16)-32768);break;
            case 1: pcm[i]=(int16_t)(i%2?32767:-32768);break;
            case 2: pcm[i]=(int16_t)(i/2%2?32767:-32768);break;
            case 3: pcm[i]=-32768;break;
            case 4: pcm[i]=32767;break;
            case 5: pcm[i]=0;break;
            default: {
                double frequency=(frame/128)%8==6?90.0:720.0;
                pcm[i]=(int16_t)lround(30000.0*sin(sample*6.283185307179586*frequency/16000.0));
            }}
        }
        kws_pitch_features_t a=kws_pitch_reference_block(&reference,pcm),b=kws_pitch_block(&fast,pcm);
        assert(a.frequency_q4==b.frequency_q4 && a.periodicity_q12==b.periodicity_q12);
        assert(memcmp(&reference,&fast,sizeof(reference))==0);
        if(b.frequency_q4) ++voiced;
    }
    assert(voiced>128);
    puts("{\"complete\":true,\"frames\":2048,\"state_and_feature_values_exact\":true}");
    return 0;
}
