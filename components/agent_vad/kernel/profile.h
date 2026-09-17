/* Optional timing only; excluded from the normal Agent. */
#ifndef TEN_PROFILE_H
#define TEN_PROFILE_H
enum {
    TEN_STFT, TEN_LPC, TEN_FIR, TEN_BIQUAD, TEN_XCORR, TEN_TRACK,
    TEN_MEL, TEN_CONV, TEN_RECURRENT, TEN_DENSE, TEN_PROFILE_COUNT
};
#ifdef TEN_DEVICE
void ten_profile_begin(unsigned slot);
void ten_profile_end(unsigned slot);
#else
static inline void ten_profile_begin(unsigned slot) { (void)slot; }
static inline void ten_profile_end(unsigned slot) { (void)slot; }
#endif
#endif
