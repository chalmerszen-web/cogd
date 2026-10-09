#include "phrase_head.h"
#include <limits.h>

bool kws_phrase_head_valid(const kws_layer_t *layer)
{
    if (!layer || !layer->weights || !layer->bias || !layer->shift ||
        layer->inputs != KWS_PHRASE_INPUTS || layer->outputs != KWS_PHRASE_CLASSES ||
        layer->kernel != 1 || layer->dilation != 1 || layer->depthwise || layer->relu)
        return false;
    const int32_t margin = KWS_PHRASE_INPUTS * 16384;
    /* Also preserve exact FP32 integer accumulation in the training contract. */
    for (unsigned i = 0; i < KWS_PHRASE_CLASSES; ++i)
        if (layer->shift[i] < -31 || layer->shift[i] > 31 ||
            layer->bias[i] >= (1 << 24) - margin ||
            layer->bias[i] <= -(1 << 24) + margin)
            return false;
    return true;
}

int16_t kws_phrase_head_step(const kws_layer_t *layer,
    const int8_t representation[KWS_PHRASE_INPUTS], int16_t logits[KWS_PHRASE_CLASSES])
{
    int16_t temporary[KWS_PHRASE_CLASSES];
    int16_t *out = logits ? logits : temporary;
    for (unsigned oc = 0; oc < KWS_PHRASE_CLASSES; ++oc) {
        int32_t acc = layer->bias[oc];
        for (unsigned ic = 0; ic < KWS_PHRASE_INPUTS; ++ic)
            acc += (int32_t)representation[ic] * layer->weights[oc * KWS_PHRASE_INPUTS + ic];
        out[oc] = (int16_t)kws_requantize(acc, layer->shift[oc], INT16_MIN, INT16_MAX);
    }
    int16_t other = out[1];
    for (unsigned i = 2; i < KWS_PHRASE_CLASSES; ++i)
        if (out[i] > other) other = out[i];
    int32_t score = (int32_t)out[0] - other;
    return (int16_t)(score > INT16_MAX ? INT16_MAX : score < INT16_MIN ? INT16_MIN : score);
}
