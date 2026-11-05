#ifndef P4_MEGA_KWS_VERIFIED_INTERNAL_H
#define P4_MEGA_KWS_VERIFIED_INTERNAL_H
#include "kws_verified.h"
#include "kws96/kws_internal.h"
#include "sync_kws_temporal_owner.h"

#if KWS_BUILD_CHANNELS != 96 || !defined(KWS_ALLOW_MIXED24)
#error "Verified runtime requires compiled96 capacity with registered24 support"
#endif

/* P4: all three heads use full 48-channel causal storage and typed seeds.
 * Retain the private legacy alias to keep the verified algorithm identical. */
typedef kws_neural_t kws_neural24_t;
typedef struct { int8_t history[80+124*48]; uint8_t position[6]; int8_t current[48],next[48]; } p4_neural48_t;
typedef struct {
    const kws_model_t *model;
    int16_t previous[256];
    union { kiss_fft_cpx fft_in[512]; uint32_t power[257]; };
    kiss_fft_cpx fft_out[512];
} kws_verified_frontend_t;

struct kws_verified {
    kws_verified_frontend_t first;
    kws_neural24_t first_neural, second;
    p4_neural48_t verifier;
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
    p4_neural48_t verifier;
    kws_temporal_owner_t decision;
    uint64_t last_sample;
    int16_t recent[2][3], heads[3], score[2];
    uint8_t next, count, half;
} kws_verified_seed_t;
#endif
