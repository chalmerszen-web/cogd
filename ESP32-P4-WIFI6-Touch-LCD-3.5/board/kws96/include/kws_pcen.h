#ifndef KWS_PCEN_H
#define KWS_PCEN_H
#include <stdint.h>
#include "kws.h"

/* Experimental fixed-parameter PCEN, not enabled in the shipped frontend.
 * Input: nonnegative Mel power. Output: PCEN magnitude compression in Q8.
 * alpha=1, delta=2, r=1/2, epsilon=1 magnitude unit, smoothing=1/32.
 * Forty Q16 magnitude estimates; reset all to zero at stream start/gaps.
 * Requires separately trained weights and normalization.
 */
typedef struct { uint32_t mean_q16[40]; } kws_pcen_t;
void kws_pcen_reset(kws_pcen_t *state);
void kws_pcen_step(kws_pcen_t *state, const uint32_t power[40], int16_t output[40]);
/* Explicit experimental path; the regular kws_step_pcm remains log-Mel. */
void kws_frontend_power(kws_handle_t *handle,const int16_t pcm[256],uint32_t power[40]);
void kws_pcen_frontend(kws_handle_t *handle,kws_pcen_t *state,const int16_t pcm[256],kws_trace_t *trace);
#endif
