#ifndef AGENT_PROGRESS_H
#define AGENT_PROGRESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fixed 16-kHz mono progress speech. Owns no memory or device resources. */
typedef struct {
    const uint8_t *data;
    unsigned bytes, samples, position, offset, block_bytes;
    int predictor, index;
    bool high;
} agent_progress_t;

void agent_progress_init(agent_progress_t *state, bool cantonese);
size_t agent_progress_render(agent_progress_t *state, int16_t *pcm, size_t capacity);
unsigned agent_progress_samples(bool cantonese);
const char *agent_progress_text(bool cantonese);
void agent_progress_search_init(agent_progress_t *,bool cantonese);
unsigned agent_progress_search_samples(bool cantonese);
const char *agent_progress_search_text(bool cantonese);
void agent_progress_memory_init(agent_progress_t *,bool cantonese);
unsigned agent_progress_memory_samples(bool cantonese);
const char *agent_progress_memory_text(bool cantonese);
/* Finished light operation; checked against its consumer's actual PCM rate. */
#define AGENT_PROGRESS_LIGHT_RATE 16000u
void agent_progress_light_init(agent_progress_t *,bool cantonese);
const char *agent_progress_light_text(bool cantonese);
#endif
