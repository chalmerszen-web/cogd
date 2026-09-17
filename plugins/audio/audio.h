#ifndef AGENT_AUDIO_H
#define AGENT_AUDIO_H
#include "agent.h"
#include "song.h"

#define AGENT_AUDIO_RATE 24000u
#define AGENT_MIC_RATE 16000u
#define AGENT_SCORE_NOTES 64u
#define AGENT_SCORE_SECONDS 30u

typedef enum { AGENT_WAVE_SINE, AGENT_WAVE_TRIANGLE } agent_wave_t;
typedef struct { uint8_t midi, ticks; } agent_note_t;
typedef struct {
    bool ready,playing,mic_requested,mic_enabled,mic_valid,recording,clip_ready,clip_storage,listening;
    unsigned volume,mic_rms,clip_ms;
    uint64_t mic_updated_ms;
    agent_err_t play_error,capture_error,mic_error;
} agent_audio_state_t;

typedef struct {
    uint16_t bpm, count;
    agent_wave_t wave;
    uint32_t samples;
    agent_note_t notes[AGENT_SCORE_NOTES];
} agent_score_t;
typedef struct {
    const agent_score_t *score;
    uint32_t phase, position, note_samples, remainder, rendered;
    unsigned note;
} agent_synth_t;
typedef struct {
    int32_t bias_q8;
    uint64_t squares;
    uint32_t count, peak, clipped;
    bool initialized;
} agent_mic_meter_t;

typedef struct {
    agent_err_t (*status)(void *ctx, char *output, size_t capacity);
    agent_err_t (*play)(void *ctx, const agent_score_t *score);
    agent_err_t (*stop)(void *ctx);
    agent_err_t (*volume)(void *ctx, unsigned percent);
    agent_err_t (*microphone)(void *ctx, bool enabled);
    void *ctx;
    agent_err_t (*inspect)(void *,agent_audio_state_t *);
    agent_err_t (*capture)(void *,unsigned duration_ms);
    agent_err_t (*replay)(void *);
    agent_err_t (*play_song)(void *,const agent_song_t *);
    agent_err_t (*read_song)(void *,agent_write_fn,void *);
    agent_err_t (*listen)(void *,bool enabled);
} agent_audio_ops_t;

struct cJSON;
/* Borrows a parsed node; leaves score unchanged on failure. */
agent_err_t agent_score_parse_node(const struct cJSON *root, agent_score_t *score);
agent_err_t agent_score_parse(const char *json, agent_score_t *score);
agent_err_t agent_score_validate(const agent_score_t *score);
agent_err_t agent_synth_init(agent_synth_t *synth, const agent_score_t *score);
size_t agent_synth_render(agent_synth_t *synth, int16_t *pcm, size_t capacity, unsigned volume);
void agent_mic_meter_feed(agent_mic_meter_t *meter, uint16_t raw);
uint32_t agent_mic_meter_rms(const agent_mic_meter_t *meter);
uint32_t agent_audio_phase_step(unsigned midi);
int32_t agent_audio_sine(uint32_t phase);
#endif
