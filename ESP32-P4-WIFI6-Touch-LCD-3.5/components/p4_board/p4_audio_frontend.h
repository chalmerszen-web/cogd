#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One AOQ connection owns one conditioner. Never changes capture, KWS or
 * passive interruption PCM, and never allocates while processing a frame. */
typedef struct p4_audio_frontend p4_audio_frontend_t;
p4_audio_frontend_t *p4_audio_frontend_open(void);
bool p4_audio_frontend_process(p4_audio_frontend_t *state,int16_t *pcm,size_t samples);
/* Voice and level from the last pre-AGC frame, never amplified output. */
bool p4_audio_frontend_voiced(const p4_audio_frontend_t *state);
unsigned p4_audio_frontend_level(const p4_audio_frontend_t *state);
unsigned p4_audio_frontend_input_level(const p4_audio_frontend_t *state);
void p4_audio_frontend_close(p4_audio_frontend_t *state);
/* Volatile diagnostic for the next connection: bit 0 gain, bit 1 noise,
 * bit 2 180 Hz high-pass. Local endpointing uses noise suppression before AGC.
 * The USB entry point exists only in a native AOQ probe build. */
bool p4_audio_frontend_configure(unsigned profile,unsigned gain_db);
