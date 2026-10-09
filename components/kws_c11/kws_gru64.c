#include "kws_gru64_q6.h"
#include "kws_gru_kernel.h"

_Static_assert(sizeof(kws_gru64_model_t) == 20872, "Tagged 64/Q8 model layout");
_Static_assert(sizeof(kws_gru64_q6_model_t) == 20872, "Tagged 64/Q6 model layout");
_Static_assert(sizeof(kws_gru64_state_t) == 128, "Fixed 64-channel state");
_Static_assert(sizeof(kws_gru64_scratch_t) == 128, "Fixed next-state scratch");

int kws_gru64_reset(kws_gru64_state_t *state)
{
    if (!state || (uintptr_t)state % _Alignof(kws_gru64_state_t)) return -1;
    memset(state, 0, sizeof(*state));
    return 0;
}

int kws_gru64_q6_step(const kws_gru64_q6_model_t *model, kws_gru64_state_t *state,
    const int8_t input[KWS_GRU64_INPUTS], kws_gru64_scratch_t *scratch, int16_t output[KWS_GRU64_OUTPUTS])
{
    if (!kws_gru_valid(model, sizeof(*model), _Alignof(kws_gru64_q6_model_t), state, input,
                       scratch, output, KWS_GRU64_HIDDEN) || model->abi != KWS_GRU64_Q6_ABI) return -1;
    kws_gru_step(KWS_GRU_WEIGHTS(model), KWS_GRU64_HIDDEN,
                 state->hidden, input, scratch->next, output, 8, 8192);
    return 0;
}

int kws_gru64_step(const kws_gru64_model_t *model, kws_gru64_state_t *state,
    const int8_t input[KWS_GRU64_INPUTS], kws_gru64_scratch_t *scratch, int16_t output[KWS_GRU64_OUTPUTS])
{
    if (!kws_gru_valid(model, sizeof(*model), _Alignof(kws_gru64_model_t), state, input,
                       scratch, output, KWS_GRU64_HIDDEN) || model->abi != KWS_GRU64_ABI) return -1;
    kws_gru_step(KWS_GRU_WEIGHTS(model), KWS_GRU64_HIDDEN,
                 state->hidden, input, scratch->next, output, 32, 32768);
    return 0;
}
