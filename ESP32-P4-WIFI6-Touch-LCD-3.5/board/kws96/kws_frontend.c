#include "kws_internal.h"
#include "kws_pcen.h"
#include <string.h>

static uint16_t log2_q8(uint32_t x)
{
    if(!x) return 0;
    unsigned exponent=0;
    for(uint32_t n=x;n>1;n>>=1) ++exponent;
    uint32_t normalized=exponent>=8?x>>(exponent-8):x<<(8-exponent);
    return (uint16_t)(exponent*256+kws_log_fraction[normalized-256]);
}
static void frontend_power(const kws_frontend_buffers_t *k,const int16_t pcm[256],uint32_t power[40])
{
    for(unsigned i=0;i<512;++i) {
        int16_t sample=i<256?k->previous[i]:pcm[i-256];
        k->fft_in[i]=(kiss_fft_cpx){(int16_t)kws_requantize((int32_t)sample*kws_hann[i],15,-32768,32767),0};
    }
    memcpy(k->previous,pcm,512);
    kws_fft512(k->fft_in,k->fft_out);
    /* fft_in is dead; power now owns the shared scratch. */
    for(unsigned i=0;i<257;++i) {
        int32_t r=k->fft_out[i].r,im=k->fft_out[i].i;
        k->power[i]=(uint32_t)((int64_t)r*r+(int64_t)im*im);
    }
    for(unsigned band=0;band<40;++band) {
        const kws_mel_band_t *m=&kws_mel[band];
        uint64_t energy=0;
        for(unsigned j=0;j<m->count;++j)
            energy+=(uint64_t)k->power[m->first+j]*kws_mel_weights[m->offset+j];
        /* Q15 Mel weights, nonnegative round-to-nearest. */
        uint64_t scaled=(energy+16384)>>15;
        if(scaled>UINT32_MAX) scaled=UINT32_MAX;
        power[band]=(uint32_t)scaled;
    }
}
static kws_frontend_buffers_t buffers(kws_handle_t *k)
{ return (kws_frontend_buffers_t){k->previous,k->fft_in,k->fft_out,k->power}; }

void kws_frontend_power(kws_handle_t *k,const int16_t pcm[256],uint32_t power[40])
{
    const kws_frontend_buffers_t view=buffers(k);
    frontend_power(&view,pcm,power);
}
static int8_t normalize(const kws_model_t *model,int16_t logmel,unsigned band)
{
    int64_t value=(int64_t)(logmel-model->mean_q8[band])*model->inverse_std_q12[band];
    /* Beyond int32 the final INT8 result is necessarily saturated. */
    if(value>INT32_MAX) value=INT32_MAX;
    if(value<INT32_MIN) value=INT32_MIN;
    return (int8_t)kws_requantize((int32_t)value,15,-128,127);
}
void kws_frontend_buffers_step(const kws_frontend_buffers_t *k,const kws_model_t *model,
    const int16_t pcm[256],int8_t features[40],kws_trace_t *trace)
{
    uint32_t power[40];
    frontend_power(k,pcm,power);
    for(unsigned band=0;band<40;++band) {
        int16_t value=(int16_t)log2_q8(power[band]);
        features[band]=normalize(model,value,band);
        if(trace) { trace->logmel[band]=value; trace->input[band]=features[band]; }
    }
}
void kws_frontend(kws_handle_t *k,const int16_t pcm[256],kws_trace_t *trace)
{
    const kws_frontend_buffers_t view=buffers(k);
    kws_frontend_buffers_step(&view,k->model,pcm,trace->input,trace);
}
void kws_pcen_frontend(kws_handle_t *k,kws_pcen_t *state,const int16_t pcm[256],kws_trace_t *trace)
{
    uint32_t power[40];
    kws_frontend_power(k,pcm,power);
    kws_pcen_step(state,power,trace->logmel);
    for(unsigned band=0;band<40;++band)
        trace->input[band]=normalize(k->model,trace->logmel[band],band);
}
