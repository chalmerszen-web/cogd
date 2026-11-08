#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A periodic low-frequency residual can pass both VADs without being a new
 * utterance. In the captured tutoring failure, the two confirming frames
 * had 92/98 percent of their energy below 200 Hz. Average before decimating
 * and require some upper-band variation for a STRONG vote. Ordinary votes,
 * onset retention and the actual uplink keep all low-pitched speech intact.
 * This is only additional evidence alongside VAD, echo rejection and pitch;
 * it does not classify a frame as speech on its own. */
static inline bool p4_voice_above_low_band(const int16_t pcm[512])
{
    int16_t x[128];int32_t sum=0;
    for(unsigned i=0;i<128;++i){
        int32_t value=0;
        for(unsigned j=0;j<4;++j)value+=pcm[4*i+j]>>5;
        x[i]=(int16_t)(value/4);sum+=x[i];
    }
    int32_t mean=sum/128,energy=0,change=0;
    for(unsigned i=0;i<128;++i){
        int32_t value=x[i]-mean;energy+=value*value;
        if(i){int32_t delta=x[i]-x[i-1];change+=delta*delta;}
    }
    /* The shifted inputs lie in [-1024,1023]; both sums fit int32_t,
     * including alternating full-scale values. No FFT or allocation. */
    return energy>0 && change>energy/8;
}

/* Additional evidence for a strong native-duplex vote, not a speech gate.
 * A recorded echo-only failure passed VAD/energy with aperiodic NLP residue.
 * Require voiced structure in a few strong frames while retaining unvoiced
 * frames, the original onset and all samples in the interruption replay.
 * At 4 kHz, lags 8..64 cover periods from 2 to 16 ms. No sqrt, allocation or
 * software double is needed on the P4 capture task. */
static inline bool p4_voice_periodic(const int16_t pcm[512])
{
    int16_t x[128];int32_t sum=0;
    for(unsigned i=0;i<128;++i){x[i]=pcm[4*i]>>4;sum+=x[i];}
    int32_t mean=sum/128;
    for(unsigned i=0;i<128;++i)x[i]=(int16_t)(x[i]-mean);
    /* Centered full-scale samples stay in +/-4095; even 128 products are
     * below INT32_MAX. Cast before multiplying energies/correlations. */
    for(unsigned lag=8;lag<=64;++lag){
        int32_t cross=0,a=0,b=0;
        for(unsigned i=lag;i<128;++i){
            int32_t u=x[i],v=x[i-lag];
            cross+=u*v;a+=u*u;b+=v*v;
        }
        if(cross>0 && a>0 && b>0 &&
           (float)cross*(float)cross>=0.5625f*(float)a*(float)b)return true;
    }
    return false;
}

/* Called only after the existing VAD, linear-energy, alignment and echo
 * correlation admission. NLP gain is not a reliable near-voice amplitude:
 * two captured missed interruptions retained speech in the linear output
 * while NLP attenuated the same frames below the former 620 floor.
 * A quieter NLP frame can supply periodic evidence above a noise floor; a
 * more heavily suppressed frame additionally needs the independent linear
 * VAD and linear voiced spectrum. Low-band NLP residue remains rejected.
 * This changes confirmation evidence, never samples or retained onset. */
static inline bool p4_voice_duplex_strong(const int16_t nlp[512],
    const int16_t linear[512],bool linear_voice,unsigned nlp_level)
{
    if(!p4_voice_above_low_band(nlp))return false;
    if(nlp_level>200 && p4_voice_periodic(nlp))return true;
    return nlp_level<=620 && linear_voice &&
        p4_voice_above_low_band(linear) && p4_voice_periodic(linear);
}
