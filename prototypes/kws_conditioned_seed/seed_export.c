/* Host-only typed exporter: no raw pointers, ABI offsets, or dead FFT scratch. */
#include "kws_conditioned_seed.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void values(const void *memory, size_t count, bool signed_values)
{
    putchar('{');
    for (size_t i = 0; i < count; ++i)
        printf("%s%d", i ? "," : "", signed_values ?
            (int)((const int8_t *)memory)[i] : (int)((const uint8_t *)memory)[i]);
    putchar('}');
}

static void branch(const kws_conditioned_branch_t *s)
{
    printf("{.history="); values(s->history, sizeof(s->history), true);
    printf(",.current="); values(s->current, sizeof(s->current), true);
    printf(",.next="); values(s->next, sizeof(s->next), true);
    printf(",.position="); values(s->position, sizeof(s->position), false);
    putchar('}');
}

int main(void)
{
    kws_conditioned_runtime_t *s = NULL;
    void *memory = calloc(1, kws_conditioned_runtime_size());
    assert(memory && !kws_conditioned_runtime_init(memory, kws_conditioned_runtime_size(),
        &kws_frozen_primary, &kws_frozen_secondary, &kws_trained_model, &s));
    assert(kws_conditioned_runtime_prime(s) == 32768);
    const int16_t zero[256] = {0};
    assert(!memcmp(s->first.previous, zero, sizeof(zero)));
    assert(s->decision.original.blocks == 64 && !s->decision.pending && !s->decision.last_support);
    printf("/* Exact 64 zero-PCM blocks; regenerate on any declared source/model change. */\n");
    printf("static const kws_conditioned_seed_t kws_conditioned_silence = {\n.primary=");
    branch(&s->primary);
    printf(",\n.secondary="); branch(&s->secondary);
    printf(",\n.verifier={.history="); values(s->verifier.history, sizeof(s->verifier.history), true);
    printf(",.current="); values(s->verifier.current, sizeof(s->verifier.current), true);
    printf(",.next="); values(s->verifier.next, sizeof(s->verifier.next), true);
    printf(",.position="); values(s->verifier.position, sizeof(s->verifier.position), false);
    printf("},\n.decision={.original={.last_sample=%" PRIu64 ",.cooldown_until=%" PRIu64
        ",.blocks=%u,.votes=%u,.stable=%s,.threshold_q8=%d},.pending=%" PRIu64
        ",.last_support=%" PRIu64 ",.verify_votes=%u},\n.last_sample=%" PRIu64,
        s->decision.original.last_sample, s->decision.original.cooldown_until,
        (unsigned)s->decision.original.blocks, (unsigned)s->decision.original.votes,
        s->decision.original.stable ? "true" : "false", s->decision.original.threshold_q8,
        s->decision.pending, s->decision.last_support, (unsigned)s->decision.verify_votes, s->last_sample);
    printf(",\n.recent={{%d,%d,%d},{%d,%d,%d}},.heads={%d,%d,%d},.score={%d,%d},"
        ".next=%u,.count=%u,.half=%u\n};\n", s->recent[0][0], s->recent[0][1], s->recent[0][2],
        s->recent[1][0], s->recent[1][1], s->recent[1][2], s->heads[0], s->heads[1], s->heads[2],
        s->score[0], s->score[1], (unsigned)s->next, (unsigned)s->count, (unsigned)s->half);
    free(memory);
    return 0;
}
