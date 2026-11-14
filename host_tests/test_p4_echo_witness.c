#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "p4_echo_witness.h"
static uint32_t rng=987654321;
static int noise(void){rng=rng*1664525u+1013904223u;return (int)(rng>>16)-32768;}
static void controls(void){
    unsigned counts[3][2]={{0}};
    for(unsigned gain=0;gain<3;++gain){
        float g=gain==0?.25f:gain==1?1.f:2.f;float history[64]={0};
        p4_echo_witness_t *s=calloc(1,sizeof(*s));assert(s);rng=987654321;
        for(unsigned f=0;f<128;++f){int16_t m[512],r[512];
            for(unsigned i=0;i<512;++i){float x=(float)noise()/16;
                memmove(history+1,history,63*sizeof(float));history[0]=x;
                float echo=.6f*history[28]-.2f*history[41]+.3f*history[59];
                float independent=f>=96?(float)noise()/16:0;
                r[i]=(int16_t)(g*x);m[i]=(int16_t)(g*(echo+independent));}
            p4_echo_witness_result_t result=p4_echo_witness_process(s,m,r);
            bool v=p4_echo_witness_veto(result,true,true,4,(uint32_t)(1600*g*g));
            if(f>=56 && f<96)counts[gain][0]+=v;
            if(f>=96)counts[gain][1]+=v;
            assert(!p4_echo_witness_veto(result,false,true,4,UINT32_MAX));
            assert(!p4_echo_witness_veto(result,true,false,4,UINT32_MAX));
        }
        assert(counts[gain][0]>=35 && counts[gain][1]==0);free(s);
    }
    p4_echo_witness_t *s=calloc(1,sizeof(*s));int16_t zero[512]={0},clip[512];
    for(unsigned i=0;i<512;++i)clip[i]=INT16_MAX;
    for(unsigned i=0;i<64;++i){p4_echo_witness_result_t r=p4_echo_witness_process(s,zero,zero);assert(!p4_echo_witness_veto(r,true,true,4,UINT32_MAX));}
    unsigned trained=s->trained;p4_echo_witness_process(s,clip,clip);assert(s->trained==trained);free(s);
    fprintf(stderr,"PASS gain-scale controls: %u/%u %u/%u %u/%u echo/near vetoes; silence, clipping and validity guards\n",counts[0][0],counts[0][1],counts[1][0],counts[1][1],counts[2][0],counts[2][1]);
}
static void independent_power_controls(void)
{
    /* 225 mixed capture and the older 184 echo counterexample. Powers are
     * in the scaled PCM domain here, so the comparison uses scale one. */
    p4_echo_witness_result_t mixed={.reference_power=1,.residual_power=566224,.trained=103};
    p4_echo_witness_result_t echo={.reference_power=1,.residual_power=194800,.trained=103};
    assert(p4_echo_witness_independent_power(mixed,true,true,1,44241,285526));
    assert(!p4_echo_witness_independent_power(echo,true,true,1,54495,3351425));
    assert(!p4_echo_witness_independent_power(mixed,true,true,1,566224,285526));
    assert(!p4_echo_witness_independent_power(mixed,true,false,1,44241,285526));
    mixed.reference_power=0;
    assert(!p4_echo_witness_independent_power(mixed,true,true,1,44241,285526));
    assert(p4_echo_witness_gap(P4_ECHO_WITNESS_RELATIVE,true,true));
    assert(!p4_echo_witness_gap(P4_ECHO_WITNESS_RELATIVE,false,true));
    assert(!p4_echo_witness_gap(P4_ECHO_WITNESS_RELATIVE,true,false));
    assert(!p4_echo_witness_gap(0,true,true));
    assert(!p4_echo_witness_gap(P4_ECHO_WITNESS_ABSOLUTE,true,true));
    assert(!p4_echo_witness_gap(P4_ECHO_WITNESS_ABSOLUTE|P4_ECHO_WITNESS_RELATIVE,true,true));
    p4_echo_witness_result_t fraction={.mic_power=3200.f,.residual_power=100.f};
    assert(p4_echo_witness_residual_fraction(fraction));
    fraction.residual_power=99.f;
    assert(!p4_echo_witness_residual_fraction(fraction));
    fraction.mic_power=0.f;
    assert(!p4_echo_witness_residual_fraction(fraction));
    fraction.mic_power=320.f;fraction.residual_power=10.f;
    assert(p4_echo_witness_residual_fraction(fraction));
}
int main(void){controls();independent_power_controls();return 0;}
