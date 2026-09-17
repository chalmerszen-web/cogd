#include "clip_packed.h"
#include "crc.h"
#include <string.h>

#if AGENT_PACKED_CLIP
enum { HEADER=32, BLOCK=128 };

static size_t encode(const int16_t *pcm,size_t n,uint8_t out[257])
{
    size_t costs[3]={0};
    for(size_t i=1;i<n;++i) {
        int32_t d=(int32_t)pcm[i]-pcm[i-1];
        for(unsigned j=0;j<3;++j) {
            unsigned width=4+j*2;int32_t limit=(int32_t)((1u<<(width-1))-1)*16;
            costs[j]+=width+(d%16==0 && d>=-limit && d<=limit?0:16);
        }
    }
    size_t best=1+n*2;unsigned width=0;
    for(unsigned j=0;j<3;++j) {
        size_t bytes=4+(costs[j]+7)/8;
        if(bytes<best) { best=bytes;width=4+j*2; }
    }
    out[0]=(uint8_t)((n-1)|(width?128u:0u));size_t used=1;
    if(!width) {
        for(size_t i=0;i<n;++i) {
            out[used++]=(uint8_t)pcm[i]; out[used++]=(uint8_t)((uint16_t)pcm[i]>>8);
        }
        return used;
    }
    out[used++]=(uint8_t)width;out[used++]=(uint8_t)pcm[0];out[used++]=(uint8_t)((uint16_t)pcm[0]>>8);
    uint32_t bits=0;unsigned count=0,escape=(1u<<width)-1;int32_t radius=(int32_t)(escape/2);
    for(size_t i=1;i<n;++i) {
        int32_t d=(int32_t)pcm[i]-pcm[i-1];
        bool literal=d%16!=0 || d< -radius*16 || d>radius*16;
        uint32_t token=literal?escape:(uint32_t)(d/16+radius);
        bits|=token<<count;count+=width;
        if(literal) { bits|=(uint32_t)(uint16_t)pcm[i]<<count;count+=16; }
        while(count>=8) { out[used++]=(uint8_t)bits;bits>>=8;count-=8; }
    }
    if(count) out[used++]=(uint8_t)bits;
    return used;
}

agent_err_t agent_packed_flush(agent_clip_t *c)
{
    if(!c->pack.used) return AGENT_OK;
    size_t end=HEADER+c->pack.flushed+c->pack.used;
    while(c->erased<end) {
        bool done; agent_err_t e=agent_clip_prepare(c,&done); if(e) return e;
    }
    agent_err_t e=c->flash.write(c->flash.ctx,HEADER+c->pack.flushed,c->pack.page,c->pack.used);
    if(e) { agent_clip_abort(c); return e; }
    c->pack.flushed+=c->pack.used; c->pack.used=0;
    /* A currently encoded record is not included in written until it is whole. */
    c->available=c->written; return AGENT_OK;
}
static agent_err_t append(agent_clip_t *c,const uint8_t *data,size_t n)
{
    if(n>c->flash.size-HEADER-c->storage_bytes) return AGENT_ERR_FULL;
    while(n) {
        size_t room=sizeof(c->pack.page)-(HEADER+c->pack.flushed)%sizeof(c->pack.page)-c->pack.used;
        size_t take=n<room?n:room;
        memcpy(c->pack.page+c->pack.used,data,take);
        c->pack.used+=(unsigned)take; c->storage_bytes+=take; data+=take; n-=take;
        if(take==room) { agent_err_t e=agent_packed_flush(c); if(e) return e; }
    }
    return AGENT_OK;
}
agent_err_t agent_packed_write(agent_clip_t *c,const int16_t *pcm,size_t count)
{
    uint8_t data[257];
    while(count) {
        size_t n=count<BLOCK?count:BLOCK,bytes=encode(pcm,n,data);
        agent_err_t e=append(c,data,bytes);
        if(e) { agent_clip_abort(c); return e; }
        c->encoded_crc=agent_crc32_update(c->encoded_crc,data,bytes);
        for(size_t i=0;i<n;++i) { data[2*i]=(uint8_t)pcm[i];data[2*i+1]=(uint8_t)((uint16_t)pcm[i]>>8); }
        c->crc=agent_crc32_update(c->crc,data,n*2); c->written+=n;
        if(c->storage_bytes==c->pack.flushed) c->available=c->written;
        pcm+=n; count-=n;
    }
    return AGENT_OK;
}
static agent_err_t byte(agent_clip_t *c,size_t limit,uint8_t *out)
{
    if(c->reader.offset>=limit) return AGENT_ERR_CORRUPT;
    if(c->reader.at==c->reader.used) {
        size_t n=limit-c->reader.offset; if(n>sizeof(c->reader.cache)) n=sizeof(c->reader.cache);
        agent_err_t e=c->flash.read(c->flash.ctx,HEADER+c->reader.offset,c->reader.cache,n); if(e) return e;
        c->reader.crc=agent_crc32_update(c->reader.crc,c->reader.cache,n);
        c->reader.at=0; c->reader.used=(unsigned)n;
    }
    *out=c->reader.cache[c->reader.at++]; ++c->reader.offset; return AGENT_OK;
}
static agent_err_t literal(agent_clip_t *c,size_t limit,int32_t *out)
{
    uint8_t lo,hi; agent_err_t e=byte(c,limit,&lo); if(!e) e=byte(c,limit,&hi);
    if(!e) *out=(int16_t)((uint16_t)lo|(uint16_t)hi<<8);
    return e;
}
static agent_err_t bits(agent_clip_t *c,size_t limit,unsigned count,unsigned *out)
{
    while(c->reader.bit_count<count) {
        uint8_t value;agent_err_t e=byte(c,limit,&value);if(e)return e;
        c->reader.bits|=(uint32_t)value<<c->reader.bit_count;c->reader.bit_count+=8;
    }
    *out=c->reader.bits&((1u<<count)-1);
    c->reader.bits>>=count;c->reader.bit_count-=count;return AGENT_OK;
}
static agent_err_t next(agent_clip_t *c,size_t limit,int16_t *out)
{
    agent_err_t e=AGENT_OK;
    if(!c->reader.remaining) {
        uint8_t header; e=byte(c,limit,&header); if(e) return e;
        c->reader.remaining=(header&127u)+1; c->reader.delta=(header&128u)!=0; c->reader.first=true;
        c->reader.width=0;
        if(c->packed_version==3 && c->reader.delta) {
            uint8_t width;e=byte(c,limit,&width);if(e)return e;
            if(width!=4 && width!=6 && width!=8)return AGENT_ERR_CORRUPT;
            c->reader.width=width;
        }
    }
    int32_t value=0;
    if(!c->reader.delta || c->reader.first) e=literal(c,limit,&value);
    else if(c->reader.width) {
        unsigned token,width=c->reader.width,escape=(1u<<width)-1;
        e=bits(c,limit,width,&token);if(e)return e;
        if(token==escape) { unsigned raw;e=bits(c,limit,16,&raw);if(!e)value=(int16_t)raw; }
        else value=c->reader.previous+((int32_t)token-(int32_t)(escape/2))*16;
    } else {
        uint8_t token; e=byte(c,limit,&token); if(e) return e;
        if(token<=126) value=c->reader.previous+((int32_t)token-63)*16;
        else if(token==128) e=literal(c,limit,&value);
        else return AGENT_ERR_CORRUPT;
    }
    if(e) return e;
    if(value<INT16_MIN || value>INT16_MAX) return AGENT_ERR_CORRUPT;
    c->reader.first=false; c->reader.previous=value;
    --c->reader.remaining; ++c->reader.sample; *out=(int16_t)value;
    if(!c->reader.remaining) {
        if(c->reader.bits)return AGENT_ERR_CORRUPT;
        c->reader.bit_count=0;
    }
    return AGENT_OK;
}
agent_err_t agent_packed_read(agent_clip_t *c,size_t bytes,size_t sample,int16_t *out,size_t count)
{
    if(bytes>c->flash.size-HEADER) return AGENT_ERR_LIMIT;
    if(sample<c->reader.sample) memset(&c->reader,0,sizeof(c->reader));
    if(!c->reader.sample && !c->reader.offset) c->reader.crc=UINT32_MAX;
    agent_err_t e=AGENT_OK;
    while(c->reader.sample<sample) { int16_t ignored; e=next(c,bytes,&ignored); if(e) goto failed; }
    while(count--) { e=next(c,bytes,out++); if(e) goto failed; }
    return AGENT_OK;
failed:
    memset(&c->reader,0,sizeof(c->reader)); return e;
}
agent_err_t agent_packed_checksum(agent_clip_t *c,size_t samples,uint32_t *result)
{
    memset(&c->reader,0,sizeof(c->reader));
    uint32_t crc=UINT32_MAX; int16_t pcm[128]; uint8_t data[256];
    for(size_t at=0;at<samples;) {
        size_t n=samples-at; if(n>128) n=128;
        agent_err_t e=agent_packed_read(c,c->storage_bytes,at,pcm,n); if(e) return e;
        for(size_t i=0;i<n;++i) { data[2*i]=(uint8_t)pcm[i];data[2*i+1]=(uint8_t)((uint16_t)pcm[i]>>8); }
        crc=agent_crc32_update(crc,data,n*2); at+=n;
    }
    *result=~crc; return AGENT_OK;
}
agent_err_t agent_packed_checksum_prefix(agent_clip_t *c,size_t total,size_t keep,uint32_t *full,uint32_t *prefix)
{
    if(!c || !full || !prefix || !keep || keep>total) return AGENT_ERR_ARGUMENT;
    if(c->flash.size<HEADER || c->storage_bytes>c->flash.size-HEADER) return AGENT_ERR_LIMIT;
    memset(&c->reader,0,sizeof(c->reader));c->reader.crc=UINT32_MAX;
    uint8_t data[256];uint32_t crc=UINT32_MAX,retained=0;
    for(size_t at=0;at<total;) {
        size_t n=total-at;if(n>BLOCK)n=BLOCK;
        if(at<keep && n>keep-at)n=keep-at;
        for(size_t i=0;i<n;++i) {
            int16_t value;agent_err_t e=next(c,c->storage_bytes,&value);
            if(e) { memset(&c->reader,0,sizeof(c->reader));return e; }
            data[2*i]=(uint8_t)value;data[2*i+1]=(uint8_t)((uint16_t)value>>8);
        }
        crc=agent_crc32_update(crc,data,n*2);at+=n;
        if(at==keep)retained=~crc;
    }
    /* The discarded suffix is still decoded and checked before committing. */
    *full=~crc;*prefix=retained;return AGENT_OK;
}
agent_err_t agent_packed_verify(agent_clip_t *c,uint32_t encoded_crc)
{
    memset(&c->reader,0,sizeof(c->reader));c->reader.crc=UINT32_MAX;
    while(c->reader.offset<c->storage_bytes || c->reader.remaining) {
        int16_t value;agent_err_t e=next(c,c->storage_bytes,&value);if(e)return e;
        if(c->reader.sample>AGENT_MIC_RATE*AGENT_CLIP_MAX_MS/1000) return AGENT_ERR_CORRUPT;
    }
    return c->reader.sample>=c->samples && ~c->reader.crc==encoded_crc?AGENT_OK:AGENT_ERR_CORRUPT;
}
#endif
