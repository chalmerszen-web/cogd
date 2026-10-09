#include "kws_ctc_head.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static int16_t oracle(int64_t value, int shift)
{
    int64_t magnitude = value < 0 ? -value : value;
    if (shift > 0) magnitude = (magnitude + ((int64_t)1 << (shift - 1))) >> shift;
    else if (shift < 0) magnitude *= (int64_t)1 << -shift;
    value = value < 0 ? -magnitude : magnitude;
    return (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
}

int main(void)
{
    int8_t weights[KWS_CHANNELS * KWS_CTC_CLASSES] = {0}, shifts[KWS_CTC_CLASSES] = {0};
    int32_t biases[KWS_CTC_CLASSES] = {0};
    int8_t input[KWS_CHANNELS]; int16_t logits[KWS_CTC_CLASSES];
    kws_layer_t layer = {weights, biases, shifts, KWS_CHANNELS, KWS_CTC_CLASSES, 1, 1, 0, 0};
    assert(kws_ctc_head_valid(&layer));
    uint32_t rng = 447;
    for (unsigned row = 0; row < 1000; ++row) {
        for (unsigned i = 0; i < sizeof(weights); ++i) {
            rng = rng * 1664525u + 1013904223u;
            weights[i] = (int8_t)((int)(rng % 256) - 128);
        }
        for (unsigned i = 0; i < KWS_CHANNELS; ++i) {
            rng = rng * 1664525u + 1013904223u;
            input[i] = (int8_t)((int)(rng % 256) - 128);
        }
        for (unsigned c = 0; c < KWS_CTC_CLASSES; ++c) {
            shifts[c] = (int8_t)((int)((row + c) % 63) - 31);
            biases[c] = (int32_t)row - 500;
        }
        if (row % 3 == 0) biases[0] = INT32_MAX - KWS_CHANNELS * 16384;
        if (row % 3 == 1) biases[0] = INT32_MIN + KWS_CHANNELS * 16384;
        assert(!kws_ctc_head_step(&layer, input, logits));
        for (unsigned c = 0; c < KWS_CTC_CLASSES; ++c) {
            int64_t acc = biases[c];
            for (unsigned i = 0; i < KWS_CHANNELS; ++i)
                acc += (int64_t)input[i] * weights[c * KWS_CHANNELS + i];
            assert(logits[c] == oracle(acc, shifts[c]));
        }
    }
    assert(!kws_ctc_head_valid(NULL));
    for (unsigned kind = 0; kind < 10; ++kind) {
        kws_layer_t bad = layer;
        if (kind == 0) bad.inputs--;
        if (kind == 1) bad.outputs = 1; /* legacy scalar ABI */
        if (kind == 2) bad.kernel = 3;
        if (kind == 3) bad.dilation = 2;
        if (kind == 4) bad.depthwise = 1;
        if (kind == 5) bad.relu = 1;
        if (kind == 6) bad.weights = NULL;
        if (kind == 7) bad.bias = NULL;
        if (kind == 8) bad.shift = NULL;
        if (kind == 9) shifts[0] = 32;
        assert(!kws_ctc_head_valid(&bad));
        for (unsigned c = 0; c < KWS_CTC_CLASSES; ++c) logits[c] = 777;
        assert(kws_ctc_head_step(&bad, input, logits) == -1);
        for (unsigned c = 0; c < KWS_CTC_CLASSES; ++c) assert(!logits[c]);
    }
    shifts[0] = 0; biases[0] = INT32_MAX;
    assert(!kws_ctc_head_valid(&layer));
    biases[0] = INT32_MIN; assert(!kws_ctc_head_valid(&layer));
    biases[0] = 0; assert(kws_ctc_head_valid(&layer));
    assert(kws_ctc_head_step(&layer, NULL, logits) == -1);
    assert(kws_ctc_head_step(&layer, input, NULL) == -1);
    printf("{\"channels\":%u,\"independent_head_values\":14000,\"invalid_guards\":14}\n", KWS_CHANNELS);
    return 0;
}
