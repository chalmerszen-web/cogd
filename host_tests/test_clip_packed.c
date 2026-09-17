#include "clip.h"
#include "crc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[0x70000],saved[0x70000];
static int16_t input[160000],output[256];
static int write_budget=-1,read_budget=-1,erase_budget=-1;
static size_t reads,writes,programs,erases;
static agent_err_t read_flash(void *ctx,size_t at,void *out,size_t n)
{
    (void)ctx; assert(at<=sizeof(memory) && n<=sizeof(memory)-at); ++reads;
    if(read_budget==0) return AGENT_ERR_CANCELLED;
    if(read_budget>0) --read_budget;
    memcpy(out,memory+at,n); return AGENT_OK;
}
static agent_err_t write_flash(void *ctx,size_t at,const void *in,size_t n)
{
    (void)ctx; const uint8_t *p=in;assert(at<=sizeof(memory) && n<=sizeof(memory)-at);
    ++writes; programs+=(at%256+n+255)/256;
    for(size_t i=0;i<n;++i) {
        if(write_budget==0) return AGENT_ERR_STORAGE;
        if(write_budget>0) --write_budget;
        assert((memory[at+i]&p[i])==p[i]);memory[at+i]&=p[i];
    }
    return AGENT_OK;
}
static agent_err_t erase_flash(void *ctx,size_t at,size_t n)
{
    (void)ctx; assert(at<=sizeof(memory) && n<=sizeof(memory)-at && !(at%4096) && n==4096);
    if(erase_budget==0) return AGENT_ERR_STORAGE;
    if(erase_budget>0) --erase_budget;
    memset(memory+at,255,n); ++erases; return AGENT_OK;
}
static agent_flash_ops_t ops={read_flash,write_flash,erase_flash,NULL,sizeof(memory),4096};
static uint32_t random_state=41;
static unsigned random32(void) { random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static agent_clip_t clip;
static void put32(uint8_t *out,uint32_t value)
{ for(unsigned i=0;i<4;++i)out[i]=(uint8_t)(value>>(i*8)); }
static void header(unsigned version,unsigned samples,unsigned bytes)
{
    put32(memory,0x31504341);put32(memory+4,version);put32(memory+8,16000);
    put32(memory+12,samples);put32(memory+16,agent_crc32(memory+32,bytes));put32(memory+20,bytes);
    put32(memory+24,agent_crc32(memory,24));put32(memory+28,0x54494d43);
}
static void begin(unsigned ms)
{
    memset(memory,255,sizeof(memory)); assert(agent_clip_open(&clip,&ops)==AGENT_ERR_NOT_FOUND);
    assert(!agent_clip_begin_packed(&clip,ms));reads=writes=programs=erases=0;
}
static void verify(const int16_t *expected,size_t n)
{
    agent_clip_t reboot; assert(!agent_clip_open(&reboot,&ops) && reboot.packed && reboot.samples==n);
    for(size_t at=0;at<n;) {
        size_t take=1+random32()%256; if(take>n-at) take=n-at;
        assert(!agent_clip_read(&reboot,at,output,take));assert(!memcmp(output,expected+at,take*2));at+=take;
    }
    for(unsigned i=0;i<100;++i) {
        size_t at=random32()%n,take=1+random32()%256;if(take>n-at)take=n-at;
        assert(!agent_clip_read(&reboot,at,output,take) && !memcmp(output,expected+at,take*2));
    }
}
int main(void)
{
    /* A previously committed version2 record still opens and decodes exactly. */
    memset(memory,255,sizeof(memory));memory[32]=255;memory[33]=memory[34]=0;
    memset(memory+35,63,127);header(2,128,130);
    assert(!agent_clip_open(&clip,&ops) && clip.packed_version==2);
    assert(!agent_clip_read(&clip,0,output,128));
    for(unsigned i=0;i<128;++i)assert(output[i]==0);
    /* A hand-built6-bit stream has an unaligned escaped negative literal. */
    const uint8_t record[]={130,6,0xe8,3,0xe0,0x0f,0,8};
    memset(memory,255,sizeof(memory));memcpy(memory+32,record,sizeof(record));header(3,3,sizeof(record));
    assert(!agent_clip_open(&clip,&ops) && !agent_clip_read(&clip,0,output,3));
    assert(output[0]==1000 && output[1]==1016 && output[2]==INT16_MIN);
    memory[39]|=0x80;header(3,3,sizeof(record));
    assert(agent_clip_open(&clip,&ops)==AGENT_ERR_CORRUPT); /* Nonzero padding, valid CRC. */
    memory[39]=8;memory[33]=5;header(3,3,sizeof(record));
    assert(agent_clip_open(&clip,&ops)==AGENT_ERR_CORRUPT); /* Invalid width. */
    memory[33]=6;memory[34]=255;memory[35]=127;header(3,3,sizeof(record));
    assert(agent_clip_open(&clip,&ops)==AGENT_ERR_CORRUPT); /* Delta overflow. */
    memcpy(memory+32,record,sizeof(record));header(3,3,sizeof(record)-1);
    assert(agent_clip_open(&clip,&ops)==AGENT_ERR_CORRUPT); /* Escaped literal truncated. */
    for(unsigned mode=0;mode<4;++mode) {
        int32_t value=0;
        for(unsigned i=0;i<160000;++i) {
            if(mode==0) value=0;
            else if(mode==1) value=(int16_t)random32();
            else if(mode==2) value=i%2?INT16_MIN:INT16_MAX;
            else { value+=((int)(random32()%41)-20)*16; if(value>30000 || value< -30000)value=0; }
            input[i]=(int16_t)value;
        }
        begin(10000);size_t consumed=0;
        for(size_t at=0;at<160000;) {
            size_t n=1+random32()%256;if(n>160000-at)n=160000-at;
            assert(!agent_clip_write(&clip,input+at,n));at+=n;
            size_t samples,bytes;agent_clip_progress(&clip,&samples,&bytes);
            assert(samples<=at && bytes<=clip.storage_bytes && bytes==clip.pack.flushed);
            assert(agent_clip_read_pending(&clip,samples,bytes,samples,output,1)==AGENT_ERR_LIMIT);
            assert(agent_clip_read(&clip,0,output,1)==AGENT_ERR_NOT_FOUND);
            while(samples-consumed>=256) {
                assert(!agent_clip_read_pending(&clip,samples,bytes,consumed,output,256));
                assert(!memcmp(output,input+consumed,sizeof(output)));consumed+=256;
            }
        }
        assert(!agent_clip_flush(&clip));size_t samples,bytes;agent_clip_progress(&clip,&samples,&bytes);
        assert(samples==160000 && bytes==clip.storage_bytes);
        while(samples>consumed) {
            size_t n=samples-consumed;if(n>256)n=256;
            assert(!agent_clip_read_pending(&clip,samples,bytes,consumed,output,n));
            assert(!memcmp(output,input+consumed,n*2));consumed+=n;
        }
        assert(!agent_clip_commit(&clip));verify(input,160000);
        printf("mode%u: exact160000samples, %zu encoded bytes, %zu sector erases, %zu page programs\n",mode,clip.storage_bytes,erases,programs);
    }
    /* Prefixes ending inside delta or raw records keep the original whole CRC. */
    for(unsigned mode=0;mode<2;++mode) {
        for(unsigned i=0;i<3200;++i)input[i]=mode?(int16_t)random32():(int16_t)((i%90)*16);
        begin(300);assert(!agent_clip_write(&clip,input,3200));assert(!agent_clip_finish_at(&clip,1937));verify(input,1937);
        begin(300);assert(!agent_clip_write(&clip,input,3200));assert(!agent_clip_flush(&clip));
        memory[32+clip.storage_bytes-2]^=1;
        assert(agent_clip_finish_at(&clip,1937)==AGENT_ERR_CORRUPT && !clip.ready && !clip.writing);
    }
    /* Cut every programmed byte, including both commit writes. */
    memset(input,0,3200);
    begin(100);assert(!agent_clip_write(&clip,input,1600));assert(!agent_clip_flush(&clip));
    agent_clip_t original=clip;size_t original_bytes=clip.storage_bytes;
    /* Another valid block segmentation preserves exactly the same PCM and
     * byte length. Its changed record metadata must still fail the writer CRC. */
    begin(100);assert(!agent_clip_write(&clip,input,126));assert(!agent_clip_write(&clip,input+126,1474));
    assert(!agent_clip_flush(&clip) && clip.storage_bytes==original_bytes && clip.encoded_crc!=original.encoded_crc);
    assert(agent_clip_finish(&original)==AGENT_ERR_CORRUPT && !original.ready);
    for(unsigned i=0;i<1600;++i)input[i]=(int16_t)((i%71)*16);
    begin(100);assert(!agent_clip_write(&clip,input,1600));assert(!agent_clip_commit(&clip));
    size_t bytes=clip.storage_bytes+32;memcpy(saved,memory,sizeof(saved));
    for(size_t cut=0;cut<bytes;++cut) {
        memcpy(memory,saved,sizeof(memory));assert(!agent_clip_open(&clip,&ops));assert(!agent_clip_begin_packed(&clip,100));
        write_budget=(int)cut;agent_err_t e=agent_clip_write(&clip,input,1600);if(!e)e=agent_clip_commit(&clip);
        assert(e && !clip.ready);write_budget=-1;
        agent_clip_t reboot;e=agent_clip_open(&reboot,&ops);assert(e==AGENT_ERR_NOT_FOUND || e==AGENT_ERR_CORRUPT);
    }
    begin(200);assert(!agent_clip_write(&clip,input,1600));assert(!agent_clip_flush(&clip));
    size_t before_reads=reads;assert(!agent_clip_finish_at(&clip,1600));size_t scan_reads=reads-before_reads;
    assert(scan_reads>1);
    for(size_t cut=0;cut<scan_reads;++cut) {
        begin(200);assert(!agent_clip_write(&clip,input,1600));assert(!agent_clip_flush(&clip));
        read_budget=(int)cut;assert(agent_clip_finish_at(&clip,1600)==AGENT_ERR_CANCELLED && !clip.ready);read_budget=-1;
    }
    /* An interrupted read can be retried from its requested sample. */
    memcpy(memory,saved,sizeof(memory));assert(!agent_clip_open(&clip,&ops));
    read_budget=0;assert(agent_clip_read(&clip,0,output,128)==AGENT_ERR_CANCELLED);read_budget=-1;
    assert(!agent_clip_read(&clip,0,output,128) && !memcmp(input,output,256));
    /* Invalid delta token, overflow, truncation and oversized metadata. */
    begin(100);clip.packed_version=2;memory[32]=129;memory[33]=0;memory[34]=0;memory[35]=127;
    assert(agent_clip_read_pending(&clip,2,4,0,output,2)==AGENT_ERR_CORRUPT);
    memory[33]=255;memory[34]=127;memory[35]=126;
    assert(agent_clip_read_pending(&clip,2,4,0,output,2)==AGENT_ERR_CORRUPT);
    memory[32]=1;
    assert(agent_clip_read_pending(&clip,2,4,0,output,2)==AGENT_ERR_CORRUPT);
    assert(agent_clip_read_pending(&clip,2,sizeof(memory),0,output,2)==AGENT_ERR_LIMIT);
    assert(agent_clip_read_pending(&clip,SIZE_MAX,4,0,output,1)==AGENT_ERR_LIMIT);
    begin(100);erase_budget=0;assert(agent_clip_write(&clip,input,1600)==AGENT_ERR_STORAGE && !clip.ready);erase_budget=-1;
    agent_flash_ops_t small=ops;small.size=4096*2;
    memset(memory,255,sizeof(memory));assert(agent_clip_open(&clip,&small)==AGENT_ERR_NOT_FOUND);
    assert(!agent_clip_begin_packed(&clip,200));
    agent_err_t full=AGENT_OK;for(unsigned i=0;i<3200 && !full;++i)full=agent_clip_write(&clip,input,1);
    assert(full==AGENT_ERR_FULL && !clip.writing && !clip.ready);
    printf("packed: %zu byte power cuts, cancelled reads, malformed data, bounds and prefix CRC passed; struct%zu bytes\n",bytes,sizeof(clip));
    return 0;
}
