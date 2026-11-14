#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "p4_adaptive_energy.h"
#include "p4_uplink_expander.h"
#include "p4_local_turn.h"
#include "../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_board/p4_near_votes.h"
#include "../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_board/p4_voice_periodicity.h"

static void provider_retry_retains_reference(void)
{
    const unsigned wake_noise=76861,wake_foreground=273431;
    const unsigned floor[]={59646,90561,123941,90561,57181,157321,239429,37615};
    const unsigned endpoint=1698432, voice=1679232;
    p4_adaptive_energy_t original, retry;
    p4_energy_init(&original,wake_noise,wake_foreground);
    for(unsigned i=0;i<64;++i){
        unsigned cursor=endpoint-320*(64-i);
        assert(!p4_energy_frame(&original,cursor,floor[i%8],false,false,false,true,true,true,false));
    }
    assert(original.dispersion_valid && original.noise_mad>0 && original.background_count==64);
    original.foreground=332820; /* trusted foreground from earlier accepted text */
    p4_energy_turn_begin(&original);
    for(unsigned i=0;i<8;++i)p4_energy_turn_sample(&original,3856983);
    assert(!p4_energy_frame(&original,endpoint,47807,false,true,false,false,false,true,false));
    p4_energy_turn_resolve(&original,false);
    original.candidate_pending=original.candidate_confirmed=true;
    original.candidate_change_cursor=voice;
    original.candidate[0]=3856983;
    p4_energy_retain_reference(&retry,&original);
    assert(retry.noise==original.noise && retry.foreground==original.foreground);
    assert(retry.noise_median==original.noise_median && retry.noise_mad==original.noise_mad);
    assert(retry.noise_upper==original.noise_upper && retry.continuation_upper==original.continuation_upper);
    assert(retry.prior_valid && retry.dispersion_valid && retry.background_count==64 && retry.background_next==64);
    assert(!memcmp(retry.background,original.background,sizeof(original.background)));
    assert(!retry.turn_open && !retry.turn_count && !retry.turn_foreground && !retry.observed_frames);
    assert(!retry.candidate_pending && !retry.candidate_confirmed && !retry.candidate_change_cursor && !retry.candidate[0]);
    assert(p4_energy_threshold(&retry,false)==p4_energy_threshold(&original,false));

    /* Replay of the known older interval may keep its audio decisions but
     * must not retrain the established reference, even with the same power. */
    for(unsigned cursor=voice;cursor<=endpoint;cursor+=320)
        (void)p4_energy_frame(&retry,cursor,3260276,true,true,false,false,false,true,false);
    assert(retry.last_cursor==endpoint && !retry.observed_frames && retry.background_next==64);
    assert(!p4_energy_frame(&retry,endpoint+320,floor[0],false,false,false,true,true,true,false));
    assert(retry.last_cursor==endpoint+320 && retry.background_next==65);

    /* Cold wake/network recovery initializes a different reference. */
    p4_energy_init(&retry,wake_noise,wake_foreground);
    assert(retry.noise_median==wake_noise && !retry.dispersion_valid && !retry.cursor_valid && !retry.background_count);
}

static void scale_cases(void)
{
    /* Identical SNR and foreground contrast must give identical decisions,
     * including levels far below the removed fixed PCM amplitude floors. */
    const unsigned powers[]={1,4,25,10000};
    const unsigned energies[]={100,160,900,1200,1400,1600,1800,2000};
    bool baseline[sizeof(energies)/sizeof(*energies)];
    for(unsigned p=0;p<sizeof(powers)/sizeof(*powers);++p){
        p4_adaptive_energy_t s;p4_energy_init(&s,100*powers[p],16100*powers[p]);
        for(unsigned i=0;i<sizeof(energies)/sizeof(*energies);++i){
            bool accepted=p4_energy_frame(&s,(i+1)*320,energies[i]*powers[p],true,false,true,(false && true),false,true,false);
            if(!p)baseline[i]=accepted;else assert(accepted==baseline[i]);
        }
        assert(!baseline[0] && !baseline[2] && baseline[3] && baseline[7]);
    }
    int16_t low[]={-3,3,-3,3},high[]={-30,30,-30,30},rails[]={INT16_MIN,INT16_MAX};
    assert(p4_energy_mean_square(low,4)==9);
    assert(p4_energy_mean_square(high,4)==900);
    assert(p4_energy_mean_square(rails,2)==1073709056u);
    assert(p4_energy_mean_square(low,0)==0);
    p4_adaptive_energy_t large;p4_energy_init(&large,UINT32_MAX,UINT32_MAX);
    assert(p4_energy_threshold(&large,false)==UINT32_MAX);
}
static void prior_and_real_background_cases(void)
{
    const unsigned ages_ms[]={0,600,1600};
    const unsigned powers[]={1,4,25,10000};
    for(unsigned p=0;p<sizeof(powers)/sizeof(*powers);++p){
        unsigned power=powers[p];
        for(unsigned a=0;a<sizeof(ages_ms)/sizeof(*ages_ms);++a){
            p4_adaptive_energy_t s;p4_energy_init(&s,100*power,16100*power);
            p4_local_turn_t turn={0};
            assert(s.prior_valid && p4_energy_ready(&s));
            assert(!s.background_count && !s.background_next && !s.dispersion_valid);
            /* The existing scalar prior still admits a 120 ms quiet command,
             * including a delayed early command whose idle frames cannot learn.
             * No wall-age threshold or capture eligibility changes here. */
            bool fresh=ages_ms[a]<=80;
            for(unsigned i=0;i<6;++i){
                unsigned cursor=3584+(i+1)*320;
                bool voiced=p4_energy_frame(&s,cursor,1600*power,true,false,turn.votes!=0,(fresh && p4_energy_observable(false,false,fresh,false)),
                    fresh,p4_energy_observable(false,false,fresh,false),false);
                assert(voiced);
                assert(p4_local_turn_feed(&turn,cursor,voiced,true,1200)==
                    (i==5?P4_LOCAL_START:P4_LOCAL_NONE));
            }
            assert(!s.background_count && !s.dispersion_valid && s.noise==100*power);
            assert(s.observed_frames==(fresh?6u:0u));
            assert(p4_energy_barge_admit(1600*power,s.noise,s.foreground,s.noise_upper,p4_energy_ready(&s)));
        }
        p4_adaptive_energy_t s;p4_energy_init(&s,100*power,16100*power);
        const unsigned first_eight[]={100,700,100,700,100,700,100,700};
        for(unsigned i=0;i<8;++i){
            p4_energy_frame(&s,(i+1)*320,first_eight[i]*power,false,false,false,(true && true),true,true,false);
            assert(s.background_count==i+1 && s.background_next==i+1);
            if(i<7){
                p4_energy_background_stats(&s); /* Too few real values cannot replace prior. */
                assert(!s.dispersion_valid && s.noise==100*power && s.noise_mad==0);
                assert(s.noise_median==100*power && s.noise_upper==100*power);
            }
        }
        assert(s.dispersion_valid && s.noise==100*power && s.noise_median==400*power);
        assert(s.noise_mad==300*power && s.noise_upper==1300*power);
        assert(s.threshold==1100*power); /* Eighth frame used its pre-update prior. */
        assert(p4_energy_threshold(&s,false)==1300*power);
        assert(!p4_energy_frame(&s,9*320,1200*power,true,false,false,(true && true),true,true,false));
        assert(s.background_count==8); /* Quiet possible foreground is not learned as noise. */
        for(unsigned i=0;i<56;++i)
            p4_energy_frame(&s,(10+i)*320,first_eight[i%8]*power,false,false,false,(true && true),true,true,false);
        assert(s.background_count==64 && s.background_next==64);
        assert(s.noise_median==400*power && s.noise_mad==300*power);
        for(unsigned i=0;i<64;++i)
            p4_energy_frame(&s,(66+i)*320,100*power,false,false,false,(true && true),true,true,false);
        assert(s.background_count==64 && s.background_next==128);
        assert(s.dispersion_valid && s.noise==100*power && s.noise_mad==0);
    }
    /* Even extreme first-seven impulses are merely observations until the
     * eight-sample boundary. A single isolated impulse cannot redefine N/MAD. */
    p4_adaptive_energy_t s;p4_energy_init(&s,100,16100);
    const uint32_t impulse[]={UINT32_MAX,100,700,100,700,100,700,100};
    for(unsigned i=0;i<8;++i){
        p4_energy_frame(&s,(i+1)*320,impulse[i],false,false,false,(true && true),true,true,false);
        if(i<7)assert(s.noise==100 && s.noise_median==100 && !s.noise_mad && !s.dispersion_valid);
    }
    assert(s.noise==100 && s.noise_median==400 && s.noise_mad==300 && s.dispersion_valid);
    p4_energy_init(&s,100,16100);
    for(unsigned i=0;i<8;++i)
        p4_energy_frame(&s,(i+1)*320,i%2?UINT32_MAX:0,false,false,false,(true && true),true,true,false);
    assert(s.noise_median==UINT32_MAX/2 && s.noise_mad==UINT32_MAX/2);
    assert(s.noise_upper==UINT32_MAX && s.continuation_upper==UINT32_MAX);

    /* N-only readiness remains valid, but an accepted turn never invents F. */
    p4_energy_init(&s,100,0);assert(s.prior_valid && p4_energy_ready(&s));
    p4_energy_turn_begin(&s);
    for(unsigned i=0;i<6;++i)
        assert(p4_energy_frame(&s,(i+1)*320,1600,true,true,false,(false && true),false,true,false));
    p4_energy_turn_resolve(&s,true);
    assert(!s.foreground && !s.background_count && !s.dispersion_valid);
    /* Without a prior, eight real noise observations establish only the
     * background. Measured zero dispersion is distinct from an absent one. */
    p4_energy_init(&s,0,0);assert(!s.prior_valid && !p4_energy_ready(&s));
    for(unsigned i=0;i<8;++i){
        assert(!p4_energy_frame(&s,(i+1)*320,100,false,false,false,(true && true),true,true,false));
        assert(p4_energy_ready(&s)==(i==7));
    }
    assert(!s.prior_valid && s.dispersion_valid && !s.foreground && !s.noise_mad);
    assert(p4_energy_frame(&s,9*320,401,true,false,false,(false && true),false,true,false));
    p4_energy_init(&s,0,0);
    for(unsigned i=0;i<8;++i)p4_energy_frame(&s,(i+1)*320,0,false,false,false,(true && true),true,true,false);
    assert(p4_energy_ready(&s) && s.dispersion_valid && !s.noise && !s.noise_mad && !s.foreground);
    /* The old unobserved176 candidate is NOT claimed fixed: its exact
     * threshold and decision stay the same with zero eligible observations. */
    p4_energy_init(&s,14989,604155);
    assert(p4_energy_threshold(&s,false)==59956);
    assert(p4_energy_frame(&s,15680,67281,true,false,true,(false && false),false,false,false));
    assert(s.threshold==59956 && !s.observed_frames && !s.background_count && !s.dispersion_valid);
}
static void background_cases(void)
{
    p4_adaptive_energy_t s;p4_energy_init(&s,100,16100);
    unsigned cursor=0;
    /* Occasional mechanical spikes cannot redefine the background. */
    for(unsigned i=0;i<128;++i)
        p4_energy_frame(&s,cursor+=320,i%8?100:100000,false,false,false,(true && true),true,true,false);
    assert(s.noise==100 && s.foreground==16100);
    /* A sustained background increase and later decrease adapt in bounded
     * time. Weak voiced background is not promoted to trusted foreground. */
    for(unsigned i=0;i<64;++i)
        assert(!p4_energy_frame(&s,cursor+=320,800,true,false,false,(true && true),true,true,false));
    assert(s.noise==800 && s.foreground==16100);
    assert(!p4_energy_frame(&s,cursor+=320,2000,true,false,false,(false && true),false,true,false));
    assert(p4_energy_frame(&s,cursor+=320,4000,true,false,false,(false && true),false,true,false));
    for(unsigned i=0;i<64;++i)p4_energy_frame(&s,cursor+=320,100,false,false,false,(true && true),true,true,false);
    assert(s.noise==100);
}
static void quiet_short_and_pause_cases(void)
{
    p4_adaptive_energy_t s;p4_energy_init(&s,100,16100);
    p4_local_turn_t turn={0};unsigned cursor=0;
    /* A 120 ms quiet command, well below wake power, still receives the
     * original six-frame quorum. Pre-roll and text handling remain separate. */
    for(unsigned i=0;i<6;++i){
        bool voice=p4_energy_frame(&s,cursor+=320,1600,true,turn.active,turn.votes!=0,(true && true),true,true,false);
        p4_local_event_t event=p4_local_turn_feed(&turn,cursor,voice,true,1200);
        assert(event==(i==5?P4_LOCAL_START:P4_LOCAL_NONE));
    }
    p4_energy_turn_begin(&s);assert(s.turn_count==6 && s.turn_foreground==1600);
    assert(s.noise==100);
    /* Quieter following consonants survive hysteresis; the 924 ms clause
     * pause is retained. Background alone must not refresh last_voice. */
    assert(p4_energy_frame(&s,cursor+=320,300,true,true,true,(false && true),false,true,false));
    p4_local_turn_feed(&turn,cursor,true,true,1200);
    for(unsigned i=0;i<47;++i){
        bool voice=p4_energy_frame(&s,cursor+=320,100,true,true,true,(false && true),false,true,false);
        assert(!voice);
        assert(p4_local_turn_feed(&turn,cursor,voice,true,1200)==P4_LOCAL_NONE);
    }
    assert(p4_energy_frame(&s,cursor+=320,400,true,true,true,(false && true),false,true,false));
    assert(p4_local_turn_feed(&turn,cursor,true,true,1200)==P4_LOCAL_NONE);
    for(unsigned i=0;i<60;++i){
        bool voice=p4_energy_frame(&s,cursor+=320,100,false,true,true,(false && true),false,true,false);
        assert(p4_local_turn_feed(&turn,cursor,voice,true,1200)==(i==59?P4_LOCAL_END:P4_LOCAL_NONE));
    }
    assert(s.noise==100);
    p4_energy_turn_resolve(&s,true);
    assert(s.foreground<16100 && s.foreground>1600);
}
static void ownership_and_replay_cases(void)
{
    p4_adaptive_energy_t s;p4_energy_init(&s,100,16100);
    unsigned cursor=0;
    /* Candidate, active and committed ownership freeze the background.
     * Own DAC/cue/tail frames cannot train either reference or seed a turn. */
    for(unsigned i=0;i<80;++i){
        assert(!p4_energy_frame(&s,cursor+=320,100000,true,false,false,(true && true),true,true,true));
        p4_energy_frame(&s,cursor+=320,1000,false,false,true,(true && true),true,true,false);
        p4_energy_frame(&s,cursor+=320,1000,false,true,false,(true && true),true,true,false);
        p4_energy_frame(&s,cursor+=320,1000,false,false,false,(false && true),false,true,false);
        p4_energy_frame(&s,cursor+=320,1000,false,false,false,(true && false),true,false,false);
    }
    assert(s.noise==100 && s.foreground==16100 && !s.turn_count);
    assert(!s.background_count && !s.background_next && !s.dispersion_valid);
    unsigned observed=s.observed_frames,background=s.background_next;
    for(unsigned i=0;i<64;++i)
        p4_energy_frame(&s,cursor-i*320,1000,false,false,false,(true && true),true,true,false);
    assert(s.observed_frames==observed && s.background_next==background);
    /* Accepted foreground cannot be learned from a duplicate/replayed
     * candidate, an empty transcript, or an unanchored first soft input. */
    p4_energy_turn_begin(&s);assert(!s.turn_count);
    for(unsigned i=0;i<6;++i)p4_energy_frame(&s,cursor+=320,2000,true,true,false,(false && true),false,true,false);
    uint32_t foreground=s.foreground;
    p4_energy_turn_resolve(&s,false);assert(s.foreground==foreground);
    p4_energy_init(&s,100,0);p4_energy_turn_begin(&s);
    for(unsigned i=0;i<6;++i)p4_energy_frame(&s,cursor+=320,2000,true,true,false,(false && true),false,true,false);
    p4_energy_turn_resolve(&s,true);assert(!s.foreground);
    /* Sample cursor rollover must not make subsequent live frames stale. */
    p4_energy_init(&s,100,16100);
    p4_energy_frame(&s,UINT32_MAX-319,100,false,false,false,(true && true),true,true,false);
    p4_energy_frame(&s,0,100,false,false,false,(true && true),true,true,false);
    assert(s.observed_frames==2);
}
static void wake_and_capture_age_cases(void)
{
    uint32_t values[64],noise=999,foreground=999;
    for(unsigned i=0;i<64;++i)values[i]=i<32?100:1600;
    p4_energy_wake_snapshot(values,64,true,&noise,&foreground);
    assert(noise==100 && foreground==1600);
    p4_energy_wake_snapshot(values,64,false,&noise,&foreground);
    assert(!noise && !foreground); /* New wake with DAC/old history clears F. */
    noise=foreground=999;
    p4_energy_wake_snapshot(values,7,true,&noise,&foreground);
    assert(!noise && !foreground);
    for(unsigned i=0;i<64;++i)values[i]=100;
    p4_energy_wake_snapshot(values,64,true,&noise,&foreground);
    assert(noise==100 && !foreground); /* Flat noise is not a wake anchor. */
    assert(p4_energy_observable(false,false,true,false));
    assert(!p4_energy_observable(false,false,false,false));
    assert(!p4_energy_observable(true,false,true,true));
    assert(!p4_energy_observable(true,true,true,false));
    assert(!p4_energy_observable(true,true,false,false));
    /* After ordinary onset rewind, new cursor frames remain ~500 ms behind
     * capture. They refine current speech while background remains frozen. */
    p4_adaptive_energy_t s;p4_energy_init(&s,100,16100);p4_energy_turn_begin(&s);
    for(unsigned i=1;i<=12;++i)
        assert(p4_energy_frame(&s,i*320,1600,true,true,true,(false && p4_energy_observable(true,false,false,false)),false,
            p4_energy_observable(true,false,false,false),false));
    assert(s.turn_count==12 && s.turn_foreground==1600 && s.noise==100);
    for(unsigned i=1;i<=12;++i)
        p4_energy_frame(&s,i*320,3200,true,true,true,(false && p4_energy_observable(true,false,false,false)),false,
            p4_energy_observable(true,false,false,false),false);
    assert(s.turn_count==12 && s.turn_foreground==1600);
}
static void adaptive_barge_cases(void)
{
    unsigned votes=0,strong=0;
    /* Background can exceed the legacy 620 PCM-level gate yet remain too
     * weak relative to this wake. It must never accumulate barge votes. */
    for(unsigned i=0;i<32;++i){
        bool admitted=p4_energy_barge_admit(900000,100000,16100000,0,true);
        votes=(votes<<1)|(unsigned)admitted;strong=(strong<<1)|(unsigned)admitted;
        assert(!p4_near_duplex_confirm(admitted,votes,strong));
    }
    /* An intentional quieter-than-wake interruption still passes the same
     * unchanged sustained six-frame duplex confirmation once eligible. */
    for(unsigned i=0;i<6;++i){
        bool admitted=p4_energy_barge_admit(1600000,100000,16100000,0,true);
        assert(admitted);votes=(votes<<1)|1u;strong=(strong<<1)|1u;
        assert(p4_near_duplex_confirm(admitted,votes,strong)==(i==5));
    }
    assert(!p4_energy_barge_admit(1600000,100000,16100000,0,false));
    assert(!p4_energy_barge_admit(1100000,100000,16100000,0,true));
    assert(!p4_energy_barge_admit(1600000,800000,16100000,0,true));

    /* A measured MAD upper bound must remain the active dynamic floor. The
     * quiet near source is below the old four-times-noise fallback but above
     * the measured upper bound and the relative foreground term. */
    assert(p4_energy_barge_threshold(27928,143815,52841)==52841);
    assert(!p4_energy_barge_admit(52841,27928,143815,52841,true));
    assert(p4_energy_barge_admit(107000,27928,143815,52841,true));
    assert(p4_energy_barge_threshold(27928*4,143815*4,52841*4)==52841*4);
}
static void actual_strong_scaled_barge_cases(void)
{
    /* Use the production strong-spectrum branch, not idealized strong=true.
     * The quietest copy falls below both former 620 linear and 200 NLP
     * amplitude floors while retaining exactly the same power ratios. */
    const unsigned divisors[]={1,2,4,8};
    for(unsigned d=0;d<sizeof(divisors)/sizeof(*divisors);++d){
        unsigned divisor=divisors[d],power=divisor*divisor;
        int16_t linear[512],nlp[512];unsigned votes=0,strong=0;
        for(unsigned i=0;i<512;++i){
            linear[i]=(int16_t)(((i/32)%2?4000:-4000)/(int)divisor);
            nlp[i]=(int16_t)(((i/32)%2?300:-300)/(int)divisor);
        }
        uint32_t energy=p4_energy_mean_square(linear,512);
        bool adaptive=p4_energy_barge_admit(energy,160000/power,64000000/power,0,true);
        assert(adaptive);
        assert(p4_near_speech(false,true,adaptive));
        assert(!p4_near_speech(false,false,adaptive));
        assert(!p4_near_speech(true,true,false));
        assert(!p4_voice_duplex_strong(nlp,linear,false,true,300/divisor));
        assert(p4_voice_duplex_strong(nlp,linear,true,true,300/divisor));
        assert(!p4_voice_duplex_strong(nlp,linear,false,false,300/divisor));
        for(unsigned i=0;i<32;++i){
            bool near=p4_near_speech(false,true,adaptive);
            bool solid=near && p4_voice_duplex_strong(nlp,linear,false,true,300/divisor);
            votes=(votes<<1)|(unsigned)near;strong=(strong<<1)|(unsigned)solid;
            assert(near && !p4_near_duplex_confirm(near,votes,strong));
        }
        votes=strong=0;
        for(unsigned i=0;i<6;++i){
            bool near=p4_near_speech(true,true,adaptive);
            bool solid=near && p4_voice_duplex_strong(nlp,linear,true,true,300/divisor);
            votes=(votes<<1)|(unsigned)near;strong=(strong<<1)|(unsigned)solid;
            assert(p4_near_duplex_confirm(near,votes,strong)==(i==5));
        }
        /* Echo rejection remains an independent veto after energy/VAD. */
        bool mic_echo=true;
        bool near=p4_near_speech(false,true,adaptive) && !mic_echo;
        assert(!p4_near_duplex_confirm(near,UINT_MAX,UINT_MAX));
        /* Aperiodic residue does not acquire strong votes by being loud
         * enough. Exercise this production path at the same quiet gains. */
        uint32_t rng=173;
        for(unsigned i=0;i<512;++i){
            rng=rng*1664525u+1013904223u;
            int sample=(int)(rng>>16)-32768;
            linear[i]=(int16_t)(sample/(int)(8*divisor));
            nlp[i]=(int16_t)(sample/(int)(128*divisor));
        }
        assert(!p4_voice_duplex_strong(nlp,linear,false,true,300/divisor));
    }
}
static void dispersion_cases(void)
{
    const uint32_t pattern[]={100,100,700,1300};
    const unsigned powers[]={1,4,25};
    for(unsigned k=0;k<3;++k){
        uint32_t scale=powers[k];p4_adaptive_energy_t s;p4_energy_init(&s,100*scale,16100*scale);
        unsigned cursor=0;
        for(unsigned i=0;i<64;++i)p4_energy_frame(&s,cursor+=320,pattern[i%4]*scale,false,false,false,(true && true),true,true,false);
        assert(s.noise==100*scale && s.noise_median==400*scale && s.noise_mad==300*scale);
        assert(s.noise_upper==1300*scale && s.continuation_upper==1000*scale);
        assert(p4_energy_threshold(&s,false)==p4_energy_barge_threshold(s.noise,s.foreground,s.noise_upper));
        assert(!p4_energy_barge_admit(1300*scale,s.noise,s.foreground,s.noise_upper,true));
        assert(p4_energy_barge_admit(1300*scale+1,s.noise,s.foreground,s.noise_upper,true));
        unsigned count=s.background_next;
        /* Ambiguous quiet voiced foreground above its trusted contrast is
         * not absorbed merely because current noise dispersion vetoes it. */
        for(unsigned i=0;i<64;++i)
            assert(!p4_energy_frame(&s,cursor+=320,1200*scale,true,false,false,(true && true),true,true,false));
        assert(s.background_next==count && s.noise_upper==1300*scale);
        assert(p4_energy_frame(&s,cursor+=320,1600*scale,true,false,false,(true && true),true,true,false));
        p4_energy_turn_begin(&s);
        assert(p4_energy_frame(&s,cursor+=320,1100*scale,true,true,true,(false && true),false,true,false));
        p4_energy_turn_resolve(&s,false);
        for(unsigned i=0;i<64;++i)p4_energy_frame(&s,cursor+=320,100*scale,false,false,false,(true && true),true,true,false);
        assert(s.noise==100*scale && s.noise_median==100*scale && !s.noise_mad && s.noise_upper==100*scale);
        s.background[7]=UINT32_MAX;p4_energy_background_stats(&s);
        assert(s.noise==100*scale && s.noise_median==100*scale && !s.noise_mad);
    }
    p4_adaptive_energy_t s;p4_energy_init(&s,100,0);
    for(unsigned i=0;i<64;++i)s.background[i]=pattern[i%4];
    s.background_count=s.background_next=64;
    p4_energy_background_stats(&s);
    unsigned before=s.background_next;
    p4_energy_frame(&s,320,1200,true,false,false,(true && true),true,true,false);
    assert(s.background_next==before+1); /* No fake foreground anchor at F=0. */
    for(unsigned i=0;i<64;++i)s.background[i]=i<32?0:UINT32_MAX;
    p4_energy_background_stats(&s);
    assert(s.noise_median==UINT32_MAX/2 && s.noise_mad==UINT32_MAX/2);
    assert(s.noise_upper==UINT32_MAX && s.continuation_upper==UINT32_MAX);
    assert(!p4_energy_barge_admit(UINT32_MAX,s.noise,s.foreground,s.noise_upper,true));
}
static void recorded_background_dispersion(void)
{
    /* Actual baseline172b noise_low linear PCM, source 2.00..3.28 s,
     * independently aligned offset11109 samples, 320-sample integer MSE.
     * WAV SHA256 4d341f1e21386180a16e50ccb72a1f6d8daeb13206ba38dc2cb517cb2bfa98fd.
     * This tests dispersion arithmetic, not VAD, distance or acceptance. */
    const uint32_t recorded[]={14205,14350,16609,23491,15667,14114,12758,16483,
        14242,26001,43579,28409,20273,12023,17140,12674,23260,17835,14719,15774,
        22198,15130,17538,19836,15845,17022,23302,10937,11566,28426,18321,19954,
        12621,9200,12139,24310,12500,14505,20971,13456,13938,22662,11815,11973,
        8554,13116,13566,14565,59316,66709,32994,33428,26432,38101,25670,30291,
        20381,17659,19473,32805,42551,32931,26461,21373};
    p4_adaptive_energy_t s;p4_energy_init(&s,100,16100);
    memcpy(s.background,recorded,sizeof(recorded));s.background_count=s.background_next=64;
    p4_energy_background_stats(&s);
    assert(s.noise==13938 && s.noise_median==17598 && s.noise_mad==4882);
    assert(s.noise_upper==32244 && s.continuation_upper==27362);
    assert(p4_energy_barge_threshold(s.noise,s.foreground,s.noise_upper)==32244);
    assert(p4_energy_threshold(&s,false)>=p4_energy_barge_threshold(s.noise,s.foreground,s.noise_upper));
}
static void candidate_capture_independent_of_noise_learning(void)
{
    for(unsigned mode=0;mode<3;++mode){
        p4_adaptive_energy_t s;p4_energy_init(&s,100,8000);
        for(unsigned i=0;i<8;++i)s.background[i]=100;
        s.background_count=8;p4_energy_background_stats(&s);
        p4_local_turn_t turn;p4_local_turn_reset(&turn,100000);
        for(unsigned i=0;i<6;++i){
            unsigned cursor=100320+i*320;bool fresh=mode==0 || (mode==1 && i<5);
            bool v=p4_energy_frame(&s,cursor,20000,true,false,turn.votes!=0,true,
                fresh,p4_energy_observable(false,false,fresh,false),false);
            assert(v);assert(p4_local_turn_feed(&turn,cursor,v,true,1200)==(i==5?P4_LOCAL_START:P4_LOCAL_NONE));
            if(i==4){
                unsigned retained=s.candidate_next;
                assert(p4_energy_frame(&s,cursor,200000,true,false,true,true,fresh,fresh,false));
                assert(s.candidate_next==retained);
            }
        }
        assert(s.noise==100 && s.noise_mad==0 && s.background_count==8 && s.background_next==0);
        assert(s.observed_frames==(mode==0?6u:mode==1?5u:0u));
        p4_energy_turn_begin(&s);assert(s.turn_count==6 && s.turn_foreground==20000);
        p4_uplink_expander_t x;p4_uplink_expander_begin(&x,&s);assert(x.enabled && x.foreground==20000);
        p4_energy_turn_resolve(&s,false);assert(!s.turn_count && !s.candidate_next);
        /* Blocked ownership and own output clear candidates even when
         * delayed PCM would otherwise pass; neither trains background. */
        for(unsigned i=0;i<5;++i)p4_energy_frame(&s,110000+i*320,20000,true,false,true,true,false,false,false);
        assert(s.candidate_next==5);
        p4_energy_frame(&s,112000,20000,true,false,true,false,false,false,false);
        p4_energy_turn_begin(&s);assert(!s.turn_count);
        p4_energy_turn_resolve(&s,false);
        for(unsigned i=0;i<5;++i)p4_energy_frame(&s,113000+i*320,20000,true,false,true,true,false,false,false);
        assert(!p4_energy_frame(&s,115000,20000,true,false,true,true,false,false,true));
        p4_energy_turn_begin(&s);assert(!s.turn_count);
    }
}
static void dynamic_contrast_cases(void)
{
    p4_adaptive_energy_t s;p4_energy_init(&s,100,16100);
    /* A measured, low-absolute but variable floor gets a stricter
     * dimensionless foreground contrast; no PCM amplitude floor is involved. */
    s.noise_median=400;s.noise_mad=110;s.noise_upper=730;s.dispersion_valid=true;
    assert(p4_energy_onset_foreground_den(&s)==6);
    assert(p4_energy_threshold(&s,false)==p4_energy_reference_threshold_with_den(100,16100,false,6));
    /* A flat observed floor leaves the original quiet-speech margin intact. */
    s.noise_mad=0;s.noise_upper=400;
    assert(p4_energy_onset_foreground_den(&s)==16);
    assert(p4_energy_threshold(&s,false)==p4_energy_reference_threshold_with_den(100,16100,false,16));
    /* A nonzero, very low coefficient of variation tightens the ratio to
     * reject a sustained remote source sitting above a stable floor. */
    s.noise_median=700;s.noise_mad=50;s.noise_upper=850;
    assert(p4_energy_onset_foreground_den(&s)==3);
    /* A variable floor whose robust upper bound is already above the scalar
     * 1/16 threshold still receives the same ratio-based penalty. */
    s.noise=1000;s.foreground=16100;s.noise_median=1000;s.noise_mad=350;s.noise_upper=2050;
    assert(p4_energy_onset_foreground_den(&s)==6);
    /* The fresh step-noise failure has a stable, material floor (CV about
     * .15 and M/F above 1/128); the stronger ratio must reject its remote
     * burst while a normal near-field floor with similar CV but M/F below
     * 1/128 remains at den6. */
    s.noise=59270;s.foreground=4730077;s.noise_median=66909;s.noise_mad=9984;s.noise_upper=96861;
    assert(p4_energy_onset_foreground_den(&s)==3);
    s.noise=7304;s.foreground=5054847;s.noise_median=8767;s.noise_mad=1365;s.noise_upper=12862;
    assert(p4_energy_onset_foreground_den(&s)==6);
}
int main(void)
{
    provider_retry_retains_reference();
    candidate_capture_independent_of_noise_learning();
    scale_cases();prior_and_real_background_cases();background_cases();quiet_short_and_pause_cases();ownership_and_replay_cases();wake_and_capture_age_cases();
    adaptive_barge_cases();
    actual_strong_scaled_barge_cases();
    dispersion_cases();recorded_background_dispersion();dynamic_contrast_cases();
    puts("PASS adaptive energy, real background counts, prior readiness, MAD, recorded noise, quiet speech and replay boundaries");
    return 0;
}
