#include "kws_internal.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const kws_model_t kws_mixed_primary24, kws_mixed_secondary24, kws_trained_model;
static bool no_allocation;
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n) { assert(!no_allocation); return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t m) { assert(!no_allocation); return __real_calloc(n, m); }
void *__wrap_realloc(void *p, size_t n) { assert(!no_allocation); return __real_realloc(p, n); }
void __wrap_free(void *p) { assert(!no_allocation); __real_free(p); }

int main(void)
{
    _Static_assert(KWS_CHANNELS == 48 && KWS_TRACE_VALUES == 529, "Capacity ABI stays48");
    assert(kws_model_valid(&kws_mixed_primary24));
    assert(kws_model_valid(&kws_mixed_secondary24));
    assert(kws_model_valid(&kws_trained_model));
    kws_layer_t layers[12];
    memcpy(layers, kws_mixed_primary24.layers, sizeof(layers));
    kws_model_t wrong = kws_mixed_primary24; wrong.layers = layers;
    layers[0].outputs = 36; assert(!kws_model_valid(&wrong));
    layers[0] = kws_mixed_primary24.layers[0];
    layers[4].inputs = 48; assert(!kws_model_valid(&wrong));
    layers[4] = kws_mixed_primary24.layers[4];
    int32_t bias[48] = {0}; bias[23] = INT32_MAX; layers[2].bias = bias;
    assert(!kws_model_valid(&wrong));
    bias[23] = INT32_MIN; assert(!kws_model_valid(&wrong));

    size_t bytes = kws_workspace_size();
    unsigned char *allocation = malloc(bytes + 128); assert(allocation);
    void *memory = allocation + 64;
    const kws_model_t *models[] = {&kws_mixed_primary24, &kws_mixed_secondary24, &kws_trained_model};
    uint32_t random = 2026100278;
    for (unsigned model = 0; model < 3; ++model) {
        memset(allocation, 0xa5, bytes + 128); kws_handle_t *state = NULL;
        assert(kws_init((char *)memory + 4, bytes, models[model], &state) < 0 && !state);
        assert(kws_init(memory, bytes - 1, models[model], &state) < 0 && !state);
        assert(!kws_init(memory, bytes, models[model], &state));
        no_allocation = true;
        kws_set_threshold(state, 268);
        for (unsigned block = 0; block < 128; ++block) {
            int16_t pcm[512]; kws_event_t event;
            for (unsigned i = 0; i < 512; ++i) {
                random = random * 1664525u + 1013904223u;
                unsigned value = random >> 16;
                pcm[i] = block == 0 ? 0 : block == 1 ? INT16_MAX : block == 2 ? INT16_MIN :
                    (int16_t)(value <= INT16_MAX ? (int)value : (int)value - 65536);
            }
            assert(!kws_feed_512_armed(state, pcm, (uint64_t)(block + 1) * 512, block >= 16, &event));
            assert(event.score_q8 == state->trace.layer[11 * models[model]->layers[0].outputs]);
            for (unsigned i = 0; i < 64; ++i)
                assert(allocation[i] == 0xa5 && allocation[64 + bytes + i] == 0xa5);
        }
        no_allocation = false;
    }
    free(allocation);
    printf("mixed registered24/48: malformed layouts, overflow,384PCM blocks, head offsets, guards and no allocation passed; workspace=%zu neural=%zu\n",
        bytes, sizeof(kws_neural_t));
    return 0;
}
