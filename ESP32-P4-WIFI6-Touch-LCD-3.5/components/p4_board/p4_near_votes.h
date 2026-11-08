#pragma once
#include <stdbool.h>

/* NLP can erase the spectrum used by VAD even when strong near speech
 * survives. The second VAD observes the linear residual; it may bridge that
 * loss only with the existing strong NLP energy, before independent echo
 * rejection and the multi-frame confirmation. */
static inline bool p4_near_speech(bool nlp_voice,bool linear_voice,unsigned nlp_level)
{ return nlp_voice || (linear_voice && nlp_level>620u); }

/* Keep the established 384 ms fast path. A recorded natural utterance had
 * six independent strong frames spread across 512 ms, but never six within
 * 384 ms. The longer path requires four NLP-strong frames, not two. Neither
 * path changes VAD, energy or correlation admission for an individual frame.
 * Two isolated voiced echo remnants 285 ms apart caused a recorded false
 * cut. Require a pair of strong frames at most 64 ms apart within the
 * existing 512 ms history. This retains syllable gaps without treating
 * unrelated isolated periodic residues as sustained near-end speech. */
static inline bool p4_near_duplex_confirm(bool eligible,unsigned votes,unsigned strong)
{
    strong&=0xffffu;
    bool sustained=(strong&((strong>>1)|(strong>>2)))!=0;
    return eligible && sustained &&
        ((__builtin_popcount(votes&0xfffu)>=6 && __builtin_popcount(strong&0xfffu)>=2) ||
         (__builtin_popcount(votes&0xffffu)>=6 && __builtin_popcount(strong&0xffffu)>=4));
}
