#ifndef AGENT_AUDIO_SESSION_H
#define AGENT_AUDIO_SESSION_H
#include <stdbool.h>
#include <stdint.h>
#define AGENT_AUDIO_MODEL "qwen-audio-3.1-realtime-flash"
#define AGENT_AUDIO_VOICE "longanqian_v3.1"
/* One connection actor owns this state. Transport generation and response
 * identity are checked before PCM enters the playback queue. */
typedef struct {
    unsigned generation,epoch,stale,interrupts;
    char response[129],retired[128][129],calls[4][129];
    unsigned retired_next,call_count;
    bool active,blocked,speaking,awaiting;
} agent_audio_session_t;
void agent_audio_session_reset(agent_audio_session_t *,unsigned generation);
bool agent_audio_session_response(agent_audio_session_t *,unsigned generation,const char *id);
bool agent_audio_session_pcm(agent_audio_session_t *,unsigned generation,const char *id);
bool agent_audio_session_done(agent_audio_session_t *,unsigned generation,const char *id);
bool agent_audio_session_call(agent_audio_session_t *,unsigned generation,const char *response,const char *call);
/* Returns true only when an existing response must be silenced. */
bool agent_audio_session_interrupt(agent_audio_session_t *);
void agent_audio_session_speech(agent_audio_session_t *,bool started,bool invalid);
bool agent_audio_session_idle(const agent_audio_session_t *);
#endif
