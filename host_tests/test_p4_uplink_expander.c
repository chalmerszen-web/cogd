#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "p4_uplink_expander.h"

static p4_adaptive_energy_t reference(uint32_t upper,uint32_t foreground)
{
    p4_adaptive_energy_t e={.noise_upper=upper,.dispersion_valid=true,.turn_count=6};
    for(unsigned i=0;i<6;++i)e.turn[i]=foreground;
    return e;
}
static void fill(int16_t *pcm)
{ for(unsigned i=0;i<320;++i)pcm[i]=(i&1)?INT16_MAX:INT16_MIN; }
int main(void)
{
    /* Power rescaling, including very low PCM values, changes neither the
     * contrast weight nor attenuation. No absolute loudness gate exists. */
    const uint32_t scales[]={1,4,100,1000000};
    const uint32_t powers[]={10,25,50,100,500,100,25,10,10,10,10,10};
    int16_t baseline[12][320];
    for(unsigned scale=0;scale<4;++scale){
        p4_adaptive_energy_t e=reference(10*scales[scale],100*scales[scale]);
        p4_uplink_expander_t s;p4_uplink_expander_begin(&s,&e);
        assert(s.enabled && s.weight_q15==32768 && s.gain_q15==128);
        for(unsigned f=0;f<12;++f){
            int16_t pcm[320],retry[320];fill(pcm);
            p4_uplink_expander_apply(&s,pcm,320,powers[f]*scales[scale],(f+1)*320);
            memcpy(retry,pcm,sizeof(pcm));p4_uplink_expander_t before=s;
            /* Repeated BUSY processing of this exact in-place buffer is inert. */
            p4_uplink_expander_apply(&s,pcm,320,powers[f]*scales[scale],(f+1)*320);
            assert(!memcmp(&s,&before,sizeof(s)) && !memcmp(pcm,retry,sizeof(pcm)));
            if(!scale)memcpy(baseline[f],pcm,sizeof(pcm));
            else assert(!memcmp(baseline[f],pcm,sizeof(pcm)));
            assert(s.gain_q15>=128 && s.gain_q15<=32768);
        }
        assert(s.frames==12 && s.attenuated_frames>0);
        assert(baseline[4][319]==INT16_MAX); /* Strong speech reaches unity. */
        assert(s.gain_q15<32768); /* Background eventually releases. */
    }
    /* Low separation is passed unchanged; missing observed dispersion or
     * fresh candidate support cannot borrow a wake's loud foreground. */
    for(unsigned mode=0;mode<4;++mode){
        p4_adaptive_energy_t e=reference(100,mode?1000:200);
        if(mode==1)e.dispersion_valid=false;
        if(mode==2)e.turn_count=0;
        if(mode==3)e.turn_count=5;
        p4_uplink_expander_t s;p4_uplink_expander_begin(&s,&e);assert(!s.enabled);
        int16_t pcm[320],original[320];fill(pcm);memcpy(original,pcm,sizeof(pcm));
        p4_uplink_expander_apply(&s,pcm,320,0,320);assert(!memcmp(pcm,original,sizeof(pcm)));
    }
    /* The knee is continuous and relative to the background distribution. */
    uint32_t prior=0;
    for(unsigned f=200;f<=400;++f){
        p4_adaptive_energy_t e=reference(100,f);
        p4_uplink_expander_t s;p4_uplink_expander_begin(&s,&e);
        assert(s.weight_q15>=prior && s.weight_q15-prior<=164);prior=s.weight_q15;
    }
    assert(prior==32768);
    /* One outlier cannot set candidate Q75; no arithmetic overflow near rail. */
    p4_adaptive_energy_t e=reference(100,300);e.turn[5]=UINT32_MAX;
    p4_uplink_expander_t s;p4_uplink_expander_begin(&s,&e);
    assert(s.foreground==300 && s.weight_q15==16384);
    e=reference(UINT32_MAX/2,UINT32_MAX);p4_uplink_expander_begin(&s,&e);
    assert(s.weight_q15==0); /* Rounding may enable a zero-weight knee. */
    e=reference(0,100);p4_uplink_expander_begin(&s,&e);assert(s.enabled && s.weight_q15==32768);
    int16_t pcm[320];fill(pcm);p4_uplink_expander_apply(&s,pcm,320,UINT32_MAX,320);
    p4_uplink_expander_begin(&s,&e);assert(!s.cursor_valid && !s.frames && s.gain_q15==128);
    /* A material, measured floor switches to the trusted wake foreground and
     * the steeper ratio curve.  The branch is scale-free and uses the same
     * median/MAD state as adaptive admission. */
    e=(p4_adaptive_energy_t){.noise=46760,.foreground=4360870,
        .noise_median=59438,.noise_mad=20562,.noise_upper=121124,
        .dispersion_valid=true,.turn_count=6};
    for(unsigned i=0;i<6;++i)e.turn[i]=1825819;
    p4_uplink_expander_begin(&s,&e);
    assert(s.enabled && s.robust_contrast && s.foreground==4360870);
    assert(s.background==1825819);
    int16_t loud[320];fill(loud);p4_uplink_expander_apply(&s,loud,320,1150000,320);
    assert(s.gain_q15<12000); /* The remote-only level is below the wake anchor. */
    /* A retained frame before the confirmed onset must not inherit the
     * full-gain release from the beginning of a robust turn. */
    p4_uplink_expander_begin(&s,&e);s.onset_cursor=640;
    int16_t prefix[320];fill(prefix);p4_uplink_expander_apply(&s,prefix,320,1150000,320);
    assert(s.gain_q15<=128 && s.attenuated_frames==1);
    /* Recorded strong-onset candidate: vowel Q50 is not background. A
     * quieter consonant four times above the independently measured floor
     * retains >=75% gain after strict capture corroboration. Rescaling the
     * recording changes neither this gain nor the noise rejection. */
    for(unsigned scale=1;scale<=4;scale*=4){
        e=(p4_adaptive_energy_t){.noise=27762*scale,.foreground=457903*scale,
            .noise_median=33167*scale,.noise_mad=6536*scale,.noise_upper=52775*scale,
            .dispersion_valid=true,.turn_count=6,.turn_quorum=true};
        for(unsigned i=0;i<6;++i)e.turn[i]=(i<3?10322996u:11956513u)*scale;
        p4_uplink_expander_begin(&s,&e);s.onset_cursor=640;
        assert(s.enabled && s.preserve_speech && !s.robust_contrast && s.background==e.noise_upper);
        fill(prefix);p4_uplink_expander_apply(&s,prefix,320,4*e.noise_upper,320);
        assert(s.gain_q15>=24576);
        for(unsigned i=0;i<24;++i){fill(prefix);p4_uplink_expander_apply(&s,prefix,320,e.noise_upper,640+i*320);}
        assert(s.gain_q15<=128);
    }
    puts("p4 uplink expander: scale, replay, low contrast, robust prior, strict onset and noise bounds OK");
    return 0;
}
