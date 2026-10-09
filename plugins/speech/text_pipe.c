#include "text_pipe.h"
#include "json.h"
#include <string.h>

agent_err_t agent_text_pipe_begin(agent_text_pipe_t *p,char *memory,size_t capacity,const atomic_bool *cancelled)
{
    if(!p || !memory || !capacity)return AGENT_ERR_ARGUMENT;
    p->text=memory;p->capacity=capacity<AGENT_TEXT_PIPE_CAPACITY?capacity:AGENT_TEXT_PIPE_CAPACITY;
    p->cancelled=cancelled;
    atomic_init(&p->published,0);atomic_init(&p->finished,false);
    return AGENT_OK;
}
agent_err_t agent_text_pipe_write(agent_text_pipe_t *p,const char *text,size_t length)
{
    if(!p || !p->text || (!text && length))return AGENT_ERR_ARGUMENT;
    if(p->cancelled && atomic_load(p->cancelled))return AGENT_ERR_CANCELLED;
    if(atomic_load_explicit(&p->finished,memory_order_relaxed))return AGENT_ERR_PROTOCOL;
    size_t used=atomic_load_explicit(&p->published,memory_order_relaxed);
    if(length>p->capacity-used)return AGENT_ERR_LIMIT;
    if(!length)return AGENT_OK;
    if(memchr(text,0,length) || !agent_utf8_valid(text,length))return AGENT_ERR_PROTOCOL;
    memcpy(p->text+used,text,length);
    atomic_store_explicit(&p->published,used+length,memory_order_release);
    return AGENT_OK;
}
void agent_text_pipe_finish(agent_text_pipe_t *p)
{
    if(p)atomic_store_explicit(&p->finished,true,memory_order_release);
}
agent_err_t agent_text_pipe_next(const agent_text_pipe_t *p,size_t offset,const char **span,size_t *length,bool *done)
{
    if(!p || !p->text || !span || !length || !done)return AGENT_ERR_ARGUMENT;
    *span=NULL;*length=0;*done=false;
    if(p->cancelled && atomic_load(p->cancelled))return AGENT_ERR_CANCELLED;
    /* EOF publishes the final prefix as well. Observe it first: reading an
     * older empty prefix and then a newer EOF could otherwise drop last text. */
    bool finished=atomic_load_explicit(&p->finished,memory_order_acquire);
    size_t published=atomic_load_explicit(&p->published,memory_order_acquire);
    if(offset>published)return AGENT_ERR_ARGUMENT;
    size_t available=published-offset;
    if(!available){*done=finished;return AGENT_OK;}
    size_t n=available<AGENT_TEXT_PIPE_SPAN?available:AGENT_TEXT_PIPE_SPAN;
    if(n<available)while(n && ((unsigned char)p->text[offset+n]&0xc0)==0x80)--n;
    if(!n)return AGENT_ERR_PROTOCOL;
    *span=p->text+offset;*length=n;
    return AGENT_OK;
}
