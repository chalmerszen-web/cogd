#include "draft.h"
#include "ima.h"
#include <string.h>

void agent_speech_draft_init(agent_speech_draft_t *d,void *memory,size_t capacity)
{
    memset(d,0,sizeof(*d));d->data=memory;d->capacity=capacity;
}
void agent_speech_draft_init_pages(agent_speech_draft_t *d,uint8_t **pages,size_t capacity)
{ agent_speech_draft_init(d,NULL,capacity);d->pages=pages; }
void agent_speech_draft_reset(agent_speech_draft_t *d)
{
    uint8_t **pages=d->pages;
    agent_speech_draft_init(d,d->data,d->capacity);d->pages=pages;
}
static uint8_t *byte(agent_speech_draft_t *d,size_t at)
{ return d->pages?d->pages[at/AGENT_DRAFT_PAGE_BYTES]+at%AGENT_DRAFT_PAGE_BYTES:d->data+at; }
static bool present(const agent_speech_draft_t *d,size_t begin,size_t end)
{
    if(!d->pages)return d->data!=NULL;
    if(begin==end)return true;
    for(size_t i=begin/AGENT_DRAFT_PAGE_BYTES;i<=(end-1)/AGENT_DRAFT_PAGE_BYTES;++i)
        if(!d->pages[i])return false;
    return true;
}
agent_err_t agent_speech_draft_write(agent_speech_draft_t *d,const int16_t *pcm,size_t n)
{
    if(!d || (!d->data && !d->pages) || d->capacity>65536 || (!pcm && n) || d->read ||
       d->samples>1+2*d->capacity)return AGENT_ERR_ARGUMENT;
    if(d->full || n>1+2*d->capacity-d->samples) {d->full=true;return AGENT_ERR_FULL;}
    if(n && !present(d,d->samples?((d->samples-1)/2):0,(d->samples+n)/2))return AGENT_ERR_MEMORY;
    for(size_t i=0;i<n;++i) {
        if(!d->samples) {d->first=pcm[i];d->encoder=pcm[i];}
        else {
            size_t at=d->samples-1;unsigned code=agent_ima_encode(&d->encoder,&d->index,pcm[i]);
            if(at%2)*byte(d,at/2)|=(uint8_t)(code<<4);
            else *byte(d,at/2)=(uint8_t)code;
        }
        ++d->samples;
    }
    return AGENT_OK;
}
agent_err_t agent_speech_draft_read(agent_speech_draft_t *d,int16_t *pcm,size_t capacity,size_t *count)
{
    if(!d || !pcm || !count || d->full || d->capacity>65536 ||
       d->samples>1+2*d->capacity || d->read>d->samples)return AGENT_ERR_ARGUMENT;
    *count=d->samples-d->read;if(*count>capacity)*count=capacity;
    if(*count && !present(d,d->read?((d->read-1)/2):0,(d->read+*count)/2)) {
        *count=0;return AGENT_ERR_MEMORY;
    }
    for(size_t i=0;i<*count;++i) {
        if(!d->read)d->decoder=d->first;
        else {
            size_t at=d->read-1;unsigned code=(*byte(d,at/2)>>((at%2)*4))&15;
            d->decoder=agent_ima_decode(&d->decoder,&d->decode_index,code);
        }
        pcm[i]=(int16_t)d->decoder;++d->read;
    }
    return AGENT_OK;
}
