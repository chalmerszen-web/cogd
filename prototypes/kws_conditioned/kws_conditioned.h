#ifndef KWS_CONDITIONED_H
#define KWS_CONDITIONED_H
#include "kws.h"

/* Default: raw40 + frozen E24/L24. Optional90 appends two encoded pitch
 * values. Each build validates its exact topology; never use kws_init. */
#ifndef KWS_CONDITIONED_INPUT_COUNT
#define KWS_CONDITIONED_INPUT_COUNT 88
#endif
#if KWS_CONDITIONED_INPUT_COUNT != 88 && KWS_CONDITIONED_INPUT_COUNT != 90
#error "Only registered88/90 conditioned inputs are supported"
#endif
enum { KWS_CONDITIONED_INPUTS = KWS_CONDITIONED_INPUT_COUNT, KWS_CONDITIONED_CHANNELS = 24,
       KWS_CONDITIONED_TRACE = 265 };
typedef struct {
    int8_t history[2 * KWS_CONDITIONED_INPUTS + 124 * KWS_CONDITIONED_CHANNELS];
    int8_t current[KWS_CONDITIONED_INPUTS], next[KWS_CONDITIONED_CHANNELS];
    uint8_t position[6];
} kws_conditioned_t;

bool kws_conditioned_valid(const kws_model_t *model);
void kws_conditioned_reset(kws_conditioned_t *state);
/* Validate the model once before stepping. Input may alias current exactly.
 * Caller provides the build's exact INT8 width and optional265 INT16 trace. */
int16_t kws_conditioned_step(kws_conditioned_t *state, const kws_model_t *model,
                           const int8_t input[KWS_CONDITIONED_INPUTS],
                           int16_t trace[KWS_CONDITIONED_TRACE]);
#endif
