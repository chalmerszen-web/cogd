#include "kws_verified_internal.h"
#include <stdalign.h>
#include <string.h>

#ifdef AGENT_KWS_VERIFIED_PRIME
extern const kws_model_t kws_verified_primary, kws_verified_secondary, kws_trained_model;
#include "kws_verified_seed.inc"
#endif

size_t kws_verified_size(void) { return sizeof(kws_verified_t); }
size_t kws_verified_alignment(void) { return alignof(kws_verified_t); }

void kws_verified_reset(kws_verified_t *s)
{
    const kws_model_t *first = s->first.model, *second = s->second_model, *verifier = s->verifier_model;
    int16_t threshold = s->decision.original.threshold_q8;
    bool stable = s->decision.original.stable;
    memset(s, 0, sizeof(*s));
    s->first.model = first; s->second_model = second; s->verifier_model = verifier;
    kws_temporal_owner_reset(&s->decision, threshold);
    kws_detector_stability(&s->decision.original, stable);
}

int kws_verified_init(void *memory, size_t bytes, const kws_model_t *first,
    const kws_model_t *second, const kws_model_t *verifier, kws_verified_t **out)
{
    if (!out) return -1;
    *out = NULL;
    if (!memory || bytes < sizeof(kws_verified_t) || (uintptr_t)memory % alignof(kws_verified_t) ||
        !kws_model_valid(first) || !kws_model_valid(second) || !kws_model_valid(verifier) ||
        first->layers[0].outputs != 24 || second->layers[0].outputs != 24 || verifier->layers[0].outputs != 48 ||
        memcmp(first->mean_q8, second->mean_q8, 40 * sizeof(int16_t)) ||
        memcmp(first->mean_q8, verifier->mean_q8, 40 * sizeof(int16_t)) ||
        memcmp(first->inverse_std_q12, second->inverse_std_q12, 40 * sizeof(uint16_t)) ||
        memcmp(first->inverse_std_q12, verifier->inverse_std_q12, 40 * sizeof(uint16_t))) return -1;
    kws_verified_t *s = memory;
    memset(s, 0, sizeof(*s));
    s->first.model = first; s->second_model = second; s->verifier_model = verifier;
    *out = s;
    return 0;
}

static void clear_evidence(kws_verified_t *s)
{
    s->decision.pending = s->decision.last_support = 0;
    s->decision.verify_votes = s->decision.original.votes = 0;
}
void kws_verified_threshold(kws_verified_t *s, int16_t threshold)
{ s->decision.original.threshold_q8 = threshold; clear_evidence(s); }
void kws_verified_stability(kws_verified_t *s, bool stable)
{ kws_detector_stability(&s->decision.original, stable); clear_evidence(s); }

uint64_t kws_verified_prime(kws_verified_t *s)
{
#ifdef AGENT_KWS_VERIFIED_PRIME
    if (!s || s->first.model != &kws_verified_primary || s->second_model != &kws_verified_secondary ||
        s->verifier_model != &kws_trained_model || s->last_sample || s->half || s->count || s->next ||
        s->decision.original.blocks || s->decision.original.threshold_q8 || s->decision.original.stable ||
        s->decision.pending || s->decision.last_support || s->decision.verify_votes) return 0;
    memcpy(s->first.previous, kws_verified_silence.previous, sizeof(s->first.previous));
    s->first_neural = kws_verified_silence.first;
    s->second = kws_verified_silence.second;
    s->verifier = kws_verified_silence.verifier; s->decision = kws_verified_silence.decision;
    s->last_sample = kws_verified_silence.last_sample;
    memcpy(s->recent, kws_verified_silence.recent, sizeof(s->recent));
    memcpy(s->heads, kws_verified_silence.heads, sizeof(s->heads));
    memcpy(s->score, kws_verified_silence.score, sizeof(s->score));
    s->next = kws_verified_silence.next; s->count = kws_verified_silence.count; s->half = kws_verified_silence.half;
    return s->last_sample;
#else
    (void)s; return 0;
#endif
}

static void clock_step(kws_verified_t *s, uint64_t end)
{
    if (s->last_sample && end - s->last_sample != 256) kws_verified_reset(s);
    s->last_sample = end;
}
static void neural_step(kws_verified_t *s, const int8_t input[40], uint64_t end,
    bool armed, kws_event_t *event, kws_trace_t *trace)
{
    if (!armed) clear_evidence(s);
    kws_neural_buffers_t view = {s->first_neural.history, s->first_neural.current,
        s->first_neural.next, s->first_neural.position};
    s->heads[0] = kws_neural_buffers_step(&view, s->first.model, input, trace);
    view = (kws_neural_buffers_t){s->second.history, s->second.current, s->second.next, s->second.position};
    s->heads[1] = kws_neural_buffers_step(&view, s->second_model, input, NULL);
    view = (kws_neural_buffers_t){s->verifier.history, s->verifier.current, s->verifier.next, s->verifier.position};
    s->heads[2] = kws_neural_buffers_step(&view, s->verifier_model, input, NULL);
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
        s->second_model->trained && s->verifier_model->trained, end};
}

int kws_verified_step_features_armed(kws_verified_t *s, const int8_t input[40],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace)
{
    if (!s || !input || !event || end < 256) return -1;
    clock_step(s, end);
    neural_step(s, input, end, armed, event, trace);
    return 0;
}
int kws_verified_step_pcm_armed(kws_verified_t *s, const int16_t pcm[256],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace)
{
    if (!s || !pcm || !event || end < 256) return -1;
    clock_step(s, end);
    int8_t input[40];
    const kws_frontend_buffers_t view = {s->first.previous, s->first.fft_in, s->first.fft_out, s->first.power};
    kws_frontend_buffers_step(&view, s->first.model, pcm, input, trace);
    neural_step(s, input, end, armed, event, trace);
    return 0;
}
int kws_verified_feed_512_armed(kws_verified_t *s, const int16_t pcm[512],
    uint64_t end, bool armed, kws_event_t *event)
{
    if (!s || !pcm || !event || end < 512) return -1;
    int error = kws_verified_step_pcm_armed(s, pcm, end - 256, armed, event, NULL);
    return error ? error : kws_verified_step_pcm_armed(s, pcm + 256, end, armed, event, NULL);
}
void kws_verified_scores(const kws_verified_t *s, int16_t heads[3], int16_t filtered[2])
{ memcpy(heads, s->heads, sizeof(s->heads)); memcpy(filtered, s->score, sizeof(s->score)); }
