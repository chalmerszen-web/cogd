#include "kws_verified_internal.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const kws_model_t kws_verified_primary, kws_verified_secondary, kws_trained_model;
static bool no_allocation;
static unsigned frontends, neural_calls;
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n) { assert(!no_allocation); return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t m) { assert(!no_allocation); return __real_calloc(n, m); }
void *__wrap_realloc(void *p, size_t n) { assert(!no_allocation); return __real_realloc(p, n); }
void __wrap_free(void *p) { assert(!no_allocation); __real_free(p); }
void __real_kws_frontend_buffers_step(const kws_frontend_buffers_t *s, const kws_model_t *m,
    const int16_t pcm[256], int8_t input[40], kws_trace_t *trace);
void __wrap_kws_frontend_buffers_step(const kws_frontend_buffers_t *s, const kws_model_t *m,
    const int16_t pcm[256], int8_t input[40], kws_trace_t *trace)
{ ++frontends; __real_kws_frontend_buffers_step(s, m, pcm, input, trace); }
int16_t __real_kws_neural_buffers_step(const kws_neural_buffers_t *s, const kws_model_t *m,
    const int8_t input[40], kws_trace_t *trace);
int16_t __wrap_kws_neural_buffers_step(const kws_neural_buffers_t *s, const kws_model_t *m,
    const int8_t input[40], kws_trace_t *trace)
{ ++neural_calls; return __real_kws_neural_buffers_step(s, m, input, trace); }

static void init(kws_verified_t *s)
{
    kws_verified_t *out = NULL;
    assert(!kws_verified_init(s, kws_verified_size(), &kws_verified_primary,
        &kws_verified_secondary, &kws_trained_model, &out) && out == s);
}
static void equal(const kws_verified_t *a, const kws_verified_t *b)
{
    assert(a->first.model == b->first.model && a->second_model == b->second_model && a->verifier_model == b->verifier_model);
    assert(!memcmp(a->first.previous, b->first.previous, sizeof(a->first.previous)));
    assert(!memcmp(&a->first_neural, &b->first_neural, sizeof(a->first_neural)));
    assert(!memcmp(&a->second, &b->second, sizeof(a->second)) && !memcmp(&a->verifier, &b->verifier, sizeof(a->verifier)));
    const kws_detector_t *x = &a->decision.original, *y = &b->decision.original;
    assert(x->last_sample == y->last_sample && x->cooldown_until == y->cooldown_until &&
        x->blocks == y->blocks && x->votes == y->votes && x->stable == y->stable && x->threshold_q8 == y->threshold_q8);
    assert(a->decision.pending == b->decision.pending && a->decision.last_support == b->decision.last_support &&
        a->decision.verify_votes == b->decision.verify_votes && a->last_sample == b->last_sample);
    assert(!memcmp(a->recent, b->recent, sizeof(a->recent)) && !memcmp(a->heads, b->heads, sizeof(a->heads)) &&
        !memcmp(a->score, b->score, sizeof(a->score)) && a->next == b->next && a->count == b->count && a->half == b->half);
}
static void same_event(kws_event_t a, kws_event_t b)
{ assert(a.score_q8 == b.score_q8 && a.detected == b.detected && a.sample_end == b.sample_end); }

int main(void)
{
    size_t bytes = kws_verified_size();
    unsigned char *allocation = malloc(bytes + 128);
    kws_verified_t *b = malloc(bytes), *c = malloc(bytes);
    assert(allocation && b && c);
    memset(allocation, 0xa5, bytes + 128);
    kws_verified_t *a = (void *)(allocation + 64), *out = (void *)1;
    assert(kws_verified_init(NULL, 0, NULL, NULL, NULL, NULL) < 0);
    assert(kws_verified_init((char *)a + 1, bytes, &kws_verified_primary, &kws_verified_secondary,
        &kws_trained_model, &out) < 0 && !out);
    assert(kws_verified_init(a, bytes - 1, &kws_verified_primary, &kws_verified_secondary,
        &kws_trained_model, &out) < 0 && !out);
    assert(kws_verified_init(a, bytes, &kws_trained_model, &kws_verified_secondary,
        &kws_trained_model, &out) < 0 && !out);
    int16_t mean[40]; memcpy(mean, kws_verified_secondary.mean_q8, sizeof(mean)); ++mean[0];
    kws_model_t wrong = kws_verified_secondary; wrong.mean_q8 = mean;
    assert(kws_verified_init(a, bytes, &kws_verified_primary, &wrong, &kws_trained_model, &out) < 0 && !out);
    init(a); init(b); init(c);
    const int16_t zero[256] = {0}; const int8_t features[40] = {0}; kws_event_t x, y;
    assert(kws_verified_step_pcm_armed(NULL, zero, 256, true, &x, NULL) < 0);
    assert(kws_verified_step_pcm_armed(a, NULL, 256, true, &x, NULL) < 0);
    assert(kws_verified_step_pcm_armed(a, zero, 255, true, &x, NULL) < 0);
    assert(kws_verified_step_features_armed(a, features, 256, true, NULL, NULL) < 0);
    assert(kws_verified_feed_512_armed(a, (const int16_t[512]){0}, 511, true, &x) < 0);
    no_allocation = true;
    uint64_t epoch = 0;
#ifdef AGENT_KWS_VERIFIED_PRIME
    frontends = neural_calls = 0;
    epoch = kws_verified_prime(a);
    assert(epoch == 32768 && !frontends && !neural_calls && !kws_verified_prime(a));
    for (uint64_t end = 256; end <= epoch; end += 256) {
        assert(!kws_verified_step_pcm_armed(b, zero, end, true, &x, NULL));
        assert(!x.detected);
    }
    equal(a, b);
    kws_model_t copy = kws_verified_primary;
    assert(!kws_verified_init(c, bytes, &copy, &kws_verified_secondary, &kws_trained_model, &out));
    assert(!kws_verified_prime(c));
    init(c); kws_verified_threshold(c, 268); assert(!kws_verified_prime(c));
    init(c); kws_verified_stability(c, true); assert(!kws_verified_prime(c));
    init(c); assert(!kws_verified_step_pcm_armed(c, zero, 256, true, &x, NULL)); assert(!kws_verified_prime(c));
#else
    assert(!kws_verified_prime(a));
#endif
    kws_verified_threshold(a, 268); kws_verified_threshold(b, 268);
    equal(a, b);
    uint32_t random = 2026100279;
    for (unsigned frame = 0; frame < 512; ++frame) {
        int16_t pcm[256];
        for (unsigned i = 0; i < 256; ++i) {
            random = random * 1664525u + 1013904223u;
            unsigned value = random >> 16;
            pcm[i] = frame == 0 ? 0 : frame == 1 ? INT16_MAX : frame == 2 ? INT16_MIN :
                (int16_t)(value <= INT16_MAX ? (int)value : (int)value - 65536);
        }
        frontends = neural_calls = 0;
        assert(!kws_verified_step_pcm_armed(a, pcm, epoch + (uint64_t)(frame + 1) * 256,
            frame >= 16, &x, NULL));
        assert(frontends == 1 && neural_calls == 3);
        assert(!kws_verified_step_pcm_armed(b, pcm, epoch + (uint64_t)(frame + 1) * 256,
            frame >= 16, &y, NULL));
        same_event(x, y); equal(a, b);
        for (unsigned i = 0; i < 64; ++i)
            assert(allocation[i] == 0xa5 && allocation[64 + bytes + i] == 0xa5);
    }
    /* Disarm on the first16-ms half must clear old evidence immediately. */
    a->decision.pending = a->decision.last_support = a->last_sample;
    a->decision.verify_votes = a->decision.original.votes = 7;
    frontends = neural_calls = 0;
    assert(!kws_verified_step_features_armed(a, features, a->last_sample + 256, false, &x, NULL));
    assert(!x.detected && !a->decision.pending && !a->decision.last_support &&
        !a->decision.verify_votes && !a->decision.original.votes && !frontends && neural_calls == 3);
    /* A clock gap resets causal state before processing the first resumed frame. */
    uint64_t resumed = a->last_sample + 1024;
    init(c); kws_verified_threshold(c, 268);
    assert(!kws_verified_step_pcm_armed(a, zero, resumed, true, &x, NULL));
    assert(!kws_verified_step_pcm_armed(c, zero, resumed, true, &y, NULL));
    same_event(x, y); equal(a, c);
    kws_verified_stability(a, true); kws_verified_reset(a);
    assert(a->decision.original.stable && a->decision.original.threshold_q8 == 268 &&
        !a->decision.original.blocks && !a->last_sample && !a->decision.pending);
    init(a); init(b); const int16_t block[512] = {0};
    for (unsigned i = 1; i <= 128; ++i) {
        assert(!kws_verified_feed_512_armed(a, block, (uint64_t)i * 512, true, &x));
        assert(!kws_verified_step_pcm_armed(b, block, (uint64_t)i * 512 - 256, true, &y, NULL));
        assert(!kws_verified_step_pcm_armed(b, block + 256, (uint64_t)i * 512, true, &y, NULL));
        same_event(x, y); equal(a, b);
    }
    kws_model_t untrained = kws_trained_model; untrained.trained = false;
    assert(!kws_verified_init(c, bytes, &kws_verified_primary, &kws_verified_secondary, &untrained, &out));
    kws_verified_threshold(c, INT16_MIN);
    for (unsigned i = 1; i <= 256; ++i) {
        assert(!kws_verified_feed_512_armed(c, block, (uint64_t)i * 512, true, &x));
        assert(!x.detected);
    }
    no_allocation = false;
    free(c); free(b); free(allocation);
    printf("verified shared frontend, typed prime epoch=%llu, guards, no allocation, clock gap, disarm, reset, untrained suppression passed; workspace=%zu seed=%zu\n",
        (unsigned long long)epoch, bytes, sizeof(kws_verified_seed_t));
    return 0;
}
