#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "p4_near_onset.h"

/* During device output, saturated PCM cannot supply independent near-end
 * evidence. These are the representation's endpoints, not a speech-level
 * floor. All three arrays must describe the same 512-sample capture period:
 * delayed raw microphone plus the current NLP and linear AEC results.
 * Keep the samples and alignment history; discard only the invalid voting
 * and replay-onset history. A later clean utterance starts fresh. */
static inline bool p4_near_saturation_guard(bool duplex,
    const int16_t mic[512],const int16_t nlp[512],const int16_t linear[512],
    unsigned *votes,unsigned *strong,p4_near_onset_t *onset,p4_near_onset_t *hint)
{
    if(!duplex)return false;
    for(unsigned i=0;i<512;++i){
        if(mic[i]!=INT16_MIN && mic[i]!=INT16_MAX &&
           nlp[i]!=INT16_MIN && nlp[i]!=INT16_MAX &&
           linear[i]!=INT16_MIN && linear[i]!=INT16_MAX)continue;
        *votes=*strong=0;
        onset->first=onset->last=hint->first=hint->last=0;
        onset->active=hint->active=false;
        return true;
    }
    return false;
}
