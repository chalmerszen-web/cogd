#include "p4_speech.h"
#include "communication_mode.h"
#include "aoq_speech.h"
#if COGD_COMM_MODE != COGD_COMM_AOQ
#error "The native speech adapter must only be linked by the AOQ P4 build"
#endif
typedef struct {
    atomic_flag owner;
    bool asr;
    const atomic_bool *cancelled;
    p4_aoq_speech_t *session;
} speech_transport_t;
static speech_transport_t asr={.owner=ATOMIC_FLAG_INIT,.asr=true},tts={.owner=ATOMIC_FLAG_INIT};
static bool cancelled(void *context)
{speech_transport_t *s=context;return s->cancelled && atomic_load(s->cancelled);}
static agent_err_t open_native(void *context,const atomic_bool *flag)
{
    speech_transport_t *s=context;
    if(atomic_flag_test_and_set_explicit(&s->owner,memory_order_acquire))return AGENT_ERR_BUSY;
    s->cancelled=flag;
    agent_err_t e=(agent_err_t)p4_aoq_speech_open(&s->session,s->asr,s,cancelled);
    if(e){s->cancelled=NULL;atomic_flag_clear_explicit(&s->owner,memory_order_release);}
    return e;
}
static agent_err_t send_native(void *context,bool binary,char *data,size_t size,const atomic_bool *flag)
{
    speech_transport_t *s=context;
    if(flag && atomic_load(flag))return AGENT_ERR_CANCELLED;
    return (agent_err_t)p4_aoq_speech_send(s->session,binary,data,size);
}
static agent_err_t read_native(void *context,char *out,size_t cap,agent_ws_chunk_t *part,unsigned wait,const atomic_bool *flag)
{
    speech_transport_t *s=context;
    if(flag && atomic_load(flag))return AGENT_ERR_CANCELLED;
    p4_aoq_speech_chunk_t chunk={0};
    agent_err_t e=(agent_err_t)p4_aoq_speech_read(s->session,out,cap,&chunk,wait>50?50:wait);
    *part=(agent_ws_chunk_t){.length=chunk.length,.opcode=chunk.opcode,.first=chunk.first,.final=chunk.final};
    return e==AGENT_ERR_BUSY?AGENT_OK:e;
}
static void close_native(void *context)
{
    speech_transport_t *s=context;p4_aoq_speech_close(s->session);s->session=NULL;s->cancelled=NULL;
    atomic_flag_clear_explicit(&s->owner,memory_order_release);
}
const agent_ws_ops_t esp_agent_aoq_asr={open_native,send_native,read_native,close_native,&asr};
const agent_ws_ops_t esp_agent_aoq_tts={open_native,send_native,read_native,close_native,&tts};
