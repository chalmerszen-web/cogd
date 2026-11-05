#include "audio_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    agent_audio_session_t s;agent_audio_session_reset(&s,7);
    assert(agent_audio_session_idle(&s));
    assert(!agent_audio_session_pcm(&s,7,"unannounced"));
    for(unsigned i=0;i<100;++i) {
        char id[40];snprintf(id,sizeof(id),"response_%u",i);
        agent_audio_session_speech(&s,true,false);
        assert(!agent_audio_session_idle(&s));
        agent_audio_session_speech(&s,false,false);
        assert(!agent_audio_session_idle(&s));
        assert(agent_audio_session_response(&s,7,id));
        assert(agent_audio_session_pcm(&s,7,id));
        assert(agent_audio_session_call(&s,7,id,"call_0"));
        assert(!agent_audio_session_call(&s,7,id,"call_0"));
        assert(!agent_audio_session_call(&s,6,id,"call_1"));
        unsigned epoch=s.epoch;
        assert(agent_audio_session_interrupt(&s));assert(s.epoch==epoch+1);
        assert(!agent_audio_session_interrupt(&s));
        assert(!agent_audio_session_pcm(&s,7,id));
        assert(!agent_audio_session_call(&s,7,id,"call_1"));
        assert(agent_audio_session_done(&s,7,id));
        assert(!agent_audio_session_done(&s,7,id));
        assert(!agent_audio_session_pcm(&s,7,id));
        assert(!agent_audio_session_response(&s,7,id));
        if(i)assert(!agent_audio_session_response(&s,7,"response_0"));
    }
    assert(s.interrupts==100);
    agent_audio_session_speech(&s,true,false);
    agent_audio_session_speech(&s,false,true);
    assert(agent_audio_session_idle(&s));
    agent_audio_session_reset(&s,8);
    assert(!agent_audio_session_response(&s,7,"late_previous_connection"));
    assert(agent_audio_session_response(&s,8,"new"));
    assert(!agent_audio_session_pcm(&s,7,"new"));
    assert(agent_audio_session_pcm(&s,8,"new"));
    assert(agent_audio_session_done(&s,8,"new"));
    assert(!agent_audio_session_pcm(&s,8,"new"));
    char long_id[130];memset(long_id,'x',129);long_id[129]=0;
    assert(!agent_audio_session_response(&s,8,long_id));
    puts("PASS: 100 interruption/late-audio/duplicate-call cycles, invalid turns and connection generations");
    return 0;
}
