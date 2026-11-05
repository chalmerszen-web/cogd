#ifndef P4_KWS_CONTRAST_H
#define P4_KWS_CONTRAST_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* P4 experiment: causal per-band mean removal, 512 ms time constant.
 * Work on the quantized log-Mel input so training and inference use exactly
 * the same integer operation. Zero-only priming cannot learn a room floor.
 * This is feature normalization, not a claim of reconstructed clean speech. */
typedef struct { int32_t mean_q8[40]; bool initialized; } p4_contrast_t;
static inline void p4_contrast_step(p4_contrast_t *s, const int8_t input[40],
                                   const int8_t zero[40], int8_t output[40])
{
    if (!s->initialized) {
        for (unsigned i=0;i<40;++i) s->mean_q8[i]=(int32_t)input[i]*256;
        s->initialized=memcmp(input,zero,40)!=0;
    }
    for (unsigned i=0;i<40;++i) {
        int32_t value=(int32_t)input[i]*256;
        s->mean_q8[i]+=(value-s->mean_q8[i])/32;
        int32_t delta=value-s->mean_q8[i];
        int32_t rounded=delta<0?-((-delta+128)/256):(delta+128)/256;
        output[i]=(int8_t)(rounded<-128?-128:rounded>127?127:rounded);
    }
}
#endif
