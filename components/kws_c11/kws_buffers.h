#ifndef KWS_BUFFERS_H
#define KWS_BUFFERS_H
#include "kws.h"
#define FIXED_POINT 16
#include "kiss_fft.h"

/* Private borrowed storage: topology validation owns the buffer capacities.
 * Views never allocate, persist pointers, or change the public handle layout. */
typedef struct {
    int8_t *history, *current, *next;
    uint8_t *position;
} kws_neural_buffers_t;
typedef struct {
    int16_t *previous;
    kiss_fft_cpx *fft_in, *fft_out;
    uint32_t *power;
} kws_frontend_buffers_t;

int16_t kws_neural_buffers_step(const kws_neural_buffers_t *buffers,
    const kws_model_t *model, const int8_t features[40], kws_trace_t *trace);
void kws_frontend_buffers_step(const kws_frontend_buffers_t *buffers,
    const kws_model_t *model, const int16_t pcm[256], int8_t features[40], kws_trace_t *trace);
#endif
