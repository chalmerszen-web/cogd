#include "pool_shared.h"
#include "fused_scale.h"
#include "model.h"
#include "profile.h"
#include <math.h>
#include <limits.h>
#include <string.h>
#include "model_data.h"
#include "binary_scale.h"
#include "hot.h"
#include "binary_guards.h"
#include "conversion.h"
#ifdef TEN_FIXED8
#include "model_fixed8_data.h"
#if !defined(TEN_FIXED_DSP) || (defined(TEN_CACHE_ROWS) && TEN_CACHE_ROWS != 0)
#error "TEN_FIXED8 requires fixed DSP and no row staging"
#endif
#endif

static float relu(float x) { return ten_guard_gt(x,0)?x:0; }
static float activation(float x,bool reference)
{
#ifdef TEN_FLOAT_REFERENCE
    if(reference) return tanhf(x);
#else
    (void)reference;
#endif
    float value=fabsf(x)*64.0f;
    if(value>=512) return x<0?-1:1;
    unsigned at=(unsigned)value;float f=value-(float)at;
    value=tanh_table[at]+f*(tanh_table[at+1]-tanh_table[at]);
    return x<0?-value:value;
}
static float sigmoid(float x,bool reference)
{
#ifdef TEN_FLOAT_REFERENCE
    if(reference) return 1.0f/(1.0f+expf(-x));
#else
    (void)reference;
#endif
    return .5f+.5f*activation(.5f*x,false);
}
static void quantize(ten_nn_t *s,unsigned n)
{
    float maximum=1e-12f;
    for(unsigned i=0;i<n;++i) maximum=ten_guard_peak(maximum,s->input[i]);
    s->scale=maximum/32760.0f;
    for(unsigned i=0;i<n;++i) s->quantized[i]=(int16_t)roundf(s->input[i]/s->scale);
}
static float dot(ten_nn_t *s,const int8_t *w,float scale,float bias,unsigned n)
{
    int32_t a=0,b=0,c=0,d=0;unsigned i=0;
    /* n <= 144 and |q| <= 32760, |w| <= 127: bound 599126400. */
    for(;i+3<n;i+=4) { a+=(int32_t)s->quantized[i]*w[i];b+=(int32_t)s->quantized[i+1]*w[i+1];c+=(int32_t)s->quantized[i+2]*w[i+2];d+=(int32_t)s->quantized[i+3]*w[i+3]; }
    int32_t sum=a+b+c+d;
    for(;i<n;++i) sum+=(int32_t)s->quantized[i]*w[i];
    return (float)sum*(s->scale*scale)+bias;
}
#ifdef TEN_FLOAT_REFERENCE
static float dot_float(const float *x,const float *w,float bias,unsigned n)
{ for(unsigned i=0;i<n;++i) bias+=x[i]*w[i]; return bias; }
#define LINEAR(name,row,n) (reference?dot_float(s->input,name##_float+(row)*(n),name##_bias[row],n):dot(s,name##_q+(row)*(n),name##_scale[row],name##_bias[row],n))
#else
#define LINEAR(name,row,n) dot(s,name##_q+(row)*(n),name##_scale[row],name##_bias[row],n)
#endif
/* Isolated faster variant: Q12 row weights, power-of-two scales and Q15 gates. */
#ifdef TEN_FIXED_DSP
#ifndef TEN_CACHE_ROWS
#define TEN_CACHE_ROWS 0
#endif
static int16_t weight_at(const void *weights,unsigned i)
{
#ifdef TEN_FIXED8
    return ((const int8_t *)weights)[i];
#else
    int16_t value;memcpy(&value,(const uint8_t *)weights+2*i,sizeof(value));return value;
#endif
}
static void TEN_HOT quantize_fixed(ten_nn_t *s,unsigned n)
{
    float maximum=1e-9f;
    for(unsigned i=0;i<n;++i) maximum=ten_guard_peak(maximum,s->input[i]);
    int e;frexpf(maximum,&e);s->exponent=e-11;
    float multiplier=ldexpf(1.0f,-s->exponent);
    for(unsigned i=0;i<n;++i) s->quantized[i]=(int16_t)ten_power_i32(s->input[i],multiplier,-s->exponent,true);
}
static int32_t TEN_HOT fixed_dot(ten_nn_t *s,const void *w,int exponent,int32_t bias,unsigned n)
{
    int32_t sum=0;
    /* |input| <= 2048, |weight| <= 2047, n <= 144: <= 603684864. */
    int32_t a=0,b=0,c=0,d=0;unsigned i=0;
    for(;i+3<n;i+=4) { a+=(int32_t)s->quantized[i]*weight_at(w,i);b+=(int32_t)s->quantized[i+1]*weight_at(w,i+1);c+=(int32_t)s->quantized[i+2]*weight_at(w,i+2);d+=(int32_t)s->quantized[i+3]*weight_at(w,i+3); }
    sum=a+b+c+d;for(;i<n;++i) sum+=(int32_t)s->quantized[i]*weight_at(w,i);
    int shift=s->exponent+exponent+12;int64_t value=sum;
    if(shift>15) return sum<0?INT32_MIN:sum>0?INT32_MAX:bias;
    if(shift>0) value*=INT64_C(1)<<shift;
    else if(shift<=-31) value=0;
    else value=sum<0?-(int32_t)((uint32_t)(-sum)>>(unsigned)(-shift)):
                     (int32_t)((uint32_t)sum>>(unsigned)(-shift));
    value+=bias;
    return value>INT32_MAX?INT32_MAX:value<INT32_MIN?INT32_MIN:(int32_t)value;
}
#ifdef TEN_FIXED8
#define FIXED_ROW(name,row,n) fixed_dot(s,name##_fixed8+(row)*(n),name##_exponent8[row],name##_bias_fixed[row],n)
#else
#define FIXED_ROW(name,row,n) fixed_dot(s,name##_fixed+(row)*(n),name##_exponent[row],name##_bias_fixed[row],n)
#endif
static int32_t fixed_tanh(int32_t q)
{
    int64_t x=q<0?-(int64_t)q:q;
    if(x>=32768) return q<0?-32767:32767;
    unsigned at=(unsigned)x>>6,part=(unsigned)x&63;
    int32_t y=tanh_fixed[at]+((tanh_fixed[at+1]-tanh_fixed[at])*(int32_t)part+32)/64;
    return q<0?-y:y;
}
static int32_t fixed_sigmoid(int32_t q) { return (32768+fixed_tanh(q/2))/2; }
static void recurrent_fixed(ten_nn_t *s,unsigned layer,unsigned n)
{
    quantize_fixed(s,n);
#if TEN_CACHE_ROWS == 4
    /* Gates occupy work[0..255]. The completed CNN leaves 1472 spare bytes;
       stage four rows (<=1152 bytes) there without another allocation. */
    void *cache=s->work+256;
    for(unsigned row=0;row<256;row+=4) {
        const int16_t *weights=layer?lstm1_fixed:lstm0_fixed;
        const int8_t *exponents=layer?lstm1_exponent:lstm0_exponent;
        const int32_t *biases=layer?lstm1_bias_fixed:lstm0_bias_fixed;
        memcpy(cache,weights+row*n,4*n*sizeof(*weights));
        for(unsigned j=0;j<4;++j) {
            int32_t q=fixed_dot(s,(const uint8_t *)cache+j*n*sizeof(*weights),exponents[row+j],biases[row+j],n);
            memcpy(s->work+row+j,&q,sizeof(q));
        }
    }
#else
    for(unsigned row=0;row<256;++row) {
        int32_t q=layer?FIXED_ROW(lstm1,row,n):FIXED_ROW(lstm0,row,n);
        memcpy(s->work+row,&q,sizeof(q));
    }
#endif
    for(unsigned i=0;i<64;++i) {
        int32_t gate[4];for(unsigned g=0;g<4;++g) memcpy(gate+g,s->work+i+64*g,sizeof(*gate));
        int64_t cell=ten_convert_cell(s->cell[layer][i]);
        cell=((int64_t)fixed_sigmoid(gate[2])*cell+2*(int64_t)fixed_sigmoid(gate[0])*fixed_tanh(gate[3]))/32768;
        s->cell[layer][i]=(ten_combo_mode ? ten_fused_scale(cell,-16) : ten_power_float(ten_convert_float(cell),1.0f/65536,-16));
        int32_t hidden=(int32_t)((int64_t)fixed_sigmoid(gate[1])*fixed_tanh((int32_t)(cell/16))/32768);
        s->hidden[layer][i]=(ten_combo_mode ? ten_fused_scale(hidden,-15) : ten_power_float((float)hidden,1.0f/32768,-15));
    }
}
#endif
static void recurrent(ten_nn_t *s,unsigned layer,unsigned inputs,bool reference)
{
#ifndef TEN_FLOAT_REFERENCE
    (void)reference;
#endif
    memcpy(s->input+inputs,s->hidden[layer],64*sizeof(float));
    unsigned n=inputs+64;
#ifdef TEN_FIXED_DSP
    if(!reference) { recurrent_fixed(s,layer,n);return; }
#endif
    quantize(s,n);
    /* Visit weight rows in Flash order; CNN scratch is now dead and reusable. */
    for(unsigned row=0;row<256;++row) s->work[row]=layer?LINEAR(lstm1,row,n):LINEAR(lstm0,row,n);
    for(unsigned i=0;i<64;++i) {
        s->cell[layer][i]=sigmoid(s->work[i+128],reference)*s->cell[layer][i]+sigmoid(s->work[i],reference)*activation(s->work[i+192],reference);
        s->hidden[layer][i]=sigmoid(s->work[i+64],reference)*activation(s->cell[layer][i],reference);
    }
}
bool TEN_HOT ten_nn_step(ten_nn_t *s,const float features[123],bool reference,float *probability)
{
#ifndef TEN_FLOAT_REFERENCE
    reference=false;
#endif
    if(!s || !features || !probability) return false;
    for(unsigned i=0;i<123;++i) if(!ten_guard_abs_le(features[i],64)) return false;
    ten_profile_begin(TEN_CONV);
    /* 3x3 valid convolution across three 41-bin feature frames. */
    for(unsigned x=0;x<39;++x) {
        float v=0;
        for(unsigned t=0;t<3;++t) for(unsigned k=0;k<3;++k) v+=features[t*41+x+k]*dw0[t*3+k];
        s->temp[x]=v;
    }
    if(ten_combo_mode) shared_pool(s,pw0,bias0);
    else {
    for(unsigned c=0;c<16;++c) for(unsigned x=0;x<39;++x) s->work[c*39+x]=relu(s->temp[x]*pw0[c]+bias0[c]);
    /* In-place 1x3 max pool, stride two; outputs never overwrite a future input. */
    for(unsigned c=0;c<16;++c) for(unsigned x=0;x<19;++x) {
        float v=s->work[c*39+2*x];
        for(unsigned k=1;k<3;++k) if(ten_guard_gt(s->work[c*39+2*x+k],v)) v=s->work[c*39+2*x+k];
        s->work[c*19+x]=v;
    }
    }
    for(unsigned layer=0;layer<2;++layer) {
        unsigned width=layer?10:19,out=layer?5:10;int left=layer?0:1;
        const float *dw=layer?dw2:dw1,*pw=layer?pw2:pw1,*bias=layer?bias2:bias1;
        for(unsigned c=0;c<16;++c) for(unsigned x=0;x<out;++x) {
            float v=0;
            for(unsigned k=0;k<3;++k) { int at=(int)(2*x+k)-left;if(at>=0 && at<(int)width) v+=s->work[c*width+(unsigned)at]*dw[c*3+k]; }
            s->temp[c*out+x]=v;
        }
#ifdef TEN_FIXED_DSP
        if(!reference) {
            for(unsigned x=0;x<out;++x) {
                for(unsigned k=0;k<16;++k) s->input[k]=s->temp[k*out+x];
                quantize_fixed(s,16);
                for(unsigned c=0;c<16;++c) {
                    int32_t q=layer?FIXED_ROW(pw2,c,16):FIXED_ROW(pw1,c,16);
                    s->work[c*out+x]=q>0?ten_power_float((float)q,1.0f/4096,-12):0;
                }
            }
            continue;
        }
#endif
        for(unsigned c=0;c<16;++c) for(unsigned x=0;x<out;++x) {
            float v=bias[c];for(unsigned k=0;k<16;++k) v+=s->temp[k*out+x]*pw[c*16+k];
            s->work[c*out+x]=relu(v);
        }
    }
    for(unsigned x=0;x<5;++x) for(unsigned c=0;c<16;++c) s->input[x*16+c]=s->work[c*5+x];
    ten_profile_end(TEN_CONV);
    ten_profile_begin(TEN_RECURRENT);
    recurrent(s,0,80,reference);
    memcpy(s->input,s->hidden[0],64*sizeof(float));recurrent(s,1,64,reference);
    ten_profile_end(TEN_RECURRENT);
    ten_profile_begin(TEN_DENSE);
    memcpy(s->input,s->hidden[1],64*sizeof(float));memcpy(s->input+64,s->hidden[0],64*sizeof(float));
#ifdef TEN_FIXED_DSP
    if(!reference) {
        quantize_fixed(s,128);
        for(unsigned i=0;i<32;++i) { int32_t q=FIXED_ROW(dense0,i,128);s->temp[i]=q>0?ten_power_float((float)q,1.0f/4096,-12):0; }
        memcpy(s->input,s->temp,32*sizeof(float));quantize_fixed(s,32);
        *probability=(ten_combo_mode ? ten_fused_scale(fixed_sigmoid(FIXED_ROW(dense1,0,32)),-15) : ten_power_float((float)fixed_sigmoid(FIXED_ROW(dense1,0,32)),1.0f/32768,-15));
        ten_profile_end(TEN_DENSE);return true;
    }
#endif
    quantize(s,128);
    for(unsigned i=0;i<32;++i) s->temp[i]=relu(LINEAR(dense0,i,128));
    memcpy(s->input,s->temp,32*sizeof(float));quantize(s,32);
    *probability=sigmoid(LINEAR(dense1,0,32),reference);
    ten_profile_end(TEN_DENSE);
    return isfinite(*probability);
}
