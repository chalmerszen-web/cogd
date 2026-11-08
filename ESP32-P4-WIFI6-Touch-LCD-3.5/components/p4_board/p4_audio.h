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
/* Lifetime frames, processing max us, near-detector max us, >32 ms frames,
 * and hardware RX queue overflows. No per-frame logging on the audio task. */
void p4_audio_capture_stats(unsigned values[5]);
void p4_audio_near_monitor(bool enable);
uint32_t p4_audio_confirmed_at(void);
uint32_t p4_audio_confirmed_onset(void);
/* Lower quartile of recent idle, DAC-quiet microphone levels after ADC gain
 * compensation. A new native conversation freezes this noise reference. */
unsigned p4_audio_idle_noise(void);
/* Optional finite USB diagnostic. Records only PCM accepted by the native
 * transport; never waits, allocates or logs on the sending path. */
void p4_audio_probe_sent(const int16_t *pcm,size_t samples,uint64_t capture_us);
void p4_audio_probe_trigger(void);
