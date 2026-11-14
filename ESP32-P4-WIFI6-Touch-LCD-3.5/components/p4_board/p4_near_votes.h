#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A DAC syllable gap or the output owner's stop must not promote its echo
 * tail to the three-vote idle rule. Keep the same 640 ms acoustic history
 * already used by capture and background learning. This changes evidence
 * requirements only; it neither mutes input nor prevents a confirmed cut. */
typedef struct { uint32_t last_output_ms; bool valid; } p4_near_output_tail_t;
static inline bool p4_near_output_duplex(p4_near_output_tail_t *s,
    uint32_t now_ms,bool output_active,bool reference_active)
{
    if(output_active || reference_active){s->last_output_ms=now_ms;s->valid=true;return true;}
    return s->valid && now_ms-s->last_output_ms<640u;
}

/* NLP can erase the spectrum used by VAD even when independent linear
 * speech survives. Both paths require the calibrated adaptive power gate;
 * correlation, spectral evidence and sustained votes still follow. */
static inline bool p4_near_speech(bool nlp_voice,bool linear_voice,bool adaptive_admission)
{ return adaptive_admission && (nlp_voice || linear_voice); }

/* Keep the established 384 ms fast path. A recorded natural utterance had
 * six independent strong frames spread across 512 ms, but never six within
 * 384 ms. The longer path requires four NLP-strong frames, not two. Neither
 * path changes VAD, energy or correlation admission for an individual frame.
 * Two isolated voiced echo remnants 285 ms apart caused a recorded false
 * cut. Require a pair of strong frames at most 64 ms apart within the
 * existing 512 ms history. This retains syllable gaps without treating
 * unrelated isolated periodic residues as sustained near-end speech.
 * Recorded /shi-er/ echo also produced one adjacent pair. A pair (64 ms)
 * alone cannot supply the fast-path quorum: require three strong frames,
 * alongside six eligible frames in the same unchanged 384 ms window.
 * The independent four-strong-frame, 512 ms syllable-gap path is unchanged. */
static inline bool p4_near_duplex_confirm(bool eligible,unsigned votes,unsigned strong)
{
    strong&=0xffffu;
    bool sustained=(strong&((strong>>1)|(strong>>2)))!=0;
    return eligible && sustained &&
        ((__builtin_popcount(votes&0xfffu)>=6 && __builtin_popcount(strong&0xfffu)>=3) ||
         (__builtin_popcount(votes&0xffffu)>=6 && __builtin_popcount(strong&0xffffu)>=4));
}

/* Aperiodic residuals can pass both VADs and the dynamic power check for a
 * whole echo burst. Require voiced corroboration somewhere in the SAME
 * confirmation window, while retaining unvoiced syllables as strong votes.
 * A corroborating frame need not itself be strong: its own channel VAD and
 * spectrum, adaptive admission and echo rejection have already been checked.
 * Existing votes, durations and replay onset remain unchanged. Fully unvoiced
 * interruptions may lack this evidence; no whisper recognition is promised. */
static inline bool p4_near_duplex_corroborated(bool eligible,unsigned votes,
    unsigned strong,unsigned structure)
{
    return p4_near_duplex_confirm(eligible,votes,strong) &&
        ((__builtin_popcount(votes&0xfffu)>=6 && __builtin_popcount(strong&0xfffu)>=3 && (structure&0xfffu)) ||
         (__builtin_popcount(votes&0xffffu)>=6 && __builtin_popcount(strong&0xffffu)>=4 && (structure&0xffffu)));
}

/* Count a short relative-only gap as ordinary evidence only when independent
 * strong frames directly enclose it. Keep the fast window's six ordinary and
 * three strong votes, and require those three strong votes to be independent
 * in this path. Do not retain these provisional votes after this decision. */
static inline bool p4_near_duplex_close_gap(unsigned votes,unsigned strong,unsigned structure,
    unsigned independent,unsigned provisional)
{
    const unsigned fast=0xfffu;
    independent&=strong&fast;
    if(!(independent&1u) || __builtin_popcount(independent)<3)return false;
    unsigned gap=0;
    for(unsigned prior=2u;prior<=8u;prior<<=1){
        gap|=prior;
        if((provisional&gap)==gap && (independent&(prior<<1)))
            return p4_near_duplex_corroborated(true,(votes|gap)&fast,independent,structure&fast);
    }
    return false;
}

/* The two current anchors are admitted independently on their own frames.
 * Pitch-linked provisional edges can supply votes only around that pair,
 * and only for this decision. Use the existing fast quorum unchanged. */
static inline bool p4_near_duplex_pitch_pair(unsigned votes,unsigned strong,unsigned structure,
    unsigned independent,unsigned provisional,unsigned linked_pitch)
{
    const unsigned anchors=0x6u,edges=0x9u,window=0xfffu;
    if((independent&strong&anchors)!=anchors || (provisional&edges)!=edges ||
       (linked_pitch&edges)!=edges)return false;
    return p4_near_duplex_corroborated(true,(votes|edges)&window,
        (strong|edges)&window,structure&window);
}

/* A short run of independently powered broadband residue can reinforce an
 * ordinary voiced decision in the same window. Require four original votes,
 * including the current original strong anchor, and never carry the added
 * votes forward as established speech. Reuse both standard quorums. */
static inline unsigned p4_near_duplex_residual_burst(unsigned votes,unsigned strong,
    unsigned structure,unsigned residual)
{
    if(!(votes&strong&structure&1u))return 0;
    unsigned burst=residual&(residual>>1)&(residual>>2);
    if(!burst)return 0;
    burst|=(burst<<1)|(burst<<2);
    const unsigned fast=0xfffu,wide=0xffffu;
    if(((burst&fast)&((burst&fast)>>1)&((burst&fast)>>2)) &&
       __builtin_popcount(votes&fast)>=4 &&
       p4_near_duplex_corroborated(true,(votes|burst)&fast,(strong|burst)&fast,structure&fast))
        return burst&fast;
    if(__builtin_popcount(votes&wide)>=4 &&
       p4_near_duplex_corroborated(true,(votes|burst)&wide,(strong|burst)&wide,structure&wide))
        return burst&wide;
    return 0;
}

/* Reference-gap evidence supplements only existing eligible frames. Keep
 * the usual sustained pair and same-window six ordinary / three or four
 * strong votes. Never let the added evidence affect ordinary admission. */
static inline unsigned p4_near_duplex_reference_gap(bool eligible,unsigned votes,
    unsigned strong,unsigned structure,unsigned gap_shape)
{
    gap_shape&=votes&0xffffu;
    if(!(gap_shape&1u))return 0;
    return p4_near_duplex_corroborated(eligible,votes,strong|gap_shape,structure|gap_shape)?gap_shape:0;
}

/* Two adjacent, independently admitted voiced frames can seed one
 * confirmation across the immediately following four coherent residuals.
 * The residuals must have passed both reference correlation checks, VAD
 * and adaptive power, and must have only the relative transfer veto.
 * Credit them for this decision only and reuse the normal six-vote quorum. */
static inline unsigned p4_near_duplex_residual_continuation(unsigned votes,
    unsigned strong,unsigned structure,unsigned independent,unsigned coherent)
{
    const unsigned anchors=0x30u,tail=0x0fu,voiced_tail=0x0cu;
    if((votes&strong&structure&independent&anchors)!=anchors || (coherent&tail)!=tail)return 0;
    return p4_near_duplex_corroborated(true,votes|tail,strong|voiced_tail,structure)?tail:0;
}
