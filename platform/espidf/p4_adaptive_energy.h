#ifndef P4_ADAPTIVE_ENERGY_H
#define P4_ADAPTIVE_ENERGY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Power ratios, not PCM amplitudes. Keep calibration choices together.
 * These defaults require physical calibration; they do not measure distance. */
enum {
    P4_ENERGY_WINDOW=64,
    P4_ENERGY_CANDIDATE_REFERENCE=8,
    P4_ENERGY_CANDIDATE_RECENT=4,
    /* Keep a pre-trigger candidate long enough to observe a second measured
     * change point.  This is a bounded time window, not an audio-level gate. */
    P4_ENERGY_CANDIDATE_HOLD_SAMPLES=32000,
    /* After a late first change point, keep a bounded second-change window
     * long enough to cover a remote phrase followed by a near-end utterance.
     * This is a sample-time bound, not a PCM amplitude threshold. */
    P4_ENERGY_CANDIDATE_SUSPECT_HOLD_SAMPLES=96000,
    /* A second change point must exceed the measured first-step peak by
     * 25 percent.  This remains scale-free: the reference is learned from
     * the current candidate window rather than an absolute PCM threshold. */
    P4_ENERGY_CANDIDATE_PEAK_RATIO_NUM=2,
    P4_ENERGY_CANDIDATE_PEAK_RATIO_DEN=1,
    P4_ENERGY_CANDIDATE_CHANGE_MIN_SAMPLES=3200,
    P4_ENERGY_ONSET_NOISE_NUM=4, P4_ENERGY_ONSET_NOISE_DEN=1,
    P4_ENERGY_ONSET_FOREGROUND_NUM=1, P4_ENERGY_ONSET_FOREGROUND_DEN=16,
    P4_ENERGY_CONTINUE_NOISE_NUM=9, P4_ENERGY_CONTINUE_NOISE_DEN=4,
    P4_ENERGY_CONTINUE_FOREGROUND_NUM=1, P4_ENERGY_CONTINUE_FOREGROUND_DEN=64,
    P4_ENERGY_ONSET_MAD_MULTIPLIER=3, P4_ENERGY_CONTINUE_MAD_MULTIPLIER=2,
    P4_ENERGY_ONSET_VARIABILITY_MULTIPLIER=8,
    P4_ENERGY_ONSET_UPPER_GUARD_NUM=9, P4_ENERGY_ONSET_UPPER_GUARD_DEN=8,
    P4_ENERGY_ONSET_FOREGROUND_DEN_MIN=6,
    P4_ENERGY_ONSET_FOREGROUND_DEN_STABLE=3,
    /* Dimensionless robust coefficient-of-variation boundary.  A measured
     * MAD no larger than one fourth of the median is a stable floor.  The
     * floor must also be material relative to the measured foreground; this
     * prevents a quiet near-field utterance with a similarly low CV from
     * being treated as remote noise. */
    P4_ENERGY_ONSET_STABLE_MAD_NUM=1,
    P4_ENERGY_ONSET_STABLE_MAD_DEN=4,
    P4_ENERGY_ONSET_STABLE_FLOOR_DEN=128
};
typedef struct {
    uint32_t noise,foreground,turn_foreground,last_cursor;
    uint32_t noise_median,noise_mad,noise_upper,continuation_upper;
    uint32_t background[P4_ENERGY_WINDOW],turn[P4_ENERGY_WINDOW],candidate[10];
    uint32_t candidate_reference[P4_ENERGY_CANDIDATE_REFERENCE],candidate_recent[P4_ENERGY_CANDIDATE_RECENT],candidate_started,candidate_change_cursor,candidate_rise_peak;
    unsigned background_count,background_next,turn_count,turn_next,candidate_next;
    unsigned candidate_reference_count,candidate_reference_next,candidate_recent_count,candidate_recent_next;
    bool candidate_pending,candidate_confirmed,candidate_seen,candidate_pretrigger,candidate_rise_seen;
    uint32_t energy,threshold,decision_noise,decision_foreground;
    uint32_t observed_frames,admitted_frames,rejected_frames;
    bool cursor_valid,turn_open,prior_valid,dispersion_valid;
} p4_adaptive_energy_t;

static inline uint32_t p4_energy_mean_square(const int16_t *pcm,size_t count)
{
    uint64_t sum=0;
    for(size_t i=0;i<count;++i){int32_t v=pcm[i];sum+=(uint64_t)((int64_t)v*v);}
    return count?(uint32_t)(sum/count):0;
}
static inline void p4_energy_sort(uint32_t *values,unsigned count)
{
    for(unsigned i=0;i<count;++i){
        uint32_t value=values[i];unsigned j=i;
        while(j && values[j-1]>value){values[j]=values[j-1];--j;}
        values[j]=value;
    }
}
static inline uint32_t p4_energy_median_sorted(const uint32_t *values,unsigned count)
{ return count?(uint32_t)(((uint64_t)values[(count-1)/2]+values[count/2])/2):0; }
static inline uint32_t p4_energy_quantile(const uint32_t *values,unsigned count,unsigned numerator,unsigned denominator)
{
    if(!count)return 0;
    uint32_t sorted[P4_ENERGY_WINDOW];
    if(count>P4_ENERGY_WINDOW)count=P4_ENERGY_WINDOW;
    memcpy(sorted,values,count*sizeof(*values));p4_energy_sort(sorted,count);
    unsigned index=(count-1)*numerator/denominator;
    return sorted[index<count?index:count-1];
}
static inline bool p4_energy_candidate_rise(const p4_adaptive_energy_t *s,uint32_t energy)
{
    unsigned count=s->candidate_reference_count;
    unsigned recent_count=s->candidate_recent_count;
    if(count<4 || recent_count<P4_ENERGY_CANDIDATE_RECENT)return false;
    uint32_t sorted[P4_ENERGY_CANDIDATE_REFERENCE],deviation[P4_ENERGY_CANDIDATE_REFERENCE];
    memcpy(sorted,s->candidate_reference,count*sizeof(*sorted));p4_energy_sort(sorted,count);
    uint32_t median=p4_energy_median_sorted(sorted,count);
    for(unsigned i=0;i<count;++i)deviation[i]=sorted[i]>median?sorted[i]-median:median-sorted[i];
    p4_energy_sort(deviation,count);
    uint32_t mad=p4_energy_median_sorted(deviation,count);
    uint32_t recent[P4_ENERGY_CANDIDATE_RECENT];
    memcpy(recent,s->candidate_recent,recent_count*sizeof(*recent));p4_energy_sort(recent,recent_count);
    uint32_t recent_median=p4_energy_median_sorted(recent,recent_count);
    /* A single remote syllable can be louder than its own local baseline.
     * Require a sustained four-frame elevation, with a robust relative floor
     * expressed only in ratios of the measured candidate distribution. */
    uint64_t robust=(uint64_t)median+(uint64_t)mad*2u;
    uint64_t relative=(uint64_t)median+median;
    uint64_t limit=robust>relative?robust:relative;
    (void)energy;
    return (uint64_t)recent_median>limit;
}
static inline uint32_t p4_energy_upper(uint32_t median,uint32_t mad,unsigned multiplier)
{
    uint64_t upper=(uint64_t)median+(uint64_t)mad*multiplier;
    return upper>UINT32_MAX?UINT32_MAX:(uint32_t)upper;
}
static inline void p4_energy_background_stats(p4_adaptive_energy_t *s)
{
    unsigned count=s->background_count;if(count<8)return;
    uint32_t values[P4_ENERGY_WINDOW];
    memcpy(values,s->background,count*sizeof(*values));p4_energy_sort(values,count);
    s->noise=values[(count-1)/4];s->noise_median=p4_energy_median_sorted(values,count);
    for(unsigned i=0;i<count;++i)values[i]=values[i]>s->noise_median?
        values[i]-s->noise_median:s->noise_median-values[i];
    p4_energy_sort(values,count);s->noise_mad=p4_energy_median_sorted(values,count);
    /* Raw power-domain MAD is a robust dispersion measure, not a Gaussian
     * sigma estimate or a probabilistic confidence bound. All terms scale
     * with signal power; there is no amplitude epsilon when MAD is zero. */
    s->noise_upper=p4_energy_upper(s->noise_median,s->noise_mad,P4_ENERGY_ONSET_MAD_MULTIPLIER);
    s->continuation_upper=p4_energy_upper(s->noise_median,s->noise_mad,P4_ENERGY_CONTINUE_MAD_MULTIPLIER);
    s->dispersion_valid=true;
}
static inline uint32_t p4_energy_ratio(uint32_t value,unsigned numerator,unsigned denominator)
{
    uint64_t result=(uint64_t)value*numerator/denominator;
    return result>UINT32_MAX?UINT32_MAX:(uint32_t)result;
}
static inline void p4_energy_wake_snapshot(const uint32_t *values,unsigned count,bool eligible,
    uint32_t *noise,uint32_t *foreground)
{
    /* Every newly accepted wake gets a new reference, including an explicit
     * invalid one. A previous loud wake must not stand in for this wake. */
    *noise=*foreground=0;
    if(!eligible || count<8)return;
    *noise=p4_energy_quantile(values,count,1,4);
    uint32_t value=p4_energy_quantile(values,count,3,4);
    if(value>*noise)*foreground=value;
}
static inline bool p4_energy_observable(bool active,bool replayed_turn,bool capture_fresh,bool own_output)
{
    /* Normal onset replays pre-roll and then paces new frames behind capture.
     * Those unseen active frames may refine F; N still needs fresh idle PCM.
     * An explicitly seeded barge/ASR replay never retrains its foreground. */
    return !own_output && (active?!replayed_turn:capture_fresh);
}
static inline uint32_t p4_energy_reference_threshold_with_den(uint32_t noise_energy,uint32_t foreground,
    bool active,unsigned onset_foreground_den)
{
    uint32_t excess=foreground>noise_energy?foreground-noise_energy:0;
    uint32_t noise=p4_energy_ratio(noise_energy,
        active?P4_ENERGY_CONTINUE_NOISE_NUM:P4_ENERGY_ONSET_NOISE_NUM,
        active?P4_ENERGY_CONTINUE_NOISE_DEN:P4_ENERGY_ONSET_NOISE_DEN);
    uint64_t anchored=(uint64_t)noise_energy+p4_energy_ratio(excess,
        active?P4_ENERGY_CONTINUE_FOREGROUND_NUM:P4_ENERGY_ONSET_FOREGROUND_NUM,
        active?P4_ENERGY_CONTINUE_FOREGROUND_DEN:onset_foreground_den);
    uint32_t anchor=anchored>UINT32_MAX?UINT32_MAX:(uint32_t)anchored;
    return noise>anchor?noise:anchor;
}
static inline uint32_t p4_energy_reference_threshold(uint32_t noise_energy,uint32_t foreground,bool active)
{
    return p4_energy_reference_threshold_with_den(noise_energy,foreground,active,
        P4_ENERGY_ONSET_FOREGROUND_DEN);
}
/* Once a real idle distribution is available, use its robust dispersion to
 * require a stronger relative foreground contrast for an inactive candidate.
 * The denominator is dimensionless and chosen from the measured state; no
 * fixed PCM level is introduced.  A zero MAD is the explicitly flat-floor
 * case and retains the normal 1/16 term so quiet speech remains admissible. */
static inline unsigned p4_energy_onset_foreground_den(const p4_adaptive_energy_t *s)
{
    unsigned den=P4_ENERGY_ONSET_FOREGROUND_DEN;
    if(s->dispersion_valid){
        uint32_t nominal=p4_energy_reference_threshold_with_den(s->noise,s->foreground,false,den);
        /* Convert robust dispersion into a dimensionless penalty.  Ceiling
         * preserves a conservative decision at integer power scales without
         * introducing a PCM-level floor. */
        uint32_t scale=s->noise_median?s->noise_median:1u;
        /* A nonzero but very small MAD is a stable measured floor.  A
         * sustained offset above that floor is then more likely a remote
         * source than a quiet local onset, so require a stronger ratio. */
        /* If the measured robust upper bound already dominates the scalar
         * onset floor by more than 12.5%, keep that bound as the decision;
         * the dispersion has already supplied the needed protection. */
        bool ratio_guard=(uint64_t)s->noise_upper*P4_ENERGY_ONSET_UPPER_GUARD_DEN <=
            (uint64_t)nominal*P4_ENERGY_ONSET_UPPER_GUARD_NUM;
        bool stable_floor=s->noise_mad &&
           (uint64_t)s->noise_mad*P4_ENERGY_ONSET_STABLE_MAD_DEN <=
           (uint64_t)scale*P4_ENERGY_ONSET_STABLE_MAD_NUM &&
           (uint64_t)scale*P4_ENERGY_ONSET_STABLE_FLOOR_DEN >= s->foreground;
        if(ratio_guard && stable_floor){
            /* Keep the strongest ratio only for a very stable measured floor.
             * A borderline stable floor still needs a usable quiet-speech
             * margin, so use the intermediate 1/4 relative foreground term. */
            bool very_stable=(uint64_t)s->noise_mad*6u <= scale;
            den=very_stable?P4_ENERGY_ONSET_FOREGROUND_DEN_STABLE:4u;
        }else if(ratio_guard && s->noise_mad){
            uint64_t numerator=(uint64_t)s->noise_mad*P4_ENERGY_ONSET_VARIABILITY_MULTIPLIER;
            unsigned spread=(unsigned)((numerator+(uint64_t)scale-1u)/scale);
            if(spread>12u)spread=12u;
            den=P4_ENERGY_ONSET_FOREGROUND_DEN/(1u+spread);
            if(den<P4_ENERGY_ONSET_FOREGROUND_DEN_MIN)den=P4_ENERGY_ONSET_FOREGROUND_DEN_MIN;
        }
    }
    return den;
}
/* A measured background floor is material when its robust median is within
 * the same dimensionless contrast range as the trusted foreground.  This is
 * deliberately expressed as a power ratio; it is not a PCM-level cutoff and
 * remains invariant when the whole capture path is rescaled. */
static inline bool p4_energy_material_background(const p4_adaptive_energy_t *s)
{
    return s->dispersion_valid && s->noise_median && s->foreground &&
        (uint64_t)s->noise_median*P4_ENERGY_ONSET_STABLE_FLOOR_DEN >= s->foreground;
}
static inline uint32_t p4_energy_threshold(const p4_adaptive_energy_t *s,bool active)
{
    uint32_t foreground=active && s->turn_foreground?s->turn_foreground:s->foreground;
    uint32_t threshold=p4_energy_reference_threshold_with_den(s->noise,foreground,active,
        active?P4_ENERGY_ONSET_FOREGROUND_DEN:p4_energy_onset_foreground_den(s));
    uint32_t upper=active?s->continuation_upper:s->noise_upper;
    return threshold>upper?threshold:upper;
}
static inline uint32_t p4_energy_barge_threshold(uint32_t noise,uint32_t foreground,uint32_t upper)
{
    /* Once a real MAD/upper bound is available, use that measured robust
     * floor instead of reapplying the conservative four-times-noise onset
     * term. The foreground contribution remains relative to the measured
     * N/F pair, so the decision is scale-free and has no PCM floor. Prior
     * wakes without dispersion keep the original conservative fallback. */
    if(upper>noise){
        uint32_t excess=foreground>noise?foreground-noise:0;
        uint64_t anchored=(uint64_t)noise+(uint64_t)excess/P4_ENERGY_ONSET_FOREGROUND_DEN;
        uint32_t threshold=anchored>UINT32_MAX?UINT32_MAX:(uint32_t)anchored;
        return threshold>upper?threshold:upper;
    }
    return p4_energy_reference_threshold(noise,foreground,false);
}
static inline bool p4_energy_barge_admit(uint32_t energy,uint32_t noise,uint32_t foreground,uint32_t upper,bool calibrated)
{ return calibrated && energy>p4_energy_barge_threshold(noise,foreground,upper); }
static inline bool p4_energy_ready(const p4_adaptive_energy_t *s)
{ return s->prior_valid || s->background_count>=8; }
static inline void p4_energy_init(p4_adaptive_energy_t *s,uint32_t noise,uint32_t foreground)
{
    memset(s,0,sizeof(*s));s->noise=noise;s->foreground=foreground;
    s->noise_median=s->noise_upper=s->continuation_upper=noise;
    /* A scalar wake reference preserves early-command admission, but is not
     * an observed background distribution. Keep its fallback statistics until
     * eight real eligible samples exist; D=0 here is not measured dispersion. */
    s->prior_valid=noise || foreground;
}
/* A provider-only ASR retry retains the same live microphone sample clock.
 * Keep the trusted reference and its sampling boundary, not the rejected
 * turn, the change-point candidate, or connection-local telemetry counts. */
static inline void p4_energy_retain_reference(p4_adaptive_energy_t *next,
    const p4_adaptive_energy_t *previous)
{
    *next=(p4_adaptive_energy_t){
        .noise=previous->noise,.foreground=previous->foreground,
        .noise_median=previous->noise_median,.noise_mad=previous->noise_mad,
        .noise_upper=previous->noise_upper,.continuation_upper=previous->continuation_upper,
        .background_count=previous->background_count,.background_next=previous->background_next,
        .last_cursor=previous->last_cursor,.cursor_valid=previous->cursor_valid,
        .prior_valid=previous->prior_valid,.dispersion_valid=previous->dispersion_valid,
    };
    memcpy(next->background,previous->background,sizeof(next->background));
}
static inline void p4_energy_turn_sample(p4_adaptive_energy_t *s,uint32_t energy)
{
    s->turn[s->turn_next++%P4_ENERGY_WINDOW]=energy;
    if(s->turn_count<P4_ENERGY_WINDOW)++s->turn_count;
    /* A bounded median ignores one loud syllable. The active threshold may
     * follow quieter speech without changing the next turn's trusted anchor. */
    if(s->turn_count && (s->turn_count<8 || !(s->turn_next%8)))
        s->turn_foreground=p4_energy_quantile(s->turn,s->turn_count,1,2);
}
/* The caller supplies capture eligibility and ownership, never upload time.
 * Replayed/duplicate frames still get the same gate decision but cannot
 * retrain either reference. Candidate/active/committed speech never trains N. */
static inline bool p4_energy_frame(p4_adaptive_energy_t *s,uint32_t cursor,uint32_t energy,
    bool vad,bool active,bool candidate,bool candidate_eligible,bool learn_noise,bool observable,bool own_audio)
{
    s->energy=energy;s->threshold=p4_energy_threshold(s,active);
    s->decision_noise=s->noise;
    s->decision_foreground=active && s->turn_foreground?s->turn_foreground:s->foreground;
    bool admitted=!own_audio && vad && p4_energy_ready(s) && energy>s->threshold;
    bool unseen=!s->cursor_valid || (int32_t)(cursor-s->last_cursor)>0;
    if(!unseen)return admitted;
    s->last_cursor=cursor;s->cursor_valid=true;
    if(!active){
        /* A unique delayed frame can contribute a real local onset vote.
         * Keep its energy with that same vote, independently of the stricter
         * fresh-idle requirement for learning background. Ownership, blocked
         * starts and explicit replay still exclude candidate collection. */
        if(!candidate_eligible || own_audio){
            memset(s->candidate,0,sizeof(s->candidate));s->candidate_next=0;
            memset(s->candidate_reference,0,sizeof(s->candidate_reference));
            memset(s->candidate_recent,0,sizeof(s->candidate_recent));
            s->candidate_reference_count=s->candidate_reference_next=0;
            s->candidate_recent_count=s->candidate_recent_next=0;
            s->candidate_pending=s->candidate_confirmed=s->candidate_seen=false;
            s->candidate_pretrigger=s->candidate_rise_seen=false;
            s->candidate_started=s->candidate_change_cursor=s->candidate_rise_peak=0;
        }else {
            bool relative_pretrigger=s->dispersion_valid && s->noise_mad && !own_audio && vad &&
                energy>s->noise_upper && energy<=s->threshold;
            /* A remote voice can clear the ordinary six-vote gate before the
             * near-end speaker starts.  Keep that first candidate local to a
             * bounded, scale-free change-point check: the reference is its
             * own median/MAD, and only a second relative rise releases input.
             * A timeout preserves ordinary speech when no second rise exists. */
            /* The change-point hold is only meaningful when the measured
             * idle floor has real robust dispersion.  A flat synthetic floor
             * has no relative change to estimate and keeps the ordinary gate
             * semantics used by quiet speech and deterministic host tests. */
            if(s->dispersion_valid && s->noise_mad){
                if(!s->candidate_pending && !s->candidate_confirmed && (admitted || relative_pretrigger)){
                    s->candidate_pending=true;s->candidate_started=cursor;
                    s->candidate_pretrigger=relative_pretrigger;
                    s->candidate_rise_seen=false;
                    s->candidate_rise_peak=0;
                    s->candidate_change_cursor=0;
                    s->candidate_reference_count=s->candidate_reference_next=0;
                    s->candidate_recent_count=s->candidate_recent_next=0;
                    s->candidate_reference[s->candidate_reference_next++]=energy;
                    s->candidate_reference_count=1;
                    s->candidate_recent[s->candidate_recent_next++%P4_ENERGY_CANDIDATE_RECENT]=energy;
                    s->candidate_recent_count=1;
                    if(relative_pretrigger)s->candidate_seen=true;
                }
                if(s->candidate_pending && candidate)s->candidate_seen=true;
                /* The first admitted frame is also the first vote that turns
                 * the local-turn candidate on.  Keep that seed frame visible;
                 * only subsequent candidate frames are held while the
                 * relative change point is being checked. */
                if(s->candidate_pending && s->candidate_seen){
                    if(s->candidate_reference_count<P4_ENERGY_CANDIDATE_REFERENCE){
                        s->candidate_reference[s->candidate_reference_next++]=energy;
                        ++s->candidate_reference_count;
                    }
                    s->candidate_recent[s->candidate_recent_next++%P4_ENERGY_CANDIDATE_RECENT]=energy;
                    if(s->candidate_recent_count<P4_ENERGY_CANDIDATE_RECENT)++s->candidate_recent_count;
                    bool rise=p4_energy_candidate_rise(s,energy);
                    if(rise && s->candidate_pretrigger && !s->candidate_rise_seen &&
                       (uint32_t)(cursor-s->candidate_started)>=P4_ENERGY_CANDIDATE_CHANGE_MIN_SAMPLES){
                        /* The first sustained rise may be the remote source
                         * itself. Rebase the measured candidate distribution
                         * at that change point and wait for a second relative
                         * rise, which is the only scale-free evidence of a
                         * later near-end onset available to one microphone. */
                        unsigned recent_count=s->candidate_recent_count;
                        if(recent_count>P4_ENERGY_CANDIDATE_REFERENCE)
                            recent_count=P4_ENERGY_CANDIDATE_REFERENCE;
                        uint32_t peak=0;
                        for(unsigned i=0;i<recent_count;++i)
                            if(s->candidate_recent[i]>peak)peak=s->candidate_recent[i];
                        s->candidate_rise_peak=peak;
                        memcpy(s->candidate_reference,s->candidate_recent,
                            recent_count*sizeof(*s->candidate_reference));
                        s->candidate_reference_count=s->candidate_reference_next=recent_count;
                        s->candidate_recent_count=s->candidate_recent_next=0;
                        s->candidate_recent[s->candidate_recent_next++]=energy;
                        s->candidate_recent_count=1;
                        s->candidate_rise_seen=true;
                        /* Start the bounded second-change window at the
                         * first suspected remote step, not at the original
                         * pre-trigger. */
                        s->candidate_started=cursor;
                        admitted=false;
                    }else if(rise){
                        uint32_t peak=0;
                        for(unsigned i=0;i<s->candidate_recent_count;++i)
                            if(s->candidate_recent[i]>peak)peak=s->candidate_recent[i];
                        bool second_change=!s->candidate_pretrigger ||
                            !s->candidate_rise_seen ||
                            ((uint64_t)peak*P4_ENERGY_CANDIDATE_PEAK_RATIO_DEN >=
                             (uint64_t)s->candidate_rise_peak*P4_ENERGY_CANDIDATE_PEAK_RATIO_NUM);
                        if(second_change){
                            s->candidate_pending=false;s->candidate_confirmed=true;s->candidate_change_cursor=cursor;
                            /* A relative rise is allowed to open a quiet turn even
                             * when the scalar wake threshold still trails it. */
                            admitted=true;
                        }else{
                            /* A smaller second step is another measured
                             * background change. Keep the largest observed
                             * peak as the scale-free reference and rebase the
                             * robust distribution for a later near-end rise. */
                            if(peak>s->candidate_rise_peak)s->candidate_rise_peak=peak;
                            unsigned next_count=s->candidate_recent_count;
                            if(next_count>P4_ENERGY_CANDIDATE_REFERENCE)
                                next_count=P4_ENERGY_CANDIDATE_REFERENCE;
                            memcpy(s->candidate_reference,s->candidate_recent,
                                next_count*sizeof(*s->candidate_reference));
                            s->candidate_reference_count=s->candidate_reference_next=next_count;
                            s->candidate_recent_count=s->candidate_recent_next=0;
                            s->candidate_recent[s->candidate_recent_next++]=energy;
                            s->candidate_recent_count=1;
                            s->candidate_started=cursor;
                            admitted=false;
                        }
                    }else if((uint32_t)(cursor-s->candidate_started)>=
                             (s->candidate_pretrigger && s->candidate_rise_seen?
                              P4_ENERGY_CANDIDATE_SUSPECT_HOLD_SAMPLES:
                              P4_ENERGY_CANDIDATE_HOLD_SAMPLES)){
                        /* A pre-trigger candidate without a second measured
                         * change is ambiguous. Fail closed instead of
                         * uploading a sustained remote voice; ordinary local
                         * candidates retain the historical bounded fallback. */
                        if(s->candidate_pretrigger){
                            s->candidate_pending=false;s->candidate_seen=false;
                            s->candidate_pretrigger=s->candidate_rise_seen=false;
                            s->candidate_started=s->candidate_change_cursor=s->candidate_rise_peak=0;
                            s->candidate_reference_count=s->candidate_reference_next=0;
                            s->candidate_recent_count=s->candidate_recent_next=0;
                        }else{
                            s->candidate_pending=false;s->candidate_confirmed=true;s->candidate_change_cursor=cursor;
                        }
                    }else admitted=false;
                }
            }
            s->candidate[s->candidate_next++%10]=admitted?energy:0;
        }
    }
    if(!observable || own_audio)return admitted;
    ++s->observed_frames;
    if(admitted)++s->admitted_frames;
    else if(vad)++s->rejected_frames;
    if(active && admitted && s->turn_open)p4_energy_turn_sample(s,energy);
    uint64_t foreground_limit=(uint64_t)s->noise+p4_energy_ratio(s->foreground>s->noise?s->foreground-s->noise:0,
        P4_ENERGY_ONSET_FOREGROUND_NUM,p4_energy_onset_foreground_den(s));
    bool possible_foreground=vad && s->foreground>s->noise && (uint64_t)energy>foreground_limit;
    if(learn_noise && !active && !candidate && !admitted && !possible_foreground){
        /* Rejected weak speech belongs to the background distribution, not
         * the trusted foreground. A lower quartile resists isolated impulses. */
        s->background[s->background_next++%P4_ENERGY_WINDOW]=energy;
        if(s->background_count<P4_ENERGY_WINDOW)++s->background_count;
        if(s->background_count>=8 && !(s->background_next%8))p4_energy_background_stats(s);
    }
    return admitted;
}
static inline bool p4_energy_hint(const p4_adaptive_energy_t *s,uint32_t energy,bool vad,bool own_audio)
{ return !own_audio && vad && energy>p4_energy_ratio(s->noise,5,4); }
static inline void p4_energy_turn_begin(p4_adaptive_energy_t *s)
{
    s->turn_count=s->turn_next=0;s->turn_foreground=0;s->turn_open=true;
    for(unsigned i=0;i<10;++i)if(s->candidate[i])p4_energy_turn_sample(s,s->candidate[i]);
    memset(s->candidate,0,sizeof(s->candidate));s->candidate_next=0;
    memset(s->candidate_reference,0,sizeof(s->candidate_reference));
    memset(s->candidate_recent,0,sizeof(s->candidate_recent));
    s->candidate_reference_count=s->candidate_reference_next=0;
    s->candidate_recent_count=s->candidate_recent_next=0;
    s->candidate_pending=s->candidate_confirmed=s->candidate_seen=false;
    s->candidate_pretrigger=s->candidate_rise_seen=false;
    s->candidate_started=s->candidate_change_cursor=s->candidate_rise_peak=0;
}
static inline void p4_energy_turn_resolve(p4_adaptive_energy_t *s,bool accepted)
{
    /* Only an already trusted wake/foreground anchor can be refined. Empty
     * ASR and the first unanchored soft transcript cannot establish one. */
    if(accepted && s->foreground && s->turn_open && s->turn_count>=6){
        uint32_t value=p4_energy_quantile(s->turn,s->turn_count,1,2);
        if(value>p4_energy_ratio(s->noise,P4_ENERGY_ONSET_NOISE_NUM,P4_ENERGY_ONSET_NOISE_DEN))
            s->foreground=(uint32_t)(((uint64_t)s->foreground*3+value)/4);
    }
    s->turn_open=false;s->turn_count=s->turn_next=0;s->turn_foreground=0;
    memset(s->candidate,0,sizeof(s->candidate));s->candidate_next=0;
    memset(s->candidate_reference,0,sizeof(s->candidate_reference));
    memset(s->candidate_recent,0,sizeof(s->candidate_recent));
    s->candidate_reference_count=s->candidate_reference_next=0;
    s->candidate_recent_count=s->candidate_recent_next=0;
    s->candidate_pending=s->candidate_confirmed=s->candidate_seen=false;
    s->candidate_pretrigger=s->candidate_rise_seen=false;
    s->candidate_started=s->candidate_change_cursor=s->candidate_rise_peak=0;
}
#endif
