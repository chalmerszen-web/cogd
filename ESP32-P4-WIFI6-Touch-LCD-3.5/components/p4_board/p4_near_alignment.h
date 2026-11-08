#ifndef COGD_P4_NEAR_ALIGNMENT_H
#define COGD_P4_NEAR_ALIGNMENT_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Measured FD_HIGH_PERF linear/NLP output delay: 512 samples at 16 kHz.
 * Compare both microphone signals at that clock. Current raw microphone
 * and delayed residual can otherwise veto/admit different syllables. */
typedef struct {
    int16_t mic[512],reference[2048];
    bool ready;
} p4_near_alignment_t;
static inline void p4_near_alignment_push(p4_near_alignment_t *s,
    const int16_t mic[512],const int16_t reference[512])
{
    memcpy(s->mic,mic,sizeof(s->mic));
    memmove(s->reference,s->reference+512,1536*sizeof(int16_t));
    memcpy(s->reference+1536,reference,512*sizeof(int16_t));s->ready=true;
}
#endif
