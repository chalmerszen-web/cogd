#include "ack_spool.h"
#include "ima.h"
#include "crc.h"
#include <string.h>
enum { HEADER=12 };
_Static_assert(((AGENT_ACK_SAMPLES+255)/256)*AGENT_ACK_BLOCK_BYTES<=AGENT_ACK_STORAGE,"ACK Flash budget");
static unsigned get16(const uint8_t *p) { return (unsigned)p[0]|(unsigned)p[1]<<8; }
static void put16(uint8_t *p,unsigned v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static uint32_t checksum(const uint8_t *p)
{
    uint32_t crc=agent_crc32_update(UINT32_MAX,p,8);
    return ~agent_crc32_update(crc,p+HEADER,AGENT_ACK_BLOCK_BYTES-HEADER);
}
agent_err_t agent_ack_spool_init(agent_ack_spool_t *s,const agent_flash_ops_t *f,size_t keep)
{
    if(!s || !f || !f->read || !f->write || !f->erase || f->sector!=4096 ||
       f->size<AGENT_ACK_STORAGE || f->size%f->sector) return AGENT_ERR_ARGUMENT;
    if(keep>f->size-AGENT_ACK_STORAGE) return AGENT_ERR_FULL;
    memset(s,0,sizeof(*s));s->flash=*f;s->base=f->size-AGENT_ACK_STORAGE;
    return AGENT_OK;
}
agent_err_t agent_ack_spool_buffer(agent_ack_spool_t *s,void *buffer,size_t capacity)
{
    if(!s || !s->flash.write || !buffer || capacity<AGENT_ACK_BLOCK_BYTES || capacity>8192)return AGENT_ERR_ARGUMENT;
    if(s->writer.samples || s->writer.sealed || s->writer.error || s->writer.buffer)return AGENT_ERR_BUSY;
    s->writer.buffer=buffer;s->writer.capacity=(unsigned)(capacity/AGENT_ACK_BLOCK_BYTES)*AGENT_ACK_BLOCK_BYTES;
    return AGENT_OK;
}
static agent_err_t commit(agent_ack_spool_t *s,const uint8_t *data,unsigned bytes,unsigned samples)
{
    unsigned at=s->writer.blocks*AGENT_ACK_BLOCK_BYTES,end=at+bytes;
    if(end>AGENT_ACK_STORAGE)return AGENT_ERR_FULL;
    while(s->writer.erased<end) {
        agent_err_t e=s->flash.erase(s->flash.ctx,s->base+s->writer.erased,s->flash.sector);
        if(e)return s->writer.error=e;
        s->writer.erased+=(unsigned)s->flash.sector;
    }
    agent_err_t e=s->flash.write(s->flash.ctx,s->base+at,data,bytes);
    if(!e) {s->writer.blocks+=bytes/AGENT_ACK_BLOCK_BYTES;s->writer.published+=samples;}
    return s->writer.error=e;
}
static agent_err_t commit_buffer(agent_ack_spool_t *s)
{
    if(!s->writer.staged)return AGENT_OK;
    agent_err_t e=commit(s,s->writer.buffer,s->writer.staged,s->writer.staged_samples);
    if(!e)s->writer.staged=s->writer.staged_samples=0;
    return e;
}
static agent_err_t flush(agent_ack_spool_t *s)
{
    if(!s->writer.used)return AGENT_OK;
    uint8_t *b=s->writer.block;put16(b,s->writer.used);
    uint32_t crc=checksum(b);for(unsigned i=0;i<4;++i)b[8+i]=(uint8_t)(crc>>(8*i));
    agent_err_t e=AGENT_OK;
    if(s->writer.buffer) {
        memcpy(s->writer.buffer+s->writer.staged,b,AGENT_ACK_BLOCK_BYTES);
        s->writer.staged+=AGENT_ACK_BLOCK_BYTES;s->writer.staged_samples+=s->writer.used;
        if(s->writer.staged==s->writer.capacity)e=commit_buffer(s);
    } else e=commit(s,b,AGENT_ACK_BLOCK_BYTES,s->writer.used);
    if(!e)s->writer.used=0;
    return e;
}
agent_err_t agent_ack_spool_write(agent_ack_spool_t *s,const int16_t *pcm,size_t n)
{
    if(!s || !s->flash.write || (!pcm && n) || s->writer.sealed)return AGENT_ERR_ARGUMENT;
    if(s->writer.error)return s->writer.error;
    if(n>AGENT_ACK_SAMPLES-s->writer.samples)return AGENT_ERR_LIMIT;
    for(size_t i=0;i<n;++i) {
        unsigned used=s->writer.used;uint8_t *b=s->writer.block;
        if(!used) {
            memset(b,0,AGENT_ACK_BLOCK_BYTES);put16(b+2,(uint16_t)pcm[i]);
            b[4]=(uint8_t)s->writer.index;s->writer.predictor=pcm[i];
        } else {
            unsigned code=agent_ima_encode(&s->writer.predictor,&s->writer.index,pcm[i]);
            b[HEADER+(used-1)/2]|=(uint8_t)(code<<((used-1)%2*4));
        }
        ++s->writer.samples;++s->writer.used;
        if(s->writer.used==AGENT_ACK_BLOCK_SAMPLES) {agent_err_t e=flush(s);if(e)return e;}
    }
    return AGENT_OK;
}
agent_err_t agent_ack_spool_seal(agent_ack_spool_t *s)
{
    if(!s || !s->flash.write || s->writer.sealed || !s->writer.samples)return AGENT_ERR_ARGUMENT;
    if(s->writer.error)return s->writer.error;
    agent_err_t e=flush(s);if(!e)e=commit_buffer(s);
    if(!e) {s->writer.sealed=true;s->writer.buffer=NULL;}
    return e;
}
agent_err_t agent_ack_spool_read(agent_ack_spool_t *s,unsigned published,int16_t *pcm,size_t n)
{
    if(!s || !pcm || published>AGENT_ACK_SAMPLES || s->reader.samples>published ||
       n>published-s->reader.samples)return AGENT_ERR_ARGUMENT;
    for(size_t i=0;i<n;++i) {
        if(s->reader.used==s->reader.count) {
            uint8_t *b=s->reader.block;
            size_t at=(size_t)s->reader.blocks*AGENT_ACK_BLOCK_BYTES;
            if(at+AGENT_ACK_BLOCK_BYTES>AGENT_ACK_STORAGE)return AGENT_ERR_CORRUPT;
            agent_err_t e=s->flash.read(s->flash.ctx,s->base+at,b,AGENT_ACK_BLOCK_BYTES);if(e)return e;
            uint32_t crc=0;for(unsigned j=0;j<4;++j)crc|=(uint32_t)b[8+j]<<(8*j);
            unsigned count=get16(b);
            if(!count || count>AGENT_ACK_BLOCK_SAMPLES || count>published-s->reader.samples ||
               b[4]>88 || b[5] || b[6] || b[7] || crc!=checksum(b))return AGENT_ERR_CORRUPT;
            unsigned raw=get16(b+2);s->reader.predictor=raw>=32768?(int)raw-65536:(int)raw;
            s->reader.index=b[4];s->reader.count=count;s->reader.used=0;++s->reader.blocks;
        }
        unsigned used=s->reader.used;
        if(used) {
            unsigned code=(s->reader.block[HEADER+(used-1)/2]>>((used-1)%2*4))&15u;
            (void)agent_ima_decode(&s->reader.predictor,&s->reader.index,code);
        }
        pcm[i]=(int16_t)s->reader.predictor;++s->reader.used;++s->reader.samples;
    }
    return AGENT_OK;
}
