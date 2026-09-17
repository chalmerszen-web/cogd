#ifndef TEN_LINEAR_PITCH_BANDS_H
#define TEN_LINEAR_PITCH_BANDS_H
/* The original log10-domain -8/-2.5 floors, expressed in linear energy.
 * Real-arithmetic equivalent; binary32 rounding is explicitly NOT bit-exact.
 * Input is the original eighteen finite, nonnegative pitch-band powers. */
static inline void ten_linear_pitch_bands(const float powers[18],const float compensation[18],float out[18])
{
    float maximum=.01f,previous=.01f;
    for(unsigned i=0;i<18;++i) {
        float energy=.01f+powers[i];
        float floor=previous*.0031622776601683793f;
        if(energy<floor)energy=floor;
        floor=maximum*1e-8f;
        if(energy<floor)energy=floor;
        if(maximum<energy)maximum=energy;
        previous=energy;
        out[i]=energy*compensation[i];
    }
}
#endif
