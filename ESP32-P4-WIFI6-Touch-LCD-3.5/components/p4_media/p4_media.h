#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded MP4/High/AAC -> local cache -> timed LCD/PCM playback. */
int p4_media_run(const char *url, unsigned generation, unsigned fps, char *out, size_t cap);
int p4_media_available(void);
void p4_media_touch(unsigned generation);
void p4_media_pause(void);
void p4_media_voice_hold(bool hold);
int p4_media_playback_active(void);
void p4_media_control_status(char *out,size_t cap);
void p4_cloud_media_status(unsigned generation,const char *json);
int p4_media_audio_open(void *ring, size_t bytes);
int p4_media_audio_write(const int16_t *pcm, size_t count);
unsigned p4_media_audio_position(void);
unsigned p4_media_audio_underruns(void);
void p4_media_audio_timing(unsigned values[2]);
int p4_media_audio_started(void);
void p4_media_audio_seal(void);
int p4_media_audio_finish(int error);
/* Background work yields while active speech has less than 500 ms queued. */
int p4_media_audio_reserve(void);
int p4_media_background_wait(unsigned generation);
#ifdef __cplusplus
}
#endif
