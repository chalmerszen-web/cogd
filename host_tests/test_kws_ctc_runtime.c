#include "kws_ctc_runtime_internal.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static kws_layer_t layers[12], head;
static int8_t weights[20000], shifts[12][48], head_weights[14 * KWS_CHANNELS], head_shift[14];
static int32_t bias[12][48], head_bias[14];
static int16_t mean[40];
static uint16_t inverse[40];
static kws_model_t base;
static kws_ctc_model_t model;
static unsigned frontend_calls;

/* Synthetic acoustic adapter: token code -> one-hot feature. Not real PCM. */
void kws_frontend(kws_handle_t *k, const int16_t pcm[256], kws_trace_t *trace)
{
    ++frontend_calls;
    assert(pcm[0] >= 0 && pcm[0] < KWS_CTC_CLASSES);
    memset(trace->input, 0, 40);
    trace->input[pcm[0]] = 64;
    memcpy(k->previous, pcm, 512);
}

void kws_frontend_buffers_step(const kws_frontend_buffers_t *buffers,
    const kws_model_t *features_model, const int16_t pcm[256],
    int8_t features[40], kws_trace_t *trace)
{
    (void)features_model;
    ++frontend_calls;
    assert(pcm[0] >= 0 && pcm[0] < KWS_CTC_CLASSES);
    memset(features, 0, 40);
    features[pcm[0]] = 64;
    memcpy(buffers->previous, pcm, 512);
    if (trace) memcpy(trace->input, features, 40);
}

static void fixture(unsigned variant)
{
    memset(weights, 0, sizeof(weights));
    memset(bias, 0, sizeof(bias));
    memset(shifts, 0, sizeof(shifts));
    memset(head_weights, 0, sizeof(head_weights));
    memset(head_bias, 0, sizeof(head_bias));
    memset(head_shift, 0, sizeof(head_shift));
    unsigned offset = 0;
    for (unsigned i = 0; i < 12; ++i) {
        unsigned dw = i > 0 && i < 11 && (i & 1);
        unsigned inputs = i ? KWS_CHANNELS : 40;
        unsigned outputs = i == 11 ? 1 : KWS_CHANNELS;
        unsigned kernel = i == 0 ? 3 : dw ? 5 : 1;
        unsigned count = outputs * kernel * (dw ? 1 : inputs);
        assert(offset + count <= sizeof(weights));
        layers[i] = (kws_layer_t){weights + offset, dw ? NULL : bias[i], shifts[i],
            (uint8_t)inputs, (uint8_t)outputs, (uint8_t)kernel,
            (uint8_t)(dw ? 1u << ((i - 1) / 2) : 1), (uint8_t)dw,
            (uint8_t)(i < 11 && !dw)};
        if (!variant && i < 11) {
            for (unsigned oc = 0; oc < outputs; ++oc)
                weights[offset + (dw ? oc * kernel + kernel - 1 :
                    (oc * kernel + kernel - 1) * inputs + oc % inputs)] = 1;
        } else if (variant) {
            for (unsigned j = 0; j < count; ++j)
                weights[offset + j] = (int8_t)((int)((j * 7 + i * 3 + variant) % 9) - 4);
            for (unsigned oc = 0; oc < outputs; ++oc) {
                shifts[i][oc] = (int8_t)(variant % 3 + 3);
                if (!dw) bias[i][oc] = (int32_t)((int)((oc + variant + i) % 7) - 3) * 97;
            }
        }
        offset += count;
    }
    for (unsigned i = 0; i < 40; ++i) {
        mean[i] = variant ? (int16_t)(((int)i % 5 - 2) * 128) : 0;
        inverse[i] = variant && i % 7 == 0 ? 65535 : 4096;
    }
    if (!variant) mean[0] = -256;
    else { mean[0] = INT16_MIN; mean[1] = INT16_MAX; }
    for (unsigned i = 0; i < 14; ++i) head_weights[i * KWS_CHANNELS + i] = 1;
    base = (kws_model_t){"synthetic_ctc", layers, mean, inverse, false};
    head = (kws_layer_t){head_weights, head_bias, head_shift, KWS_CHANNELS, 14, 1, 1, 0, 0};
    model = (kws_ctc_model_t){&base, &head, true};
}

static int8_t floor_input(unsigned i)
{
    int64_t value = -(int64_t)mean[i] * inverse[i];
    int64_t magnitude = value < 0 ? -value : value;
    int64_t result = (magnitude + 16384) / 32768;
    if (value < 0) result = -result;
    return (int8_t)(result < -128 ? -128 : result > 127 ? 127 : result);
}

static unsigned seed_checks(void)
{
    unsigned values = 0;
    for (unsigned variant = 0; variant < 8; ++variant) {
        fixture(variant);
        alignas(max_align_t) unsigned char memory[sizeof(kws_ctc_stream_t)];
        kws_ctc_stream_t *stream;
        assert(kws_ctc_stream_init(memory, sizeof(memory), &model, &stream) == 0);
        kws_handle_t expected;
        expected.model = &base;
        kws_reset(&expected);
        int8_t feature[40];
        for (unsigned i = 0; i < 40; ++i) feature[i] = floor_input(i);
        for (unsigned frame = 0; frame < 128; ++frame)
            kws_neural_step(&expected.neural, &base, feature, NULL);
        assert(memcmp(&stream->signal.neural, &expected.neural, sizeof(expected.neural)) == 0);
        uint32_t random = 452 + variant;
        for (unsigned frame = 0; frame < 256; ++frame) {
            for (unsigned i = 0; i < 40; ++i) {
                random = random * 1664525u + 1013904223u;
                feature[i] = (int8_t)(random >> 24);
            }
            kws_trace_t actual, reference;
            kws_neural_step(&stream->signal.neural, &base, feature, &actual);
            kws_neural_step(&expected.neural, &base, feature, &reference);
            assert(memcmp(actual.layer, reference.layer, sizeof(actual.layer)) == 0);
            assert(memcmp(&stream->signal.neural, &expected.neural, sizeof(expected.neural)) == 0);
            values += KWS_TRACE_VALUES;
        }
        kws_ctc_stream_reset(stream);
        assert(!stream->armed && !stream->last_sample);
    }
    return values;
}

static kws_ctc_match_t token(kws_ctc_stream_t *s, unsigned t, uint64_t end, bool armed)
{
    int16_t pcm[512] = {0};
    pcm[0] = pcm[256] = (int16_t)t;
    kws_ctc_output_t out;
    assert(kws_ctc_stream_feed(s, pcm, end, armed, &out) == 0);
    assert(out.sample_end == end);
    return out.phrase;
}

static void sequences(void)
{
    fixture(0);
    alignas(max_align_t) unsigned char memory[sizeof(kws_ctc_stream_t)];
    kws_ctc_stream_t *s;
    assert(kws_ctc_stream_init(memory, sizeof(memory), &model, &s) == 0);
    const unsigned zh[] = {1,2,3,4,0,0}, yue[] = {5,6,7,8,0,0};
    for (unsigned language = 0; language < 2; ++language) {
        const unsigned *phrase = language ? yue : zh;
        kws_ctc_stream_reset(s);
        for (unsigned i = 0; i < 6; ++i)
            assert(token(s, phrase[i], (i + 1) * 512, true) ==
                (i == 5 ? language ? KWS_CTC_CANTONESE : KWS_CTC_MANDARIN : KWS_CTC_NONE));
    }
    for (unsigned tail = 9; tail < 14; ++tail) {
        kws_ctc_stream_reset(s);
        for (unsigned i = 0; i < 6; ++i)
            assert(token(s, i == 3 ? tail : zh[i], (i + 1) * 512, true) == KWS_CTC_NONE);
    }
    kws_ctc_stream_reset(s);
    for (unsigned i = 0; i < 3; ++i) assert(token(s, zh[i], (i + 1) * 512, true) == KWS_CTC_NONE);
    unsigned calls = frontend_calls;
    assert(token(s, 4, 2048, false) == KWS_CTC_NONE);
    assert(token(s, 4, 2560, false) == KWS_CTC_NONE);
    assert(frontend_calls == calls);
    for (unsigned i = 0; i < 3; ++i) assert(token(s, i ? 0 : 4, (i + 6) * 512, true) == KWS_CTC_NONE);
    kws_ctc_stream_reset(s);
    assert(token(s, 1, 512, true) == KWS_CTC_NONE);
    for (unsigned i = 1; i < 6; ++i) assert(token(s, zh[i], (i + 2) * 512, true) == KWS_CTC_NONE);
    model.trained = false;
    kws_ctc_stream_reset(s);
    for (unsigned i = 0; i < 6; ++i) assert(token(s, zh[i], (i + 1) * 512, true) == KWS_CTC_NONE);
}

static void invalid(void)
{
    fixture(0);
    alignas(max_align_t) unsigned char memory[sizeof(kws_ctc_stream_t) + 16];
    kws_ctc_stream_t *s = (void *)memory;
    assert(kws_ctc_stream_init(NULL, sizeof(memory), &model, &s) == -1 && !s);
    assert(kws_ctc_stream_init(memory + 1, sizeof(memory) - 1, &model, &s) == -1 && !s);
    assert(kws_ctc_stream_init(memory, 1, &model, &s) == -1 && !s);
    assert(kws_ctc_stream_init(memory, sizeof(memory), &model, NULL) == -1);
    base.trained = true;
    assert(kws_ctc_stream_init(memory, sizeof(memory), &model, &s) == -1 && !s);
    base.trained = false;
    assert(kws_ctc_stream_init(memory, sizeof(memory), &model, &s) == 0);
    token(s, 1, 512, true);
    kws_ctc_output_t out;
    memset(&out, 0xff, sizeof(out));
    int16_t pcm[512] = {0};
    assert(kws_ctc_stream_feed(s, pcm, 513, true, &out) == -1);
    assert(out.phrase == KWS_CTC_NONE && !s->armed && !s->sequence.position[0]);
    assert(kws_ctc_stream_feed(s, NULL, 1024, true, &out) == -1);
    assert(kws_ctc_stream_feed(s, pcm, 1024, true, NULL) == -1);
    assert(kws_ctc_stream_feed(NULL, pcm, 1024, true, &out) == -1);
    kws_ctc_stream_reset(NULL);
}

int main(void)
{
    assert(kws_ctc_stream_size() == sizeof(kws_ctc_stream_t));
    assert(kws_ctc_stream_alignment() == alignof(kws_ctc_stream_t));
    assert(sizeof(kws_ctc_stream_t) - sizeof(kws_ctc_signal_t) <= 128);
    unsigned values = seed_checks();
    sequences(); invalid();
    printf("{\"width\":%u,\"seed_states\":8,\"postseed_values\":%u,\"owner_bytes\":%zu,\"workspace_bytes\":%zu}\n",
        KWS_CHANNELS, values, sizeof(kws_ctc_stream_t) - sizeof(kws_ctc_signal_t), sizeof(kws_ctc_stream_t));
    return 0;
}
