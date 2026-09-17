#include "clip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[327680],saved[327680];
static int budget=-1;
static unsigned erases;
static unsigned page_programs;
static int read_budget=-1;
static agent_err_t read_flash(void *ctx,size_t offset,void *out,size_t n)
{
    (void)ctx; assert(offset+n<=sizeof(flash));
    if(!read_budget) return AGENT_ERR_CANCELLED;
    if(read_budget>0) --read_budget;
    memcpy(out,flash+offset,n); return AGENT_OK;
}
static agent_err_t write_flash(void *ctx,size_t offset,const void *in,size_t n)
{
    (void)ctx; assert(offset+n<=sizeof(flash)); const uint8_t *p=in;
    page_programs+=(unsigned)((offset%256+n+255)/256);
    for(size_t i=0;i<n;++i) {
        if(budget==0) return AGENT_ERR_STORAGE;
        if(budget>0) --budget;
        assert((flash[offset+i]&p[i])==p[i]); flash[offset+i]&=p[i];
    }
    return AGENT_OK;
}
static agent_err_t erase_flash(void *ctx,size_t offset,size_t n)
{ (void)ctx; assert(offset+n<=sizeof(flash) && !(offset%4096) && n==4096); memset(flash+offset,0xff,n); ++erases; return AGENT_OK; }
static const agent_flash_ops_t ops={read_flash,write_flash,erase_flash,NULL,32768,4096};
static void prepare(agent_clip_t *c,unsigned ms)
{ bool ready=false; assert(!agent_clip_begin(c,ms)); while(!ready) assert(!agent_clip_prepare(c,&ready)); }
static unsigned batching(agent_clip_t *clip,bool aligned)
{
    prepare(clip,1000); page_programs=0;
    for(size_t at=0;at<16000;) {
        int16_t pcm[256];
        size_t n=aligned && !at?AGENT_CLIP_FIRST_BATCH:256;
        if(n>16000-at) n=16000-at;
        for(size_t i=0;i<n;++i) pcm[i]=(int16_t)((at+i)*41-32000);
        assert(!agent_clip_write(clip,pcm,n)); at+=n;
    }
    unsigned programs=page_programs;
    assert(!agent_clip_commit(clip)); return programs;
}
int main(void)
{
    agent_clip_t clip,reboot; int16_t input[1600],output[1600];
    for(unsigned i=0;i<1600;++i) input[i]=(int16_t)(i*41-32000);
    memset(flash,0xff,sizeof(flash)); assert(agent_clip_open(&clip,&ops)==AGENT_ERR_NOT_FOUND);
    assert(agent_clip_begin(&clip,1001)==AGENT_OK); /* Fits the fixture partition. */
    agent_clip_abort(&clip);
    assert(agent_clip_begin(&clip,1100)==AGENT_ERR_FULL);
    assert(agent_clip_begin(&clip,99)==AGENT_ERR_ARGUMENT);
    assert(!agent_clip_begin(&clip,100));
    assert(agent_clip_write(&clip,input,1600)==AGENT_ERR_ARGUMENT); /* No capture into an unerased sector. */
    bool ready=false; assert(!agent_clip_prepare(&clip,&ready) && ready);
    assert(agent_clip_commit(&clip)==AGENT_ERR_ARGUMENT);
    assert(agent_clip_write(&clip,input,1601)==AGENT_ERR_LIMIT);
    assert(!agent_clip_write(&clip,input,137)); assert(!agent_clip_write(&clip,input+137,1463));
    assert(!agent_clip_commit(&clip) && clip.ready && erases==1);
    assert(!agent_clip_open(&reboot,&ops)); assert(reboot.samples==1600);
    assert(!agent_clip_read(&reboot,0,output,1600) && !memcmp(input,output,sizeof(input)));
    assert(agent_clip_read(&reboot,1600,output,1)==AGENT_ERR_LIMIT);
    memcpy(saved,flash,sizeof(flash));
    flash[200]^=1; assert(agent_clip_open(&reboot,&ops)==AGENT_ERR_CORRUPT && !reboot.ready);
    memcpy(flash,saved,sizeof(flash)); flash[12]^=1;
    assert(agent_clip_open(&reboot,&ops)==AGENT_ERR_CORRUPT);
    /* Cut every possible byte of PCM and both header commit writes. */
    for(unsigned cut=0;cut<sizeof(input)+32;++cut) {
        memcpy(flash,saved,sizeof(flash)); assert(!agent_clip_open(&clip,&ops)); prepare(&clip,100);
        budget=(int)cut; agent_err_t e=agent_clip_write(&clip,input,1600);
        if(!e) e=agent_clip_commit(&clip);
        assert(e); budget=-1;
        e=agent_clip_open(&reboot,&ops); assert(e==AGENT_ERR_CORRUPT || e==AGENT_ERR_NOT_FOUND);
        assert(!reboot.ready);
    }
    memset(flash,0xff,sizeof(flash)); assert(agent_clip_open(&clip,&ops)==AGENT_ERR_NOT_FOUND);
    prepare(&clip,100); assert(!agent_clip_write(&clip,input,1600));
    flash[300]^=1; assert(agent_clip_commit(&clip)==AGENT_ERR_CORRUPT && !clip.ready);
    assert(agent_clip_open(&reboot,&ops)==AGENT_ERR_NOT_FOUND);
    /* Incremental sector erase preserves bounded ADC service time; unused tail
     * is not interpreted as samples when VAD commits a shorter clip. */
    assert(!agent_clip_begin(&clip,900));
    assert(!agent_clip_prepare(&clip,&ready) && !ready);
    assert(!agent_clip_write(&clip,input,1600));
    assert(agent_clip_write(&clip,input,1600)==AGENT_ERR_ARGUMENT);
    assert(!agent_clip_prepare(&clip,&ready) && !ready);
    assert(!agent_clip_write(&clip,input,1600));
    assert(!agent_clip_finish(&clip));
    assert(!agent_clip_open(&reboot,&ops) && reboot.samples==3200);
    /* A background reader sees only the explicitly published written prefix. */
    prepare(&clip,300); assert(!agent_clip_write(&clip,input,1600));
    assert(agent_clip_read(&clip,0,output,1)==AGENT_ERR_NOT_FOUND);
    assert(!agent_clip_read_provisional(&ops,137,0,output,137) && !memcmp(input,output,274));
    assert(agent_clip_read_provisional(&ops,137,137,output,1)==AGENT_ERR_LIMIT);
    assert(agent_clip_read_provisional(&ops,1600,SIZE_MAX,output,1)==AGENT_ERR_LIMIT);
    assert(agent_clip_read_provisional(&ops,SIZE_MAX,0,output,1)==AGENT_ERR_LIMIT);
    assert(!agent_clip_write(&clip,input,1600));
    assert(agent_clip_finish_at(&clip,1599)==AGENT_ERR_ARGUMENT);
    assert(agent_clip_finish_at(&clip,3201)==AGENT_ERR_ARGUMENT);
    assert(!agent_clip_finish_at(&clip,1920) && clip.samples==1920 && clip.ready);
    assert(!agent_clip_open(&reboot,&ops) && reboot.samples==1920);
    assert(!agent_clip_read(&reboot,1600,output,320) && !memcmp(input,output,640));
    /* Do not recompute a fresh CRC over already-corrupt queued PCM. Even
     * corruption in a discarded suffix must fail the original writer check. */
    for(unsigned at=100;at<=5000;at+=4900) {
        prepare(&clip,300); assert(!agent_clip_write(&clip,input,1600));
        assert(!agent_clip_write(&clip,input,1600)); flash[32+at]^=1;
        assert(agent_clip_finish_at(&clip,1920)==AGENT_ERR_CORRUPT && !clip.ready && !clip.writing);
        assert(agent_clip_open(&reboot,&ops)==AGENT_ERR_NOT_FOUND);
    }
    /* Cancellation at each whole/prefix scan boundary publishes no header. */
    for(unsigned cut=0;cut<40;++cut) {
        prepare(&clip,300); assert(!agent_clip_write(&clip,input,1600));
        assert(!agent_clip_write(&clip,input,1600)); read_budget=(int)cut;
        assert(agent_clip_finish_at(&clip,1920)==AGENT_ERR_CANCELLED && !clip.ready && !clip.writing);
        read_budget=-1; assert(agent_clip_open(&reboot,&ops)==AGENT_ERR_NOT_FOUND);
    }
    for(unsigned cut=0;cut<32;++cut) {
        prepare(&clip,300); assert(!agent_clip_write(&clip,input,1600));
        assert(!agent_clip_write(&clip,input,1600)); budget=(int)cut;
        assert(agent_clip_finish_at(&clip,1920)==AGENT_ERR_STORAGE && !clip.ready);
        budget=-1; agent_err_t e=agent_clip_open(&reboot,&ops);
        assert(e==AGENT_ERR_CORRUPT || e==AGENT_ERR_NOT_FOUND);
    }
    puts("clip: CRC, partial endpoint, incremental erase, reboot and 3232 power cuts passed");
    puts("clip: provisional bounds, prefix integrity, 40 cancelled reads and 32 prefix-header cuts passed");
    unsigned unaligned=batching(&clip,false); memcpy(saved,flash,sizeof(flash));
    unsigned aligned=batching(&clip,true);
    assert(!memcmp(flash,saved,sizeof(flash)) && !agent_clip_open(&reboot,&ops));
    assert(reboot.samples==16000 && aligned==126 && unaligned==250);
    printf("clip: identical PCM/header, 256-byte page-program model %u -> %u: passed\n",unaligned,aligned);
    /* Aligned producer starts with112; it must publish its final partial batch
     * at the sample limit, before joining a256-sample reader. */
    agent_flash_ops_t full=ops; full.size=sizeof(flash);
    assert(!agent_clip_open(&clip,&full)); prepare(&clip,10000);
    size_t used=0,read_at=0; unsigned blocks=0; int16_t staging[256],block[256];
    for(size_t sample=0;sample<160000;++sample) {
        staging[used++]=(int16_t)(sample*41-32000);
        size_t batch=clip.written?256:AGENT_CLIP_FIRST_BATCH;
        if(used==batch || sample+1==clip.samples) {
            assert(!agent_clip_write(&clip,staging,used)); used=0;
            while(clip.written-read_at>=256) {
                assert(!agent_clip_read_provisional(&full,clip.written,read_at,block,256));
                for(unsigned i=0;i<256;++i) assert(block[i]==(int16_t)((read_at+i)*41-32000));
                read_at+=256; ++blocks;
            }
        }
    }
    assert(!used && read_at==160000 && blocks==625);
    assert(!agent_clip_finish_at(&clip,160000));
    assert(!agent_clip_open(&reboot,&full) && reboot.samples==160000);
    puts("clip: aligned10s producer publishes625 complete reader blocks with no missing tail: passed");
}
