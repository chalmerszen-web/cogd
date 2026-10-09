#include "preroll.h"
#include "ima.h"
#include "crc.h"
#include <string.h>

static bool overlap(const void *a,size_t na,const void *b,size_t nb)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>UINTPTR_MAX-na || y>UINTPTR_MAX-nb || (x<y+nb && y<x+na);
}
static bool aligned(const void *p,size_t n) { return p && !((uintptr_t)p%n); }
static bool valid(const agent_preroll_t *s)
{
    return aligned(s,_Alignof(agent_preroll_t)) && s->data &&
        s->next<AGENT_PREROLL_FRAMES && s->count<=AGENT_PREROLL_FRAMES &&
        s->read_at<AGENT_PREROLL_FRAMES && s->remaining<=s->count &&
        s->index>=0 && s->index<=88 && (!s->count || s->last_sample>=(uint64_t)s->count*512u) &&
        !overlap(s,sizeof(*s),s->data,AGENT_PREROLL_BYTES);
}
static uint32_t little32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static bool packet_valid(const uint8_t *p)
{
    return p[2]<=88 && p[3]==1 && !(p[259]&0xf0u) &&
        agent_crc32(p,260)==little32(p+260);
}
agent_err_t agent_preroll_init(agent_preroll_t *s,void *data,size_t capacity)
{
    if(!aligned(s,_Alignof(agent_preroll_t)) || !data || capacity<AGENT_PREROLL_BYTES ||
       overlap(s,sizeof(*s),data,AGENT_PREROLL_BYTES))return AGENT_ERR_ARGUMENT;
    if(s->data)return AGENT_ERR_BUSY;
    memset(s,0,sizeof(*s));s->data=data;
    return AGENT_OK;
}
agent_err_t agent_preroll_feed(agent_preroll_t *s,const int16_t pcm[AGENT_PREROLL_SAMPLES],uint64_t end)
{
    if(!valid(s) || !aligned(pcm,_Alignof(int16_t)) || end<512 || end%512 ||
       overlap(pcm,512*sizeof(*pcm),s,sizeof(*s)) ||
       overlap(pcm,512*sizeof(*pcm),s->data,AGENT_PREROLL_BYTES))return AGENT_ERR_ARGUMENT;
    if(s->sealed)return AGENT_ERR_BUSY;
    if(s->last_sample && end<=s->last_sample)return AGENT_ERR_PROTOCOL;
    if(s->last_sample && end-s->last_sample!=512) {
        s->next=s->count=0;s->index=0;
        if(s->discontinuities<UINT32_MAX)++s->discontinuities;
    }
    uint8_t *p=s->data+s->next*AGENT_PREROLL_PACKET;
    unsigned first=(uint16_t)pcm[0];
    p[0]=(uint8_t)first;p[1]=(uint8_t)(first>>8);p[2]=(uint8_t)s->index;p[3]=1;
    int predictor=pcm[0],index=s->index;
    for(unsigned i=0;i<511;++i) {
        unsigned code=agent_ima_encode(&predictor,&index,pcm[i+1]);
        if(i&1u)p[4+i/2]|=(uint8_t)(code<<4);
        else p[4+i/2]=(uint8_t)code;
    }
    uint32_t crc=agent_crc32(p,260);
    for(unsigned i=0;i<4;++i)p[260+i]=(uint8_t)(crc>>(8*i));
    s->index=index;s->last_sample=end;
    s->next=(s->next+1)%AGENT_PREROLL_FRAMES;
    if(s->count<AGENT_PREROLL_FRAMES)++s->count;
    return AGENT_OK;
}
agent_err_t agent_preroll_seal(agent_preroll_t *s)
{
    if(!valid(s))return AGENT_ERR_ARGUMENT;
    if(s->sealed)return AGENT_ERR_BUSY;
    if(!s->count)return AGENT_ERR_NOT_FOUND;
    unsigned oldest=s->count==AGENT_PREROLL_FRAMES?s->next:0;
    for(unsigned i=0;i<s->count;++i)
        if(!packet_valid(s->data+((oldest+i)%AGENT_PREROLL_FRAMES)*AGENT_PREROLL_PACKET))
            return AGENT_ERR_CORRUPT;
    s->read_at=oldest;s->remaining=s->count;s->sealed=true;
    return AGENT_OK;
}
agent_err_t agent_preroll_read(agent_preroll_t *s,int16_t pcm[AGENT_PREROLL_SAMPLES],uint64_t *end)
{
    if(!valid(s) || !aligned(pcm,_Alignof(int16_t)) || !aligned(end,_Alignof(uint64_t)) ||
       overlap(pcm,512*sizeof(*pcm),s,sizeof(*s)) ||
       overlap(pcm,512*sizeof(*pcm),s->data,AGENT_PREROLL_BYTES) ||
       overlap(end,sizeof(*end),s,sizeof(*s)) ||
       overlap(end,sizeof(*end),s->data,AGENT_PREROLL_BYTES) ||
       overlap(end,sizeof(*end),pcm,512*sizeof(*pcm)))return AGENT_ERR_ARGUMENT;
    if(!s->sealed)return AGENT_ERR_BUSY;
    if(!s->remaining)return AGENT_ERR_NOT_FOUND;
    const uint8_t *p=s->data+s->read_at*AGENT_PREROLL_PACKET;
    if(!packet_valid(p))return AGENT_ERR_CORRUPT;
    int predictor=p[0]+((unsigned)p[1]<<8),index=p[2];
    if(predictor>=32768)predictor-=65536;
    pcm[0]=(int16_t)predictor;
    for(unsigned i=0;i<511;++i)
        pcm[i+1]=agent_ima_decode(&predictor,&index,(p[4+i/2]>>((i&1u)*4))&15u);
    *end=s->last_sample-(uint64_t)(s->remaining-1)*512u;
    --s->remaining;s->read_at=(s->read_at+1)%AGENT_PREROLL_FRAMES;
    return AGENT_OK;
}
void agent_preroll_reset(agent_preroll_t *s) { if(s)memset(s,0,sizeof(*s)); }
