#ifndef KWS_INTERNAL_H
#define KWS_INTERNAL_H
#include "kws.h"
#define FIXED_POINT 16
#include "kiss_fft.h"
#include "kws_buffers.h"

typedef struct { uint16_t first,count,offset; } kws_mel_band_t;
extern const int16_t kws_hann[512];
extern const kws_mel_band_t kws_mel[40];
extern const uint16_t kws_mel_weights[];
extern const uint16_t kws_log_fraction[256];
typedef struct {
    /* Two40-band stem frames plus4*(1+2+4+8+16) channel frames. */
    int8_t history[2*KWS_BANDS+124*KWS_CHANNELS];
    uint8_t position[6];
    int8_t current[KWS_BANDS>KWS_CHANNELS?KWS_BANDS:KWS_CHANNELS],next[KWS_CHANNELS];
} kws_neural_t;
struct kws_handle {
    const kws_model_t *model;
    kws_detector_t detector;
    int16_t previous[256];
    /* Power bins reuse the input after the disjoint-buffer FFT finishes. */
    union {
        kiss_fft_cpx fft_in[512];
        uint32_t power[257];
    };
    kiss_fft_cpx fft_out[512];
    kws_neural_t neural;
    kws_trace_t trace;
};
struct kws_fusion {
    kws_handle_t first;
    kws_neural_t second;
    const kws_model_t *second_model;
    kws_detector_t detector;
    uint64_t last_sample;
    int16_t recent[3],scores[2],score;
    uint8_t next,count,half;
};
/* Only causal state, emitted as typed C initializers by the host generator.
 * FFT/power/trace are overwritten before their next use. There is no PCEN
 * state in this log-mel fusion path. No pointer or host object bytes escape. */
typedef struct {
    int16_t previous[256];
    kws_neural_t first,second;
    kws_detector_t detector;
    uint64_t last_sample;
    int16_t recent[3],scores[2],score;
    uint8_t next,count,half;
} kws_silence_seed_t;
void kws_fft512(const kiss_fft_cpx *input,kiss_fft_cpx *output);
void kws_frontend(kws_handle_t *kws,const int16_t pcm[256],kws_trace_t *trace);
bool kws_model_valid(const kws_model_t *model);
int16_t kws_neural_step(kws_neural_t *state,const kws_model_t *model,const int8_t features[40],kws_trace_t *trace);
#endif
