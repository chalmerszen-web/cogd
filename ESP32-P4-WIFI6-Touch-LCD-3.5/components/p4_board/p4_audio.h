#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
esp_err_t p4_audio_init(void);
void p4_audio_capture(bool enable);
esp_err_t p4_audio_read(int16_t pcm[128], uint32_t *samples, unsigned *lost);
esp_err_t p4_audio_output(unsigned source_rate);
esp_err_t p4_audio_write(const int16_t *pcm, size_t samples);
esp_err_t p4_audio_mute(void);
enum { P4_AUDIO_DMA_FRAMES=240, P4_AUDIO_DMA_COUNT=6 };
uint32_t p4_audio_tx_samples(void);
/* Passive counters from the single capture owner. Levels are mean absolute PCM. */
void p4_audio_echo_stats(unsigned values[7]);
void p4_audio_near_monitor(bool enable);
uint32_t p4_audio_confirmed_at(void);
