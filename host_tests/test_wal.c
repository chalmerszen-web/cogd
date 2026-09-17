#include "wal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned char data[8192]; long remaining; } flash_t;
static agent_err_t read_flash(void *ctx,size_t o,void *p,size_t n)
{ flash_t *f=ctx; assert(o+n<=sizeof(f->data)); memcpy(p,f->data+o,n); return AGENT_OK; }
static agent_err_t write_flash(void *ctx,size_t o,const void *p,size_t n)
{
    flash_t *f=ctx; const unsigned char *s=p; assert(o+n<=sizeof(f->data));
    for(size_t i=0;i<n;++i) {
        if(f->remaining==0) return AGENT_ERR_STORAGE;
        if(f->remaining>0) --f->remaining;
        assert((f->data[o+i]&s[i])==s[i]); f->data[o+i]&=s[i];
    }
    return AGENT_OK;
}
static agent_err_t erase_flash(void *ctx,size_t o,size_t n)
{
    flash_t *f=ctx; assert(o%256==0 && n%256==0 && o+n<=sizeof(f->data));
    for(size_t i=0;i<n;++i) {
        if(f->remaining==0) return AGENT_ERR_STORAGE;
        if(f->remaining>0) --f->remaining;
        f->data[o+i]=255;
    }
    return AGENT_OK;
}
static agent_err_t count_record(void *ctx,const agent_record_t *r,const char *p)
{ if(r->kind==AGENT_WAL_EVENT) { assert(!strcmp(p,"hello")); ++*(unsigned *)ctx; } return AGENT_OK; }
static unsigned count(agent_wal_t *w)
{ char scratch[128]; unsigned n=0; assert(!agent_wal_iterate(w,0,scratch,sizeof(scratch),count_record,&n)); return n; }
static bool keep_all(void *ctx,const agent_record_t *r) { (void)ctx; (void)r; return true; }
int main(void)
{
    assert(agent_crc32("123456789",9)==0xcbf43926);
    flash_t f; memset(&f,255,sizeof(f)); f.remaining=-1;
    agent_flash_ops_t ops={read_flash,write_flash,erase_flash,&f,sizeof(f.data),256};
    agent_wal_t w; assert(agent_wal_open(&w,&ops)==AGENT_ERR_NOT_FOUND);
    assert(!agent_wal_format(&w,&ops));
    uint64_t seq; assert(!agent_wal_append(&w,AGENT_WAL_EVENT,"hello",5,&seq) && seq==1);
    flash_t original=f;
    for(long cut=0;cut<40;++cut) {
        f=original; assert(!agent_wal_open(&w,&ops)); f.remaining=cut;
        agent_err_t e=agent_wal_append(&w,AGENT_WAL_EVENT,"hello",5,NULL);
        f.remaining=-1; assert(!agent_wal_open(&w,&ops));
        assert(count(&w)==(e?1u:2u));
        if(w.tail_recovered) {
            assert(agent_wal_append(&w,AGENT_WAL_EVENT,"hello",5,NULL)==AGENT_ERR_FULL);
            assert(!agent_wal_compact(&w,"{}",2,keep_all,NULL));
            assert(!agent_wal_append(&w,AGENT_WAL_EVENT,"hello",5,NULL));
        }
    }
    /* Every byte boundary of erase, header, snapshot, copy and final bank commit. */
    for(long cut=0;cut<4250;++cut) {
        f=original; assert(!agent_wal_open(&w,&ops)); f.remaining=cut;
        agent_err_t e=agent_wal_compact(&w,"{\"saved\":true}",14,keep_all,NULL);
        f.remaining=-1; assert(!agent_wal_open(&w,&ops)); assert(count(&w)==1);
        assert(w.generation==(e?1u:2u));
    }
    f=original; assert(!agent_wal_open(&w,&ops));
    assert(!agent_wal_append(&w,AGENT_WAL_EVENT,"hello",5,NULL));
    f.data[64+32]^=1; assert(agent_wal_open(&w,&ops)==AGENT_ERR_CORRUPT);
    f=original; f.data[64+32]^=1; assert(!agent_wal_open(&w,&ops)); assert(w.tail_recovered && count(&w)==0);
    f=original; assert(!agent_wal_open(&w,&ops));
    assert(!agent_wal_snapshot_put(&w,"{\"x\":1}",7));
    char snap[32]; size_t n; assert(!agent_wal_snapshot_get(&w,snap,sizeof(snap),&n)); assert(n==7 && !strcmp(snap,"{\"x\":1}"));
    while(!agent_wal_append(&w,AGENT_WAL_EVENT,"hello",5,NULL)) {}
    char large[256]; memset(large,'x',sizeof(large));
    assert(agent_wal_compact(&w,large,sizeof(large),keep_all,NULL)==AGENT_ERR_FULL);
    puts("CRC, append power loss, every compaction byte, recovery, middle corruption and full log passed");
}
