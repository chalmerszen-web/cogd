#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool mic_echo,output_echo;
    unsigned mic_permille,output_permille;
} p4_near_correlation_t;

/* The P4 has a single-precision FPU. Keep software double arithmetic out of
 * the 32 ms capture deadline. Each of the 64 products is bounded by 4096^2,
 * so the signed accumulators stay within +/-2^30 even for full-scale PCM.
 * Energy products and squared cross-products are evaluated as float. */
static inline p4_near_correlation_t p4_near_correlate_impl(
    const int16_t mic[512],const int16_t output[512],
    const int16_t reference[512],const int16_t previous[1536],bool diagnostic,bool refine)
{
    p4_near_correlation_t result={0};
    float peaks[2][3]={{0}};int peak_lag[2][3]={{-1,-1,-1},{-1,-1,-1}};
    int16_t x[64],z[64];int32_t xx=0,zz=0;
    for(int k=0;k<64;k++) {
        x[k]=mic[k*8]>>3;z[k]=output[k*8]>>3;
        xx+=(int32_t)x[k]*x[k];zz+=(int32_t)z[k]*z[k];
    }
    /* First retain the existing 193 coarse lags. Native duplex additionally
     * checks at most 84 neighbours of its strongest raw/residual peaks.
     * A real acoustic delay is not constrained to multiples of 8 samples. */
    /* The recorded /shi-er/ echo had genuine peaks at 28 and 42 samples,
     * missed by three coarse maxima in other regions. Cover the direct
     * acoustic path densely without changing either correlation threshold. */
    for(int probe=0;probe<(refine?373:193);probe++) {
        int lag;
        if(probe<193)lag=probe*8;
        else if(probe>=277){lag=probe-276;if(lag%8==0)continue;}
        else {
            int fine=probe-193,base=peak_lag[fine/42][(fine%42)/14];
            int delta=fine%14-7;if(delta>=0)++delta;
            lag=base+delta;
            if(base<0 || lag<0 || lag>1536 || lag%8==0)continue;
        }
        int32_t xy=0,zy=0,yy=0;
        for(int k=0;k<64;k++) {
            int j=k*8-lag,y=(j<0?previous[1536+j]:reference[j])>>3;
            xy+=(int32_t)x[k]*y;zy+=(int32_t)z[k]*y;yy+=(int32_t)y*y;
        }
        float m=(float)xy*(float)xy,o=(float)zy*(float)zy;
        float md=(float)xx*(float)yy,od=(float)zz*(float)yy;
        result.mic_echo=xx && yy && m>.50f*md;
        result.output_echo=zz && yy && o>.25f*od;
        if(refine && probe<193 && !result.mic_echo && !result.output_echo) {
            float score[2]={md>0?m/md:0,od>0?o/od:0};
            for(int channel=0;channel<2;channel++)for(int k=0;k<3;k++) {
                if(score[channel]<=peaks[channel][k])continue;
                for(int j=2;j>k;j--){peaks[channel][j]=peaks[channel][j-1];peak_lag[channel][j]=peak_lag[channel][j-1];}
                peaks[channel][k]=score[channel];peak_lag[channel][k]=lag;break;
            }
        }
        if(diagnostic) {
            unsigned mc=md>0?(unsigned)(m*1000.f/md):0;
            unsigned oc=od>0?(unsigned)(o*1000.f/od):0;
            if(mc>result.mic_permille)result.mic_permille=mc;
            if(oc>result.output_permille)result.output_permille=oc;
        }
        if(result.mic_echo || result.output_echo)break;
    }
    return result;
}

/* WSS retains its original coarse decision and arithmetic. */
static inline p4_near_correlation_t p4_near_correlate(
    const int16_t mic[512],const int16_t output[512],
    const int16_t reference[512],const int16_t previous[1536],bool diagnostic)
{return p4_near_correlate_impl(mic,output,reference,previous,diagnostic,false);}

static inline p4_near_correlation_t p4_near_correlate_refined(
    const int16_t mic[512],const int16_t output[512],
    const int16_t reference[512],const int16_t previous[1536],bool diagnostic)
{return p4_near_correlate_impl(mic,output,reference,previous,diagnostic,true);}
