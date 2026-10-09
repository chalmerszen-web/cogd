#include "ack_spool.h"
#include "crc.h"
#include "ima.h"
#include "clip.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
enum { FLASH_BYTES=448*1024, KEEP_BYTES=FLASH_BYTES-AGENT_ACK_STORAGE };
static uint8_t flash[FLASH_BYTES],encoded[AGENT_ACK_STORAGE];
static int16_t input[AGENT_ACK_SAMPLES],expected[AGENT_ACK_SAMPLES],actual[AGENT_ACK_SAMPLES];
static unsigned writes,erases,reads,fail_write,fail_erase,fail_read;
static unsigned full_unbuffered_writes,full_buffered_writes;
static bool protect_input=true;
static uint32_t rng=5317;
static unsigned random_size(unsigned maximum)
{ rng=rng*1664525u+1013904223u;return 1+rng%maximum; }
static agent_err_t rd(void *ctx,size_t at,void *out,size_t n)
{
    (void)ctx;assert((!protect_input || at>=KEEP_BYTES) && at<=sizeof(flash) && n<=sizeof(flash)-at);
    if(++reads==fail_read)return AGENT_ERR_STORAGE;
    memcpy(out,flash+at,n);return AGENT_OK;
}
static agent_err_t wr(void *ctx,size_t at,const void *in,size_t n)
{
    (void)ctx;assert((!protect_input || at>=KEEP_BYTES) && at<=sizeof(flash) && n<=sizeof(flash)-at);
    bool failed=++writes==fail_write;const uint8_t *p=in;
    for(size_t i=0;i<(failed?n/2:n);++i) {assert((flash[at+i]&p[i])==p[i]);flash[at+i]&=p[i];}
    return failed?AGENT_ERR_STORAGE:AGENT_OK;
}
static agent_err_t erase(void *ctx,size_t at,size_t n)
{
    (void)ctx;assert((!protect_input || at>=KEEP_BYTES) && !(at%4096) && n==4096 && at+n<=sizeof(flash));
    if(++erases==fail_erase)return AGENT_ERR_STORAGE;
    memset(flash+at,255,n);return AGENT_OK;
}
static const agent_flash_ops_t storage={rd,wr,erase,NULL,sizeof(flash),4096};
static void init(agent_ack_spool_t *s)
{
    memset(flash,0x5a,sizeof(flash));writes=erases=reads=fail_write=fail_erase=fail_read=0;
    protect_input=true;
    assert(!agent_ack_spool_init(s,&storage,321282));
    assert(!writes && !erases && !reads);
}
static void protected(void)
{ for(unsigned i=0;i<KEEP_BYTES;++i)assert(flash[i]==0x5a); }
static void fragmented(unsigned samples,unsigned seed,unsigned capacity)
{
    agent_ack_spool_t s;init(&s);rng=seed;
    uint8_t buffer[8192];
    if(capacity)assert(!agent_ack_spool_buffer(&s,buffer,capacity));
    unsigned sent=0,received=0;
    while(sent<samples) {
        unsigned n=random_size(731);if(n>samples-sent)n=samples-sent;
        assert(!agent_ack_spool_write(&s,input+sent,n));sent+=n;
        unsigned published=s.writer.published;
        while(received<published) {
            n=random_size(417);if(n>published-received)n=published-received;
            assert(!agent_ack_spool_read(&s,published,actual+received,n));received+=n;
        }
    }
    assert(!agent_ack_spool_seal(&s) && s.writer.published==samples);
    assert(!s.writer.buffer);memset(buffer,0xa5,sizeof(buffer));
    assert(!agent_ack_spool_read(&s,s.writer.published,actual+received,samples-received));
    assert(!memcmp(actual,expected,samples*sizeof(*actual)));
    assert(!memcmp(flash+KEEP_BYTES,encoded,AGENT_ACK_STORAGE));protected();
    unsigned blocks=(samples+255)/256,batch=capacity?capacity/AGENT_ACK_BLOCK_BYTES:1;
    assert(writes==(blocks+batch-1)/batch);
    if(samples==AGENT_ACK_SAMPLES) {
        if(!capacity)full_unbuffered_writes=writes;
        if(capacity==8192)full_buffered_writes=writes;
    }
}
static void buffered_faults(void)
{
    agent_ack_spool_t s;uint8_t buffer[8192];
    init(&s);
    assert(agent_ack_spool_buffer(&s,NULL,sizeof(buffer))==AGENT_ERR_ARGUMENT);
    assert(agent_ack_spool_buffer(&s,buffer,139)==AGENT_ERR_ARGUMENT);
    assert(agent_ack_spool_buffer(&s,buffer,8193)==AGENT_ERR_ARGUMENT);
    assert(!agent_ack_spool_buffer(&s,buffer,sizeof(buffer)));
    assert(agent_ack_spool_buffer(&s,buffer,sizeof(buffer))==AGENT_ERR_BUSY);
    assert(!agent_ack_spool_write(&s,input,300));
    assert(!s.writer.published && !writes && !erases);
    assert(agent_ack_spool_read(&s,0,actual,1)==AGENT_ERR_ARGUMENT);
    assert(!agent_ack_spool_seal(&s) && s.writer.published==300 && writes==1);
    /* Partial NOR writes and sector erase failures never publish the batch.
     * Earlier committed batches remain readable; errors are sticky. */
    for(unsigned mode=0;mode<4;++mode) {
        init(&s);assert(!agent_ack_spool_buffer(&s,buffer,sizeof(buffer)));
        if(mode==0)fail_write=1;
        if(mode==1)fail_write=2;
        if(mode==2)fail_erase=2;
        unsigned samples=mode==1?30000:mode==3?400:15000;
        agent_err_t error=agent_ack_spool_write(&s,input,samples);
        if(mode==3) {assert(!error);fail_write=1;error=agent_ack_spool_seal(&s);}
        unsigned published=mode==1?58*256:0;
        assert(error==AGENT_ERR_STORAGE && s.writer.published==published);
        if(published)assert(!agent_ack_spool_read(&s,published,actual,published));
        assert(agent_ack_spool_write(&s,input,1)==AGENT_ERR_STORAGE);
        assert(agent_ack_spool_seal(&s)==AGENT_ERR_STORAGE);protected();
    }
    init(&s);assert(!agent_ack_spool_buffer(&s,buffer,sizeof(buffer)));
    assert(agent_ack_spool_write(&s,input,AGENT_ACK_SAMPLES+1)==AGENT_ERR_LIMIT);
    assert(!writes && !erases);protected();
}
static void recording_preserved(void)
{
    agent_ack_spool_t s;init(&s);protect_input=false;
    agent_clip_t clip={.flash=storage};
    unsigned samples=AGENT_MIC_RATE*AGENT_CLIP_MAX_MS/1000;
#if AGENT_PACKED_CLIP
    assert(!agent_clip_begin_packed(&clip,AGENT_CLIP_MAX_MS));
#else
    assert(!agent_clip_begin(&clip,AGENT_CLIP_MAX_MS));
    bool done=false;while(!done)assert(!agent_clip_prepare(&clip,&done));
#endif
    int16_t block[128];rng=72;
    for(unsigned at=0;at<samples;at+=128) {
        unsigned n=samples-at;if(n>128)n=128;
        for(unsigned i=0;i<n;++i)block[i]=(int16_t)random_size(65536);
        assert(!agent_clip_write(&clip,block,n));
    }
    assert(!agent_clip_commit(&clip));
    size_t keep=32+clip.samples*2;
#if AGENT_PACKED_CLIP
    if(clip.packed)keep=32+clip.storage_bytes;
#endif
    uint32_t before=agent_crc32(flash,KEEP_BYTES);
    protect_input=true;
    assert(!agent_ack_spool_init(&s,&storage,keep));
    uint8_t staging[8192];assert(!agent_ack_spool_buffer(&s,staging,sizeof(staging)));
    assert(!agent_ack_spool_write(&s,input,AGENT_ACK_SAMPLES));
    assert(!agent_ack_spool_seal(&s));
    assert(!agent_ack_spool_read(&s,s.writer.published,actual,AGENT_ACK_SAMPLES));
    assert(agent_crc32(flash,KEEP_BYTES)==before);
    protect_input=false;
    agent_clip_t reopened;assert(!agent_clip_open(&reopened,&storage));
    assert(reopened.samples==samples && reopened.ready);
    printf("10-second recording preserved and reopened: %zu stored bytes\n",keep);
}
int main(void)
{
    int p=0,index=0;
    assert(agent_ima_decode(&p,&index,7)==13 && index==8);
    assert(agent_ima_decode(&p,&index,7)==43 && index==16);
    assert(agent_ima_decode(&p,&index,7)==106 && index==24);
    agent_ack_spool_t s;init(&s);
    assert(sizeof(s)<=512);
    assert(agent_ack_spool_init(&s,&storage,KEEP_BYTES+1)==AGENT_ERR_FULL);
    assert(!writes && !erases);protected();
    agent_flash_ops_t bad=storage;bad.sector=1024;
    assert(agent_ack_spool_init(&s,&bad,100)==AGENT_ERR_ARGUMENT);
    init(&s);assert(agent_ack_spool_seal(&s)==AGENT_ERR_ARGUMENT);
    for(unsigned i=0;i<AGENT_ACK_SAMPLES;++i)
        input[i]=(int16_t)(9000*sin(i*.05235987756)+2500*sin(i*.1413716694));
    const unsigned lengths[]={1,2,255,256,257,8203,AGENT_ACK_SAMPLES};
    for(unsigned k=0;k<sizeof(lengths)/sizeof(*lengths);++k) {
        unsigned n=lengths[k];init(&s);
        assert(!agent_ack_spool_write(&s,input,n));assert(s.writer.published==n/256*256);
        assert(!agent_ack_spool_seal(&s));assert(s.writer.published==n);
        assert(!agent_ack_spool_read(&s,n,expected,n));protected();
        memcpy(encoded,flash+KEEP_BYTES,sizeof(encoded));
        assert(agent_ack_spool_read(&s,n,actual,1)==AGENT_ERR_ARGUMENT);
        assert(agent_ack_spool_write(&s,input,1)==AGENT_ERR_ARGUMENT);
        assert(agent_ack_spool_seal(&s)==AGENT_ERR_ARGUMENT);
        for(unsigned seed=1;seed<=9;++seed) {
            fragmented(n,seed,0);fragmented(n,seed,8192);
        }
        fragmented(n,10,140);fragmented(n,11,4096);fragmented(n,12,AGENT_ACK_BLOCK_BYTES*8u);
    }
    double signal=0,noise=0;
    for(unsigned i=0;i<AGENT_ACK_SAMPLES;++i) {
        double x=input[i],d=x-actual[i];signal+=x*x;noise+=d*d;
    }
    double snr=10*log10(signal/noise);assert(snr>25);
    assert(agent_ack_spool_write(&s,input,1)==AGENT_ERR_ARGUMENT);
    init(&s);assert(agent_ack_spool_write(&s,input,AGENT_ACK_SAMPLES+1u)==AGENT_ERR_LIMIT);
    assert(!writes && !erases && !s.writer.samples);protected();
    /* Failed NOR writes may alter bytes, but never publish the damaged block.
     * Sticky errors prohibit retrying or sealing that partial job. */
    init(&s);fail_write=2;
    assert(agent_ack_spool_write(&s,input,512)==AGENT_ERR_STORAGE && s.writer.published==256);
    assert(!agent_ack_spool_read(&s,256,actual,256));
    assert(agent_ack_spool_write(&s,input,1)==AGENT_ERR_STORAGE);
    assert(agent_ack_spool_seal(&s)==AGENT_ERR_STORAGE);protected();
    init(&s);fail_erase=1;
    assert(agent_ack_spool_write(&s,input,256)==AGENT_ERR_STORAGE && !s.writer.published);
    assert(agent_ack_spool_seal(&s)==AGENT_ERR_STORAGE);protected();
    for(unsigned where=0;where<AGENT_ACK_BLOCK_BYTES;++where) {
        init(&s);assert(!agent_ack_spool_write(&s,input,256));
        flash[KEEP_BYTES+where]^=1;
        assert(agent_ack_spool_read(&s,256,actual,1)==AGENT_ERR_CORRUPT);protected();
    }
    init(&s);assert(!agent_ack_spool_write(&s,input,256));fail_read=1;
    assert(agent_ack_spool_read(&s,256,actual,1)==AGENT_ERR_STORAGE);protected();
    recording_preserved();
    buffered_faults();
    printf("ACK spool: random producer/consumer chunks, six-second bound, protected clip, partial NOR failure, CRC; state=%zu B, tone SNR=%.2f dB\n",sizeof(s),snr);
    printf("Six-second Flash write calls: individual=%u batched=%u; bytes and decoded PCM identical\n",
           full_unbuffered_writes,full_buffered_writes);
    return 0;
}
