/* Independent64-bit filter plus unchanged YIN reference on full-range input. */
#include "pitch_lowpass.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t random_state = (uint32_t)20261003405ull;
static uint32_t next_random(void) {
    random_state=random_state*1664525u+1013904223u;
    return random_state;
}
static void reference_filter(int64_t section[2],const int16_t *pcm,int16_t *out) {
    for(unsigned i=0;i<256;++i) {
        int64_t value=(int64_t)pcm[i]*2;
        for(unsigned n=0;n<2;++n) {
            section[n]+=((value-section[n])*12313)/32768;
            value=section[n];
            assert(value>=-65536 && value<=65534);
        }
        out[i]=(int16_t)(value/2);
    }
}
static bool near(double estimate,double target) {
    return fabs(estimate-target)<=fmax(2.,target*.02);
}
int main(void) {
    kws_pitch_lowpass_t state,combined;
    kws_pitch_t reference;
    int64_t section[2]={0,0};
    int16_t pcm[256],expected[256],actual[256];
    kws_pitch_lowpass_reset(&state);kws_pitch_lowpass_reset(&combined);
    kws_pitch_reset(&reference);
    assert(sizeof(state)>=sizeof(reference)+8 && sizeof(state)<=784);
    for(unsigned frame=0;frame<4096;++frame) {
        if(frame%113==0) {
            kws_pitch_lowpass_reset(&state);kws_pitch_lowpass_reset(&combined);
            kws_pitch_reset(&reference);section[0]=section[1]=0;
        }
        for(unsigned i=0;i<256;++i) {
            switch((frame/128)%7) {
            case 0:pcm[i]=(int16_t)((int)(next_random()>>16)-32768);break;
            case 1:pcm[i]=i%2?32767:-32768;break;
            case 2:pcm[i]=i/2%2?32767:-32768;break;
            case 3:pcm[i]=-32768;break;
            case 4:pcm[i]=32767;break;
            case 5:pcm[i]=0;break;
            default:pcm[i]=(int16_t)lround(30000*sin((frame*256+i)*6.283185307179586*200/16000));break;
            }
        }
        reference_filter(section,pcm,expected);
        assert(kws_pitch_lowpass_filter(&state,pcm,actual));
        assert(memcmp(expected,actual,sizeof(expected))==0);
        for(unsigned n=0;n<2;++n)assert(state.section_q1[n]==section[n]);
        kws_pitch_features_t a=kws_pitch_block(&reference,expected);
        kws_pitch_features_t b=kws_pitch_lowpass_block(&combined,pcm);
        assert(a.frequency_q4==b.frequency_q4 && a.periodicity_q12==b.periodicity_q12);
        assert(memcmp(&reference,&combined.pitch,sizeof(reference))==0);
        assert(memcmp(state.section_q1,combined.section_q1,sizeof(state.section_q1))==0);
    }
    double frequencies[]={90,120,160,200,320,480,720};
    int amplitudes[]={300,3000,30000};
    unsigned clean_frames=0;
    for(unsigned f=0;f<7;++f)for(unsigned a=0;a<3;++a) {
        kws_pitch_lowpass_reset(&state);
        for(unsigned frame=0;frame<42;++frame) {
            for(unsigned i=0;i<256;++i)
                pcm[i]=(int16_t)lround(amplitudes[a]*sin((frame*256+i)*6.283185307179586*frequencies[f]/16000));
            kws_pitch_features_t result=kws_pitch_lowpass_block(&state,pcm);
            if(frame>=12) {assert(near(result.frequency_q4/16.,frequencies[f]));++clean_frames;}
        }
    }
    assert(clean_frames==630);
    for(unsigned kind=0;kind<4;++kind) {
        kws_pitch_lowpass_reset(&state);
        for(unsigned frame=0;frame<256;++frame) {
            for(unsigned i=0;i<256;++i)
                pcm[i]=kind==0?0:kind==1?12345:kind==2?(i%2?30000:-30000):
                    (int16_t)((int)(next_random()>>16)-32768);
            kws_pitch_features_t result=kws_pitch_lowpass_block(&state,pcm);
            assert(result.frequency_q4==0 && result.periodicity_q12==0);
        }
    }
    kws_pitch_lowpass_reset(&state);
    kws_pitch_lowpass_t before=state;
    assert(!kws_pitch_lowpass_filter(NULL,pcm,actual));
    assert(!kws_pitch_lowpass_filter(&state,NULL,actual));
    assert(!kws_pitch_lowpass_filter(&state,pcm,NULL));
    assert(memcmp(&before,&state,sizeof(state))==0);
    kws_pitch_features_t missing=kws_pitch_lowpass_block(NULL,pcm);
    assert(!missing.frequency_q4 && !missing.periodicity_q12);
    for(unsigned i=0;i<256;++i)pcm[i]=(int16_t)i;
    assert(kws_pitch_lowpass_filter(&state,pcm,expected));
    kws_pitch_lowpass_reset(&state);
    assert(kws_pitch_lowpass_filter(&state,pcm,pcm));
    assert(memcmp(pcm,expected,sizeof(pcm))==0);
    printf("{\"complete\":true,\"adversarial_frames\":4096,\"filter_samples_exact\":1048576,\"clean_frames\":%u,\"noise_frames\":256,\"state_bytes\":%zu,\"old_state_bytes\":%zu,\"independent_INT64_exact\":true}\n",clean_frames,sizeof(state),sizeof(reference));
    return 0;
}
