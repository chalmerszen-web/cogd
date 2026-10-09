#include "kws_verified_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern const kws_model_t kws_verified_primary, kws_verified_secondary, kws_trained_model;
static void i8(const char *name, const int8_t *p, size_t n)
{
    printf(".%s={", name);
    for (size_t i = 0; i < n; ++i) printf("%s%d%s", i ? "," : "", p[i], i % 32 == 31 ? "\n" : "");
    puts("},");
}
static void i16(const char *name, const int16_t *p, size_t n)
{
    printf(".%s={", name);
    for (size_t i = 0; i < n; ++i) printf("%s%d%s", i ? "," : "", p[i], i % 32 == 31 ? "\n" : "");
    puts("},");
}
static void neural(const char *name, const kws_neural_buffers_t *s, unsigned channels)
{
    printf(".%s={\n", name);
    i8("history", s->history, 80 + 124 * channels);
    printf(".position={");
    for (unsigned i = 0; i < 6; ++i) printf("%s%u", i ? "," : "", s->position[i]);
    puts("},");
    i8("current", s->current, channels < 40 ? 40 : channels);
    i8("next", s->next, channels);
    puts("},");
}
int main(void)
{
    void *memory = malloc(kws_verified_size());
    assert(memory);
    kws_verified_t *s = NULL;
    assert(!kws_verified_init(memory, kws_verified_size(), &kws_verified_primary,
        &kws_verified_secondary, &kws_trained_model, &s));
    const int16_t zero[256] = {0};
    kws_event_t event;
    for (unsigned frame = 1; frame <= 128; ++frame) {
        assert(!kws_verified_step_pcm_armed(s, zero, (uint64_t)frame * 256, true, &event, NULL));
        assert(!event.detected);
    }
    assert(s->last_sample == 32768 && s->decision.original.blocks == 64 &&
        !s->decision.original.votes && !s->decision.original.cooldown_until &&
        !s->decision.pending && !s->decision.last_support && !s->decision.verify_votes);
    puts("/* Generated typed state; source/model hashes in kws_verified_seed.cmake. */");
    puts("static const kws_verified_seed_t kws_verified_silence={");
    i16("previous", s->first.previous, 256);
    neural("first", &(kws_neural_buffers_t){s->first_neural.history,s->first_neural.current,
        s->first_neural.next,s->first_neural.position}, 24);
    neural("second", &(kws_neural_buffers_t){s->second.history,s->second.current,
        s->second.next,s->second.position}, 24);
    neural("verifier", &(kws_neural_buffers_t){s->verifier.history,s->verifier.current,
        s->verifier.next,s->verifier.position}, 48);
    printf(".decision={.original={.last_sample=%llu,.cooldown_until=%llu,.blocks=%u,"
           ".votes=%u,.stable=%u,.threshold_q8=%d},.pending=%llu,.last_support=%llu,.verify_votes=%u},\n",
        (unsigned long long)s->decision.original.last_sample,
        (unsigned long long)s->decision.original.cooldown_until, s->decision.original.blocks,
        s->decision.original.votes, s->decision.original.stable, s->decision.original.threshold_q8,
        (unsigned long long)s->decision.pending, (unsigned long long)s->decision.last_support,
        s->decision.verify_votes);
    printf(".last_sample=%llu,\n", (unsigned long long)s->last_sample);
    printf(".recent={{%d,%d,%d},{%d,%d,%d}},\n", s->recent[0][0], s->recent[0][1],
        s->recent[0][2], s->recent[1][0], s->recent[1][1], s->recent[1][2]);
    i16("heads", s->heads, 3); i16("score", s->score, 2);
    printf(".next=%u,.count=%u,.half=%u\n};\n", s->next, s->count, s->half);
    puts("_Static_assert(sizeof(kws_verified_silence)<=14336,\"Verified seed Flash budget\");");
    fprintf(stderr, "{\"frames\":128,\"synthetic_samples\":32768,\"seed_bytes\":%zu,"
        "\"workspace_bytes\":%zu}\n", sizeof(kws_verified_seed_t), kws_verified_size());
    free(memory);
    return 0;
}
