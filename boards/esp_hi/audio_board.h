#ifndef ESP_HI_AUDIO_BOARD_H
#define ESP_HI_AUDIO_BOARD_H
#include "audio.h"
#ifdef AGENT_KEYWORD_PCM
#include "keyword_pcm.h"
/* USB owner opens/closes only after inspect reports all audio/mic activity off. */
keyword_pcm_t *esp_hi_keyword_pcm(void);
#endif
agent_err_t esp_hi_wake_status(char *,size_t);
agent_err_t esp_hi_wake_threshold(unsigned permille);
agent_err_t esp_hi_wake_gain(unsigned multiplier); /* 1..4, while listening is off. */
/* Release the keyword model before TLS allocations; listening resumes after. */
agent_err_t esp_hi_wake_network(bool active);
agent_err_t esp_hi_clip_export(unsigned offset,unsigned count,char *,size_t);
agent_err_t esp_hi_audio_init(void);
extern const agent_audio_ops_t esp_hi_audio_ops;
#endif
