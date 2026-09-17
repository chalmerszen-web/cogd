#ifndef TEN_REFERENCE_MODEL_H
#define TEN_REFERENCE_MODEL_H
#ifdef TEN_DEVICE
#include "model.h"
typedef struct { unsigned frames; ten_nn_t nn; } AUP_MODULE_AIVAD;
#else
typedef struct { unsigned frames; } AUP_MODULE_AIVAD;
#endif
int ten_model_reset(AUP_MODULE_AIVAD *state);
int ten_model_process(AUP_MODULE_AIVAD *state,const float input[123],float *output);
#endif
