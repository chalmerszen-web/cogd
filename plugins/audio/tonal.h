#ifndef STUDY_TONAL_FILTER_H
#define STUDY_TONAL_FILTER_H
#include "voice.h"
typedef struct { agent_biquad_t tones[2]; } tonal_filter_t;
/* Zero initial state; consumes original voice/cue-filtered PCM, before any
 * classifier callback can modify its buffer. Exact frozen narrow-notch math. */
int16_t tonal_filter_sample(tonal_filter_t *,int16_t);
/* 300-Hz Butterworth high-pass for fast endpoint energy only. Zero state per
 * capture. The spectral classifier and recorded/uploaded PCM are unchanged. */
int16_t tonal_highpass_sample(agent_biquad_t *,int16_t);
#endif
