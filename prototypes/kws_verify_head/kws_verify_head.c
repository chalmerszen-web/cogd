#include "kws_verify_head.h"
#include "kws.h"

bool kws_verify_weights_valid(const kws_verify_weights_t *weights)
{
    if (!weights || weights->output_bias < -KWS_VERIFY_BIAS_LIMIT ||
        weights->output_bias > KWS_VERIFY_BIAS_LIMIT) return false;
    for (unsigned i = 0; i < KWS_VERIFY_HIDDEN; ++i)
        if (weights->hidden_bias[i] < -KWS_VERIFY_BIAS_LIMIT ||
            weights->hidden_bias[i] > KWS_VERIFY_BIAS_LIMIT) return false;
    return true;
}

int16_t kws_verify_score(const kws_verify_weights_t *weights,
                       const int8_t features[KWS_VERIFY_INPUTS])
{
    int8_t hidden[KWS_VERIFY_HIDDEN];
    for (unsigned i = 0; i < KWS_VERIFY_HIDDEN; ++i) {
        int32_t sum = weights->hidden_bias[i];
        for (unsigned j = 0; j < KWS_VERIFY_INPUTS; ++j)
            sum += (int32_t)features[j] * weights->input_weight[i][j];
        hidden[i] = (int8_t)kws_requantize(sum, 8, 0, 127);
    }
    int32_t sum = weights->output_bias;
    for (unsigned i = 0; i < KWS_VERIFY_HIDDEN; ++i)
        sum += (int32_t)hidden[i] * weights->output_weight[i];
    return (int16_t)kws_requantize(sum, 3, -32768, 32767);
}
