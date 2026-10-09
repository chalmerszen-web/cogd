#include "kws_ctc.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static kws_ctc_match_t feed(kws_ctc_t *s, unsigned token, uint64_t end, bool armed)
{
    int16_t logits[KWS_CTC_CLASSES];
    for (unsigned i = 0; i < KWS_CTC_CLASSES; ++i) logits[i] = INT16_MIN;
    if (token < KWS_CTC_CLASSES) logits[token] = INT16_MAX;
    else logits[KWS_CTC_YAN2] = logits[KWS_CTC_YAN4] = INT16_MAX;
    return kws_ctc_step(s, logits, end, armed);
}

static void example(const unsigned *tokens, size_t n, kws_ctc_match_t expected)
{
    kws_ctc_t s; kws_ctc_reset(&s);
    for (size_t i = 0; i < n; ++i)
        assert(feed(&s, tokens[i], (i + 1) * 512, true) == (i + 1 == n ? expected : KWS_CTC_NONE));
    for (unsigned i = 0; i < 8; ++i) assert(!feed(&s, 0, (n + i + 1) * 512, true));
}

static void examples(void)
{
    const unsigned zh[] = {1, 1, 0, 2, 2, 0, 3, 0, 4, 4, 0, 0};
    const unsigned yue[] = {5, 6, 7, 8, 0, 0};
    const unsigned restart[] = {1, 2, 1, 2, 3, 4, 0, 0};
    example(zh, sizeof(zh) / sizeof(*zh), KWS_CTC_MANDARIN);
    example(yue, sizeof(yue) / sizeof(*yue), KWS_CTC_CANTONESE);
    example(restart, sizeof(restart) / sizeof(*restart), KWS_CTC_MANDARIN);
    const unsigned rejected[][7] = {
        {1, 2, 3, 0, 0, 0, 0}, {4, 0, 0, 0, 0, 0, 0},
        {2, 1, 3, 4, 0, 0, 0}, {1, 6, 3, 4, 0, 0, 0},
        {1, 2, 3, 10, 0, 0, 0}, {1, 2, 3, 11, 0, 0, 0},
        {5, 6, 7, 12, 0, 0, 0}, {5, 6, 7, 13, 0, 0, 0},
        {1, 2, 3, 4, 10, 0, 0}, {1, 2, 3, 4, 255, 0, 0},
        {1, 0, 1, 2, 3, 9, 0}
    };
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i)
        example(rejected[i], 7, KWS_CTC_NONE);
}

/* Independent oracle: retain the last four collapsed tokens and compare a
 * complete suffix at blank boundaries. It has no incremental grammar states. */
typedef struct {
    uint64_t last, when[4];
    unsigned token[4], count, previous, blanks;
} oracle_t;

static kws_ctc_match_t oracle(oracle_t *o, unsigned token, uint64_t end, bool armed)
{
    if (end < 512 || end % 512) { memset(o, 0, sizeof(*o)); return KWS_CTC_NONE; }
    if (o->last && end - o->last != 512) memset(o, 0, sizeof(*o));
    o->last = end;
    if (!armed) { o->count = o->previous = o->blanks = 0; return KWS_CTC_NONE; }
    if (token >= KWS_CTC_CLASSES) token = KWS_CTC_OTHER;
    if (token == 0) {
        o->previous = 0;
        if (o->blanks < 2) ++o->blanks;
        if (o->blanks == 2 && o->count == 4 && end - o->when[0] <= KWS_CTC_SPAN_SAMPLES) {
            const unsigned zh[] = {1, 2, 3, 4}, yue[] = {5, 6, 7, 8};
            kws_ctc_match_t result = !memcmp(o->token, zh, sizeof(zh)) ? KWS_CTC_MANDARIN :
                !memcmp(o->token, yue, sizeof(yue)) ? KWS_CTC_CANTONESE : KWS_CTC_NONE;
            if (result) { o->count = o->blanks = 0; return result; }
        }
        return KWS_CTC_NONE;
    }
    o->blanks = 0;
    if (token == o->previous) return KWS_CTC_NONE;
    o->previous = token;
    if (o->count == 4) {
        memmove(o->token, o->token + 1, 3 * sizeof(*o->token));
        memmove(o->when, o->when + 1, 3 * sizeof(*o->when));
        o->count = 3;
    }
    o->token[o->count] = token; o->when[o->count++] = end;
    return KWS_CTC_NONE;
}

static unsigned random_oracle(void)
{
    kws_ctc_t s; kws_ctc_reset(&s);
    oracle_t o = {0}; uint32_t rng = 446; uint64_t end = 0;
    unsigned matches = 0;
    for (unsigned i = 0; i < 65536; ++i) {
        rng = rng * 1664525u + 1013904223u;
        end += (rng & 1023u) ? 512 : 1024;
        unsigned token = (rng >> 16) % KWS_CTC_CLASSES;
        /* Real matches are injected so oracle agreement cannot be vacuous. */
        unsigned part = i % 256;
        if (part < 4) token = part + 1 + ((i / 256) % 2) * 4;
        else if (part < 6) token = 0;
        else if (i % 19 == 0) token = 255;
        uint64_t actual_end = i % 509 == 0 ? end + 1 : end;
        bool armed = i % 97 != 0;
        kws_ctc_match_t result = feed(&s, token, actual_end, armed);
        assert(result == oracle(&o, token, actual_end, armed));
        assert(s.position[0] <= 4 && s.position[1] <= 4 && s.blanks <= 2);
        matches += result != KWS_CTC_NONE;
    }
    assert(matches > 100);
    return matches;
}

static void boundaries(void)
{
    kws_ctc_t s;
    for (unsigned finish = 94; finish <= 95; ++finish) {
        kws_ctc_reset(&s);
        for (unsigned i = 1; i <= finish; ++i) {
            unsigned token = i <= 3 ? i : i == finish - 2 ? 4 : 0;
            assert(feed(&s, token, i * 512, true) ==
                (finish == 94 && i == finish ? KWS_CTC_MANDARIN : KWS_CTC_NONE));
        }
    }
    for (unsigned kind = 0; kind < 3; ++kind) {
        kws_ctc_reset(&s); assert(!feed(&s, 1, 512, true));
        if (kind == 0) assert(!feed(&s, 2, 1024, false));
        if (kind == 1) assert(!feed(&s, 2, 1536, true));
        if (kind == 2) assert(!feed(&s, 2, 512, true));
        uint64_t end = s.last_sample;
        for (unsigned token = 3; token <= 4; ++token) assert(!feed(&s, token, end += 512, true));
        for (unsigned i = 0; i < 3; ++i) assert(!feed(&s, 0, end += 512, true));
    }
    kws_ctc_reset(NULL);
    int16_t flat[KWS_CTC_CLASSES] = {0};
    assert(!kws_ctc_step(NULL, flat, 512, true));
    assert(!feed(&s, 1, 512, true));
    assert(!kws_ctc_step(&s, NULL, 1024, true) && !s.last_sample);
    assert(!kws_ctc_step(&s, flat, 0, true) && !s.last_sample);
    assert(!kws_ctc_step(&s, flat, 513, true) && !s.last_sample);
    kws_ctc_reset(&s);
    assert(!feed(&s, 1, UINT64_MAX - 511, true));
    assert(!feed(&s, 2, 512, true)); /* clock restart must clear NI */
    assert(!feed(&s, 3, 1024, true)); assert(!feed(&s, 4, 1536, true));
    assert(!feed(&s, 0, 2048, true)); assert(!feed(&s, 0, 2560, true));
}

int main(void)
{
    assert(sizeof(kws_ctc_t) <= 48);
    examples(); boundaries(); unsigned matches = random_oracle();
    printf("{\"state_bytes\":%zu,\"oracle_frames\":65536,\"oracle_matches\":%u,"
           "\"token_classes\":%u,\"acoustic_quality_test\":false}\n",
           sizeof(kws_ctc_t), matches, KWS_CTC_CLASSES);
    return 0;
}
