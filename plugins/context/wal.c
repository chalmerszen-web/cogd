#include "wal.h"
#include <string.h>

#define BANK_HEADER 64u
#define RECORD_HEADER 32u
#define BANK_MAGIC 0x314b4241u
#define RECORD_MAGIC 0x31525741u
#define COMMIT 0x54494d43u

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t)get32(p+4)<<32; }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(i*8)); }
static void put64(uint8_t *p, uint64_t n) { put32(p,(uint32_t)n); put32(p+4,(uint32_t)(n>>32)); }
static size_t aligned(size_t n) { return (n+3u)&~(size_t)3u; }
static agent_err_t read_at(agent_wal_t *w, size_t off, void *p, size_t n) { return w->flash.read(w->flash.ctx,off,p,n); }
static agent_err_t write_at(agent_wal_t *w, size_t off, const void *p, size_t n) { return w->flash.write(w->flash.ctx,off,p,n); }
static bool blank(const uint8_t *p, size_t n) { while(n--) if (*p++!=0xff) return false; return true; }

static agent_err_t header_write(agent_wal_t *w, size_t base, uint64_t generation, uint64_t high)
{
    uint8_t h[BANK_HEADER]; memset(h,0xff,sizeof(h));
    put32(h,BANK_MAGIC); put32(h+4,1); put64(h+8,generation); put64(h+16,high);
    put32(h+24,agent_crc32(h,24));
    return write_at(w,base,h,60);
}
static agent_err_t commit_at(agent_wal_t *w, size_t off)
{
    uint8_t c[4]; put32(c,COMMIT); return write_at(w,off,c,4);
}
static bool bank_valid(const uint8_t *h)
{
    return get32(h)==BANK_MAGIC && get32(h+4)==1 && get32(h+24)==agent_crc32(h,24) && get32(h+60)==COMMIT;
}
static agent_err_t record_at(agent_wal_t *w, size_t offset, agent_record_t *r)
{
    uint8_t h[RECORD_HEADER], block[256];
    if (offset+sizeof(h)>w->base+w->bank_size) return AGENT_ERR_NOT_FOUND;
    agent_err_t e=read_at(w,offset,h,sizeof(h)); if(e) return e;
    if(blank(h,sizeof(h))) return AGENT_ERR_NOT_FOUND;
    if(get32(h)!=RECORD_MAGIC || get32(h+28)!=COMMIT || get32(h+24)!=agent_crc32(h,24)) return AGENT_ERR_CORRUPT;
    *r=(agent_record_t){.length=get32(h+4),.seq=get64(h+8),.kind=get32(h+16),.offset=offset};
    if(!r->length || r->length>AGENT_REQUEST_MAX || offset+RECORD_HEADER+aligned(r->length)>w->base+w->bank_size ||
       (r->kind!=AGENT_WAL_EVENT && r->kind!=AGENT_WAL_SNAPSHOT)) return AGENT_ERR_CORRUPT;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<r->length;) {
        size_t n=r->length-i; if(n>sizeof(block)) n=sizeof(block);
        e=read_at(w,offset+RECORD_HEADER+i,block,n); if(e) return e;
        crc=agent_crc32_update(crc,block,n); i+=n;
    }
    return ~crc==get32(h+20) ? AGENT_OK : AGENT_ERR_CORRUPT;
}
static agent_err_t scan(agent_wal_t *w)
{
    size_t offset=w->base+BANK_HEADER;
    while(offset+RECORD_HEADER<=w->base+w->bank_size) {
        agent_record_t r; agent_err_t e=record_at(w,offset,&r);
        if(e==AGENT_ERR_NOT_FOUND || e==AGENT_ERR_CORRUPT) {
            /* A valid later frame makes this middle corruption, never a recoverable tail. */
            uint8_t block[256];
            for(size_t i=offset;i<w->base+w->bank_size;) {
                size_t n=w->base+w->bank_size-i; if(n>sizeof(block)) n=sizeof(block);
                e=read_at(w,i,block,n); if(e) return e;
                if(!blank(block,n)) w->tail_recovered=true;
                for(size_t j=0;j+4<=n;j+=4)
                    if(i+j>offset && get32(block+j)==RECORD_MAGIC && record_at(w,i+j,&r)==AGENT_OK)
                        return AGENT_ERR_CORRUPT;
                i+=n;
            }
            break;
        }
        if(e) return e;
        if(r.seq>=w->next_seq) w->next_seq=r.seq+1;
        ++w->records; offset+=RECORD_HEADER+aligned(r.length);
    }
    w->used=offset-w->base; w->ready=true; return AGENT_OK;
}
static bool geometry(const agent_flash_ops_t *f)
{
    return f && f->read && f->write && f->erase && f->sector>=64 && f->size>=f->sector*4 && f->size%(2*f->sector)==0;
}
agent_err_t agent_wal_open(agent_wal_t *w, const agent_flash_ops_t *f)
{
    if(!w || !geometry(f)) return AGENT_ERR_ARGUMENT;
    memset(w,0,sizeof(*w)); w->flash=*f; w->bank_size=f->size/2;
    uint8_t h[2][BANK_HEADER];
    for(unsigned i=0;i<2;++i) { agent_err_t e=read_at(w,i*w->bank_size,h[i],sizeof(h[i])); if(e) return e; }
    bool a=bank_valid(h[0]),b=bank_valid(h[1]);
    if(!a && !b) {
        uint8_t block[256];
        for(size_t i=0;i<f->size;i+=sizeof(block)) {
            size_t n=f->size-i; if(n>sizeof(block)) n=sizeof(block);
            agent_err_t e=read_at(w,i,block,n); if(e) return e;
            if(!blank(block,n)) return AGENT_ERR_CORRUPT;
        }
        return AGENT_ERR_NOT_FOUND;
    }
    unsigned bank=b && (!a || get64(h[1]+8)>get64(h[0]+8)) ? 1 : 0;
    w->base=bank*w->bank_size; w->generation=get64(h[bank]+8); w->next_seq=get64(h[bank]+16)+1;
    return scan(w);
}
agent_err_t agent_wal_format(agent_wal_t *w, const agent_flash_ops_t *f)
{
    if(!w || !geometry(f)) return AGENT_ERR_ARGUMENT;
    memset(w,0,sizeof(*w)); w->flash=*f; w->bank_size=f->size/2;
    agent_err_t e=f->erase(f->ctx,0,f->size);
    if(!e) e=header_write(w,0,1,0);
    if(!e) e=commit_at(w,60);
    return e ? e : agent_wal_open(w,f);
}
static agent_err_t record_write(agent_wal_t *w, size_t off, uint64_t seq, uint32_t kind, const char *data, size_t n)
{
    uint8_t h[RECORD_HEADER]; memset(h,0xff,sizeof(h));
    put32(h,RECORD_MAGIC); put32(h+4,(uint32_t)n); put64(h+8,seq); put32(h+16,kind);
    put32(h+20,agent_crc32(data,n)); put32(h+24,agent_crc32(h,24));
    agent_err_t e=write_at(w,off,h,28);
    if(!e) e=write_at(w,off+RECORD_HEADER,data,n);
    if(!e) e=commit_at(w,off+28);
    return e;
}
agent_err_t agent_wal_append(agent_wal_t *w, uint32_t kind, const char *data, size_t n, uint64_t *seq)
{
    if(!w->ready) return AGENT_ERR_STORAGE;
    if(!data || !n || n>AGENT_REQUEST_MAX || (kind!=AGENT_WAL_EVENT && kind!=AGENT_WAL_SNAPSHOT)) return AGENT_ERR_ARGUMENT;
    if(w->tail_recovered || RECORD_HEADER+aligned(n)>w->bank_size-w->used) return AGENT_ERR_FULL;
    uint64_t next=w->next_seq;
    agent_err_t e=record_write(w,w->base+w->used,next,kind,data,n);
    if(e) { w->tail_recovered=true; return e; }
    w->used+=RECORD_HEADER+aligned(n); ++w->records; ++w->next_seq;
    if(seq) *seq=next;
    return AGENT_OK;
}
agent_err_t agent_wal_read(agent_wal_t *w, const agent_record_t *r, char *buf, size_t cap)
{
    if(cap<=r->length) return AGENT_ERR_LIMIT;
    agent_record_t actual;
    agent_err_t e=record_at(w,r->offset,&actual);
    if(e) return e;
    if(actual.seq!=r->seq || actual.kind!=r->kind || actual.length!=r->length) return AGENT_ERR_CORRUPT;
    e=read_at(w,r->offset+RECORD_HEADER,buf,r->length); buf[r->length]=0; return e;
}
agent_err_t agent_wal_iterate(agent_wal_t *w, uint64_t after, char *buf, size_t cap, agent_record_fn fn, void *ctx)
{
    if(!w->ready) return AGENT_ERR_STORAGE;
    for(size_t off=w->base+BANK_HEADER;off<w->base+w->used;) {
        agent_record_t r; agent_err_t e=record_at(w,off,&r); if(e) return e;
        if(r.seq>after) { e=agent_wal_read(w,&r,buf,cap); if(!e) e=fn(ctx,&r,buf); if(e) return e; }
        off+=RECORD_HEADER+aligned(r.length);
    }
    return AGENT_OK;
}
agent_err_t agent_wal_snapshot_get(agent_wal_t *w, char *buf, size_t cap, size_t *length)
{
    if(!w->ready) return AGENT_ERR_STORAGE;
    agent_record_t last={0};
    for(size_t off=w->base+BANK_HEADER;off<w->base+w->used;) {
        agent_record_t r; agent_err_t e=record_at(w,off,&r); if(e) return e;
        if(r.kind==AGENT_WAL_SNAPSHOT) last=r;
        off+=RECORD_HEADER+aligned(r.length);
    }
    if(!last.length) return AGENT_ERR_NOT_FOUND;
    *length=last.length; return agent_wal_read(w,&last,buf,cap);
}
agent_err_t agent_wal_snapshot_put(agent_wal_t *w, const char *data, size_t n)
{ return agent_wal_append(w,AGENT_WAL_SNAPSHOT,data,n,NULL); }

agent_err_t agent_wal_compact(agent_wal_t *w, const char *snapshot, size_t n, agent_keep_fn keep, void *ctx)
{
    if(!w->ready || !snapshot || !n || n>AGENT_REQUEST_MAX) return AGENT_ERR_ARGUMENT;
    size_t required=BANK_HEADER+RECORD_HEADER+aligned(n), count=1;
    for(size_t off=w->base+BANK_HEADER;off<w->base+w->used;) {
        agent_record_t r; agent_err_t e=record_at(w,off,&r); if(e) return e;
        if(r.kind==AGENT_WAL_EVENT && (!keep || keep(ctx,&r))) { required+=RECORD_HEADER+aligned(r.length); ++count; }
        off+=RECORD_HEADER+aligned(r.length);
    }
    if(required>w->bank_size) return AGENT_ERR_FULL;
    size_t dst=w->base ? 0 : w->bank_size, used=BANK_HEADER;
    agent_err_t e=w->flash.erase(w->flash.ctx,dst,w->bank_size);
    if(!e) e=header_write(w,dst,w->generation+1,w->next_seq);
    if(!e) e=record_write(w,dst+used,w->next_seq,AGENT_WAL_SNAPSHOT,snapshot,n);
    if(e) return e;
    used+=RECORD_HEADER+aligned(n);
    for(size_t off=w->base+BANK_HEADER;off<w->base+w->used;) {
        agent_record_t r; e=record_at(w,off,&r); if(e) return e;
        size_t frame=RECORD_HEADER+aligned(r.length);
        if(r.kind==AGENT_WAL_EVENT && (!keep || keep(ctx,&r))) {
            uint8_t block[256];
            for(size_t i=0;i<frame;) {
                size_t size=frame-i; if(size>sizeof(block)) size=sizeof(block);
                e=read_at(w,off+i,block,size); if(!e) e=write_at(w,dst+used+i,block,size); if(e) return e;
                i+=size;
            }
            used+=frame;
        }
        off+=frame;
    }
    e=commit_at(w,dst+60); if(e) return e;
    /* Old bank remains intact until it is reused by a later rotation. */
    w->base=dst; w->used=used; w->records=count; ++w->generation; ++w->next_seq; w->tail_recovered=false;
    return AGENT_OK;
}
