#ifndef ESP_HI_SPEECH_BACKEND_H
#define ESP_HI_SPEECH_BACKEND_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef AGENT_KWS_C11
/* Audio owner passes its current listening gate; acoustic history advances
 * while closed, but ignored hits must not consume the detector's cooldown. */
bool esp_hi_speech_wake_armed(int16_t *pcm,bool armed);
bool esp_hi_kws_profile(char *out,size_t capacity);
void esp_hi_kws_profile_reset(void); /* Only reset while listening is off. */
#ifdef AGENT_KEYWORD_PCM
typedef struct {
    uint64_t sample_end;
    int16_t score_q8,scores_q8[2],heads_q8[3];
    int error;
} esp_hi_kws_input_t;
/* Audio owner reads directly after its own inference; no concurrent access. */
void esp_hi_kws_input(esp_hi_kws_input_t *out);
#endif
#endif
bool esp_hi_speech_open(void);
void esp_hi_speech_close(void);
/* Release keyword state after a hit; VAD remains available for the utterance. */
void esp_hi_speech_disarm(void);
unsigned esp_hi_speech_chunk(void);
bool esp_hi_speech_wake(int16_t *pcm);
bool esp_hi_speech_vad(int16_t *pcm); /* 320 samples / 20 ms at 16 kHz */
/* Audio owner only, before source frames: replace the initial mode3 detector
 * with mode2 for fast capture. A new listening cycle restores mode3. */
bool esp_hi_speech_vad_fast(void);
unsigned esp_hi_speech_heap(void);
bool esp_hi_speech_threshold(unsigned permille); /* Audio worker only. */
const char *esp_hi_speech_word(void);
const char *esp_hi_speech_model(void);
#ifdef AGENT_KEYWORD_VERIFY
typedef struct {
    unsigned raw,rejected,invalid,incomplete,positive,negative,max_us;
} esp_hi_keyword_stats_t;
/* Lifetime counters; individual fields are atomic snapshots for USB telemetry. */
void esp_hi_speech_keyword_stats(esp_hi_keyword_stats_t *out);
const char *esp_hi_speech_keyword_bank(void);
#endif
#ifdef AGENT_SPEECH_DIAGNOSTICS
/* Diagnostic application only; set while closed, before creating the model. */
bool esp_hi_speech_probe_mode(unsigned mode);
bool esp_hi_speech_probe_vad_mode(unsigned mode); /* Documented vendor modes 0..4. */
typedef struct {
    int rows,columns,front,exponent;
    int16_t latest[64];
} esp_hi_speech_features_t;
/* Copy the newest mono feature row; never expose or mutate vendor storage. */
bool esp_hi_speech_probe_features(esp_hi_speech_features_t *out);
#endif
#endif
