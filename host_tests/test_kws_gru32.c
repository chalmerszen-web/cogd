#include "kws_gru32.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static kws_gru32_model_t model;
static uint32_t seed = 2026100475u;
static uint32_t random32(void)
{ seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static int64_t nearest(int64_t value, int64_t divisor)
{ return value < 0 ? -(int64_t)llround((double)-value / divisor) : (int64_t)llround((double)value / divisor); }
static int16_t clamp(int64_t value)
{ return (int16_t)(value < -32768 ? -32768 : value > 32767 ? 32767 : value); }
static int64_t logistic(int64_t value)
{
    if (value <= -4096) return 0;
    if (value >= 4096) return 32768;
    int64_t slot = (value + 4096) / 16, part = (value + 4096) % 16;
    double left = (double)(slot - 256) / 16;
    int64_t a = llround(32768 / (1 + exp(-left)));
    int64_t b = llround(32768 / (1 + exp(-left - 1.0 / 16)));
    return ((16 - part) * a + part * b + 8) / 16;
}
static void reference(const int8_t input[40], int16_t state[32], int16_t scores[2])
{
    int16_t next[32];
    for (unsigned neuron = 0; neuron < 32; ++neuron) {
        int64_t term[3][2];
        for (unsigned gate = 0; gate < 3; ++gate) {
            int64_t input_sum = 0, recurrent_sum = 0;
            for (unsigned feature = 0; feature < 40; ++feature)
                input_sum += (int64_t)input[feature] * model.input[gate][neuron][feature];
            for (unsigned hidden = 0; hidden < 32; ++hidden)
                recurrent_sum += (int64_t)state[hidden] * model.recurrent[gate][neuron][hidden];
            term[gate][0] = nearest(input_sum, 32) + model.input_bias[gate][neuron];
            term[gate][1] = nearest(recurrent_sum, 32768) + model.recurrent_bias[gate][neuron];
        }
        int64_t r = logistic(term[0][0] + term[0][1]);
        int64_t z = logistic(term[1][0] + term[1][1]);
        int64_t n = clamp(2 * logistic(2 * (term[2][0] + nearest(r * term[2][1], 32768))) - 32768);
        next[neuron] = clamp(nearest((32768 - z) * n + z * state[neuron], 32768));
    }
    memcpy(state, next, sizeof(next));
    for (unsigned output = 0; output < 2; ++output) {
        int64_t sum = 0;
        for (unsigned hidden = 0; hidden < 32; ++hidden)
            sum += (int64_t)model.output[output][hidden] * state[hidden];
        scores[output] = clamp(nearest(sum, 32768) + model.output_bias[output]);
    }
}
static void random_model(void)
{
    /* Build typed values; no dependence on object padding or signed casts. */
    for (unsigned g = 0; g < 3; ++g) for (unsigned h = 0; h < 32; ++h) {
        for (unsigned i = 0; i < 40; ++i) model.input[g][h][i] = (int8_t)((int)(random32() % 256) - 128);
        for (unsigned i = 0; i < 32; ++i) model.recurrent[g][h][i] = (int8_t)((int)(random32() % 256) - 128);
        model.input_bias[g][h] = (int16_t)((int)(random32() % 65536) - 32768);
        model.recurrent_bias[g][h] = (int16_t)((int)(random32() % 65536) - 32768);
    }
    for (unsigned o = 0; o < 2; ++o) {
        for (unsigned h = 0; h < 32; ++h) model.output[o][h] = (int8_t)((int)(random32() % 256) - 128);
        model.output_bias[o] = (int16_t)((int)(random32() % 65536) - 32768);
    }
}
int main(void)
{
    struct { uint32_t pre; kws_gru32_state_t value; uint32_t post; } state = {0xabcdfedc, {{0}}, 0xabcdfedc};
    struct { uint32_t pre; kws_gru32_scratch_t value; uint32_t post; } scratch = {0xabcdfedc, {{0}}, 0xabcdfedc};
    int16_t expected[32] = {0}, output[2], scores[2];
    int8_t input[40];
    random_model();
    unsigned frames = 0;
    for (unsigned frame = 0; frame < 2048; ++frame) {
        if (!(frame % 128)) {
            random_model();
            for (unsigned h = 0; h < 32; ++h)
                state.value.hidden[h] = expected[h] = (int16_t)((int)(random32() % 65536) - 32768);
        }
        for (unsigned i = 0; i < 40; ++i)
            input[i] = (int8_t)((int)(random32() % 256) - 128);
        if (!(frame % 16)) memset(input, -128, sizeof(input));
        if (frame % 16 == 1) memset(input, 127, sizeof(input));
        reference(input, expected, scores);
        assert(kws_gru32_step(&model, &state.value, input, &scratch.value, output) == 0);
        assert(!memcmp(state.value.hidden, expected, sizeof(expected)));
        assert(!memcmp(output, scores, sizeof(scores)));
        assert(state.pre == 0xabcdfedc && state.post == 0xabcdfedc);
        assert(scratch.pre == 0xabcdfedc && scratch.post == 0xabcdfedc);
        ++frames;
    }
    /* These extrema also reach maximum dot and reset-product magnitudes. */
    for (unsigned phase = 0; phase < 8; ++phase) {
        memset(&model, 0, sizeof(model));
        for (unsigned g = 0; g < 3; ++g) for (unsigned h = 0; h < 32; ++h) {
            memset(model.input[g][h], phase & 1 ? 127 : -128, 40);
            memset(model.recurrent[g][h], phase & 2 ? 127 : -128, 32);
            model.input_bias[g][h] = phase & 4 ? 32767 : -32768;
            model.recurrent_bias[g][h] = phase & 4 ? -32768 : 32767;
            state.value.hidden[h] = expected[h] = phase & 2 ? 32767 : -32768;
        }
        memset(input, phase & 1 ? 127 : -128, sizeof(input));
        reference(input, expected, scores);
        assert(!kws_gru32_step(&model, &state.value, input, &scratch.value, output));
        assert(!memcmp(expected, state.value.hidden, sizeof(expected)) && !memcmp(output, scores, sizeof(scores)));
        ++frames;
    }
    kws_gru32_state_t old_state = state.value;
    kws_gru32_scratch_t old_scratch = scratch.value;
    int16_t old_output[2]; memcpy(old_output, output, sizeof(output));
    assert(kws_gru32_step(NULL, &state.value, input, &scratch.value, output) == -1);
    assert(kws_gru32_step(&model, NULL, input, &scratch.value, output) == -1);
    assert(kws_gru32_step(&model, &state.value, NULL, &scratch.value, output) == -1);
    assert(kws_gru32_step(&model, &state.value, input, NULL, output) == -1);
    assert(kws_gru32_step(&model, &state.value, input, &scratch.value, NULL) == -1);
    assert(kws_gru32_step(&model, &state.value, input, (kws_gru32_scratch_t *)&state.value, output) == -1);
    assert(kws_gru32_step(&model, &state.value, (const int8_t *)state.value.hidden, &scratch.value, output) == -1);
    assert(kws_gru32_step(&model, &state.value, input, &scratch.value, state.value.hidden + 1) == -1);
    assert(kws_gru32_step(&model, &state.value, input, &scratch.value, scratch.value.next + 1) == -1);
    assert(!memcmp(&state.value, &old_state, sizeof(old_state)));
    assert(!memcmp(&scratch.value, &old_scratch, sizeof(old_scratch)));
    assert(!memcmp(output, old_output, sizeof(output)));
    assert(kws_gru32_reset(NULL) == -1 && !kws_gru32_reset(&state.value));
    memset(expected, 0, sizeof(expected));
    assert(!memcmp(expected, state.value.hidden, sizeof(expected)));
    /* Recurrent n-bias is reset-gated; input n-bias is not. */
    memset(&model, 0, sizeof(model)); memset(input, 0, sizeof(input));
    for (unsigned h = 0; h < 32; ++h) {
        model.input_bias[2][h] = -512;
        model.recurrent_bias[2][h] = 1024;
    }
    assert(!kws_gru32_step(&model, &state.value, input, &scratch.value, output));
    assert(!memcmp(expected, state.value.hidden, sizeof(expected)));
    printf("gru32: %u independent INT64 frames, %u states, extrema/alias/reset/bias passed\n", frames, frames * 32);
    return 0;
}
