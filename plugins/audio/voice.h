#ifndef AGENT_VOICE_H
#define AGENT_VOICE_H
#include "clip.h"
#include "noise.h"
typedef struct { int32_t x1,x2,y1,y2; } agent_biquad_t;
typedef struct { agent_biquad_t stage[4]; } agent_voice_t;
void agent_voice_init(agent_voice_t *);
int16_t agent_voice_filter(agent_voice_t *,int16_t);
/* Zero-initialize state per capture. This is not a recording/playback filter. */
int16_t agent_voice_reject_cue(agent_biquad_t *,int16_t);
typedef struct {
    agent_clip_t *clip;
    agent_voice_t voice;
    agent_noise_t noise;
    int16_t cache[128],previous,next;
    size_t source,used,available,previous_count,rendered,total;
    unsigned phase;
    bool prepared;
} agent_replay_t;
agent_err_t agent_replay_init(agent_replay_t *,agent_clip_t *);
/* One bounded profile frame per call; caller can service DMA/cancel between calls. */
agent_err_t agent_replay_prepare(agent_replay_t *,bool *ready);
agent_err_t agent_replay_render(agent_replay_t *,int16_t *,size_t capacity,unsigned volume,size_t *count);
#endif
