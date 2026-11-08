#pragma once
#include <stdint.h>

typedef struct {float first,second;} p4_highpass_t;

/* Second-order Butterworth high-pass, 180 Hz at 16 kHz. State remains float
 * across PCM blocks; saturate only the returned sample, never the feedback.
 * This optional uplink filter targets measured low-frequency microphone hum. */
static inline int16_t p4_highpass_sample(p4_highpass_t *s,int16_t sample)
{
    float x=sample,y=.9512454212f*x+s->first;
    s->first=-1.9024908424f*x+1.9001124192f*y+s->second;
    s->second=.9512454212f*x-.9048692656f*y;
    return y>32767.f?32767:y< -32768.f?-32768:(int16_t)y;
}
