#ifndef KWS_GRU64_Q6_H
#define KWS_GRU64_Q6_H
#include "kws_gru64.h"

/* Distinct type/tag: matrices Q6, biases/logits Q8, states Q15, input x/32.
 * The Q8 API rejects this tag before writes, even if explicitly cast. */
#define KWS_GRU64_Q6_ABI UINT32_C(0x06343647)
typedef struct {
    uint32_t abi;
    int8_t input[3][KWS_GRU64_HIDDEN][KWS_GRU64_INPUTS];
    int8_t recurrent[3][KWS_GRU64_HIDDEN][KWS_GRU64_HIDDEN];
    int16_t input_bias[3][KWS_GRU64_HIDDEN];
    int16_t recurrent_bias[3][KWS_GRU64_HIDDEN];
    int8_t output[KWS_GRU64_OUTPUTS][KWS_GRU64_HIDDEN];
    int16_t output_bias[KWS_GRU64_OUTPUTS];
} kws_gru64_q6_model_t;

int kws_gru64_q6_step(const kws_gru64_q6_model_t *model, kws_gru64_state_t *state,
    const int8_t input[KWS_GRU64_INPUTS], kws_gru64_scratch_t *scratch,
    int16_t logits_q8[KWS_GRU64_OUTPUTS]);
#endif
