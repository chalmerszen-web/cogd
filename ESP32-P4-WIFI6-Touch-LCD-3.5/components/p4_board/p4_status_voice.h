#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    P4_STATUS_READING, P4_STATUS_SOLVING, P4_STATUS_COMPLETE,
    P4_STATUS_SELECTION, P4_STATUS_CONFIRMATION, P4_STATUS_FAILED,
    P4_STATUS_IDLE, P4_STATUS_PAUSED, P4_STATUS_CANCELLED,
    P4_STATUS_COUNT
} p4_status_phrase_t;
/* A lock-busy snapshot is retried by the conversation owner, never fabricated. */
int p4_tutor_progress(p4_status_phrase_t *phrase,unsigned *id,unsigned *revision);
const char *p4_status_voice_text(p4_status_phrase_t phrase);
void *p4_status_voice_open(p4_status_phrase_t phrase);
/* 24 kHz mono PCM. 0 means end; a negative result is a cache/decoder error. */
int p4_status_voice_read(void *voice,int16_t pcm[480]);
void p4_status_voice_close(void *voice);
#ifdef __cplusplus
}
#endif
