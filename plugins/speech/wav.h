#ifndef AGENT_SPEECH_WAV_H
#define AGENT_SPEECH_WAV_H
#include "agent.h"

/* PCM callbacks may block for bounded backpressure; only complete sample frames
 * are delivered. finish belongs to the caller and is required on every exit. */
typedef struct {
    agent_err_t (*open)(void *, unsigned rate);
    agent_err_t (*write)(void *, const int16_t *, size_t count);
    void *ctx;
} agent_pcm_sink_t;
typedef struct {
    agent_pcm_sink_t sink;
    uint8_t header[32], frame[4];
    int16_t block[240];
    uint32_t remaining, riff_left, rate, samples;
    unsigned stage, used, needed, channels, frame_used, block_used;
    bool format, data, opened, odd, unbounded_riff, unbounded_data;
} agent_wav_t;
void agent_wav_init(agent_wav_t *, const agent_pcm_sink_t *);
agent_err_t agent_wav_feed(void *, const char *, size_t);
agent_err_t agent_wav_finish(agent_wav_t *);
void agent_wav_header(uint8_t out[44], unsigned rate, uint32_t samples);
static inline int16_t agent_pcm_gain(int16_t sample,unsigned percent)
{
    if(percent>100) percent=100;
    return (int16_t)((int32_t)sample*(int32_t)percent/100);
}
#endif
