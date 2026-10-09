#include "kws_conditioned.h"
#include <limits.h>
#include <string.h>

bool kws_conditioned_valid(const kws_model_t *model)
{
    if (!model || !model->layers) return false;
    for (unsigned i = 0; i < KWS_LAYERS; ++i) {
        const kws_layer_t *l = model->layers + i;
        bool dw = i && i < 11 && (i & 1);
        unsigned kernel = i == 0 ? 3 : dw ? 5 : 1;
        if (!l->weights || (!dw && !l->bias) || !l->shift ||
            l->inputs != (i ? 24 : KWS_CONDITIONED_INPUTS) || l->outputs != (i == 11 ? 1 : 24) ||
            l->kernel != kernel || l->dilation != (dw ? 1u << ((i - 1) / 2) : 1) ||
            l->depthwise != dw || l->relu != (i < 11 && !dw)) return false;
        int32_t margin = (int32_t)(kernel * (dw ? 1 : l->inputs) * 16384u);
        for (unsigned oc = 0; oc < l->outputs; ++oc)
            if (l->shift[oc] < -31 || l->shift[oc] > 31 || (l->bias &&
                (l->bias[oc] > INT32_MAX - margin || l->bias[oc] < INT32_MIN + margin))) return false;
    }
    return true;
}

void kws_conditioned_reset(kws_conditioned_t *state)
{ memset(state, 0, sizeof(*state)); }

int16_t kws_conditioned_step(kws_conditioned_t *state, const kws_model_t *model,
                           const int8_t input[KWS_CONDITIONED_INPUTS], int16_t trace[265])
{
    memmove(state->current, input, KWS_CONDITIONED_INPUTS);
    unsigned history_offset = 0, slot = 0, trace_offset = 0;
    int16_t score = 0;
    for (unsigned index = 0; index < KWS_LAYERS; ++index) {
        const kws_layer_t *l = model->layers + index;
        unsigned past = (l->kernel - 1) * l->dilation;
        unsigned pos = past ? state->position[slot] : 0;
        int8_t *history = state->history + history_offset;
        for (unsigned oc = 0; oc < l->outputs; ++oc) {
            int32_t acc = l->bias ? l->bias[oc] : 0;
            for (unsigned tap = 0; tap < l->kernel; ++tap) {
                unsigned delay = (l->kernel - 1 - tap) * l->dilation;
                const int8_t *x = delay ? history + ((pos + past - delay) % past) * l->inputs : state->current;
                if (l->depthwise) acc += (int32_t)x[oc] * l->weights[oc * l->kernel + tap];
                else for (unsigned ic = 0; ic < l->inputs; ++ic)
                    acc += (int32_t)x[ic] * l->weights[(oc * l->kernel + tap) * l->inputs + ic];
            }
            int16_t value = (int16_t)kws_requantize(acc, l->shift[oc],
                l->relu ? 0 : index == 11 ? -32768 : -128, index == 11 ? 32767 : 127);
            if (trace) trace[trace_offset + oc] = value;
            if (index == 11) score = value;
            else state->next[oc] = (int8_t)value;
        }
        if (past) {
            memcpy(history + pos * l->inputs, state->current, l->inputs);
            state->position[slot++] = (uint8_t)((pos + 1) % past);
            history_offset += past * l->inputs;
        }
        if (index < 11) memcpy(state->current, state->next, l->outputs);
        trace_offset += l->outputs;
    }
    return score;
}
