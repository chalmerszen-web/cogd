#include "audio_session.h"
#include <string.h>
void agent_audio_session_reset(agent_audio_session_t *s,unsigned generation)
{ memset(s,0,sizeof(*s));s->generation=generation;s->epoch=1; }
static bool identity(const agent_audio_session_t *s,unsigned generation,const char *id)
{ return generation==s->generation && id && *id && strlen(id)<sizeof(s->response); }
static void retire(agent_audio_session_t *s)
{
    if(s->response[0])strcpy(s->retired[s->retired_next++%128],s->response);
}
bool agent_audio_session_response(agent_audio_session_t *s,unsigned generation,const char *id)
{
    if(!identity(s,generation,id)){++s->stale;return false;}
    if(!strcmp(id,s->response)){++s->stale;return false;}
    for(unsigned i=0;i<128;++i)if(!strcmp(id,s->retired[i])){++s->stale;return false;}
    retire(s);++s->epoch;strcpy(s->response,id);s->active=true;s->blocked=false;s->awaiting=false;
    s->call_count=0;
    return true;
}
bool agent_audio_session_pcm(agent_audio_session_t *s,unsigned generation,const char *id)
{
    if(!identity(s,generation,id) || strcmp(id,s->response) || !s->active || s->blocked){++s->stale;return false;}
    return true;
}
bool agent_audio_session_done(agent_audio_session_t *s,unsigned generation,const char *id)
{
    if(!identity(s,generation,id) || strcmp(id,s->response) || !s->active){++s->stale;return false;}
    s->active=false;s->awaiting=false;return true;
}
bool agent_audio_session_interrupt(agent_audio_session_t *s)
{
    if(!s->response[0] || s->blocked)return false;
    s->blocked=true;++s->epoch;++s->interrupts;return true;
}
bool agent_audio_session_call(agent_audio_session_t *s,unsigned generation,const char *response,const char *call)
{
    if(!call || !*call || strlen(call)>=sizeof(s->calls[0]) ||
       !identity(s,generation,response) || strcmp(response,s->response) || !s->active || s->blocked)return false;
    for(unsigned i=0;i<s->call_count;++i)if(!strcmp(call,s->calls[i]))return false;
    if(s->call_count==4)return false;
    strcpy(s->calls[s->call_count++],call);return true;
}
void agent_audio_session_speech(agent_audio_session_t *s,bool started,bool invalid)
{ s->speaking=started;if(started)s->awaiting=true;else if(invalid)s->awaiting=false; }
bool agent_audio_session_idle(const agent_audio_session_t *s)
{ return !s->active && !s->speaking && !s->awaiting; }
