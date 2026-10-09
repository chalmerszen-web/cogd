#ifndef KWS_VERIFIED_INTERNAL_H
#define KWS_VERIFIED_INTERNAL_H
#include "kws_verified.h"
#include "kws_internal.h"
#include "kws_temporal_owner.h"

#if KWS_BUILD_CHANNELS != 48 || !defined(KWS_ALLOW_MIXED24)
#error "Verified runtime requires compiled48 capacity with registered24 support"
#endif

/* Actual24 causal storage, shared by runtime and typed seed. */
typedef struct {
    int8_t history[2 * 40 + 124 * 24], current[40], next[24];
    uint8_t position[6];
} kws_neural24_t;
typedef struct {
    const kws_model_t *model;
    int16_t previous[256];
    union { kiss_fft_cpx fft_in[512]; uint32_t power[257]; };
    kiss_fft_cpx fft_out[512];
} kws_verified_frontend_t;

struct kws_verified {
    kws_verified_frontend_t first;
    kws_neural24_t first_neural, second;
    kws_neural_t verifier;
    const kws_model_t *second_model, *verifier_model;
    kws_temporal_owner_t decision;
    uint64_t last_sample;
    int16_t recent[2][3], heads[3], score[2];
    uint8_t next, count, half;
};

/* Compact source state, restored into the corresponding topology.
 * No pointer, FFT scratch, raw host object or compiler layout is exported. */
typedef kws_neural24_t kws_neural24_seed_t;
typedef struct {
    int16_t previous[256];
    kws_neural24_seed_t first, second;
    kws_neural_t verifier;
    kws_temporal_owner_t decision;
    uint64_t last_sample;
    int16_t recent[2][3], heads[3], score[2];
    uint8_t next, count, half;
} kws_verified_seed_t;
#endif
