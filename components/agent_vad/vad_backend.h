#ifndef ESP_HI_VAD_BACKEND_H
#define ESP_HI_VAD_BACKEND_H
#include "agent.h"

#define ESP_HI_VAD_SAMPLES 256u
#ifdef AGENT_TEN_SKIP_PITCH
#define ESP_HI_VAD_ID "ten-nopitch-fixed8-background-v1"
#else
#define ESP_HI_VAD_ID "ten-pitch-fixed8-background-v1"
#endif
/* Caller owns the single instance and joins its worker before close. */
agent_err_t esp_hi_vad_open(void);
agent_err_t esp_hi_vad_open_at(void *borrowed,size_t capacity);
agent_err_t esp_hi_vad_process(const int16_t samples[ESP_HI_VAD_SAMPLES],unsigned *probability_q15);
void esp_hi_vad_close(void);
size_t esp_hi_vad_bytes(void);
size_t esp_hi_vad_heap_bytes(void);
size_t esp_hi_vad_arena_bytes(void);
#endif
