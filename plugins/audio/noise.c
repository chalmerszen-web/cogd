#include "noise.h"
#include <limits.h>
#include <string.h>

/* Fixed-point radix-2 FFT and overlap-add spectral subtraction.
 * Q8 samples preserve quiet speech; Q15 twiddles/window, normalized transforms.
 * No heap, floating point, learned model or microphone upload. */
static const int16_t cosine[64]={32767,32728,32609,32412,32137,31785,31356,30852,30273,29621,28898,28105,27245,26319,25329,24279,23170,22005,20787,19519,18204,16846,15446,14010,12539,11039,9512,7962,6393,4808,3212,1608,0,-1608,-3212,-4808,-6393,-7962,-9512,-11039,-12539,-14010,-15446,-16846,-18204,-19519,-20787,-22005,-23170,-24279,-25329,-26319,-27245,-28105,-28898,-29621,-30273,-30852,-31356,-31785,-32137,-32412,-32609,-32728};
static const int16_t sine[64]={0,1608,3212,4808,6393,7962,9512,11039,12539,14010,15446,16846,18204,19519,20787,22005,23170,24279,25329,26319,27245,28105,28898,29621,30273,30852,31356,31785,32137,32412,32609,32728,32767,32728,32609,32412,32137,31785,31356,30852,30273,29621,28898,28105,27245,26319,25329,24279,23170,22005,20787,19519,18204,16846,15446,14010,12539,11039,9512,7962,6393,4808,3212,1608};
static const int16_t window[64]={402,1206,2009,2811,3612,4410,5205,5998,6786,7571,8351,9126,9896,10659,11417,12167,12910,13645,14372,15090,15800,16499,17189,17869,18537,19195,19841,20475,21096,21705,22301,22884,23452,24007,24547,25072,25582,26077,26556,27019,27466,27896,28310,28706,29085,29447,29791,30117,30424,30714,30985,31237,31470,31685,31880,32057,32213,32351,32469,32567,32646,32705,32745,32765};
static int16_t clamp(int32_t x) { return (int16_t)(x>32767?32767:x< -32768?-32768:x); }
static int16_t weight(unsigned i) { return window[i<64?i:127-i]; }
static void transform(agent_noise_t *n,int direction)
{
    for(unsigned i=1,j=0;i<128;++i) {
        unsigned bit=64;
        while(j&bit) { j^=bit; bit>>=1; }
        j^=bit;
        if(i<j) {
            int32_t v=n->real[i]; n->real[i]=n->real[j]; n->real[j]=v;
            v=n->imag[i]; n->imag[i]=n->imag[j]; n->imag[j]=v;
        }
    }
    for(unsigned width=2;width<=128;width*=2)
        for(unsigned base=0;base<128;base+=width)
            for(unsigned j=0;j<width/2;++j) {
                unsigned a=base+j,b=a+width/2,k=j*128/width;
                int32_t wr=cosine[k],wi=direction*sine[k];
                int32_t vr=(int32_t)(((int64_t)n->real[b]*wr-(int64_t)n->imag[b]*wi)/32768);
                int32_t vi=(int32_t)(((int64_t)n->real[b]*wi+(int64_t)n->imag[b]*wr)/32768);
                int32_t ur=n->real[a],ui=n->imag[a];
                n->real[a]=(ur+vr)/2; n->imag[a]=(ui+vi)/2;
                n->real[b]=(ur-vr)/2; n->imag[b]=(ui-vi)/2;
            }
}
static uint32_t power(const agent_noise_t *n,unsigned k)
{
    int64_t r=n->real[k]/256,i=n->imag[k]/256;
    return (uint32_t)(r*r+i*i);
}
void agent_noise_init(agent_noise_t *n)
{
    memset(n,0,sizeof(*n)); n->best_energy=UINT64_MAX;
}
static void choose_profile(agent_noise_t *n)
{
    if(!n->profile_frames) return;
    uint64_t total=0;
    for(unsigned k=0;k<=64;++k) {
        n->candidate[k]=(uint32_t)((uint64_t)n->candidate[k]*16/n->profile_frames);
        total+=n->candidate[k];
    }
    if(total<n->best_energy) { memcpy(n->noise,n->candidate,sizeof(n->noise)); n->best_energy=total; }
    memset(n->candidate,0,sizeof(n->candidate)); n->profile_frames=0;
}
void agent_noise_profile(agent_noise_t *n,const int16_t samples[128])
{
    for(unsigned i=0;i<128;++i) { n->real[i]=(int32_t)samples[i]*weight(i)/128; n->imag[i]=0; }
    transform(n,-1);
    for(unsigned k=0;k<=64;++k) n->candidate[k]+=power(n,k)/16;
    if(++n->profile_frames==16) choose_profile(n);
}
void agent_noise_ready(agent_noise_t *n)
{
    if(n->best_energy==UINT64_MAX) choose_profile(n); /* A short tail is not a reliable quiet window. */
    /* Smooth the estimate across neighboring bins, reducing musical-noise artifacts. */
    for(unsigned k=0;k<=64;++k)
        n->candidate[k]=(uint32_t)(((uint64_t)n->noise[k]*2+n->noise[k?k-1:k]+n->noise[k<64?k+1:k])/4);
    memcpy(n->noise,n->candidate,sizeof(n->noise));
    for(unsigned k=0;k<=64;++k) n->gain[k]=2048;
    memset(n->history,0,sizeof(n->history)); memset(n->overlap,0,sizeof(n->overlap));
}
void agent_noise_process(agent_noise_t *n,const int16_t input[64],int16_t output[64])
{
    for(unsigned i=0;i<128;++i) {
        int16_t x=i<64?n->history[i]:input[i-64];
        n->real[i]=(int32_t)x*weight(i)/128; n->imag[i]=0;
    }
    memcpy(n->history,input,sizeof(n->history)); transform(n,-1);
    for(unsigned k=0;k<=64;++k) {
        uint32_t p=power(n,k); uint64_t floor=(uint64_t)n->noise[k]*4;
        unsigned target=p>floor?(unsigned)(((uint64_t)p-floor)*32768/p):2048;
        if(target<2048) target=2048;
        unsigned old=n->gain[k];
        n->gain[k]=(uint16_t)(target>old?(3*target+old)/4:(target+3*old)/4);
        unsigned mirror=k?128-k:0;
        n->real[k]=(int32_t)((int64_t)n->real[k]*n->gain[k]/32768);
        n->imag[k]=(int32_t)((int64_t)n->imag[k]*n->gain[k]/32768);
        if(mirror<128 && mirror!=k) { n->real[mirror]=n->real[k]; n->imag[mirror]=-n->imag[k]; }
    }
    transform(n,1);
    for(unsigned i=0;i<64;++i) {
        int32_t a=(int32_t)((int64_t)n->real[i]*weight(i)/65536);
        int32_t b=(int32_t)((int64_t)n->real[i+64]*weight(i+64)/65536);
        output[i]=clamp(a+n->overlap[i]); n->overlap[i]=clamp(b);
    }
}
