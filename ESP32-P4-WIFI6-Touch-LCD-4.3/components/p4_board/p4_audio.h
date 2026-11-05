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
