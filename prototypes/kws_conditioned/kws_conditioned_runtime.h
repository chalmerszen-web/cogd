#ifndef KWS_CONDITIONED_RUNTIME_H
#define KWS_CONDITIONED_RUNTIME_H
#include "kws_conditioned.h"
#include "kws_internal.h"
#include "kws_temporal_owner.h"
#if KWS_CONDITIONED_INPUT_COUNT == 90
#include "pitch.h"
#endif

typedef struct {
    int8_t history[2 * 40 + 124 * 24], current[40], next[24];
    uint8_t position[6];
} kws_conditioned_branch_t;
typedef struct {
    const kws_model_t *model;
    int16_t previous[256];
    union { kiss_fft_cpx fft_in[512]; uint32_t power[257]; };
    kiss_fft_cpx fft_out[512];
} kws_conditioned_frontend_t;
typedef struct {
    kws_conditioned_frontend_t first;
    kws_conditioned_branch_t primary, secondary;
    kws_conditioned_t verifier;
#if KWS_CONDITIONED_INPUT_COUNT == 90
    kws_pitch_t pitch;
#endif
    const kws_model_t *secondary_model, *verifier_model;
    kws_temporal_owner_t decision;
    uint64_t last_sample;
    int16_t recent[2][3], heads[3], score[2];
    uint8_t next, count, half;
} kws_conditioned_runtime_t;

size_t kws_conditioned_runtime_size(void);
size_t kws_conditioned_runtime_alignment(void);
int kws_conditioned_runtime_init(void *memory, size_t bytes, const kws_model_t *primary,
    const kws_model_t *secondary, const kws_model_t *verifier, kws_conditioned_runtime_t **out);
void kws_conditioned_runtime_reset(kws_conditioned_runtime_t *state);
uint64_t kws_conditioned_runtime_prime(kws_conditioned_runtime_t *state);
int kws_conditioned_runtime_features(kws_conditioned_runtime_t *state, const int8_t input[40],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace, int16_t verify_trace[265]);
#if KWS_CONDITIONED_INPUT_COUNT == 90
/* Precomputed features require explicit pitch; the old feature entry rejects
 * calls in90-input builds rather than silently supplying two zero values. */
int kws_conditioned_runtime_features_pitched(kws_conditioned_runtime_t *state,
    const int8_t input[40], const int8_t pitch[2], uint64_t end, bool armed,
    kws_event_t *event, kws_trace_t *trace, int16_t verify_trace[265]);
#endif
int kws_conditioned_runtime_pcm(kws_conditioned_runtime_t *state, const int16_t pcm[256],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace, int16_t verify_trace[265]);
int kws_conditioned_runtime_feed(kws_conditioned_runtime_t *state, const int16_t pcm[512],
    uint64_t end, bool armed, kws_event_t *event);
void kws_conditioned_runtime_threshold(kws_conditioned_runtime_t *state, int16_t threshold);
void kws_conditioned_runtime_stability(kws_conditioned_runtime_t *state, bool stable);
void kws_conditioned_runtime_scores(const kws_conditioned_runtime_t *state, int16_t heads[3], int16_t filtered[2]);
#endif
