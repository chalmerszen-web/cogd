#include "fused_scale.h"
#include "fixed.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "fixed_tables.h"
#include "binary_scale.h"
#include "hot.h"
#include "binary_guards.h"
#include "conversion.h"

#ifdef TEN_NARROW_DSP
typedef int32_t ten_work_t;
#else
typedef int64_t ten_work_t;
#endif

/* memcpy permits reusing dead float storage without type-punning or alias UB. */
static int32_t read_q(const float *p,unsigned i)
{ int32_t q;memcpy(&q,p+i,sizeof(q));return q; }
static void write_q(float *p,unsigned i,int32_t q)
{ memcpy(p+i,&q,sizeof(q)); }
static unsigned reverse9(unsigned x)
{ unsigned r=0;for(unsigned n=0;n<9;++n) { r=(r<<1)|(x&1);x>>=1; }return r; }

void TEN_HOT ten_fft1024(float input[1024],float output[1024])
{
    float maximum=0;
    for(unsigned i=0;i<1024;++i) {
        assert(ten_guard_abs_le(input[i],65536));
        maximum=ten_guard_peak(maximum,input[i]);
    }
    if(maximum==0) { memset(output,0,1024*sizeof(float));return; }
    int exponent;frexpf(maximum,&exponent);
    if(exponent < -71) exponent=-71; /* Keep the scaling factor finite. */
    float scale=ldexpf(1.0f,29-exponent),inverse=ldexpf(1.0f,exponent-29);
    /* Each component <= 2^29; complex magnitude < 2^30. Each FFT stage /2
       prevents growth. Q30 products and sums remain strictly within int64. */
    for(unsigned i=0;i<512;++i) {
        unsigned j=2*reverse9(i);
        write_q(output,j,ten_power_i32(input[2*i],scale,29-exponent,false));
        write_q(output,j+1,ten_power_i32(input[2*i+1],scale,29-exponent,false));
    }
    assert(ten_twiddle[0][0]==1073741824 && ten_twiddle[0][1]==0);
    assert(ten_twiddle[256][0]==0 && ten_twiddle[256][1]==-1073741824);
    for(unsigned width=2;width<=512;width*=2) {
        unsigned half=width/2,step=1024/width;
        for(unsigned j=0;j<half;++j) {
            int32_t wr=ten_twiddle[j*step][0],wi=ten_twiddle[j*step][1];
            for(unsigned i=j;i<512;i+=width) {
                unsigned a=2*i,b=2*(i+half);
                int32_t ar=read_q(output,a),ai=read_q(output,a+1);
                int32_t br=read_q(output,b),bi=read_q(output,b+1);
                ten_work_t tr,ti;
                if(ten_combo_mode && j==0) { tr=br;ti=bi; }
                else if(ten_combo_mode && j*4==width) { tr=bi;ti=-br; }
                else {
                    tr=(ten_work_t)(((int64_t)br*wr-(int64_t)bi*wi)/1073741824);
                    ti=(ten_work_t)(((int64_t)br*wi+(int64_t)bi*wr)/1073741824);
                }
                /* Each rotated vector stays below 2^30 in norm. */
                write_q(output,a,(int32_t)(((ten_work_t)ar+tr)/2));
                write_q(output,a+1,(int32_t)(((ten_work_t)ai+ti)/2));
                write_q(output,b,(int32_t)(((ten_work_t)ar-tr)/2));
                write_q(output,b+1,(int32_t)(((ten_work_t)ai-ti)/2));
            }
        }
    }
    int32_t zr=read_q(output,0),zi=read_q(output,1);
    input[0]=(ten_combo_mode ? ten_fused_scale(((int64_t)zr+zi)/2,exponent-29) : ten_power_float(ten_convert_float(((int64_t)zr+zi)/2),inverse,exponent-29));
    input[1023]=(ten_combo_mode ? ten_fused_scale(((int64_t)zr-zi)/2,exponent-29) : ten_power_float(ten_convert_float(((int64_t)zr-zi)/2),inverse,exponent-29));
    for(unsigned k=1;k<512;++k) {
        ten_work_t ar=read_q(output,2*k),ai=read_q(output,2*k+1);
        ten_work_t br=read_q(output,2*(512-k)),bi=-read_q(output,2*(512-k)+1);
        ten_work_t dr=ar-br,di=ai-bi;
        int32_t wr=ten_twiddle[k][0],wi=ten_twiddle[k][1];
        ten_work_t tr=(ten_work_t)(((int64_t)dr*wr-(int64_t)di*wi)/1073741824);
        ten_work_t ti=(ten_work_t)(((int64_t)dr*wi+(int64_t)di*wr)/1073741824);
        /* Unpacking sums can exceed signed32 even though rotations fit. */
        input[2*k-1]=(ten_combo_mode ? ten_fused_scale(((int64_t)ar+br+ti)/4,exponent-29) : ten_power_float(ten_convert_float(((int64_t)ar+br+ti)/4),inverse,exponent-29));
        input[2*k]=(ten_combo_mode ? ten_fused_scale(((int64_t)ai+bi-tr)/4,exponent-29) : ten_power_float(ten_convert_float(((int64_t)ai+bi-tr)/4),inverse,exponent-29));
    }
    memcpy(output,input,1024*sizeof(float));
}

void ten_xcorr32x64(const float reference[32],const float shifted[96],float output[64])
{
    assert(reference==shifted+64);
    float maximum=1e-12f;
    for(unsigned i=0;i<96;++i) {
        assert(ten_guard_abs_le(shifted[i],1e9f));
        maximum=ten_guard_peak(maximum,shifted[i]);
    }
    int exponent;frexpf(maximum,&exponent);
    float scale=ldexpf(1.0f,23-exponent),inverse=ldexpf(1.0f,2*(exponent-23));
    int32_t q[96];
    for(unsigned i=0;i<96;++i) q[i]=ten_power_i32(shifted[i],scale,23-exponent,false);
    /* 32 products of components < 2^23 fit within 2^51. */
    for(unsigned i=0;i<64;++i) {
        int64_t sum=0;
        for(unsigned j=0;j<32;++j) sum+=(int64_t)q[64+j]*q[i+j];
        output[i]=(ten_combo_mode ? ten_fused_scale(sum,2*(exponent-23)) : ten_power_float((float)sum,inverse,2*(exponent-23)));
    }
}

static void ten_lpc_original(const float coefficients[16],const float *input,unsigned count,
               float memory[16],float *previous,float *output)
{
    int32_t coef[16],past[16];
    for(unsigned i=0;i<16;++i) {
        assert(ten_guard_abs_lt(coefficients[i],64));
        coef[i]=ten_power_i32(coefficients[i],16777216.0f,24,true);
        past[i]=(int32_t)memory[i];
    }
    for(unsigned i=0;i<count;++i) {
        assert(ten_guard_pcm16(input[i]));
        int32_t x=(int32_t)input[i];int64_t sum=(int64_t)x*16777216;
        for(unsigned j=0;j<16;++j) sum+=(int64_t)coef[j]*past[j];
        memmove(past+1,past,15*sizeof(*past));past[0]=x;
        float filtered=(ten_combo_mode ? ten_fused_scale(sum,-24) : ten_power_float((float)sum,1.0f/16777216,-24));
        output[i]=filtered+.7f*(*previous);*previous=filtered;
    }
    for(unsigned i=0;i<16;++i) memory[i]=(float)past[i];
}
static void ten_lpc_ring(const float coefficients[16],const float *input,unsigned count,
               float memory[16],float *previous,float *output)
{
    int32_t coef[16],past[32];unsigned at=0;
    for(unsigned i=0;i<16;++i) {
        assert(ten_guard_abs_lt(coefficients[i],64));
        coef[i]=ten_power_i32(coefficients[i],16777216.0f,24,true);
        past[i]=past[i+16]=(int32_t)memory[i];
    }
    for(unsigned i=0;i<count;++i) {
        assert(ten_guard_pcm16(input[i]));
        int32_t x=(int32_t)input[i];int64_t sum=(int64_t)x*16777216;
        for(unsigned j=0;j<16;++j) sum+=(int64_t)coef[j]*past[at+j];
        /* Mirrored history keeps the original16 lag values contiguous and in
         * exactly the same dot-product order, with two writes per sample. */
        at=(at+15u)&15u;past[at]=past[at+16]=x;
        float filtered=(ten_combo_mode ? ten_fused_scale(sum,-24) : ten_power_float((float)sum,1.0f/16777216,-24));
        output[i]=filtered+.7f*(*previous);*previous=filtered;
    }
    for(unsigned i=0;i<16;++i) memory[i]=(float)past[at+i];
}

void ten_lpc16(const float coefficients[16],const float *input,unsigned count,
               float memory[16],float *previous,float *output)
{
    if(ten_combo_mode) ten_lpc_ring(coefficients,input,count,memory,previous,output);
    else ten_lpc_original(coefficients,input,count,memory,previous,output);
}

bool ten_biquad(float *data,unsigned count,const float a[3],const float b[3],
                float gain,float state[2])
{
    float maximum=1e-12f;
    for(unsigned i=0;i<count;++i) { if(!ten_guard_finite(data[i])) return false;maximum=ten_guard_peak(maximum,data[i]); }
    for(unsigned i=0;i<2;++i) { if(!ten_guard_finite(state[i])) return false;maximum=ten_guard_peak(maximum,state[i]); }
    if(!ten_guard_finite(maximum) || !ten_guard_abs_le(gain,1)) return false;
    int exponent;frexpf(maximum,&exponent);
    float scale=ldexpf(1.0f,20-exponent),inverse=ldexpf(1.0f,exponent-20);
    int32_t qa[3],qb[3],qg=ten_power_i32(gain,536870912.0f,29,true);
    for(unsigned i=0;i<3;++i) {
        if(!ten_guard_abs_lt(a[i],2) || !ten_guard_abs_lt(b[i],2)) return false;
        qa[i]=ten_power_i32(a[i],536870912.0f,29,true);
        qb[i]=ten_power_i32(b[i],536870912.0f,29,true);
    }
    ten_work_t p0=ten_power_i32(state[0],scale,20-exponent,false);
    ten_work_t p1=ten_power_i32(state[1],scale,20-exponent,false);
    for(unsigned i=0;i<count;++i) {
        int32_t x=ten_power_i32(data[i],scale,20-exponent,false);
        int64_t wide_w=((int64_t)x*536870912-(int64_t)qa[1]*p0-(int64_t)qa[2]*p1)/536870912;
        if(wide_w < -536870912 || wide_w > 536870912) return false;
        ten_work_t w=(ten_work_t)wide_w; /* Guard before narrowing. */
        int64_t y=((int64_t)qb[0]*w+(int64_t)qb[1]*p0+(int64_t)qb[2]*p1)/536870912;
        y=(y*qg)/536870912;
        float result=(ten_combo_mode ? ten_fused_scale(y,exponent-20) : ten_power_float(ten_convert_float(y),inverse,exponent-20));if(!ten_guard_finite(result)) return false;
        p1=p0;p0=w;data[i]=result;
    }
    state[0]=(ten_combo_mode ? ten_fused_scale(p0,exponent-20) : ten_power_float((float)p0,inverse,exponent-20));
    state[1]=(ten_combo_mode ? ten_fused_scale(p1,exponent-20) : ten_power_float((float)p1,inverse,exponent-20));
    return true;
}
