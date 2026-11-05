#ifndef KWS_GRU64_H
#define KWS_GRU64_H
#include <stdint.h>

enum { KWS_GRU64_INPUTS = 40, KWS_GRU64_HIDDEN = 64, KWS_GRU64_OUTPUTS = 2 };
#define KWS_GRU64_ABI UINT32_C(0x01343647)
/* Tagged v1: r,z,n; input=x/32, matrices/biases Q8, state Q15.
 * Reset-after, matching PyTorch GRU. Q6 has a separate tagged entry point.
 * Raw logits do not authorize wake actions or certify a trained model. */
typedef struct {
    uint32_t abi;
    int8_t input[3][KWS_GRU64_HIDDEN][KWS_GRU64_INPUTS];
    int8_t recurrent[3][KWS_GRU64_HIDDEN][KWS_GRU64_HIDDEN];
    int16_t input_bias[3][KWS_GRU64_HIDDEN];
    int16_t recurrent_bias[3][KWS_GRU64_HIDDEN];
    int8_t output[KWS_GRU64_OUTPUTS][KWS_GRU64_HIDDEN];
    int16_t output_bias[KWS_GRU64_OUTPUTS];
} kws_gru64_model_t;
typedef struct { int16_t hidden[KWS_GRU64_HIDDEN]; } kws_gru64_state_t;
typedef struct { int16_t next[KWS_GRU64_HIDDEN]; } kws_gru64_scratch_t;

int kws_gru64_reset(kws_gru64_state_t *state);
/* One 16ms feature frame. All mutable buffers must be disjoint from each
 * other and both read-only inputs. Invalid tag, null pointer, alignment or
 * alias returns -1 before writes; success returns 0. Caller owns memory. */
int kws_gru64_step(const kws_gru64_model_t *model, kws_gru64_state_t *state,
    const int8_t input[KWS_GRU64_INPUTS], kws_gru64_scratch_t *scratch,
    int16_t logits_q8[KWS_GRU64_OUTPUTS]);
#endif
