#ifndef KWS_CTC_MATH_H
#define KWS_CTC_MATH_H
#include "kws_ctc_logadd.h"
#include <limits.h>

#define KWS_LOG_ABSENT (INT32_MIN / 4)
#define KWS_LOG_MAX (INT32_MAX / 4)

static inline int32_t kws_log_score_add(int32_t a, int32_t b)
{
    if (a == KWS_LOG_ABSENT) return KWS_LOG_ABSENT;
    int64_t sum = (int64_t)a+b;
    return sum < KWS_LOG_ABSENT+1 ? KWS_LOG_ABSENT+1 : sum > KWS_LOG_MAX ? KWS_LOG_MAX : (int32_t)sum;
}

static inline int32_t kws_log_add(int32_t a, int32_t b)
{
    if (a == KWS_LOG_ABSENT) return b;
    if (b == KWS_LOG_ABSENT) return a;
    int32_t maximum = a > b ? a : b;
    uint64_t difference = a > b ? (uint64_t)((int64_t)a-b) : (uint64_t)((int64_t)b-a);
    if (difference >= 12u*65536u) return maximum;
    unsigned index = (unsigned)(difference >> 10), fraction = (unsigned)difference & 1023u;
    unsigned value = (kws_ctc_logadd[index]*(1024u-fraction) +
                      kws_ctc_logadd[index+1]*fraction + 512u) >> 10;
    return kws_log_score_add(maximum,(int32_t)value);
}
#endif
