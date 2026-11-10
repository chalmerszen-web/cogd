#include "p4_asr_retry.h"
#include <assert.h>
#include <limits.h>
int main(void)
{
    p4_asr_retry_t s={0};
    /* candidate133 cancelled-8: complete 1.86 s submitted, empty final. */
    assert(p4_asr_retry_begin(&s,579840,600000,609600));
    assert(s.start==579840 && s.voice==600000 && s.end==609600);
    s.pending=false;
    assert(!p4_asr_retry_begin(&s,579840,600000,609600));
    /* candidate135: an empty pre-question candidate consumed the global
     * one-shot budget. Distinct later capture must retain its own retry. */
    assert(p4_asr_retry_begin(&s,700000,710000,719600));
    assert(!p4_asr_retry_begin(&s,700000,710000,719600));
    assert(p4_asr_retry_begin(&s,800000,810000,819600));
    assert(!p4_asr_retry_begin(&s,900000,910000,919600));
    p4_asr_retry_accepted(&s);
    assert(p4_asr_retry_begin(&s,700000,710000,719600));
    p4_asr_retry_accepted(&s);
    assert(!p4_asr_retry_begin(&s,10000,11000,13000));
    assert(!p4_asr_retry_begin(&s,10000,25000,20000));
    assert(!p4_asr_retry_begin(&s,10000,20000,138001));
    assert(!p4_asr_retry_begin(&s,10000,9000,20000));
    assert(!s.attempts && !s.pending);
    /* Sample cursors wrap; all policy distances remain bounded. */
    assert(p4_asr_retry_begin(&s,UINT_MAX-9999u,2000,11600));
    return 0;
}
