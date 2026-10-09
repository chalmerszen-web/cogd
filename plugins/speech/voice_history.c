#include "voice_history.h"
#include <stdio.h>
#include <string.h>

agent_err_t agent_voice_history(agent_engine_t *e,const char *input,
    agent_err_t result,agent_err_t error,unsigned flags)
{
    if(!*input)return result;
    bool candidate=(flags&AGENT_VOICE_RECORD_CANDIDATE)!=0;
    agent_json_writer_t w;
    agent_json_writer_init(&w,(char *)&e->workspace->reply,sizeof(e->workspace->reply));
    agent_json_raw(&w,"{\"format\":\"text/plain\",\"text\":");
    agent_json_quote(&w,input);agent_json_raw(&w,"}");
    if(w.error && !candidate)return result?result:w.error;
    if(!error)error=w.error;
    if(!error)error=agent_context_emit(e->context,"message","user",w.data,e->turn_id,sizeof(e->turn_id));
    agent_messages_t *m=&e->workspace->messages;
    if(!error && !result) {
        size_t n=m->used;
        if(n+14>=sizeof(m->data))error=AGENT_ERR_LIMIT;
        else {
            memmove(m->data+12,m->data,n);memcpy(m->data,"{\"messages\":",12);
            m->data[12+n]='}';m->data[13+n]=0;
            error=agent_context_emit(e->context,"turn","assistant",m->data,NULL,0);
        }
    }
    if(result || error) {
        char record[192];
        snprintf(record,sizeof(record),"{\"route\":\"%s\",\"error\":\"%s\",\"partial\":%s,\"effects\":%s}",
            candidate?"text_candidate":"qwen_fast",agent_err_name(result?result:error),
            flags&AGENT_VOICE_RECORD_PARTIAL?"true":"false",
            flags&AGENT_VOICE_RECORD_EFFECTS?"true":"false");
        (void)agent_context_emit(e->context,"error","system",record,NULL,0);
    }
    return result?result:error;
}
