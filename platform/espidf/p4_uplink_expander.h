#ifndef P4_UPLINK_EXPANDER_H
#define P4_UPLINK_EXPANDER_H
#include "p4_adaptive_energy.h"

/* Continuous, scale-independent post-AGC attenuation. Admission still belongs
 * to the existing N/F/MAD and VAD logic. Never discard samples or change their
 * capture timestamps. These power ratios need acoustic qualification; they do
 * not identify a speaker or measure distance. */
typedef struct {
    uint32_t upper,background,foreground,weight_q15,gain_q15,min_gain_q15,last_cursor,onset_cursor;
    uint32_t frames,attenuated_frames;
    bool enabled,robust_contrast,cursor_valid,preserve_speech;
} p4_uplink_expander_t;

static inline void p4_uplink_expander_begin(p4_uplink_expander_t *s,
    const p4_adaptive_energy_t *energy)
{
    *s=(p4_uplink_expander_t){.gain_q15=32768,.min_gain_q15=32768};
    /* Seeded interruption/ASR replay has no fresh six-frame candidate. A wake
     * anchor must not stand in for its quieter current speaker. Likewise a
     * scalar prior is not a measured background distribution. Bypass safely. */
    if(!energy->dispersion_valid || energy->turn_count<6)return;
    s->preserve_speech=energy->turn_quorum;
    s->upper=energy->noise_upper;
    s->foreground=p4_energy_quantile(energy->turn,energy->turn_count,3,4);
    s->background=p4_energy_quantile(energy->turn,energy->turn_count,1,2);
    if(s->foreground<=s->upper)return;
    /* When the measured idle floor is material relative to the wake anchor,
     * the candidate quartile is contaminated by the same remote source that
     * must be suppressed after the local onset.  Retain the trusted wake
     * foreground as the reference and use a steeper dimensionless power
     * curve.  MAD/median and the foreground ratio decide this branch; no
     * absolute PCM amplitude is introduced. */
    s->robust_contrast=!s->preserve_speech && p4_energy_material_background(energy) &&
        p4_energy_onset_foreground_den(energy)<=P4_ENERGY_ONSET_FOREGROUND_DEN_MIN;
    if(s->robust_contrast && energy->foreground>s->foreground)
        s->foreground=energy->foreground;
    /* A mixed candidate can begin with a remote voice before the local onset.
     * Its robust candidate median is a measured, scale-free background proxy.
     * Use it only when it lies between the MAD upper bound and the trusted
     * foreground anchor; otherwise the idle upper bound remains the floor. */
    if(!s->robust_contrast || s->background<=s->upper || s->background>=s->foreground)
        s->background=s->upper;
    /* Soft contrast weight: no attenuation at <=3 dB above robust background
     * upper, full weighting at >=6 dB. This is not a probability/confidence
     * estimate. Lower separation prioritizes preservation of quieter speech. */
    uint64_t low=(uint64_t)s->upper*2,high=(uint64_t)s->upper*4;
    if(s->foreground<=low)return;
    s->weight_q15=s->foreground>=high?32768:
        (uint32_t)(((uint64_t)s->foreground-low)*32768/(high-low));
    s->enabled=true;
    s->gain_q15=32768-s->weight_q15+(s->weight_q15*128u)/32768;
    s->min_gain_q15=s->gain_q15;
}

static inline void p4_uplink_expander_apply(p4_uplink_expander_t *s,int16_t *pcm,
    size_t count,uint32_t energy,uint32_t cursor)
{
    /* Transport BUSY retains the already conditioned buffer. Re-entering the
     * same frame must not attenuate it twice or advance release time. */
    if(!count || (s->cursor_valid && (int32_t)(cursor-s->last_cursor)<=0))return;
    s->last_cursor=cursor;s->cursor_valid=true;++s->frames;
    if(!s->enabled)return;
    /* Retained pre-onset audio can contain the last words of a remote source.
     * Once robust contrast is available, suppress that prefix immediately;
     * the normal release is still useful after the confirmed local onset. */
    bool pre_onset=s->robust_contrast && s->onset_cursor &&
        (int32_t)(cursor-s->onset_cursor)<=0;
    uint32_t floor=s->robust_contrast?s->background:s->upper;
    uint32_t ratio=energy<=floor?0:energy>=s->foreground?32768:
        (uint32_t)(((uint64_t)energy-floor)*32768/(s->foreground-floor));
    uint32_t expanded;
    if(s->preserve_speech){
        /* Independently confirmed near speech must not use its own vowel
         * median as background. Attenuate relative to the measured idle MAD
         * upper bound, preserving quieter consonants above that bound. */
        expanded=energy<=floor?0:(uint32_t)(((uint64_t)energy-floor)*32768u/energy);
    }else if(s->robust_contrast){
        uint64_t square=(uint64_t)ratio*ratio/32768u;
        expanded=(uint32_t)(square*square/32768u);
    }else expanded=(ratio*ratio)/32768;
    if(expanded<128)expanded=128; /* Dimensionless -48 dB gain floor. */
    uint32_t goal=32768-s->weight_q15+(s->weight_q15*expanded)/32768;
    /* In a material-floor turn, samples before the measured local onset are
     * the retained remote candidate by construction.  Do not let a high
     * remote syllable reopen the gain during that prefix; the actual local
     * onset starts at onset_cursor and is handled by the dynamic residual
     * curve below. */
    /* A frame already classified as retained pre-onset audio is the measured
     * remote/background prefix.  Keep only a numerical residue so provider
     * AGC cannot reconstruct that prefix; this is a dimensionless gain, not
     * an input-level or PCM-amplitude threshold. */
    if(pre_onset)goal=1;
    /* 80 ms release at 20 ms frames; immediate attack with a sample ramp. */
    uint32_t released=pre_onset?0u:(s->gain_q15*25520u)/32768;
    uint32_t gain=goal>released?goal:released;
    if(gain<s->min_gain_q15)s->min_gain_q15=gain;
    if(gain<32768)++s->attenuated_frames;
    uint32_t ramp_start=pre_onset?goal:s->gain_q15;
    for(size_t i=0;i<count;++i){
        int32_t ramp=(int32_t)ramp_start+
            (int32_t)(((int64_t)gain-ramp_start)*(int64_t)i/(int64_t)count);
        int64_t value=(int64_t)pcm[i]*ramp;
        pcm[i]=(int16_t)((value+(value>=0?16384:-16384))/32768);
    }
    s->gain_q15=gain;
}
#endif
