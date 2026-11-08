#include "p4_near_correlation.h"
#include "p4_near_alignment.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static uint32_t seed=0x78fe1234;
static int16_t sample(void)
{seed=seed*1664525u+1013904223u;return (int16_t)(seed>>16);}

/* Independent wide-precision decision oracle for the optimized arithmetic. */
static unsigned oracle(const int16_t *m,const int16_t *o,const int16_t *r,const int16_t *p,int stride)
{
    for(int lag=0;lag<=1536;lag+=stride) {
        int64_t xy=0,zy=0,xx=0,zz=0,yy=0;
        for(int i=0;i<512;i+=8) {
            int64_t x=m[i]>>3,z=o[i]>>3,j=i-lag,y=(j<0?p[1536+j]:r[j])>>3;
            xy+=x*y;zy+=z*y;xx+=x*x;zz+=z*z;yy+=y*y;
        }
        unsigned hit=(xx && yy && (double)xy*xy>.50*(double)xx*yy?1u:0u)|
                     (zz && yy && (double)zy*zy>.25*(double)zz*yy?2u:0u);
        if(hit)return hit;
    }
    return 0;
}
static void check(const int16_t *m,const int16_t *o,const int16_t *r,const int16_t *p)
{
    p4_near_correlation_t a=p4_near_correlate(m,o,r,p,true),b=p4_near_correlate(m,o,r,p,false);
    unsigned hit=(a.mic_echo?1u:0u)|(a.output_echo?2u:0u);
    assert(hit==oracle(m,o,r,p,8));
    assert(a.mic_echo==b.mic_echo && a.output_echo==b.output_echo);
    assert(a.mic_permille<=1000 && a.output_permille<=1000);
    p4_near_correlation_t fine=p4_near_correlate_refined(m,o,r,p,true);
    if(hit)assert(fine.mic_echo==a.mic_echo && fine.output_echo==a.output_echo);
    if(fine.mic_echo || fine.output_echo)assert(oracle(m,o,r,p,1));
}
int main(void)
{
    int16_t m[512]={0},o[512]={0},r[512]={0},p[1536]={0};
    check(m,o,r,p);assert(!p4_near_correlate(m,o,r,p,true).mic_echo);
    assert(!p4_near_correlate_refined(m,o,r,p,true).mic_echo);
    for(unsigned t=0;t<300;t++) {
        for(unsigned i=0;i<1536;i++)p[i]=sample();
        for(unsigned i=0;i<512;i++){r[i]=sample();m[i]=sample();o[i]=sample();}
        check(m,o,r,p);
        /* Echo at the oldest supported lag, and output-only residual echo. */
        memset(o,0,sizeof(o));memcpy(m,p,sizeof(m));check(m,o,r,p);
        assert(p4_near_correlate(m,o,r,p,true).mic_echo);
        memset(m,0,sizeof(m));memcpy(o,r,sizeof(o));check(m,o,r,p);
        assert(p4_near_correlate(m,o,r,p,true).output_echo);
    }
    for(unsigned i=0;i<512;i++)m[i]=o[i]=r[i]=INT16_MIN;
    check(m,o,r,p);assert(p4_near_correlate(m,o,r,p,true).mic_echo);
    for(unsigned i=0;i<512;i++)m[i]=o[i]=INT16_MAX;
    check(m,o,r,p);assert(p4_near_correlate(m,o,r,p,true).mic_echo);
    /* A 35-sample acoustic delay falls between the 32/40-sample probes.
     * Use deterministic band-limited noise, not a speech recording. */
    int16_t history[2048];int filtered=0;seed=0x78fe1234;
    for(unsigned i=0;i<2048;i++)history[i]=(int16_t)(filtered=(filtered*4+sample())/5);
    memcpy(p,history,sizeof(p));memcpy(r,history+1536,sizeof(r));
    for(unsigned i=0;i<512;i++)m[i]=history[1536+i-35];
    memset(o,0,sizeof(o));
    assert(!p4_near_correlate(m,o,r,p,true).mic_echo);
    assert(p4_near_correlate_refined(m,o,r,p,true).mic_echo);
    assert(p4_near_correlate_refined(m,o,r,p,false).mic_echo);
    memcpy(o,m,sizeof(o));memset(m,0,sizeof(m));
    assert(p4_near_correlate_refined(m,o,r,p,true).output_echo);
    memset(o,0,sizeof(o));
    /* Independent near speech is not itself evidence of a delayed echo. */
    for(unsigned i=0;i<512;i++)m[i]=sample();
    assert(!p4_near_correlate_refined(m,o,r,p,false).mic_echo);
    /* AEC retains one full block. At a syllable boundary, comparing the
     * current raw channel against the previous residual rejects real speech
     * or admits old echo. Both decisions must use the same physical time. */
    p4_near_alignment_t aligned={0};
    int16_t previous_mic[512],previous_dac[512],current_mic[512],current_dac[512];
    for(unsigned i=0;i<512;i++){
        previous_mic[i]=sample();previous_dac[i]=sample();current_dac[i]=sample();
        current_mic[i]=current_dac[i];o[i]=previous_mic[i];
    }
    assert(!aligned.ready);
    p4_near_alignment_push(&aligned,previous_mic,previous_dac);
    assert(aligned.ready);
    memcpy(p+1024,previous_dac,sizeof(previous_dac));
    assert(p4_near_correlate_refined(current_mic,o,current_dac,p,false).mic_echo);
    p4_near_correlation_t same_time=p4_near_correlate_refined(aligned.mic,o,aligned.reference+1536,aligned.reference,false);
    assert(!same_time.mic_echo && !same_time.output_echo);
    /* Next boundary: previous raw was echo, current raw is unrelated. */
    p4_near_alignment_push(&aligned,current_mic,current_dac);
    for(unsigned i=0;i<512;i++){current_mic[i]=sample();o[i]=sample();r[i]=sample();}
    memmove(p,p+512,1024*sizeof(*p));memcpy(p+1024,current_dac,sizeof(current_dac));
    same_time=p4_near_correlate_refined(current_mic,o,r,p,false);
    assert(!same_time.mic_echo && !same_time.output_echo);
    assert(p4_near_correlate_refined(aligned.mic,o,aligned.reference+1536,aligned.reference,false).mic_echo);
    assert(!memcmp(aligned.reference+1024,previous_dac,sizeof(previous_dac)));
    return 0;
}
