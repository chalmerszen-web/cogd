#include "kws_gru32_q6.h"
#include "kws_gru_kernel.h"

_Static_assert(sizeof(kws_gru32_model_t) == 7364, "Fixed model layout");
_Static_assert(sizeof(kws_gru32_state_t) == 64, "Fixed recurrent state");
_Static_assert(sizeof(kws_gru32_scratch_t) == 64, "Fixed next-state scratch");
_Static_assert(sizeof(kws_gru32_q6_model_t) == 7368, "Tagged Q6 model layout");

int kws_gru32_reset(kws_gru32_state_t *state)
{
    if (!state || (uintptr_t)state % _Alignof(kws_gru32_state_t)) return -1;
    memset(state, 0, sizeof(*state));
    return 0;
}

int kws_gru32_step(const kws_gru32_model_t *model, kws_gru32_state_t *state,
    const int8_t input[KWS_GRU_INPUTS], kws_gru32_scratch_t *scratch, int16_t output[KWS_GRU_OUTPUTS])
{
    if (!kws_gru_valid(model, sizeof(*model), _Alignof(kws_gru32_model_t), state, input,
                       scratch, output, KWS_GRU_HIDDEN)) return -1;
    kws_gru_step(KWS_GRU_WEIGHTS(model), KWS_GRU_HIDDEN, state->hidden, input, scratch->next, output, 32, 32768);
    return 0;
}

int kws_gru32_q6_step(const kws_gru32_q6_model_t *model, kws_gru32_state_t *state,
    const int8_t input[KWS_GRU_INPUTS], kws_gru32_scratch_t *scratch, int16_t output[KWS_GRU_OUTPUTS])
{
    if (!kws_gru_valid(model, sizeof(*model), _Alignof(kws_gru32_q6_model_t), state, input,
                       scratch, output, KWS_GRU_HIDDEN) || model->abi != KWS_GRU32_Q6_ABI) return -1;
    kws_gru_step(KWS_GRU_WEIGHTS(&model->weights), KWS_GRU_HIDDEN,
                 state->hidden, input, scratch->next, output, 8, 8192);
    return 0;
}
