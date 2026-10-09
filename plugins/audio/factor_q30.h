/* Exact numerator factoring for the pinned symmetric Q30 biquads.
 * Preconditions from the existing filters: c[0]==c[2], all input/state
 * magnitudes <= 2^25 and |c[i]| <= 2^31. Pair/difference fit int32_t;
 * every original and factored partial sum is below 2^59 in magnitude. */
#ifndef STUDY_FACTOR_Q30_H
#define STUDY_FACTOR_Q30_H
#include "voice.h"
static inline int32_t factor_q30(agent_biquad_t *s,const int32_t *c,int32_t value)
{
    int32_t pair=value+s->x2;
    int64_t sum=(int64_t)c[0]*pair-(int64_t)c[4]*s->y2;
    if(c[1]==c[3])sum+=(int64_t)c[1]*(s->x1-s->y1);
    else sum+=(int64_t)c[1]*s->x1-(int64_t)c[3]*s->y1;
    int64_t next=sum/INT64_C(1073741824);
    if(next>33554431)next=33554431;
    if(next< -33554432)next=-33554432;
    s->x2=s->x1;s->x1=value;s->y2=s->y1;s->y1=(int32_t)next;
    return (int32_t)next;
}
#endif
