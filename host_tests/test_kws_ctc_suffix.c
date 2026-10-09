#include "kws_ctc_suffix.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t sample;
static kws_ctc_match_t feed(kws_ctc_suffix_t *s, unsigned token, bool armed)
{
    int16_t scores[14];
    for (unsigned i = 0; i < 14; ++i) scores[i] = -4096;
    scores[token] = 4096;
    sample += 512;
    return kws_ctc_suffix_step(s, scores, sample, armed);
}

static void word(kws_ctc_suffix_t *s, unsigned first, kws_ctc_match_t expected)
{
    for (unsigned i = 0; i < 4; ++i) {
        assert(feed(s, first+i, true) == KWS_CTC_NONE);
        assert(feed(s, 0, true) == KWS_CTC_NONE);
    }
    assert(feed(s, 0, true) == expected);
}

int main(void)
{
    _Static_assert(sizeof(kws_ctc_suffix_t) <= 4096, "Fixed suffix-state budget");
    kws_ctc_suffix_t s;kws_ctc_suffix_reset(&s);
    word(&s, 1, KWS_CTC_MANDARIN);
    for (unsigned i = 0; i < 3000; ++i) assert(feed(&s, 0, true) == KWS_CTC_NONE);
    assert(s.beam[0].age[0][0] == 255);
    word(&s, 5, KWS_CTC_CANTONESE);
    word(&s, 1, KWS_CTC_MANDARIN);
    kws_ctc_suffix_reset(&s);
    assert(feed(&s, 1, true) == KWS_CTC_NONE);
    assert(feed(&s, 2, false) == KWS_CTC_NONE);
    for (unsigned token = 3; token <= 4; ++token) {
        assert(feed(&s, token, true) == KWS_CTC_NONE);
        assert(feed(&s, 0, true) == KWS_CTC_NONE);
    }
    assert(feed(&s, 0, true) == KWS_CTC_NONE);
    kws_ctc_suffix_reset(&s);
    assert(feed(&s, 1, true) == KWS_CTC_NONE);
    assert(feed(&s, 2, true) == KWS_CTC_NONE);
    sample += 512;
    assert(feed(&s, 3, true) == KWS_CTC_NONE);
    assert(feed(&s, 4, true) == KWS_CTC_NONE);
    assert(feed(&s, 0, true) == KWS_CTC_NONE);
    assert(feed(&s, 0, true) == KWS_CTC_NONE);
    for (unsigned wrong = 9; wrong < 14; ++wrong) {
        kws_ctc_suffix_reset(&s);
        for (unsigned token = 1; token < 4; ++token) {
            assert(feed(&s, token, true) == KWS_CTC_NONE);
            assert(feed(&s, 0, true) == KWS_CTC_NONE);
        }
        assert(feed(&s, wrong, true) == KWS_CTC_NONE);
        assert(feed(&s, 0, true) == KWS_CTC_NONE);
        assert(feed(&s, 0, true) == KWS_CTC_NONE);
    }
    int16_t equal[14]={0};
    for (unsigned i=0;i<1024;++i) {
        sample += 512;
        assert(kws_ctc_suffix_step(&s, equal, sample, true)==KWS_CTC_NONE);
        assert(s.count==1 && s.beam[0].length==0);
    }
    assert(kws_ctc_suffix_step(&s, NULL, sample, true)==KWS_CTC_NONE);
    assert(kws_ctc_suffix_step(&s, equal, 513, true)==KWS_CTC_NONE);
    assert(kws_ctc_suffix_step(NULL, equal, sample, true)==KWS_CTC_NONE);
    unsigned x=460;
    for (unsigned frame=0;frame<4096;++frame) {
        int16_t extreme[14];
        for (unsigned k=0;k<14;++k) { x=x*1664525u+1013904223u;extreme[k]=(int16_t)(x>>16); }
        sample += 512;(void)kws_ctc_suffix_step(&s, extreme, sample, true);
        assert(s.count<=8);
        for (unsigned i=0;i<s.count;++i) assert(s.beam[i].length<=4);
    }
    printf("suffix workspace %zu, node %zu; continuous/gap/disarm/span/flat/extreme guards passed\n",
           sizeof(s),sizeof(kws_ctc_suffix_node_t));
    return 0;
}
