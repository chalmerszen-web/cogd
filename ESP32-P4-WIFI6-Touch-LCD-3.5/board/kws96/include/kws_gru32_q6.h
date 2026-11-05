#ifndef KWS_GRU32_Q6_H
#define KWS_GRU32_Q6_H
#include "kws_gru32.h"

/* A separate tagged format: matrices Q6, biases Q8, hidden states Q15.
 * Input features and output logits retain the original x/32 and Q8 units.
 * The original untagged Q8 entry point remains unchanged. */
#define KWS_GRU32_Q6_ABI UINT32_C(0x3667524b)
typedef struct {
    uint32_t abi;
    kws_gru32_model_t weights;
} kws_gru32_q6_model_t;

int kws_gru32_q6_step(const kws_gru32_q6_model_t *model, kws_gru32_state_t *state,
    const int8_t input[KWS_GRU_INPUTS], kws_gru32_scratch_t *scratch,
    int16_t logits_q8[KWS_GRU_OUTPUTS]);
#endif
