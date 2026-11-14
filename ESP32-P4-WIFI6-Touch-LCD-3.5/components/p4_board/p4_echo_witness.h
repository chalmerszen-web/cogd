#pragma once
/* Causal reference model: previous-frame prediction plus the existing dynamic gate. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
enum { WITNESS_TAPS=257, WITNESS_FRAME=512, WITNESS_HISTORY=257,
       WITNESS_RATIO_HISTORY=16 };
typedef struct {
    float h[WITNESS_TAPS],x[WITNESS_HISTORY+WITNESS_FRAME],z[WITNESS_HISTORY+WITNESS_FRAME];
    float recent_power,last_mic;
    float ratio_history[WITNESS_RATIO_HISTORY];
    unsigned ratio_count,ratio_cursor;
    unsigned trained;
} p4_echo_witness_t;
typedef struct {
    float correlation,residual_power,mic_power,reference_power;
    float normalized_residual,ratio_limit;
    unsigned trained,ratio_count;
} p4_echo_witness_result_t;
static inline void p4_echo_witness_ratio_stats(const p4_echo_witness_t *s,
    float *center,float *mad)
{
    float sorted[WITNESS_RATIO_HISTORY];unsigned n=s->ratio_count;
    if(n>WITNESS_RATIO_HISTORY)n=WITNESS_RATIO_HISTORY;
    for(unsigned i=0;i<n;++i)sorted[i]=s->ratio_history[i];
    for(unsigned i=1;i<n;++i){float v=sorted[i];unsigned j=i;
        while(j && sorted[j-1]>v){sorted[j]=sorted[j-1];--j;}sorted[j]=v;}
    if(!n){*center=0;*mad=0;return;}
    *center=(n&1)?sorted[n/2]:(sorted[n/2-1]+sorted[n/2])*.5f;
    for(unsigned i=0;i<n;++i)sorted[i]=fabsf(sorted[i]-*center);
    for(unsigned i=1;i<n;++i){float v=sorted[i];unsigned j=i;
        while(j && sorted[j-1]>v){sorted[j]=sorted[j-1];--j;}sorted[j]=v;}
    *mad=(n&1)?sorted[n/2]:(sorted[n/2-1]+sorted[n/2])*.5f;
}
static inline float p4_echo_witness_ratio_limit(const p4_echo_witness_t *s)
{
    if(s->ratio_count<WITNESS_RATIO_HISTORY/2)return INFINITY;
    float center,mad;p4_echo_witness_ratio_stats(s,&center,&mad);
    /* A robust relative envelope: the median and MAD follow the measured
     * transfer path, so this remains independent of microphone PCM level. */
    return center*8.f+mad*4.f;
}
static inline p4_echo_witness_result_t p4_echo_witness_process(
    p4_echo_witness_t *s,const int16_t mic[512],const int16_t ref[512])
{
    float mx=0,my=0,xx=0,yy=0,xy=0,error=0,refpower=0;bool clipped=false;
    for(unsigned i=0;i<512;++i)s->x[257+i]=(float)ref[i];
    bool reference_present=false;
    for(unsigned i=0;i<769;++i)reference_present|=s->x[i]!=0;
    /* Prediction is frozen for the entire frame. Adaptation below cannot
     * explain away an independent voice in this frame's decision. */
    for(unsigned i=0;i<512;++i){unsigned at=257+i;float p=0,v=(float)mic[i];
        if(reference_present)for(unsigned k=0;k<257;++k)p+=s->h[k]*s->x[at-k];
        mx+=v;my+=p;xx+=v*v;yy+=p*p;xy+=v*p;error+=(v-p)*(v-p);refpower+=(float)ref[i]*ref[i];
        if(mic[i]==INT16_MIN || mic[i]==INT16_MAX)clipped=true;
    }
    float vx=xx-mx*mx/512,vy=yy-my*my/512,cov=xy-mx*my/512;
    float corr=vx>0 && vy>0?cov/sqrtf(vx*vy):0;
    float reference_power=refpower/512.f;
    float residual_power=error/512.f;
    float normalized_residual=reference_power>0?
        residual_power/reference_power:INFINITY;
    p4_echo_witness_result_t r={corr,residual_power,xx/512.f,reference_power,
        normalized_residual,p4_echo_witness_ratio_limit(s),s->trained,s->ratio_count};
    /* Relative reference history stabilizes pauses. No microphone-volume
     * threshold is introduced, and actual PCM is never modified. */
    if(!clipped && refpower>0){
        /* Keep only coherent, robust inliers in the relative residual model.
         * An independent near-end voice is therefore an outlier and cannot
         * gradually raise the veto envelope. */
        if(isfinite(normalized_residual) && corr>=.5f &&
           (s->ratio_count<WITNESS_RATIO_HISTORY/2 ||
            normalized_residual<=r.ratio_limit)) {
            s->ratio_history[s->ratio_cursor%WITNESS_RATIO_HISTORY]=normalized_residual;
            ++s->ratio_cursor;
            if(s->ratio_count<WITNESS_RATIO_HISTORY)++s->ratio_count;
        }
        s->recent_power=fmaxf(s->recent_power*.9685f,refpower/512);
        float regularizer=s->recent_power*257*.01f,cross=0;
        for(unsigned i=0;i<512;++i)cross+=s->x[257+i]*s->x[256+i];
        float alpha=fmaxf(0,fminf(.9f,cross/refpower));
        for(unsigned i=1;i<769;++i)s->z[i]=s->x[i]-alpha*s->x[i-1];
        for(unsigned i=0;i<512;i+=2){unsigned at=257+i;float p=0,power=0;
            for(unsigned k=0;k<257;++k){float x=s->z[at-k];p+=s->h[k]*x;power+=x*x;}
            float den=fmaxf(power,regularizer);
            float target=(float)mic[i]-alpha*(i?(float)mic[i-1]:s->last_mic);
            if(den>0){float step=.5f*(target-p)/den;
                for(unsigned k=0;k<257;++k)s->h[k]+=step*s->z[at-k];}
        }
        if(s->trained<UINT32_MAX)++s->trained;
    }
    s->last_mic=(float)mic[511];memmove(s->x,s->x+512,257*sizeof(float));
    return r;
}
enum {P4_ECHO_WITNESS_ABSOLUTE=1u,P4_ECHO_WITNESS_RELATIVE=2u};
static inline unsigned p4_echo_witness_reasons(p4_echo_witness_result_t r,
    bool duplex,bool calibrated,unsigned scale,uint32_t dynamic_threshold)
{
    bool absolute= r.residual_power*scale*scale<=(float)dynamic_threshold;
    bool relative= r.ratio_count>=WITNESS_RATIO_HISTORY/2 &&
        isfinite(r.normalized_residual) && isfinite(r.ratio_limit) &&
        r.reference_power>0 && r.normalized_residual<=r.ratio_limit;
    if(!duplex || !calibrated || !scale || scale>32 || r.trained<8 ||
       !isfinite(r.correlation) || !isfinite(r.residual_power) || r.residual_power<0 ||
       r.correlation<.8f)return 0;
    return (absolute?P4_ECHO_WITNESS_ABSOLUTE:0u) | (relative?P4_ECHO_WITNESS_RELATIVE:0u);
}
static inline bool p4_echo_witness_veto(p4_echo_witness_result_t r,
    bool duplex,bool calibrated,unsigned scale,uint32_t dynamic_threshold)
{return p4_echo_witness_reasons(r,duplex,calibrated,scale,dynamic_threshold)!=0;}

/* A large DAC signal can make the residual/reference ratio small. Require
 * both residuals to clear the learned floor. If the independent model
 * leaves less unexplained power than linear AEC, it cannot corroborate
 * the linear frame as additional speech evidence. */
static inline bool p4_echo_witness_independent_power(p4_echo_witness_result_t r,
    bool duplex,bool calibrated,unsigned scale,uint32_t dynamic_threshold,uint32_t linear_power)
{
    float residual=r.residual_power*scale*scale;
    return duplex && calibrated && scale>0 && scale<=32 && r.trained>=8 &&
        r.reference_power>0 && isfinite(r.residual_power) &&
        residual>(float)dynamic_threshold && residual>=(float)linear_power;
}

/* A relative-only veto can hide an unvoiced interval between independently
 * voiced frames. This frame may support continuity, never a strong vote.
 * The caller still requires alignment, VAD, adaptive admission and no
 * correlation veto before considering this evidence. */
static inline bool p4_echo_witness_gap(unsigned reasons,bool linear_voice,bool independent_power)
{
    return reasons==P4_ECHO_WITNESS_RELATIVE && linear_voice && independent_power;
}

/* A large DAC reference can hide independent sound in the residual/reference
 * ratio. Compare with the same frame's microphone power too. Require at least
 * 1/32 unexplained power (about 15 dB below the mixture) without a PCM floor.
 * The caller has already established a relative-only, independently powered
 * gap and retains both correlation vetoes. */
static inline bool p4_echo_witness_residual_fraction(p4_echo_witness_result_t r)
{
    return isfinite(r.mic_power) && r.mic_power>0 && isfinite(r.residual_power) &&
        r.residual_power>=r.mic_power/32.f;
}
