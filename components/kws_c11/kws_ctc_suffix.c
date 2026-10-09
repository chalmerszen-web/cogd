#include "kws_ctc_suffix.h"
#include "kws_ctc_logadd.h"
#include <limits.h>
#include <string.h>

#define ABSENT (INT32_MIN / 4)
#define SCORE_MAX (INT32_MAX / 4)

static int32_t add_score(int32_t a, int32_t b)
{
    if (a == ABSENT) return ABSENT;
    int64_t sum = (int64_t)a + b;
    return sum < ABSENT + 1 ? ABSENT + 1 : sum > SCORE_MAX ? SCORE_MAX : (int32_t)sum;
}

static int32_t logadd(int32_t a, int32_t b)
{
    if (a == ABSENT) return b;
    if (b == ABSENT) return a;
    int32_t maximum = a > b ? a : b;
    uint64_t difference = a > b ? (uint64_t)((int64_t)a-b) : (uint64_t)((int64_t)b-a);
    if (difference >= 12u * 65536u) return maximum;
    unsigned index = (unsigned)(difference >> 10), fraction = (unsigned)difference & 1023u;
    unsigned value = (kws_ctc_logadd[index] * (1024u-fraction) +
                      kws_ctc_logadd[index+1] * fraction + 512u) >> 10;
    return add_score(maximum, (int32_t)value);
}

static bool prefix_less(const kws_ctc_suffix_node_t *a, const kws_ctc_suffix_node_t *b)
{
    unsigned length = a->length < b->length ? a->length : b->length;
    for (unsigned i = 0; i < length; ++i)
        if (a->token[i] != b->token[i]) return a->token[i] < b->token[i];
    return a->length < b->length;
}

static bool younger(const uint8_t *a, const uint8_t *b, unsigned length)
{
    for (unsigned i = 0; i < length; ++i)
        if (a[i] != b[i]) return a[i] < b[i];
    return false;
}

static void accumulate(kws_ctc_suffix_t *s, unsigned *used,
    const uint8_t *tokens, unsigned length, int32_t mass, int32_t path,
    const uint8_t *age, unsigned slot)
{
    if (mass == ABSENT) return;
    unsigned index;
    for (index = 0; index < *used; ++index)
        if (s->next[index].length == length && !memcmp(s->next[index].token, tokens, length)) break;
    if (index == *used) {
        /* Each of8 source prefixes adds at most14 distinct candidates. */
        if (*used == KWS_CTC_NEXT) return;
        kws_ctc_suffix_node_t *n = &s->next[(*used)++];
        memset(n, 0, sizeof(*n));
        n->length = (uint8_t)length;
        memcpy(n->token, tokens, length);
        n->mass[0] = n->mass[1] = n->path[0] = n->path[1] = ABSENT;
    }
    kws_ctc_suffix_node_t *n = &s->next[index];
    n->mass[slot] = logadd(n->mass[slot], mass);
    if (path > n->path[slot] || (path == n->path[slot] && younger(age, n->age[slot], length))) {
        n->path[slot] = path;
        memcpy(n->age[slot], age, length);
    }
}

void kws_ctc_suffix_reset(kws_ctc_suffix_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->count = 1;
    s->beam[0].mass[1] = s->beam[0].path[1] = ABSENT;
}

kws_ctc_match_t kws_ctc_suffix_step(kws_ctc_suffix_t *s,
    const int16_t logits[KWS_CTC_CLASSES], uint64_t end, bool armed)
{
    if (!s) return KWS_CTC_NONE;
    if (!logits || end < 512 || end % 512) {
        kws_ctc_suffix_reset(s);
        return KWS_CTC_NONE;
    }
    if (!s->count || s->count > KWS_CTC_BEAM ||
        (s->last_sample && (end <= s->last_sample || end-s->last_sample != 512)))
        kws_ctc_suffix_reset(s);
    s->last_sample = end;
    if (!armed) {
        kws_ctc_suffix_reset(s);
        s->last_sample = end;
        return KWS_CTC_NONE;
    }
    bool flat = true;
    for (unsigned i = 1; i < KWS_CTC_CLASSES; ++i)
        if (logits[i] != logits[0]) flat = false;
    if (flat) {
        kws_ctc_suffix_reset(s);
        s->last_sample = end;
        return KWS_CTC_NONE;
    }
    unsigned used = 0;
    for (unsigned i = 0; i < s->count; ++i) {
        const kws_ctc_suffix_node_t *n = &s->beam[i];
        uint8_t ages[2][4], tokens[4], extended[4];
        for (unsigned slot = 0; slot < 2; ++slot)
            for (unsigned k = 0; k < n->length; ++k)
                ages[slot][k] = n->age[slot][k] == 255 ? 255 : n->age[slot][k]+1;
        unsigned best = n->path[0] > n->path[1] ||
            (n->path[0] == n->path[1] && younger(ages[0], ages[1], n->length)) ? 0 : 1;
        int32_t mass = logadd(n->mass[0], n->mass[1]);
        accumulate(s, &used, n->token, n->length, add_score(mass, (int32_t)logits[0]*256),
            add_score(n->path[best], (int32_t)logits[0]*256), ages[best], 0);
        for (unsigned token = 1; token < KWS_CTC_CLASSES; ++token) {
            int32_t score = (int32_t)logits[token]*256;
            unsigned offset = n->length == 4 ? 1 : 0, length = n->length-offset;
            memcpy(tokens, n->token+offset, length);
            tokens[length++] = (uint8_t)token;
            if (n->length && token == n->token[n->length-1]) {
                accumulate(s, &used, n->token, n->length, add_score(n->mass[1], score),
                    add_score(n->path[1], score), ages[1], 1);
                memcpy(extended, ages[0]+offset, length-1);extended[length-1] = 0;
                accumulate(s, &used, tokens, length, add_score(n->mass[0], score),
                    add_score(n->path[0], score), extended, 1);
            } else {
                memcpy(extended, ages[best]+offset, length-1);extended[length-1] = 0;
                accumulate(s, &used, tokens, length, add_score(mass, score),
                    add_score(n->path[best], score), extended, 1);
            }
        }
    }
    uint8_t top[KWS_CTC_BEAM];int32_t scores[KWS_CTC_BEAM];unsigned retained = 0;
    for (unsigned i = 0; i < used; ++i) {
        int32_t score = logadd(s->next[i].mass[0], s->next[i].mass[1]);
        unsigned at = 0;
        while (at < retained && (scores[at] > score || (scores[at] == score &&
               prefix_less(&s->next[top[at]], &s->next[i])))) ++at;
        if (at == KWS_CTC_BEAM) continue;
        if (retained < KWS_CTC_BEAM) ++retained;
        for (unsigned k = retained-1; k > at; --k) { top[k] = top[k-1]; scores[k] = scores[k-1]; }
        top[at] = (uint8_t)i;scores[at] = score;
    }
    if (!retained) { kws_ctc_suffix_reset(s);return KWS_CTC_NONE; }
    int32_t path_max = ABSENT;
    for (unsigned i = 0; i < retained; ++i)
        for (unsigned slot = 0; slot < 2; ++slot)
            if (s->next[top[i]].path[slot] > path_max) path_max = s->next[top[i]].path[slot];
    for (unsigned i = 0; i < retained; ++i) {
        s->beam[i] = s->next[top[i]];
        for (unsigned slot = 0; slot < 2; ++slot) {
            s->beam[i].mass[slot] = add_score(s->beam[i].mass[slot], -scores[0]);
            s->beam[i].path[slot] = add_score(s->beam[i].path[slot], -path_max);
        }
    }
    s->count = (uint8_t)retained;
    const kws_ctc_suffix_node_t *winner = &s->beam[0];
    if (s->fired_length != winner->length || memcmp(s->fired, winner->token, winner->length))
        s->fired_length = 0;
    bool blank = true;
    for (unsigned i = 1; i < KWS_CTC_CLASSES; ++i)
        if (logits[i] >= logits[0]) blank = false;
    s->quiet = blank ? s->quiet < 2 ? s->quiet+1 : 2 : 0;
    unsigned best = winner->path[0] > winner->path[1] ||
        (winner->path[0] == winner->path[1] && younger(winner->age[0], winner->age[1], winner->length)) ? 0 : 1;
    if (winner->length != 4 || s->fired_length || s->quiet != 2 ||
        (unsigned)winner->age[best][0]*512 > KWS_CTC_SPAN_SAMPLES) return KWS_CTC_NONE;
    static const uint8_t target[2][4] = {{1,2,3,4},{5,6,7,8}};
    for (unsigned i = 0; i < 2; ++i) if (!memcmp(winner->token, target[i], 4)) {
        memcpy(s->fired, winner->token, 4);s->fired_length = 4;
        return i == 0 ? KWS_CTC_MANDARIN : KWS_CTC_CANTONESE;
    }
    return KWS_CTC_NONE;
}
