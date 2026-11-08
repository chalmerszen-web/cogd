#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "p4_voice_periodicity.h"

static uint32_t rng=1;
static int noise(void)
{rng=rng*1664525u+1013904223u;return (int)(rng>>16)-32768;}

int main(void)
{
    int16_t pcm[512];
    const int dc[]={0,32767,-32768,8000,-8000};
    for(unsigned n=0;n<sizeof(dc)/sizeof(dc[0]);++n){
        for(unsigned i=0;i<512;++i)pcm[i]=(int16_t)dc[n];
        assert(!p4_voice_periodic(pcm));
        assert(!p4_voice_above_low_band(pcm));
    }
    /* Hum / low tonal residue is periodic, but cannot supply the strong
     * evidence that confirms a new near-end utterance during playback. */
    const double low_tones[]={50,65,80,100,125,160};
    for(unsigned f=0;f<sizeof(low_tones)/sizeof(low_tones[0]);++f)
        for(unsigned phase=0;phase<8;++phase){
            for(unsigned i=0;i<512;++i)
                pcm[i]=(int16_t)(8000+6000*sin(6.283185307179586*low_tones[f]*i/16000.0+phase*.7));
            assert(!p4_voice_above_low_band(pcm));
        }
    /* Low fundamental pitch with stronger vowel harmonics remains eligible.
     * This is a synthetic spectrum check, not a human voice-accuracy claim. */
    const double fundamentals[]={65,80,100,125,160,200,300,500};
    for(unsigned f=0;f<sizeof(fundamentals)/sizeof(fundamentals[0]);++f)
        for(unsigned phase=0;phase<4;++phase){
            double harmonic=ceil(600.0/fundamentals[f]);
            for(unsigned i=0;i<512;++i){
                double t=6.283185307179586*fundamentals[f]*i/16000.0+phase*.7;
                pcm[i]=(int16_t)(5000+6000*sin(t)+4000*sin(harmonic*t)+2000*sin((harmonic+2)*t));
            }
            assert(p4_voice_above_low_band(pcm));
            assert(p4_voice_periodic(pcm));
        }
    /* Broad-band residue must not become strong voiced evidence merely
     * because its amplitude exceeds the independent energy/VAD gates. */
    for(unsigned n=0;n<100;++n){
        for(unsigned i=0;i<512;++i)pcm[i]=(int16_t)noise();
        assert(!p4_voice_periodic(pcm));
    }
    /* Independent harmonic/noise signals exercise pitches and phase across
     * the intended range, including low/high voices and nonzero DC. */
    const double pitches[]={65,80,100,120,160,200,240,300,380,450,500};
    for(unsigned f=0;f<sizeof(pitches)/sizeof(pitches[0]);++f){
        for(unsigned phase=0;phase<4;++phase){
            for(unsigned i=0;i<512;++i){
                double t=6.283185307179586*pitches[f]*i/16000.0+phase*.7;
                pcm[i]=(int16_t)(5000+6000*sin(t)+2000*sin(2*t)+noise()/20);
            }
            assert(p4_voice_periodic(pcm));
        }
    }
    /* Full-scale periodic data and impulses check bounded arithmetic under
     * sanitizers; no assumption that periodicity alone denotes speech. */
    for(unsigned i=0;i<512;++i)pcm[i]=(i/32)%2?32767:-32768;
    assert(p4_voice_periodic(pcm));
    assert(p4_voice_above_low_band(pcm));
    for(unsigned i=0;i<512;++i)pcm[i]=(i/4)%2?32767:-32768;
    assert(p4_voice_above_low_band(pcm));
    for(unsigned i=0;i<512;++i)pcm[i]=0;
    pcm[256]=32767;assert(!p4_voice_periodic(pcm));
    int16_t linear[512],suppressed[512];
    for(unsigned i=0;i<512;++i){
        linear[i]=(i/32)%2?4000:-4000;
        suppressed[i]=(int16_t)(noise()/128);
    }
    /* A periodic linear residual with its own positive VAD can survive NLP
     * attenuation; the same low noise without that VAD is insufficient. */
    assert(p4_voice_duplex_strong(suppressed,linear,true,128));
    assert(!p4_voice_duplex_strong(suppressed,linear,false,128));
    for(unsigned i=0;i<512;++i)suppressed[i]=0;
    assert(!p4_voice_duplex_strong(suppressed,linear,true,0));
    for(unsigned i=0;i<512;++i)suppressed[i]=(int16_t)(6000*sin(6.283185307179586*125*i/16000.0));
    assert(!p4_voice_duplex_strong(suppressed,linear,true,3800));
    for(unsigned i=0;i<512;++i)suppressed[i]=(i/32)%2?300:-300;
    assert(p4_voice_duplex_strong(suppressed,linear,false,300));
    assert(!p4_voice_duplex_strong(suppressed,linear,false,100));
    puts("PASS voiced evidence, NLP attenuation, aperiodic residue and bounds");
}
