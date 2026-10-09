#ifndef KWS_CTC_RUNTIME_INTERNAL_H
#define KWS_CTC_RUNTIME_INTERNAL_H
#include "kws_ctc_runtime.h"
#include "kws_internal.h"

typedef struct {
    int16_t previous[256];
    union { kiss_fft_cpx fft_in[512]; uint32_t power[257]; };
    kiss_fft_cpx fft_out[512];
    kws_neural_t neural;
} kws_ctc_signal_t;

struct kws_ctc_stream {
    kws_ctc_signal_t signal;
    const kws_ctc_model_t *model;
    kws_ctc_t sequence;
    uint64_t last_sample;
    bool armed;
    int8_t features[40];
};
#endif
