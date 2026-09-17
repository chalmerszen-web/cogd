#ifndef ESP_HI_AUDIO_WORK_H
#define ESP_HI_AUDIO_WORK_H
#include "voice.h"

enum { ESP_HI_CAPTURE_CHUNK=256 };
/* The audio task switches members after stopping the keyword model and joining
 * the confirmation worker. Metering and speaker DMA keep separate buffers. */
typedef union {
    struct { int16_t capture[ESP_HI_CAPTURE_CHUNK],wake[512],vad[320]; } input;
    agent_clip_t check;
    agent_replay_t replay;
} esp_hi_audio_work_t;
_Static_assert(sizeof(esp_hi_audio_work_t)==sizeof(agent_replay_t),"Audio workspace must fit the replay state");
#endif
