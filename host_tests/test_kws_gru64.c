#include "kws_gru64.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static kws_gru64_model_t model = {.abi = KWS_GRU64_ABI};
static kws_gru64_model_t before_model;

int main(void)
{
    struct { uint32_t pre; kws_gru64_state_t value; uint32_t post; } state;
    struct { uint32_t pre; kws_gru64_scratch_t value; uint32_t post; } scratch;
    /* Extra backing storage makes even the rejected aliased/misaligned test
     * pointers cover the API's declared input/output spans. */
    struct { uint32_t pre; int16_t value[24]; uint32_t post; } output;
    memset(&state, 0xa5, sizeof(state));
    memset(&scratch, 0xa5, sizeof(scratch));
    memset(&output, 0xa5, sizeof(output));
    int8_t input[40] = {0};
    kws_gru64_state_t before_state = state.value;
    kws_gru64_scratch_t before_scratch = scratch.value;
    int16_t before_output[24]; memcpy(before_output, output.value, sizeof(before_output));
    /* Every pair of mutable regions, read-only region, misalignment and null
     * is rejected before writes; the ABI tag belongs to the protected model. */
#define REJECT(m, s, x, t, o) do { \
    before_model = model; \
    assert(kws_gru64_step((m), (s), (x), (t), (o)) == -1); \
    assert(!memcmp(&model, &before_model, sizeof(model))); \
    assert(!memcmp(&state.value, &before_state, sizeof(before_state))); \
    assert(!memcmp(&scratch.value, &before_scratch, sizeof(before_scratch))); \
    assert(!memcmp(output.value, before_output, sizeof(before_output))); \
} while (0)
    REJECT(NULL, &state.value, input, &scratch.value, output.value);
    REJECT(&model, NULL, input, &scratch.value, output.value);
    REJECT(&model, &state.value, NULL, &scratch.value, output.value);
    REJECT(&model, &state.value, input, NULL, output.value);
    REJECT(&model, &state.value, input, &scratch.value, NULL);
    REJECT(&model, &state.value, input, (kws_gru64_scratch_t *)&state.value, output.value);
    REJECT(&model, &state.value, input, &scratch.value, state.value.hidden + 1);
    REJECT(&model, &state.value, input, &scratch.value, scratch.value.next + 1);
    REJECT(&model, (kws_gru64_state_t *)&model, input, &scratch.value, output.value);
    REJECT(&model, &state.value, input, (kws_gru64_scratch_t *)&model, output.value);
    REJECT(&model, &state.value, input, &scratch.value, (int16_t *)&model);
    REJECT(&model, &state.value, (const int8_t *)&state.value, &scratch.value, output.value);
    REJECT(&model, &state.value, (const int8_t *)&scratch.value, &scratch.value, output.value);
    REJECT(&model, &state.value, (const int8_t *)output.value, &scratch.value, output.value);
    REJECT((const kws_gru64_model_t *)((const char *)&model + 2), &state.value, input, &scratch.value, output.value);
    REJECT(&model, (kws_gru64_state_t *)((char *)&state.value + 1), input, &scratch.value, output.value);
    REJECT(&model, &state.value, input, (kws_gru64_scratch_t *)((char *)&scratch.value + 1), output.value);
    REJECT(&model, &state.value, input, &scratch.value, (int16_t *)((char *)output.value + 1));
    for (unsigned i = 0; i < 3; ++i) {
        model.abi = i == 0 ? 0 : i == 1 ? KWS_GRU64_ABI ^ 1u : UINT32_MAX;
        REJECT(&model, &state.value, input, &scratch.value, output.value);
    }
    model.abi = KWS_GRU64_ABI;
    assert(kws_gru64_reset(NULL) == -1);
    assert(kws_gru64_reset((kws_gru64_state_t *)((char *)&state.value + 1)) == -1);
    assert(!memcmp(&state.value, &before_state, sizeof(before_state)));
    assert(!kws_gru64_reset(&state.value));
    int16_t zero[64] = {0};
    assert(!memcmp(&state.value, zero, sizeof(zero)));
    /* A valid reset-after equation gates recurrent n bias. */
    for (unsigned i = 0; i < 64; ++i) {
        model.input_bias[2][i] = -512;
        model.recurrent_bias[2][i] = 1024;
    }
    assert(!kws_gru64_step(&model, &state.value, input, &scratch.value, output.value));
    assert(!memcmp(&state.value, zero, sizeof(zero)));
    for (unsigned phase = 0; phase < 8; ++phase) {
        for (unsigned g = 0; g < 3; ++g) for (unsigned i = 0; i < 64; ++i) {
            memset(model.input[g][i], phase & 1 ? 127 : -128, 40);
            memset(model.recurrent[g][i], phase & 2 ? 127 : -128, 64);
            model.input_bias[g][i] = phase & 4 ? 32767 : -32768;
            model.recurrent_bias[g][i] = phase & 4 ? -32768 : 32767;
            state.value.hidden[i] = phase & 2 ? 32767 : -32768;
        }
        memset(input, phase & 1 ? 127 : -128, sizeof(input));
        for (unsigned i = 0; i < 64; ++i)
            model.output[0][i] = -128, model.output[1][i] = 127;
        for (unsigned frame = 0; frame < 256; ++frame)
            assert(!kws_gru64_step(&model, &state.value, input, &scratch.value, output.value));
    }
    assert(state.pre == UINT32_C(0xa5a5a5a5) && state.post == state.pre);
    assert(scratch.pre == UINT32_C(0xa5a5a5a5) && scratch.post == scratch.pre);
    assert(output.pre == UINT32_C(0xa5a5a5a5) && output.post == output.pre);
    assert(!memcmp(output.value + 2, before_output + 2, sizeof(before_output) - 4));
    printf("GRU64 tag/alias/alignment/reset/sentinels and 2048 extreme steps passed\n");
    return 0;
}
