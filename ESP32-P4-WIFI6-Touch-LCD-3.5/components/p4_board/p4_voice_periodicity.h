#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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

/* A voiced frame can traverse a capture boundary even though the neighbouring
 * frame does not contain a full pitch period at sufficient amplitude. Test the
 * same lag within the admitted anchor and across the boundary. Sample every
 * other point; centering prevents a DC step from acting as pitch evidence. */
static inline bool p4_voice_pitch_match(const int16_t *a,const int16_t *b,unsigned samples)
{
    int32_t sum_a=0,sum_b=0,aa=0,bb=0,ab=0,n=0;
    for(unsigned i=0;i<samples;i+=2){
        int32_t u=a[i]>>5,v=b[i]>>5;
        sum_a+=u;sum_b+=v;aa+=u*u;bb+=v*v;ab+=u*v;++n;
    }
    int64_t da=(int64_t)n*aa-(int64_t)sum_a*sum_a;
    int64_t db=(int64_t)n*bb-(int64_t)sum_b*sum_b;
    int64_t cross=(int64_t)n*ab-(int64_t)sum_a*sum_b;
    return cross>0 && da>0 && db>0 &&
        (float)cross*(float)cross>=0.5625f*(float)da*(float)db;
}

static inline bool p4_voice_low_component(const int16_t pcm[512]);
static inline bool p4_voice_high_tone(const int16_t pcm[512]);

static inline bool p4_voice_broadband(const int16_t pcm[512])
{
    return p4_voice_above_low_band(pcm) && p4_voice_low_component(pcm) &&
        !p4_voice_high_tone(pcm);
}

/* Preserve only a waveform-coherent, smooth lead for a later residual
 * confirmation. This is computed on already admissible residual candidates
 * and cannot supply either an ordinary or a strong vote. Center both signals
 * so a common DC offset does not count as agreement. */
static inline bool p4_voice_residual_onset_coherent(const int16_t linear[512],
    const int16_t nlp[512])
{
    enum { samples=256 };
    int32_t x_sum=0,y_sum=0,xx=0,yy=0,xy=0;
    /* At most 255 signed PCM16 steps of 4095 in Q4 fit uint32_t. */
    uint32_t x_change=0,y_change=0;
    int32_t last_x=linear[0]>>4,last_y=nlp[0]>>4;
    for(unsigned i=0;i<512;i+=2){
        int32_t x=linear[i]>>4,y=nlp[i]>>4;
        int32_t dx=x-last_x,dy=y-last_y;
        x_sum+=x;y_sum+=y;xx+=x*x;yy+=y*y;xy+=x*y;
        x_change+=dx*dx;y_change+=dy*dy;
        last_x=x;last_y=y;
    }
    int64_t x_variance=(int64_t)samples*xx-(int64_t)x_sum*x_sum;
    int64_t y_variance=(int64_t)samples*yy-(int64_t)y_sum*y_sum;
    int64_t covariance=(int64_t)samples*xy-(int64_t)x_sum*y_sum;
    if(x_variance<=0 || y_variance<=0 || covariance<=0 ||
       (int64_t)samples*x_change>x_variance || (int64_t)samples*y_change>y_variance)return false;
    return (float)covariance*(float)covariance>=0.7225f*(float)x_variance*(float)y_variance;
}

/* During a natural reference gap, compare the aligned microphone with the
 * linear signal and check that NLP preserves its shape. Use centered,
 * full-resolution samples because NLP can heavily attenuate a weak syllable. */
static inline bool p4_voice_reference_gap_shape(const int16_t mic[512],
    const int16_t linear[512],const int16_t nlp[512])
{
    float m=0,l=0,n=0,mm=0,ll=0,nn=0,ml=0,ln=0,dl=0,dn=0;
    float previous_l=linear[0],previous_n=nlp[0];
    for(unsigned i=0;i<512;++i){
        float x=mic[i],y=linear[i],z=nlp[i];
        float step_l=y-previous_l,step_n=z-previous_n;
        m+=x;l+=y;n+=z;mm+=x*x;ll+=y*y;nn+=z*z;ml+=x*y;ln+=y*z;
        dl+=step_l*step_l;dn+=step_n*step_n;previous_l=y;previous_n=z;
    }
    mm-=m*m/512.f;ll-=l*l/512.f;nn-=n*n/512.f;
    ml-=m*l/512.f;ln-=l*n/512.f;
    return mm>0 && ll>0 && nn>0 && ml>0 && ln>0 &&
        2.f*dl<=ll && 2.f*dn<=nn &&
        ml*ml>.81f*mm*ll && ln*ln>.5625f*ll*nn;
}

/* Inspect the three 32 ms DAC blocks ending at the aligned PCM block.
 * Compare reference power in the residual signal's scaled domain against
 * the live adaptive gate. Sustained DAC silence is measured, never forced. */
static inline bool p4_voice_reference_gap(const int16_t reference[1536],
    unsigned scale,uint32_t threshold)
{
    if(!scale || scale>32 || !threshold)return false;
    uint64_t limit=(uint64_t)threshold*512u;
    for(unsigned frame=0;frame<3;++frame){
        uint64_t power=0;
        for(unsigned i=0;i<512;++i){int32_t x=reference[frame*512+i];power+=(uint32_t)(x*x);}
        if(power*scale*scale>=limit)return false;
    }
    return true;
}

typedef struct {
    unsigned linked;
    uint32_t lags[8];
    int16_t prior[512];
} p4_voice_pitch_history_t;

/* The caller supplies only original (unpromoted) evidence history, so a
 * carried pitch period cannot promote the next frame recursively. A reset of
 * evidence also resets linked; prior PCM is then unreachable. The same period
 * must be corroborated in both anchors and across all three boundaries. */
static inline bool p4_voice_pitch_history_update(p4_voice_pitch_history_t *s,
    const int16_t linear[512],unsigned anchors,unsigned provisional,
    bool anchor,bool gap)
{
    s->linked=(s->linked<<1)&0xfu;
    bool matched=false;
    bool leading=anchor && (provisional&1u);
    bool trailing=gap && (anchors&3u)==3u && (s->linked&8u);
    if(leading){
        memset(s->lags,0,sizeof(s->lags));
        if(p4_voice_broadband(s->prior)){
            for(unsigned lag=32;lag<=256;++lag){
                if(p4_voice_pitch_match(s->prior+512-lag,linear,lag) &&
                   p4_voice_pitch_match(linear,linear+lag,128)){
                    unsigned at=lag-32;
                    s->lags[at/32]|=1u<<(at%32);
                    matched=true;
                }
            }
            if(matched)s->linked|=2u;
        }
    }
    if(anchor && (anchors&1u) && (s->linked&4u)){
        /* Limit the carried period to one that continues between the two
         * ordinary strong frames too before testing the trailing edge. */
        for(unsigned lag=32;lag<=256;++lag){
            unsigned at=lag-32;
            uint32_t *word=&s->lags[at/32],bit=1u<<(at%32);
            if((*word&bit) && !p4_voice_pitch_match(s->prior+512-lag,linear,lag))
                *word&=~bit;
        }
    }
    if(trailing && p4_voice_broadband(linear)){
        for(unsigned lag=32;lag<=256;++lag){
            unsigned at=lag-32;
            if(!(s->lags[at/32]&(1u<<(at%32))))continue;
            if(p4_voice_pitch_match(s->prior+384-lag,s->prior+384,128) &&
               p4_voice_pitch_match(s->prior+512-lag,linear,lag)){
                s->linked|=1u;
                matched=true;
                break;
            }
        }
    }
    if(anchor || gap)memcpy(s->prior,linear,sizeof(s->prior));
    return matched;
}

/* A narrow high-frequency residual can match multiples of a vocal period.
 * The recorded photo-answer false cut was dominated by about 2 kHz. Require
 * a small lower component in whichever channel supplies the strong vote:
 * eight-sample averaging retains over 5% of the centered input energy.
 * This is evidence for a few confirmation frames, never an uplink filter.
 * Eight independently recorded real interruptions keep their original
 * confirmation time; the captured false chain no longer confirms. */
static inline bool p4_voice_low_component(const int16_t pcm[512])
{
    int16_t low[64];int32_t sum=0,low_sum=0;
    for(unsigned i=0;i<64;++i){
        int32_t value=0;
        for(unsigned j=0;j<8;++j)value+=pcm[8*i+j]>>5;
        sum+=value;low[i]=(int16_t)(value/8);low_sum+=low[i];
    }
    int32_t mean=sum/512,low_mean=low_sum/64;
    int64_t energy=0,low_energy=0;
    for(unsigned i=0;i<512;++i){int32_t v=(pcm[i]>>5)-mean;energy+=(int64_t)v*v;}
    for(unsigned i=0;i<64;++i){int32_t v=low[i]-low_mean;low_energy+=(int64_t)v*v;}
    return energy>0 && low_energy*160>energy;
}

/* A narrow residual above the vocal pitch range can still contain enough
 * low noise to pass the lower-component check. Its short period must not
 * count as voiced evidence via a longer harmonic multiple. Inspect the
 * original samples: lags 4..20 cover 800..4000 Hz without decimation alias.
 * The captured 1.84 kHz failure has >85% positive correlation here. */
static inline bool p4_voice_high_tone(const int16_t pcm[512])
{
    int32_t sum=0;
    for(unsigned i=0;i<512;++i)sum+=pcm[i]>>5;
    int32_t mean=sum/512;
    for(unsigned lag=4;lag<=20;++lag){
        int32_t cross=0,a=0,b=0;
        for(unsigned i=lag;i<512;++i){
            int32_t u=(pcm[i]>>5)-mean,v=(pcm[i-lag]>>5)-mean;
            cross+=u*v;a+=u*u;b+=v*v;
        }
        if(cross>0 && a>0 && b>0 &&
           (float)cross*(float)cross>=0.7225f*(float)a*(float)b)return true;
    }
    return false;
}

/* Window corroboration is separate from a strong frame. A real syllable can
 * lose NLP VAD at its voiced onset, then supply aperiodic strong frames later.
 * Each channel still needs its own VAD and complete voiced spectrum; ordinary
 * adaptive admission and echo rejection must already hold at the caller.
 * This never promotes a frame to STRONG or changes any audio sample. */
static inline bool p4_voice_duplex_structure(const int16_t nlp[512],
    const int16_t linear[512],bool nlp_voice,bool linear_voice)
{
    return (nlp_voice && p4_voice_above_low_band(nlp) && p4_voice_periodic(nlp) &&
            p4_voice_low_component(nlp) && !p4_voice_high_tone(nlp)) ||
        (linear_voice && p4_voice_above_low_band(linear) && p4_voice_periodic(linear) &&
            p4_voice_low_component(linear) && !p4_voice_high_tone(linear));
}

/* Called only after adaptive linear-power, VAD, alignment and echo admission.
 * Relative spectral/periodic structure supplies strong votes at any usable
 * PCM scale; each branch requires VAD on the signal supplying its spectrum.
 * NLP VAD is captured before the linear bridge, which supplies eligibility
 * but cannot lend its VAD to unrelated NLP structure. During device output,
 * linear echo can retain voiced structure after NLP rejects speech. Require
 * NLP voiced corroboration for strong evidence; linear shape still needs its
 * own linear VAD. Ordinary eligibility, onset and PCM remain unchanged. Retain the
 * low-band NLP residue rejection. Integer spectral quantization remains,
 * so this does not promise detection at arbitrarily tiny amplitudes.
 * nlp_level remains a diagnostic-compatible argument, never an admission
 * floor. This changes evidence only, never samples or retained onset. */
static inline bool p4_voice_duplex_strong(const int16_t nlp[512],
    const int16_t linear[512],bool nlp_voice,bool linear_voice,unsigned nlp_level)
{
    (void)nlp_level;
    if(!nlp_voice || !p4_voice_above_low_band(nlp))return false;
    if(nlp_voice && p4_voice_periodic(nlp) && p4_voice_low_component(nlp) && !p4_voice_high_tone(nlp))return true;
    return linear_voice &&
        p4_voice_above_low_band(linear) && p4_voice_periodic(linear) && p4_voice_low_component(linear) && !p4_voice_high_tone(linear);
}

/* Natural syllables can be aperiodic in one 32 ms frame. A second route may
 * use NLP power that independently clears the SAME calibrated N/F/MAD gate
 * as linear admission. NLP and linear are in the same PCM power domain;
 * this is deliberately stricter than testing attenuated NLP against a
 * rescaled or absolute amplitude floor. Require both VADs and retain the
 * NLP low/high-band guards. The caller has already rejected correlated
 * reference echo and clipping; the sustained vote quorum is unchanged.
 * Require the NLP stage to retain at least half of the simultaneous linear
 * power. Strong nonlinear attenuation makes aperiodic residue ambiguous,
 * even when it exceeds a very quiet room's dynamic threshold. This relative
 * cross-stage check introduces no microphone-level floor. Without this extra
 * evidence, preserve the original periodic route.
 * This is corroboration, not a classifier for arbitrary aperiodic noise. */
static inline bool p4_voice_nlp_power_retained(const int16_t nlp[512],const int16_t linear[512])
{
    uint64_t a=0,b=0;
    for(unsigned i=0;i<512;++i){int32_t x=nlp[i],y=linear[i];a+=(int64_t)x*x;b+=(int64_t)y*y;}
    return b && a*2>=b;
}
static inline bool p4_voice_duplex_evidence(const int16_t nlp[512],
    const int16_t linear[512],bool nlp_voice,bool linear_voice,
    unsigned nlp_level,bool nlp_adaptive)
{
    if(p4_voice_duplex_strong(nlp,linear,nlp_voice,linear_voice,nlp_level))return true;
    return nlp_adaptive && nlp_voice && linear_voice && p4_voice_nlp_power_retained(nlp,linear) &&
        p4_voice_above_low_band(nlp) && p4_voice_low_component(nlp) &&
        !p4_voice_high_tone(nlp);
}
