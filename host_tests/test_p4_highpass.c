#include "p4_highpass.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static double sine_gain(double frequency)
{
    p4_highpass_t state={0};double in=0,out=0;
    for(int i=0;i<32000;i++) {
        int16_t x=(int16_t)(4000*sin(6.283185307179586*frequency*i/16000));
        int16_t y=p4_highpass_sample(&state,x);
        if(i>=16000){in+=(double)x*x;out+=(double)y*y;}
    }
    assert(isfinite(state.first) && isfinite(state.second));
    return sqrt(out/in);
}
int main(void)
{
    p4_highpass_t state={0};
    for(int i=0;i<16000;i++) {
        int16_t y=p4_highpass_sample(&state,10000);
        if(i>8000)assert(y>=-1 && y<=1);
    }
    assert(sine_gain(100)<.30);
    double corner=sine_gain(180);assert(corner>.70 && corner<.72);
    assert(sine_gain(1000)>.995);
    memset(&state,0,sizeof(state));
    for(int i=0;i<100000;i++) {
        (void)p4_highpass_sample(&state,(i&1)?INT16_MAX:INT16_MIN);
        assert(isfinite(state.first) && isfinite(state.second));
    }
    for(int i=0;i<16000;i++) {
        int16_t y=p4_highpass_sample(&state,0);
        if(i>8000)assert(y==0);
    }
    return 0;
}
