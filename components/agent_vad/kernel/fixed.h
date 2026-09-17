#ifndef TEN_FIXED_DSP_H
#define TEN_FIXED_DSP_H
#include <stdbool.h>
/* Fixed 1024-point real FFT. Both nonoverlapping buffers are scratch/output. */
void ten_fft1024(float input[1024],float output[1024]);
void ten_xcorr32x64(const float reference[32],const float shifted[96],float output[64]);
void ten_lpc16(const float coefficients[16],const float *input,unsigned count,
               float memory[16],float *previous,float *output);
bool ten_biquad(float *data,unsigned count,const float a[3],const float b[3],
                float gain,float state[2]);
#endif
