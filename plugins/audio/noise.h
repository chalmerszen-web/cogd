#ifndef AGENT_NOISE_H
#define AGENT_NOISE_H
#include <stdint.h>

enum { AGENT_NOISE_FFT=128,AGENT_NOISE_HOP=64 };
typedef struct {
    int32_t real[128],imag[128];
    uint32_t noise[65],candidate[65];
    uint16_t gain[65];
    int16_t history[64],overlap[64];
    uint64_t best_energy;
    unsigned profile_frames;
} agent_noise_t;
void agent_noise_init(agent_noise_t *);
/* Profile consecutive 128-sample frames; quietest ~128 ms supplies the noise estimate. */
void agent_noise_profile(agent_noise_t *,const int16_t samples[128]);
void agent_noise_ready(agent_noise_t *);
/* 50% overlap. First output block is latency; one zero input block drains the tail. */
void agent_noise_process(agent_noise_t *,const int16_t input[64],int16_t output[64]);
#endif
