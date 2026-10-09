#include "factor_q30.h"
#include "tonal.h"
#include "coefficients.h"
static int16_t notch(agent_biquad_t *s,const int32_t *c,int16_t pcm)
{
    int32_t value=(int32_t)pcm*256;
    int32_t next=factor_q30(s,c,value);
    int32_t out=(int32_t)next/256;
    return (int16_t)(out>32767?32767:out< -32768?-32768:out);
}
int16_t tonal_filter_sample(tonal_filter_t *s,int16_t value)
{
    for(unsigned i=0;i<2;++i)value=notch(&s->tones[i],tone_coefficients[i],value);
    return value;
}
int16_t tonal_highpass_sample(agent_biquad_t *s,int16_t value)
{
    /* RBJ high-pass, 16kHz, Q=sqrt(1/2), normalized and rounded to Q30. */
    static const int32_t c[5]={987913515,-1975827030,987913515,-1968955450,908956787};
    return notch(s,c,value);
}
