#include "kws_conditioned_runtime.h"
#include <stdalign.h>
#include <string.h>
#if KWS_CONDITIONED_INPUT_COUNT == 90
#include "pitch_encode.h"
#endif

size_t kws_conditioned_runtime_size(void) { return sizeof(kws_conditioned_runtime_t); }
size_t kws_conditioned_runtime_alignment(void) { return alignof(kws_conditioned_runtime_t); }

void kws_conditioned_runtime_reset(kws_conditioned_runtime_t *s)
{
    const kws_model_t *first = s->first.model, *second = s->secondary_model, *verifier = s->verifier_model;
    int16_t threshold = s->decision.original.threshold_q8;
    bool stable = s->decision.original.stable;
    memset(s, 0, sizeof(*s));
    s->first.model = first; s->secondary_model = second; s->verifier_model = verifier;
    kws_temporal_owner_reset(&s->decision, threshold);
    kws_detector_stability(&s->decision.original, stable);
}

int kws_conditioned_runtime_init(void *memory, size_t bytes, const kws_model_t *first,
    const kws_model_t *second, const kws_model_t *verifier, kws_conditioned_runtime_t **out)
{
    if (!out) return -1;
    *out = NULL;
    if (!memory || bytes < sizeof(kws_conditioned_runtime_t) ||
        (uintptr_t)memory % alignof(kws_conditioned_runtime_t) ||
        !kws_model_valid(first) || !kws_model_valid(second) || !kws_conditioned_valid(verifier) ||
        first->layers[0].outputs != 24 || second->layers[0].outputs != 24 ||
        !verifier->mean_q8 || !verifier->inverse_std_q12 ||
        memcmp(first->mean_q8, second->mean_q8, 40 * sizeof(int16_t)) ||
        memcmp(first->mean_q8, verifier->mean_q8, 40 * sizeof(int16_t)) ||
        memcmp(first->inverse_std_q12, second->inverse_std_q12, 40 * sizeof(uint16_t)) ||
        memcmp(first->inverse_std_q12, verifier->inverse_std_q12, 40 * sizeof(uint16_t))) return -1;
    kws_conditioned_runtime_t *s = memory;
    memset(s, 0, sizeof(*s));
    s->first.model = first; s->secondary_model = second; s->verifier_model = verifier;
    *out = s;
    return 0;
}

static void clear_evidence(kws_conditioned_runtime_t *s)
{
    s->decision.pending = s->decision.last_support = 0;
    s->decision.verify_votes = s->decision.original.votes = 0;
}
void kws_conditioned_runtime_threshold(kws_conditioned_runtime_t *s, int16_t threshold)
{ s->decision.original.threshold_q8 = threshold; clear_evidence(s); }
void kws_conditioned_runtime_stability(kws_conditioned_runtime_t *s, bool stable)
{ kws_detector_stability(&s->decision.original, stable); clear_evidence(s); }

static void clock_step(kws_conditioned_runtime_t *s, uint64_t end)
{
    if (s->last_sample && end - s->last_sample != 256) kws_conditioned_runtime_reset(s);
    s->last_sample = end;
}
static void neural_step(kws_conditioned_runtime_t *s, const int8_t input[40], uint64_t end,
                       bool armed, kws_event_t *event, kws_trace_t *trace, int16_t verify_trace[265],
                       const int8_t pitch[2])
{
    if (!armed) clear_evidence(s);
    /* Save raw40 before either backbone overwrites its current buffer. */
    memmove(s->verifier.current, input, 40);
    kws_neural_buffers_t view = {s->primary.history, s->primary.current, s->primary.next, s->primary.position};
    s->heads[0] = kws_neural_buffers_step(&view, s->first.model, s->verifier.current, trace);
    view = (kws_neural_buffers_t){s->secondary.history, s->secondary.current, s->secondary.next, s->secondary.position};
    s->heads[1] = kws_neural_buffers_step(&view, s->secondary_model, s->verifier.current, NULL);
    memcpy(s->verifier.current + 40, s->primary.current, 24);
    memcpy(s->verifier.current + 64, s->secondary.current, 24);
#if KWS_CONDITIONED_INPUT_COUNT == 90
    memcpy(s->verifier.current + 88, pitch, 2);
#else
    (void)pitch;
#endif
    s->heads[2] = kws_conditioned_step(&s->verifier, s->verifier_model, s->verifier.current, verify_trace);
    bool detected = false;
    if (++s->half == 2) {
        s->half = 0;
        s->recent[0][s->next] = (int16_t)(((int32_t)s->heads[0] + s->heads[1]) / 2);
        s->recent[1][s->next] = s->heads[2];
        s->next = (uint8_t)((s->next + 1) % 3);
        if (s->count < 3) ++s->count;
        for (unsigned branch = 0; branch < 2; ++branch) {
            int32_t total = 0;
            for (unsigned i = 0; i < s->count; ++i) total += s->recent[branch][i];
            s->score[branch] = (int16_t)(total / s->count);
        }
        detected = kws_temporal_owner_step(&s->decision, s->score[0], s->score[1], end, armed);
    }
    *event = (kws_event_t){s->score[0], detected && s->first.model->trained &&
        s->secondary_model->trained && s->verifier_model->trained, end};
}

int kws_conditioned_runtime_features(kws_conditioned_runtime_t *s, const int8_t input[40],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace, int16_t verify_trace[265])
{
#if KWS_CONDITIONED_INPUT_COUNT == 90
    (void)s; (void)input; (void)end; (void)armed; (void)event; (void)trace; (void)verify_trace;
    return -1;
#else
    if (!s || !input || !event || end < 256) return -1;
    clock_step(s, end); neural_step(s, input, end, armed, event, trace, verify_trace, NULL); return 0;
#endif
}
#if KWS_CONDITIONED_INPUT_COUNT == 90
int kws_conditioned_runtime_features_pitched(kws_conditioned_runtime_t *s,
    const int8_t input[40], const int8_t pitch[2], uint64_t end, bool armed,
    kws_event_t *event, kws_trace_t *trace, int16_t verify_trace[265])
{
    if (!s || !input || !pitch || !event || end < 256 || pitch[0] < 0 || pitch[1] < 0) return -1;
    clock_step(s, end); neural_step(s, input, end, armed, event, trace, verify_trace, pitch); return 0;
}
#endif
int kws_conditioned_runtime_pcm(kws_conditioned_runtime_t *s, const int16_t pcm[256],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace, int16_t verify_trace[265])
{
    if (!s || !pcm || !event || end < 256) return -1;
    clock_step(s, end);
#if KWS_CONDITIONED_INPUT_COUNT == 90
    const kws_pitch_features_t feature = kws_pitch_block(&s->pitch, pcm);
    const int16_t raw_pitch[2] = {feature.frequency_q4, feature.periodicity_q12};
    int8_t encoded[2];
    if (!kws_pitch_encode(raw_pitch, encoded)) return -1;
#else
    const int8_t *encoded = NULL;
#endif
    int8_t input[40];
    const kws_frontend_buffers_t view = {s->first.previous, s->first.fft_in, s->first.fft_out, s->first.power};
    kws_frontend_buffers_step(&view, s->first.model, pcm, input, trace);
    neural_step(s, input, end, armed, event, trace, verify_trace, encoded); return 0;
}
int kws_conditioned_runtime_feed(kws_conditioned_runtime_t *s, const int16_t pcm[512],
    uint64_t end, bool armed, kws_event_t *event)
{
    if (!s || !pcm || !event || end < 512) return -1;
    int error = kws_conditioned_runtime_pcm(s, pcm, end - 256, armed, event, NULL, NULL);
    return error ? error : kws_conditioned_runtime_pcm(s, pcm + 256, end, armed, event, NULL, NULL);
}
uint64_t kws_conditioned_runtime_prime(kws_conditioned_runtime_t *s)
{
    if (!s || s->last_sample || s->half || s->count || s->decision.original.blocks) return 0;
    const int16_t silence[512] = {0};
    kws_event_t event;
    for (unsigned block = 1; block <= 64; ++block)
        kws_conditioned_runtime_feed(s, silence, block * 512u, false, &event);
    return s->last_sample;
}
void kws_conditioned_runtime_scores(const kws_conditioned_runtime_t *s, int16_t heads[3], int16_t filtered[2])
{ memcpy(heads, s->heads, sizeof(s->heads)); memcpy(filtered, s->score, sizeof(s->score)); }
