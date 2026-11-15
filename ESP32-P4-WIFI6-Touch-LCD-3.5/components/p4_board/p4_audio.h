#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "../../../platform/espidf/p4_idle_voice_evidence.h"
#include "p4_probe_stage.h"
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
p4_idle_voice_evidence_t p4_audio_idle_voice_evidence(uint32_t from_ms,uint32_t through_ms);
uint32_t p4_audio_confirmed_at(void);
uint32_t p4_audio_strict_quorum_at(void);
uint32_t p4_audio_confirmed_onset(void);
/* Lower quartile of recent idle, DAC-quiet microphone levels after ADC gain
 * compensation. A new native conversation freezes this noise reference. */
unsigned p4_audio_idle_noise(void);
/* Snapshot the pre-cue accepted wake in the same scaled linear-residual
 * power domain as native conversation input. No distance or speaker claim. */
void p4_audio_accept_wake_energy(void);
bool p4_audio_wake_energy(uint32_t *noise,uint32_t *foreground,unsigned *sequence);
/* Single conversation owner publishes its current learned references. The
 * capture owner only reads them; device output never trains this snapshot. */
void p4_audio_energy_reference(uint32_t noise,uint32_t foreground,uint32_t upper,bool calibrated);
/* Optional finite USB diagnostic. Records only PCM accepted by the native
 * transport; never waits, allocates or logs on the sending path. */
void p4_audio_probe_sent(const int16_t *pcm,size_t samples,uint64_t capture_us);
bool p4_audio_probe_binding_active(void);
/* Bind one accepted 320-sample AOQ block to the frontend stages. The record is
 * retained only while a finite probe is active. */
void p4_audio_probe_bound(const p4_probe_bound_t *bound,const int16_t *pcm,size_t samples,
    uint32_t aoq_start_cursor,uint64_t capture_us,bool aoq_local_accept);
void p4_audio_probe_trigger(void);
/* Tag a freshly admitted local candidate on the device capture clock. */
void p4_audio_probe_candidate(uint32_t candidate_ms,uint32_t cursor);
