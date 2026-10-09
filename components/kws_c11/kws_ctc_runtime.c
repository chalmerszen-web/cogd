#include "kws_ctc_runtime_internal.h"
#include <stdalign.h>
#include <string.h>

size_t kws_ctc_stream_size(void) { return sizeof(kws_ctc_stream_t); }
size_t kws_ctc_stream_alignment(void) { return alignof(kws_ctc_stream_t); }

static void prime(kws_neural_t *n, const kws_model_t *m)
{
    /* The deployed log-mel frontend maps all-zero PCM to logmel0. */
    for (unsigned i = 0; i < KWS_BANDS; ++i) {
        int32_t value = -(int32_t)m->mean_q8[i] * m->inverse_std_q12[i];
        n->current[i] = (int8_t)kws_requantize(value, 15, -128, 127);
    }
    unsigned history_offset = 0;
    for (unsigned index = 0; index < KWS_LAYERS; ++index) {
        const kws_layer_t *l = &m->layers[index];
        unsigned past = (l->kernel - 1) * l->dilation;
        for (unsigned frame = 0; frame < past; ++frame)
            memcpy(n->history + history_offset + frame * l->inputs,
                   n->current, l->inputs);
        history_offset += past * l->inputs;
        for (unsigned oc = 0; oc < l->outputs; ++oc) {
            int32_t acc = l->bias ? l->bias[oc] : 0;
            for (unsigned tap = 0; tap < l->kernel; ++tap) {
                if (l->depthwise)
                    acc += (int32_t)n->current[oc] * l->weights[oc * l->kernel + tap];
                else for (unsigned ic = 0; ic < l->inputs; ++ic)
                    acc += (int32_t)n->current[ic] * l->weights[(oc * l->kernel + tap) * l->inputs + ic];
            }
            if (index < 11)
                n->next[oc] = (int8_t)kws_requantize(acc, l->shift[oc], l->relu ? 0 : -128, 127);
        }
        if (index < 11) memcpy(n->current, n->next, l->outputs);
    }
    /* Every causal ring length divides128; all positions are already0. */
}

void kws_ctc_stream_reset(kws_ctc_stream_t *s)
{
    if (!s) return;
    const kws_ctc_model_t *model = s->model;
    memset(s, 0, sizeof(*s));
    s->model = model;
    prime(&s->signal.neural, model->features);
}

int kws_ctc_stream_init(void *memory, size_t bytes,
    const kws_ctc_model_t *model, kws_ctc_stream_t **out)
{
    if (!out) return -1;
    *out = NULL;
    if (!memory || bytes < sizeof(kws_ctc_stream_t) ||
        (uintptr_t)memory % alignof(kws_ctc_stream_t) || !model ||
        !kws_model_valid(model->features) || model->features->trained ||
        !kws_ctc_head_valid(model->tokens)) return -1;
    kws_ctc_stream_t *s = memory;
    s->model = model;
    kws_ctc_stream_reset(s);
    *out = s;
    return 0;
}

int kws_ctc_stream_feed(kws_ctc_stream_t *s, const int16_t pcm[512],
    uint64_t end, bool armed, kws_ctc_output_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!s || !pcm || !out || end < 512 || end % 512) {
        if (s) kws_ctc_stream_reset(s);
        return -1;
    }
    out->sample_end = end;
    if (s->last_sample && (end <= s->last_sample || end - s->last_sample != 512))
        kws_ctc_stream_reset(s);
    if (!armed) {
        if (s->armed) kws_ctc_stream_reset(s);
        s->last_sample = end;
        return 0;
    }
    if (!s->armed) kws_ctc_stream_reset(s);
    s->armed = true;
    s->last_sample = end;
    const kws_frontend_buffers_t frontend = {s->signal.previous,
        s->signal.fft_in, s->signal.fft_out, s->signal.power};
    for (unsigned half = 0; half < 2; ++half) {
        kws_frontend_buffers_step(&frontend, s->model->features,
            pcm + half * 256, s->features, NULL);
        kws_neural_step(&s->signal.neural, s->model->features, s->features, NULL);
    }
    if (kws_ctc_head_step(s->model->tokens, s->signal.neural.current, out->logits)) {
        kws_ctc_stream_reset(s);
        return -1;
    }
    out->phrase = kws_ctc_step(&s->sequence, out->logits, end, s->model->trained);
    return 0;
}
