/* Pinned KISS FFT, with frozen Q15 twiddles. Only radix 4/2, disjoint buffers.
 * Configuration is read-only in kf_work; no setup or allocation runs per frame. */
#define FIXED_POINT 16
#define KISS_FFT_STATIC_512 1
#include "../../third_party/kissfft/kiss_fft.c"
#include "generated/fft_config.inc"
void kws_fft512(const kiss_fft_cpx *input,kiss_fft_cpx *output)
{
    kf_work(output,input,1,1,(int *)kws_fft_config.factors,(kiss_fft_cfg)&kws_fft_config);
}
