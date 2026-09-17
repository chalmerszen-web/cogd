#include "clip.h"
#include "clip_packed.h"
#include "crc.h"
#include <string.h>

enum { HEADER=32, MAGIC=0x31504341, COMMIT=0x54494d43 };
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i)); }
static bool geometry(const agent_flash_ops_t *f)
{ return f && f->read && f->write && f->erase && f->sector>=HEADER && f->size>=2*f->sector && !(f->size%f->sector); }
static agent_err_t checksum(agent_clip_t *c,size_t samples,uint32_t *result)
{
#if AGENT_PACKED_CLIP
    if(c->packed) return agent_packed_checksum(c,samples,result);
#endif
    const agent_flash_ops_t *f=&c->flash;
    uint8_t data[256]; uint32_t crc=UINT32_MAX;
    for(size_t offset=0;offset<samples*2;) {
        size_t n=samples*2-offset; if(n>sizeof(data)) n=sizeof(data);
        agent_err_t e=f->read(f->ctx,HEADER+offset,data,n); if(e) return e;
        crc=agent_crc32_update(crc,data,n); offset+=n;
    }
    *result=~crc; return AGENT_OK;
}
static agent_err_t verify(agent_clip_t *c,uint32_t expected)
{
    uint32_t actual; agent_err_t error=checksum(c,c->samples,&actual);
#if AGENT_PACKED_CLIP
    if(!error && c->packed && (c->reader.crc!=c->encoded_crc || c->reader.offset!=c->storage_bytes || c->reader.remaining))
        error=AGENT_ERR_CORRUPT;
#endif
    return error?error:actual==expected?AGENT_OK:AGENT_ERR_CORRUPT;
}
agent_err_t agent_clip_open(agent_clip_t *c,const agent_flash_ops_t *flash)
{
    if(!c || !geometry(flash)) return AGENT_ERR_ARGUMENT;
    agent_flash_ops_t f=*flash; memset(c,0,sizeof(*c)); c->flash=f;
    uint8_t h[HEADER]; agent_err_t e=f.read(f.ctx,0,h,sizeof(h)); if(e) return e;
    bool blank=true; for(unsigned i=0;i<HEADER;++i) if(h[i]!=0xff) blank=false;
    if(blank) return AGENT_ERR_NOT_FOUND;
    uint32_t version=get32(h+4);
#if AGENT_PACKED_CLIP
    c->packed=version==2 || version==3; c->packed_version=version;
#endif
    if(get32(h)!=MAGIC || (version!=1
#if AGENT_PACKED_CLIP
        && version!=2 && version!=3
#endif
        ) || get32(h+8)!=AGENT_MIC_RATE ||
        get32(h+24)!=agent_crc32(h,24) || get32(h+28)!=COMMIT) return AGENT_ERR_CORRUPT;
    c->samples=get32(h+12);
    if(!c->samples || c->samples>AGENT_MIC_RATE*AGENT_CLIP_MAX_MS/1000 || c->samples*2>f.size-HEADER)
        return AGENT_ERR_CORRUPT;
#if AGENT_PACKED_CLIP
    c->storage_bytes=c->packed?get32(h+20):c->samples*2;
    size_t minimum=version==3?(c->samples+1)/2:c->samples;
    if(c->storage_bytes>f.size-HEADER || c->storage_bytes<minimum) return AGENT_ERR_CORRUPT;
#endif
#if AGENT_PACKED_CLIP
    if(c->packed) e=agent_packed_verify(c,get32(h+16));
    else
#endif
        e=verify(c,get32(h+16));
    c->ready=e==AGENT_OK; return e;
}
agent_err_t agent_clip_begin(agent_clip_t *c,unsigned ms)
{
    if(!c || !geometry(&c->flash) || ms<100 || ms>AGENT_CLIP_MAX_MS) return AGENT_ERR_ARGUMENT;
    if(c->writing) return AGENT_ERR_BUSY;
    size_t samples=(size_t)AGENT_MIC_RATE*ms/1000;
    if(samples*2>c->flash.size-HEADER) return AGENT_ERR_FULL;
    c->samples=samples; c->written=0; c->erased=0; c->crc=UINT32_MAX; c->ready=false; c->writing=true;
    c->erase_end=((HEADER+samples*2+c->flash.sector-1)/c->flash.sector)*c->flash.sector;
#if AGENT_PACKED_CLIP
    c->packed=false; c->packed_version=1; c->storage_bytes=c->available=0;c->encoded_crc=UINT32_MAX;
    memset(&c->pack,0,sizeof(c->pack)); memset(&c->reader,0,sizeof(c->reader));
#endif
    return AGENT_OK;
}
#if AGENT_PACKED_CLIP
agent_err_t agent_clip_begin_packed(agent_clip_t *c,unsigned ms)
{
    agent_err_t e=agent_clip_begin(c,ms);
    if(!e) { c->packed=true; c->packed_version=3; c->erase_end=c->flash.size; }
    return e;
}
#endif
agent_err_t agent_clip_prepare(agent_clip_t *c,bool *done)
{
    if(!c || !done || !c->writing) return AGENT_ERR_ARGUMENT;
    *done=false;
    if(c->erased<c->erase_end) {
        agent_err_t e=c->flash.erase(c->flash.ctx,c->erased,c->flash.sector); if(e) { c->writing=false; return e; }
        c->erased+=c->flash.sector;
    }
    *done=c->erased==c->erase_end; return AGENT_OK;
}
agent_err_t agent_clip_write(agent_clip_t *c,const int16_t *pcm,size_t samples)
{
    if(!c || !pcm || !c->writing) return AGENT_ERR_ARGUMENT;
    if(samples>c->samples-c->written) return AGENT_ERR_LIMIT;
#if AGENT_PACKED_CLIP
    if(c->packed) return agent_packed_write(c,pcm,samples);
#endif
    if(HEADER+(c->written+samples)*2>c->erased) return AGENT_ERR_ARGUMENT;
    /* Explicit byte order keeps host and MCU formats identical. */
    uint8_t data[256];
    while(samples) {
        size_t n=samples; if(n>sizeof(data)/2) n=sizeof(data)/2;
        for(size_t i=0;i<n;++i) { data[2*i]=(uint8_t)pcm[i]; data[2*i+1]=(uint8_t)((uint16_t)pcm[i]>>8); }
        agent_err_t e=c->flash.write(c->flash.ctx,HEADER+c->written*2,data,n*2);
        if(e) { c->writing=false; return e; }
        c->crc=agent_crc32_update(c->crc,data,n*2); c->written+=n; pcm+=n; samples-=n;
    }
    return AGENT_OK;
}
static agent_err_t commit_header(agent_clip_t *c)
{
    c->writing=false;
    uint8_t h[HEADER]; memset(h,0xff,sizeof(h)); put32(h,MAGIC); put32(h+4,1); put32(h+8,AGENT_MIC_RATE);
#if AGENT_PACKED_CLIP
    if(c->packed) { put32(h+4,c->packed_version); put32(h+20,(uint32_t)c->storage_bytes); }
#endif
    put32(h+12,(uint32_t)c->samples); put32(h+16,~c->crc); put32(h+24,agent_crc32(h,24)); put32(h+28,COMMIT);
#if AGENT_PACKED_CLIP
    if(c->packed) { put32(h+16,~c->encoded_crc);put32(h+24,agent_crc32(h,24)); }
#endif
    agent_err_t e=c->flash.write(c->flash.ctx,0,h,28); if(!e) e=c->flash.write(c->flash.ctx,28,h+28,4);
    c->ready=e==AGENT_OK; return e;
}
agent_err_t agent_clip_commit(agent_clip_t *c)
{
    if(!c || !c->writing || c->written!=c->samples) return AGENT_ERR_ARGUMENT;
    agent_err_t e=agent_clip_flush(c);
    if(!e) e=verify(c,~c->crc);
    if(e) { c->writing=false; return e; }
    return commit_header(c);
}
agent_err_t agent_clip_finish(agent_clip_t *c)
{
    if(!c || !c->writing || c->written<AGENT_MIC_RATE/10 || c->written>c->samples) return AGENT_ERR_ARGUMENT;
    c->samples=c->written; return agent_clip_commit(c);
}
agent_err_t agent_clip_finish_at(agent_clip_t *c,size_t samples)
{
    if(!c || !c->writing || samples<AGENT_MIC_RATE/10 || samples>c->written || c->written>c->samples)
        return AGENT_ERR_ARGUMENT;
    if(samples==c->written) return agent_clip_finish(c);
    uint32_t crc,retained; agent_err_t e=agent_clip_flush(c);
    if(!e) {
#if AGENT_PACKED_CLIP
        if(c->packed) e=agent_packed_checksum_prefix(c,c->written,samples,&crc,&retained);
        else
#endif
            e=checksum(c,c->written,&crc);
    }
#if AGENT_PACKED_CLIP
    if(!e && c->packed && (c->reader.crc!=c->encoded_crc || c->reader.offset!=c->storage_bytes || c->reader.remaining))
        e=AGENT_ERR_CORRUPT;
#endif
    if(!e && crc!=~c->crc) e=AGENT_ERR_CORRUPT;
    if(!e
#if AGENT_PACKED_CLIP
       && !c->packed
#endif
       ) e=checksum(c,samples,&retained);
    if(e) { agent_clip_abort(c); return e; }
    c->samples=c->written=samples; c->crc=~retained;
    return commit_header(c);
}
static agent_err_t read_pcm(const agent_flash_ops_t *f,size_t sample,int16_t *out,size_t count)
{
    uint8_t data[256];
    while(count) {
        size_t n=count; if(n>sizeof(data)/2) n=sizeof(data)/2;
        agent_err_t e=f->read(f->ctx,HEADER+sample*2,data,n*2); if(e) return e;
        for(size_t i=0;i<n;++i) out[i]=(int16_t)((uint16_t)data[2*i]|(uint16_t)data[2*i+1]<<8);
        out+=n; sample+=n; count-=n;
    }
    return AGENT_OK;
}
agent_err_t agent_clip_read_provisional(const agent_flash_ops_t *f,size_t available,size_t sample,int16_t *out,size_t count)
{
    if(!f || !f->read || f->size<HEADER || !out) return AGENT_ERR_ARGUMENT;
    if(available>AGENT_MIC_RATE*AGENT_CLIP_MAX_MS/1000 || available>(f->size-HEADER)/2 ||
       sample>available || count>available-sample) return AGENT_ERR_LIMIT;
    return read_pcm(f,sample,out,count);
}
agent_err_t agent_clip_read(agent_clip_t *c,size_t sample,int16_t *out,size_t count)
{
    if(!c || !out || !c->ready) return AGENT_ERR_NOT_FOUND;
    if(sample>c->samples || count>c->samples-sample) return AGENT_ERR_LIMIT;
#if AGENT_PACKED_CLIP
    if(c->packed) return agent_packed_read(c,c->storage_bytes,sample,out,count);
#endif
    return read_pcm(&c->flash,sample,out,count);
}
agent_err_t agent_clip_flush(agent_clip_t *c)
{
    if(!c || !c->writing) return AGENT_ERR_ARGUMENT;
#if AGENT_PACKED_CLIP
    if(c->packed) return agent_packed_flush(c);
#endif
    return AGENT_OK;
}
void agent_clip_progress(const agent_clip_t *c,size_t *samples,size_t *bytes)
{
    if(!c || !samples || !bytes) return;
    *samples=c->written; *bytes=c->written*2;
#if AGENT_PACKED_CLIP
    if(c->packed) { *samples=c->available; *bytes=c->pack.flushed; }
#endif
}
agent_err_t agent_clip_read_pending(agent_clip_t *c,size_t available,size_t bytes,size_t sample,int16_t *out,size_t count)
{
    if(!c || !c->flash.read || c->flash.size<HEADER || !out) return AGENT_ERR_ARGUMENT;
    if(available>AGENT_MIC_RATE*AGENT_CLIP_MAX_MS/1000 || sample>available || count>available-sample || bytes>c->flash.size-HEADER)
        return AGENT_ERR_LIMIT;
#if AGENT_PACKED_CLIP
    if(c->packed) return agent_packed_read(c,bytes,sample,out,count);
#endif
    if(bytes<available*2) return AGENT_ERR_LIMIT;
    return agent_clip_read_provisional(&c->flash,available,sample,out,count);
}
void agent_clip_abort(agent_clip_t *c) { if(c) { c->writing=false; c->ready=false; } }
