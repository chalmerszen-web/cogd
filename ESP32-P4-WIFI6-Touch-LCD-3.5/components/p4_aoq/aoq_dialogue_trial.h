#pragma once
#include <stddef.h>
#include <stdint.h>
#include "aoq_tts_play.h"
#ifdef __cplusplus
extern "C" {
#endif
// Fresh, single-turn native Audio Realtime probe using a successful TTS cache
// as its input fixture. No microphone, tools or normal conversation state.
int p4_aoq_dialogue_trial_command(const char*,char*,size_t);
// Explicit one-turn diagnostic, disabled by default. Read requires generation.
int p4_aoq_dialogue_input_command(const char*,char*,size_t);
void p4_aoq_dialogue_trial_cancel(void);
int p4_aoq_dialogue_trial_copy_pcm(uint64_t,void*,size_t,size_t*);
/* Explicit single-turn microphone experiment. The caller owns the existing
 * capture task; read copies at most capacity samples, returning 0/count=0 when
 * none are ready. No second microphone or speaker owner is created. One fresh
 * cloud allocation per wake; no tools, history, follow-up or barge-in claim.
 * Playback streams while receiving, and finish always joins before return.
 * stop_capture ends input-session admission before playback cleanup; capture
 * storage remains owned by the board task and is never borrowed here. */
typedef struct {
    // Live open starts the existing endpoint cue on the board audio owner.
    // It is called at the matched speech-stop, before response PCM arrives;
    // it must remain nonblocking while the network receives that response.
    p4_aoq_play_ops_t playback;
    int (*read)(void*,int16_t*,size_t capacity,size_t* samples);
    void (*stop_capture)(void*);
    void (*event)(void*,const char* stage,const char* text);
    // Optional video source: nonblocking copy of a NEW 320x256 JPEG, with its
    // capture timestamp and sequence. Busy=3 means no new frame; no old-image
    // fallback. stop_capture also ends this camera lease when supplied.
    int (*video_frame)(void*,unsigned after,uint8_t*,size_t,size_t*,unsigned*,uint64_t*);
    // Required live capture origin, before allocation, on esp_timer's clock.
    // Queued PCM retains this timeline while bounded sending catches up.
    uint64_t capture_origin_us;
} p4_aoq_dialogue_live_ops_t;
int p4_aoq_dialogue_live(const p4_aoq_dialogue_live_ops_t*,char*,size_t);
#ifdef __cplusplus
}
#endif
