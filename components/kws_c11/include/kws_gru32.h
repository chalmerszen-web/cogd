#ifndef KWS_GRU32_H
#define KWS_GRU32_H
#include <stdint.h>

enum { KWS_GRU_INPUTS = 40, KWS_GRU_HIDDEN = 32, KWS_GRU_OUTPUTS = 2 };
/* Fixed ABI v1: r,z,n; input=x/32, weights=Q8, biases=Q8.
 * Reset-after n=tanh(input_n+r*(recurrent_n+bias_n)), as in PyTorch GRU.
 * Output0=no event, output1=complete bilingual wake event. This raw kernel
 * does not certify training, manage sample clocks or permit device actions. */
typedef struct {
    int8_t input[3][KWS_GRU_HIDDEN][KWS_GRU_INPUTS];
    int8_t recurrent[3][KWS_GRU_HIDDEN][KWS_GRU_HIDDEN];
    int16_t input_bias[3][KWS_GRU_HIDDEN];
    int16_t recurrent_bias[3][KWS_GRU_HIDDEN];
    int8_t output[KWS_GRU_OUTPUTS][KWS_GRU_HIDDEN];
    int16_t output_bias[KWS_GRU_OUTPUTS];
} kws_gru32_model_t;
typedef struct { int16_t hidden[KWS_GRU_HIDDEN]; } kws_gru32_state_t;
typedef struct { int16_t next[KWS_GRU_HIDDEN]; } kws_gru32_scratch_t;

int kws_gru32_reset(kws_gru32_state_t *state);
/* One16ms feature frame. Caller owns all memory. State, scratch and logits
 * must be disjoint from each other and the two read-only inputs. Invalid
 * pointers/alignment/overlap return-1 before any write; success returns0.
 * Scratch keeps all new states separate from the previous recurrent vector. */
int kws_gru32_step(const kws_gru32_model_t *model, kws_gru32_state_t *state,
    const int8_t input[KWS_GRU_INPUTS], kws_gru32_scratch_t *scratch,
    int16_t logits_q8[KWS_GRU_OUTPUTS]);
#endif
