#ifndef KWS_GRU_KERNEL_H
#define KWS_GRU_KERNEL_H
/* Private math shared by the fixed 32/64 Q8/Q6 contracts.
 * Each wrapper validates its own public layout before using these helpers. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "generated/gru32_sigmoid.inc"

_Static_assert(CHAR_BIT == 8 && sizeof(int32_t) == 4, "Fixed integer ABI");

typedef struct {
    const void *input, *recurrent, *input_bias, *recurrent_bias, *output;
    const int16_t *output_bias;
} kws_gru_weights_t;

#define KWS_GRU_WEIGHTS(m) ((kws_gru_weights_t){ \
    &(m)->input, &(m)->recurrent, &(m)->input_bias, &(m)->recurrent_bias, \
    &(m)->output, (m)->output_bias })

static inline int32_t kws_gru_round(int32_t value, int32_t divisor)
{
    /* Supported formats stay within INT32, including this rounding offset.
     * Division avoids implementation-defined signed right shifts. */
    return value < 0 ? -((-value + divisor / 2) / divisor)
                     : (value + divisor / 2) / divisor;
}

static inline int16_t kws_gru_saturate(int32_t value)
{
    return (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
}

static inline uint16_t kws_gru_sigmoid(int32_t q8)
{
    if (q8 <= -4096) return 0;
    if (q8 >= 4096) return 32768;
    unsigned position = (unsigned)(q8 + 4096), index = position / 16, part = position % 16;
    return (uint16_t)((gru_sigmoid[index] * (16u - part) + gru_sigmoid[index + 1] * part + 8u) / 16u);
}

static inline int32_t kws_gru_dot_input(const int8_t *weight, const int8_t *input, int32_t divisor)
{
    int32_t sum = 0;
    for (unsigned i = 0; i < 40; ++i) sum += (int32_t)weight[i] * input[i];
    return kws_gru_round(sum, divisor);
}

static inline int32_t kws_gru_dot_hidden(const int8_t *weight, const int16_t *state,
                                      unsigned width, int32_t divisor)
{
    int32_t sum = 0;
    for (unsigned i = 0; i < width; ++i) sum += (int32_t)weight[i] * state[i];
    return kws_gru_round(sum, divisor);
}

static inline int kws_gru_overlap(const void *first, size_t first_size,
                                  const void *second, size_t second_size)
{
    uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    return a >= b ? a - b < second_size : b - a < first_size;
}

static inline int kws_gru_valid(const void *model, size_t model_size, size_t model_alignment,
    const void *state, const int8_t *input, const void *scratch, const int16_t *output, unsigned width)
{
    if (!model || !state || !input || !scratch || !output ||
        (uintptr_t)model % model_alignment || (uintptr_t)state % _Alignof(int16_t) ||
        (uintptr_t)scratch % _Alignof(int16_t) || (uintptr_t)output % _Alignof(int16_t)) return 0;
    size_t bytes = width * sizeof(int16_t);
    return !kws_gru_overlap(state, bytes, scratch, bytes) &&
        !kws_gru_overlap(state, bytes, output, 4) &&
        !kws_gru_overlap(scratch, bytes, output, 4) &&
        !kws_gru_overlap(state, bytes, model, model_size) &&
        !kws_gru_overlap(state, bytes, input, 40) &&
        !kws_gru_overlap(scratch, bytes, model, model_size) &&
        !kws_gru_overlap(scratch, bytes, input, 40) &&
        !kws_gru_overlap(output, 4, model, model_size) &&
        !kws_gru_overlap(output, 4, input, 40);
}

static inline void kws_gru_step(kws_gru_weights_t model, unsigned width, int16_t *state,
    const int8_t input[40], int16_t *scratch, int16_t output[2],
    int32_t input_divisor, int32_t hidden_divisor)
{
    /* Bounds with INT8_MIN/INT16_MIN included:
     * 32/Q6: recurrent dot<=134217728, h<=49152, r*h<=1610612736.
     * 64/Q8: recurrent dot<=268435456, h<=40960, r*h<=1342177280.
     * Input dot<=655360; convex state update<=1073741824.
     * 64/Q6: h in [-65535,65535], r*h in [-2147450880,2147450880];
     * abs(r*h)+16384=2147467264<INT32_MAX, including round_divide. */
    /* Variably modified pointer types describe the existing fixed arrays;
     * no variable-length object is allocated. Preserve nested-array bounds. */
    const int8_t (*input_weights)[width][40] = (const int8_t (*)[width][40])model.input;
    const int8_t (*recurrent_weights)[width][width] = (const int8_t (*)[width][width])model.recurrent;
    const int16_t (*input_bias)[width] = (const int16_t (*)[width])model.input_bias;
    const int16_t (*recurrent_bias)[width] = (const int16_t (*)[width])model.recurrent_bias;
    const int8_t (*output_weights)[width] = (const int8_t (*)[width])model.output;
    for (unsigned i = 0; i < width; ++i) {
        int32_t x[3], h[3];
        for (unsigned gate = 0; gate < 3; ++gate) {
            x[gate] = kws_gru_dot_input(input_weights[gate][i], input, input_divisor) + input_bias[gate][i];
            h[gate] = kws_gru_dot_hidden(recurrent_weights[gate][i], state, width, hidden_divisor) + recurrent_bias[gate][i];
        }
        uint16_t reset = kws_gru_sigmoid(x[0] + h[0]), update = kws_gru_sigmoid(x[1] + h[1]);
        int32_t drive = x[2] + kws_gru_round((int32_t)reset * h[2], 32768);
        int16_t next = kws_gru_saturate(2 * (int32_t)kws_gru_sigmoid(2 * drive) - 32768);
        scratch[i] = kws_gru_saturate(kws_gru_round((32768 - (int32_t)update) * next +
                                                   (int32_t)update * state[i], 32768));
    }
    memcpy(state, scratch, width * sizeof(*state));
    for (unsigned i = 0; i < 2; ++i)
        output[i] = kws_gru_saturate(kws_gru_dot_hidden(output_weights[i], state, width,
                                                      hidden_divisor) + model.output_bias[i]);
}
#endif
