#ifndef KWS_CONDITIONED_SEED_H
#define KWS_CONDITIONED_SEED_H
#include "kws_conditioned_runtime.h"

/* Optional, model-specific Flash state. There is no additional heap allocation.
 * Only a fresh default init of the exact linked model triple may use the seed. */
typedef struct {
    kws_conditioned_branch_t primary, secondary;
    kws_conditioned_t verifier;
    kws_temporal_owner_t decision;
    uint64_t last_sample;
    int16_t recent[2][3], heads[3], score[2];
    uint8_t next, count, half;
} kws_conditioned_seed_t;

extern const kws_model_t kws_frozen_primary, kws_frozen_secondary, kws_trained_model;
uint64_t kws_conditioned_seed_prime(kws_conditioned_runtime_t *state);
size_t kws_conditioned_seed_bytes(void);
#endif
