#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "p4_voice_periodicity.h"
#include "../platform/espidf/p4_adaptive_energy.h"

static uint32_t rng=1;
static int noise(void)
{rng=rng*1664525u+1013904223u;return (int)(rng>>16)-32768;}

static void residual_onset_coherence(void)
{
    int16_t linear[512],nlp[512];
    for(unsigned i=0;i<512;++i){
        double phase=6.283185307179586*i/44.0;
        linear[i]=(int16_t)(2000+5500*sin(phase)+1400*sin(phase*2));
        nlp[i]=(int16_t)(-800+275*sin(phase)+70*sin(phase*2));
    }
    assert(p4_voice_residual_onset_coherent(linear,nlp));
    for(unsigned i=0;i<512;++i)nlp[i]=-nlp[i];
    assert(!p4_voice_residual_onset_coherent(linear,nlp));
    for(unsigned i=0;i<512;++i)nlp[i]=1234;
    assert(!p4_voice_residual_onset_coherent(linear,nlp));
    for(unsigned i=0;i<512;++i)linear[i]=nlp[i]=(i&2)?INT16_MIN:INT16_MAX;
    assert(!p4_voice_residual_onset_coherent(linear,nlp));
    for(unsigned i=0;i<512;++i)linear[i]=nlp[i]=(int16_t)(noise());
    assert(!p4_voice_residual_onset_coherent(linear,nlp));
}

static void pitch_pair_continuity(void)
{
    int16_t pcm[4][512];
    for(unsigned frame=0;frame<4;++frame)for(unsigned i=0;i<512;++i){
        double phase=6.283185307179586*(512*frame+i)/44.0;
        pcm[frame][i]=(int16_t)(1000+(frame==0 || frame==3 ? 300 : 6000)*
            (sin(phase)+.4*sin(2*phase)));
    }
    p4_voice_pitch_history_t pitch={0};
    assert(!p4_voice_pitch_history_update(&pitch,pcm[0],0,0,false,true));
    assert(p4_voice_pitch_history_update(&pitch,pcm[1],0,1,true,false));
    assert(!p4_voice_pitch_history_update(&pitch,pcm[2],1,2,true,false));
    assert(p4_voice_pitch_history_update(&pitch,pcm[3],3,4,false,true));
    assert(pitch.linked==9u);

    /* A separately periodic anchor at a different pitch must not lend the
     * first match to a trailing frame that continues only the second pitch. */
    for(unsigned frame=2;frame<4;++frame)for(unsigned i=0;i<512;++i){
        double phase=6.283185307179586*(512*(frame-2)+i)/71.0;
        pcm[frame][i]=(int16_t)(1000+(frame==3 ? 300 : 6000)*
            (sin(phase)+.4*sin(2*phase)));
    }
    pitch=(p4_voice_pitch_history_t){0};
    assert(!p4_voice_pitch_history_update(&pitch,pcm[0],0,0,false,true));
    assert(p4_voice_pitch_history_update(&pitch,pcm[1],0,1,true,false));
    assert(!p4_voice_pitch_history_update(&pitch,pcm[2],1,2,true,false));
    assert(!p4_voice_pitch_history_update(&pitch,pcm[3],3,4,false,true));
    assert(pitch.linked==8u);
}

static void same_channel_evidence_matrix(void)
{
    const int amplitudes[]={8000,1000,250,80};unsigned tested=0;
    for(unsigned g=0;g<4;++g){
        int16_t pcm[4][512];uint32_t state=12345;
        for(unsigned i=0;i<512;++i){
            state=state*1664525u+1013904223u;
            pcm[0][i]=0;
            pcm[1][i]=(int16_t)(((int)(state>>16)-32768)*amplitudes[g]/32768);
            pcm[2][i]=(i/32)%2?amplitudes[g]:-amplitudes[g];
            pcm[3][i]=(i/4)%2?amplitudes[g]:-amplitudes[g];
        }
        /* Silence, aperiodic residue, voiced structure and a high tone.
         * Establish each spectrum independently of the VAD combinations. */
        for(unsigned p=0;p<4;++p){
            assert(p4_voice_above_low_band(pcm[p])==(p!=0));
            bool shape=p4_voice_above_low_band(pcm[p]) && p4_voice_periodic(pcm[p]) &&
                p4_voice_low_component(pcm[p]) && !p4_voice_high_tone(pcm[p]);
            assert(shape==(p==2));
        }
        /* A VAD-positive channel cannot lend its vote to another channel's
         * spectrum. Linear shape requires its own VAD and NLP voiced
         * corroboration; tiny amplitude alone is neither a veto nor a vote.
         * The existing NLP low-band precondition remains at every scale. */
        for(unsigned a=0;a<4;++a)for(unsigned l=0;l<4;++l)
            for(unsigned av=0;av<2;++av)for(unsigned lv=0;lv<2;++lv){
                bool expected=av && a!=0 && (a==2 || (lv && l==2));
                assert(p4_voice_duplex_strong(pcm[a],pcm[l],av,lv,0)==expected);
                assert(p4_voice_duplex_evidence(pcm[a],pcm[l],av,lv,0,false)==expected);
                ++tested;
            }
    }
    assert(tested==256);
}

int main(void)
{
    same_channel_evidence_matrix();
    pitch_pair_continuity();
    residual_onset_coherence();
    int16_t pcm[512];
    const int dc[]={0,32767,-32768,8000,-8000};
    for(unsigned n=0;n<sizeof(dc)/sizeof(dc[0]);++n){
        for(unsigned i=0;i<512;++i)pcm[i]=(int16_t)dc[n];
        assert(!p4_voice_periodic(pcm));
        assert(!p4_voice_above_low_band(pcm));
        assert(!p4_voice_low_component(pcm));
    }
    /* Hum / low tonal residue is periodic, but cannot supply the strong
     * evidence that confirms a new near-end utterance during playback. */
    const double low_tones[]={50,65,80,100,125,160};
    for(unsigned f=0;f<sizeof(low_tones)/sizeof(low_tones[0]);++f)
        for(unsigned phase=0;phase<8;++phase){
            for(unsigned i=0;i<512;++i)
                pcm[i]=(int16_t)(8000+6000*sin(6.283185307179586*low_tones[f]*i/16000.0+phase*.7));
            assert(!p4_voice_above_low_band(pcm));
        }
    /* Low fundamental pitch with stronger vowel harmonics remains eligible.
     * This is a synthetic spectrum check, not a human voice-accuracy claim. */
    const double fundamentals[]={65,80,100,125,160,200,300,500};
    for(unsigned f=0;f<sizeof(fundamentals)/sizeof(fundamentals[0]);++f)
        for(unsigned phase=0;phase<4;++phase){
            double harmonic=ceil(600.0/fundamentals[f]);
            for(unsigned i=0;i<512;++i){
                double t=6.283185307179586*fundamentals[f]*i/16000.0+phase*.7;
                pcm[i]=(int16_t)(5000+6000*sin(t)+4000*sin(harmonic*t)+2000*sin((harmonic+2)*t));
            }
            assert(p4_voice_above_low_band(pcm));
            assert(p4_voice_periodic(pcm));
            assert(p4_voice_low_component(pcm));
        }
    /* A high-frequency periodic residual can match multiples of a vocal
     * period. It must not supply strong evidence without a lower component. */
    const double high_tones[]={2000,2187.5,2250};
    for(unsigned f=0;f<sizeof(high_tones)/sizeof(high_tones[0]);++f)
        for(unsigned phase=0;phase<8;++phase){
            for(unsigned i=0;i<512;++i)
                pcm[i]=(int16_t)(8000+6000*sin(6.283185307179586*high_tones[f]*i/16000.0+phase*.7));
            assert(!p4_voice_low_component(pcm));
            assert(!p4_voice_duplex_strong(pcm,pcm,true,true,4000));
            assert(!p4_voice_duplex_evidence(pcm,pcm,true,true,4000,true));
        }
    /* Broad-band residue must not become strong voiced evidence merely
     * because its amplitude exceeds the independent energy/VAD gates. */
    for(unsigned n=0;n<100;++n){
        for(unsigned i=0;i<512;++i)pcm[i]=(int16_t)noise();
        assert(!p4_voice_periodic(pcm));
    }
    /* A lower component does not turn a short-period tone into voice.
     * Mixed tones exercise the captured failure beyond its exact frequency. */
    const double mixed_tones[]={1600,1800,1843.75,1900,2200};
    for(unsigned f=0;f<sizeof(mixed_tones)/sizeof(*mixed_tones);++f)
        for(unsigned phase=0;phase<8;++phase){
            for(unsigned i=0;i<512;++i)
                pcm[i]=(int16_t)(6000*sin(6.283185307179586*mixed_tones[f]*i/16000.0+phase*.7)+
                    1800*sin(6.283185307179586*125*i/16000.0));
            assert(p4_voice_high_tone(pcm));
            assert(!p4_voice_duplex_strong(pcm,pcm,true,true,4000));
            assert(!p4_voice_duplex_evidence(pcm,pcm,true,true,4000,true));
        }
    /* Independent harmonic/noise signals exercise pitches and phase across
     * the intended range, including low/high voices and nonzero DC. */
    const double pitches[]={65,80,100,120,160,200,240,300,380,450,500};
    for(unsigned f=0;f<sizeof(pitches)/sizeof(pitches[0]);++f){
        for(unsigned phase=0;phase<4;++phase){
            for(unsigned i=0;i<512;++i){
                double t=6.283185307179586*pitches[f]*i/16000.0+phase*.7;
                pcm[i]=(int16_t)(5000+6000*sin(t)+2000*sin(2*t)+noise()/20);
            }
            assert(p4_voice_periodic(pcm));
        }
    }
    /* Full-scale periodic data and impulses check bounded arithmetic under
     * sanitizers; no assumption that periodicity alone denotes speech. */
    for(unsigned i=0;i<512;++i)pcm[i]=(i/32)%2?32767:-32768;
    assert(p4_voice_periodic(pcm));
    assert(p4_voice_above_low_band(pcm));
    for(unsigned i=0;i<512;++i)pcm[i]=(i/4)%2?32767:-32768;
    assert(p4_voice_above_low_band(pcm));
    for(unsigned i=0;i<512;++i)pcm[i]=0;
    pcm[256]=32767;assert(!p4_voice_periodic(pcm));
    int16_t linear[512],suppressed[512];
    for(unsigned i=0;i<512;++i){
        linear[i]=(i/32)%2?4000:-4000;
        suppressed[i]=(int16_t)(noise()/128);
    }
    /* Linear voiced structure alone also describes the recorded echo.
     * With NLP voiced corroboration, its own VAD/shape may support a weak
     * NLP spectrum. Neither channel may borrow the other's VAD. */
    assert(!p4_voice_duplex_strong(suppressed,linear,false,true,128));
    assert(p4_voice_duplex_structure(suppressed,linear,false,true));
    assert(!p4_voice_duplex_structure(suppressed,linear,false,false));
    assert(p4_voice_duplex_strong(suppressed,linear,true,true,128));
    assert(!p4_voice_duplex_strong(suppressed,linear,true,false,128));
    assert(!p4_voice_duplex_strong(suppressed,linear,false,false,128));
    for(unsigned i=0;i<512;++i)suppressed[i]=0;
    assert(!p4_voice_duplex_strong(suppressed,linear,false,true,0));
    for(unsigned i=0;i<512;++i)suppressed[i]=(int16_t)(6000*sin(6.283185307179586*125*i/16000.0));
    assert(!p4_voice_duplex_strong(suppressed,linear,false,true,3800));
    for(unsigned i=0;i<512;++i)suppressed[i]=(i/32)%2?300:-300;
    assert(p4_voice_duplex_strong(suppressed,linear,true,false,300));
    assert(p4_voice_duplex_strong(suppressed,linear,true,false,100));
    assert(p4_voice_duplex_structure(suppressed,linear,true,false));
    assert(!p4_voice_duplex_strong(suppressed,linear,false,false,300));
    /* Aperiodic, VAD-corroborated residual power follows the background,
     * foreground and dispersion, at multiple PCM scales. These synthetic
     * VAD inputs check the evidence contract, not actual VAD accuracy. */
    for(unsigned scale=1;scale<=8;scale*=2){
        uint32_t state=12345;
        for(unsigned i=0;i<512;++i){
            state=state*1664525u+1013904223u;
            pcm[i]=(int16_t)(((int)(state>>16)-32768)/32*(int)scale);
        }
        uint32_t e=p4_energy_mean_square(pcm,512);
        assert(!p4_voice_periodic(pcm));
        assert(p4_voice_above_low_band(pcm) && p4_voice_low_component(pcm));
        assert(!p4_voice_high_tone(pcm));
        assert(!p4_voice_duplex_structure(pcm,pcm,true,true));
        /* The aperiodic path cannot promote heavily suppressed echo just
         * because the room is quiet. Compare simultaneous stage powers at
         * several scales; retained speech and the periodic route survive. */
        for(unsigned i=0;i<512;++i){linear[i]=pcm[i];suppressed[i]=pcm[i]/2;}
        assert(!p4_voice_nlp_power_retained(suppressed,linear));
        assert(!p4_voice_duplex_evidence(suppressed,linear,true,true,0,true));
        for(unsigned i=0;i<512;++i)suppressed[i]=(int16_t)(pcm[i]*3/4);
        assert(p4_voice_nlp_power_retained(suppressed,linear));
        assert(p4_voice_duplex_evidence(suppressed,linear,true,true,0,true));
        for(unsigned av=0;av<2;++av)for(unsigned lv=0;lv<2;++lv){
            bool admit=p4_energy_barge_admit(e,e/16,e*2,0,true);
            assert(admit);
            assert(p4_voice_duplex_evidence(pcm,pcm,av,lv,0,admit)==(av&&lv));
            /* The same utterance fails when background or MAD upper grows.
             * Equality and missing calibration remain fail closed. */
            assert(!p4_energy_barge_admit(e,e/2,e*2,0,true));
            assert(!p4_energy_barge_admit(e,e/16,e*2,e,true));
            assert(!p4_energy_barge_admit(e,e/16,e*2,0,false));
            assert(!p4_voice_duplex_evidence(pcm,pcm,av,lv,0,false));
        }
    }
    for(unsigned i=0;i<512;++i)pcm[i]=linear[i]=0;
    assert(!p4_voice_nlp_power_retained(pcm,linear));
    for(unsigned i=0;i<512;++i){pcm[i]=(i&1)?INT16_MIN:INT16_MAX;linear[i]=pcm[i];}
    assert(p4_voice_nlp_power_retained(pcm,linear));
    for(unsigned i=0;i<512;++i)pcm[i]/=2;
    assert(!p4_voice_nlp_power_retained(pcm,linear));

    int16_t reference[1536]={0};
    assert(p4_voice_reference_gap(reference,4,144071));
    for(unsigned i=0;i<512;++i)reference[i]=100;
    assert(!p4_voice_reference_gap(reference,4,144071));
    assert(p4_voice_reference_gap(reference,4,170000));
    assert(!p4_voice_reference_gap(reference,4,160000));
    assert(!p4_voice_reference_gap(reference,0,170000));
    for(unsigned i=0;i<512;++i){
        pcm[i]=(int16_t)(800*sin(0.104*i));
        linear[i]=4*pcm[i];suppressed[i]=pcm[i]/80;
    }
    assert(p4_voice_reference_gap_shape(pcm,linear,suppressed));
    for(unsigned i=0;i<512;++i)suppressed[i]=-suppressed[i];
    assert(!p4_voice_reference_gap_shape(pcm,linear,suppressed));
    for(unsigned i=0;i<512;++i)suppressed[i]=(i&1)?10:-10;
    assert(!p4_voice_reference_gap_shape(pcm,linear,suppressed));
    puts("PASS voiced evidence, same-channel matrix, relative NLP preservation and bounds");
}
