#ifndef TEN_REFERENCE_NN_H
#define TEN_REFERENCE_NN_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float hidden[2][64],cell[2][64];
    float work[624],temp[160],input[144];
    int16_t quantized[144];
    float scale;
#ifdef TEN_FIXED_DSP
    int exponent;
#endif
} ten_nn_t;
/* Zero-initialize between clips. No allocation or hidden global state. */
bool ten_nn_step(ten_nn_t *,const float features[123],bool reference,float *probability);
#endif
