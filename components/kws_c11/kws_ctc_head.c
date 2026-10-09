#include "kws_ctc_head.h"
#include <limits.h>
#include <string.h>

bool kws_ctc_head_valid(const kws_layer_t *l)
{
    if (!l || !l->weights || !l->bias || !l->shift || l->inputs != KWS_CHANNELS ||
        l->outputs != KWS_CTC_CLASSES || l->kernel != 1 || l->dilation != 1 ||
        l->depthwise || l->relu) return false;
    const int32_t margin = KWS_CHANNELS * 16384;
    for (unsigned c = 0; c < KWS_CTC_CLASSES; ++c)
        if (l->shift[c] < -31 || l->shift[c] > 31 ||
            l->bias[c] > INT32_MAX - margin || l->bias[c] < INT32_MIN + margin)
            return false;
    return true;
}

int kws_ctc_head_step(const kws_layer_t *l, const int8_t *input,
                     int16_t logits[KWS_CTC_CLASSES])
{
    if (!logits) return -1;
    memset(logits, 0, KWS_CTC_CLASSES * sizeof(*logits));
    if (!input || !kws_ctc_head_valid(l)) return -1;
    for (unsigned c = 0; c < KWS_CTC_CLASSES; ++c) {
        int32_t acc = l->bias[c];
        for (unsigned i = 0; i < KWS_CHANNELS; ++i)
            acc += (int32_t)input[i] * l->weights[c * KWS_CHANNELS + i];
        logits[c] = (int16_t)kws_requantize(acc, l->shift[c], INT16_MIN, INT16_MAX);
    }
    return 0;
}
