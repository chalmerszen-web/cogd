/* Numerical/state contract only; not a keyword quality or hardware benchmark. */
#include "kws_conditioned_seed.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void equal_state(const kws_conditioned_runtime_t *a, const kws_conditioned_runtime_t *b)
{
    assert(a->first.model == b->first.model && a->secondary_model == b->secondary_model &&
        a->verifier_model == b->verifier_model);
    assert(!memcmp(a->first.previous, b->first.previous, sizeof(a->first.previous)));
    assert(!memcmp(&a->primary, &b->primary, sizeof(a->primary)));
    assert(!memcmp(&a->secondary, &b->secondary, sizeof(a->secondary)));
    assert(!memcmp(&a->verifier, &b->verifier, sizeof(a->verifier)));
    assert(!memcmp(&a->decision, &b->decision, sizeof(a->decision)));
    assert(a->last_sample == b->last_sample && a->next == b->next && a->count == b->count && a->half == b->half);
    assert(!memcmp(a->recent, b->recent, sizeof(a->recent)));
    assert(!memcmp(a->heads, b->heads, sizeof(a->heads)));
    assert(!memcmp(a->score, b->score, sizeof(a->score)));
}

static kws_conditioned_runtime_t *init(void *memory)
{
    kws_conditioned_runtime_t *s = NULL;
    assert(!kws_conditioned_runtime_init(memory, kws_conditioned_runtime_size(),
        &kws_frozen_primary, &kws_frozen_secondary, &kws_trained_model, &s));
    return s;
}

int main(void)
{
    size_t size = kws_conditioned_runtime_size();
    void *memory_a = calloc(1, size), *memory_b = calloc(1, size), *before = malloc(size);
    assert(memory_a && memory_b && before);
    kws_conditioned_runtime_t *a = init(memory_a), *b = init(memory_b);
    assert(!kws_conditioned_seed_prime(NULL));
    kws_conditioned_runtime_threshold(b, 268); memcpy(before, b, size);
    assert(!kws_conditioned_seed_prime(b) && !memcmp(before, b, size));
    b = init(memory_b); b->verifier_model = &kws_frozen_primary; memcpy(before, b, size);
    assert(!kws_conditioned_seed_prime(b) && !memcmp(before, b, size));
    b = init(memory_b); kws_conditioned_runtime_stability(b, true); memcpy(before, b, size);
    assert(!kws_conditioned_seed_prime(b) && !memcmp(before, b, size));
    b = init(memory_b);
    uint64_t end = kws_conditioned_runtime_prime(a);
    assert(end == 32768 && kws_conditioned_seed_prime(b) == end);
    equal_state(a, b); memcpy(before, b, size);
    assert(!kws_conditioned_seed_prime(b) && !memcmp(before, b, size));
    kws_conditioned_runtime_threshold(a, 268); kws_conditioned_runtime_threshold(b, 268);
    uint32_t rng = UINT32_C(3081134173); /* 20261003357 modulo 2^32. */
    unsigned armed_frames = 0, gaps = 0, detections = 0;
    for (unsigned i = 0; i < 2048; ++i) {
        int16_t pcm[256], va[265], vb[265];
        for (unsigned j = 0; j < 256; ++j) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            pcm[j] = (int16_t)rng;
        }
        if (i % 127 == 0) memset(pcm, 0, sizeof(pcm));
        bool armed = i % 41 != 0;
        armed_frames += armed;
        bool gap = i % 257 == 256;
        gaps += gap; end += gap ? 768 : 256;
        kws_event_t ea, eb;
        kws_trace_t ta, tb;
        assert(!kws_conditioned_runtime_pcm(a, pcm, end, armed, &ea, &ta, va));
        assert(!kws_conditioned_runtime_pcm(b, pcm, end, armed, &eb, &tb, vb));
        assert(ea.score_q8 == eb.score_q8 && ea.detected == eb.detected && ea.sample_end == eb.sample_end);
        detections += ea.detected;
        assert(!memcmp(ta.logmel, tb.logmel, sizeof(ta.logmel)));
        assert(!memcmp(ta.input, tb.input, sizeof(ta.input)));
        assert(!memcmp(ta.layer, tb.layer, sizeof(ta.layer)) && !memcmp(va, vb, sizeof(va)));
        equal_state(a, b);
    }
    /* Negative support must withdraw an earlier proof before its owner arrives. */
    kws_temporal_owner_t owner;
    kws_temporal_owner_reset(&owner, 268);
    for (unsigned block = 1; block <= 80; ++block) {
        assert(!kws_temporal_owner_step(&owner, block >= 79 ? 300 : -300,
            block == 76 || block == 77 ? 300 : -300, (uint64_t)block * 512, true));
        if (block == 77) assert(owner.last_support == 77 * 512 && owner.verify_votes == 3);
        if (block == 78) assert(!owner.last_support && !owner.verify_votes);
    }
    assert(!owner.last_support && !owner.verify_votes);
    printf("{\"passed\":true,\"frames\":2048,\"trace_values\":1249280,\"armed_frames\":%u,"
        "\"gaps\":%u,\"detections_equal\":%u,\"seed_bytes\":%zu,\"host_workspace\":%zu,"
        "\"prime_samples\":32768,\"rejections_without_mutation\":4,\"negative_support_revoked\":true}\n",
        armed_frames, gaps, detections, kws_conditioned_seed_bytes(), size);
    free(memory_a); free(memory_b); free(before);
    return 0;
}
