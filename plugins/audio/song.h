#ifndef AGENT_SONG_H
#define AGENT_SONG_H
#include "agent.h"

#define AGENT_SONG_PATTERNS 16u
#define AGENT_SONG_NOTES 128u
#define AGENT_SONG_SEGMENTS 32u
#define AGENT_SONG_VOICES 4u
#define AGENT_SONG_TICKS 32u
#define AGENT_SONG_SECONDS 75u

typedef struct { uint8_t pitch,ticks,velocity,gate; } agent_song_note_t;
typedef struct { uint8_t first,count; } agent_song_pattern_t;
typedef struct { int8_t parts[4]; uint8_t gain; } agent_song_segment_t;
typedef struct agent_song {
    uint16_t bpm,note_count;
    uint8_t pattern_count,segment_count;
    agent_song_note_t notes[AGENT_SONG_NOTES];
    agent_song_pattern_t patterns[AGENT_SONG_PATTERNS];
    agent_song_segment_t sequence[AGENT_SONG_SEGMENTS];
} agent_song_t;

typedef struct {
    uint32_t phase,step,age,length,release_at,release_samples,level,noise;
    int32_t low,high;
    uint8_t note,next_tick,pitch,velocity;
} agent_song_voice_t;
typedef struct {
    const agent_song_t *song;
    agent_song_voice_t voices[AGENT_SONG_VOICES];
    uint32_t rendered,samples,tick_end,soft_limited;
    uint16_t tick;
    uint8_t segment;
} agent_song_synth_t;

agent_err_t agent_song_parse(const char *,agent_song_t *);
agent_err_t agent_song_parse_detail(const char *,agent_song_t *,char *,size_t);
agent_err_t agent_song_validate(const agent_song_t *);
uint32_t agent_song_samples(const agent_song_t *);
agent_err_t agent_song_write(const agent_song_t *,agent_write_fn,void *);
agent_err_t agent_song_synth_init(agent_song_synth_t *,const agent_song_t *);
size_t agent_song_render(agent_song_synth_t *,int16_t *,size_t,unsigned);
#endif
