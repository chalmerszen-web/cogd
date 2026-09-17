#include "../boards/esp_hi/audio_work.h"
#include "crc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SOURCE_MAX=4097,OUTPUT_MAX=SOURCE_MAX*3/2 };
static int16_t source[SOURCE_MAX],reference[OUTPUT_MAX],output[OUTPUT_MAX];
static uint8_t header[32];
static struct {
    uint32_t before[8];
    esp_hi_audio_work_t work;
    uint32_t after[8];
} guarded;

static agent_err_t read_clip(void *ctx,size_t at,void *out,size_t n)
{
    (void)ctx;
    if(!at) { assert(n==sizeof(header)); memcpy(out,header,n); return AGENT_OK; }
    assert(at>=32 && at+n<=32+sizeof(source));
    memcpy(out,(const uint8_t *)source+at-32,n); return AGENT_OK;
}
static agent_err_t no_write(void *ctx,size_t at,const void *p,size_t n)
{ (void)ctx; (void)at; (void)p; (void)n; assert(false); return AGENT_ERR_STORAGE; }
static agent_err_t no_erase(void *ctx,size_t at,size_t n)
{ (void)ctx; (void)at; (void)n; assert(false); return AGENT_ERR_STORAGE; }
static void put32(unsigned at,uint32_t value)
{ for(unsigned i=0;i<4;++i) header[at+i]=(uint8_t)(value>>(8*i)); }
static void prepare(agent_replay_t *r,agent_clip_t *clip)
{
    assert(!agent_replay_init(r,clip)); bool ready=false;
    while(!ready) assert(!agent_replay_prepare(r,&ready));
}
static size_t render(agent_replay_t *r,int16_t *out,bool fragmented)
{
    size_t used=0,count; unsigned random=41;
    do {
        random=random*1664525u+1013904223u;
        size_t cap=fragmented?1+random%240:240;
        /* Include the final zero-length read without writing beyond the array. */
        int16_t chunk[240];
        assert(!agent_replay_render(r,chunk,cap,80,&count));
        assert(used+count<=OUTPUT_MAX);
        memcpy(out+used,chunk,count*sizeof(*out)); used+=count;
    } while(count);
    return used;
}
static void switch_to_input(unsigned seed)
{
    /* Simulate completed input frames after abandoning either profiling or
     * playback. All three input arrays may overwrite the previous replay. */
    for(unsigned i=0;i<ESP_HI_CAPTURE_CHUNK;++i)
        guarded.work.input.capture[i]=(int16_t)(seed+i*719u);
    for(unsigned i=0;i<512;++i) guarded.work.input.wake[i]=(int16_t)(seed+i*421u);
    for(unsigned i=0;i<320;++i) guarded.work.input.vad[i]=(int16_t)(seed+i*83u);
}
int main(void)
{
    _Static_assert(sizeof(esp_hi_audio_work_t)==sizeof(agent_replay_t),"Replay must cover the input workspace");
    for(unsigned i=0;i<SOURCE_MAX;++i) source[i]=(int16_t)((i*2179u)%16001-8000);
    const size_t lengths[]={257,320,512,1600,SOURCE_MAX}; unsigned cases=0;
    memset(&guarded,0xa5,sizeof(guarded));
    for(unsigned length=0;length<sizeof(lengths)/sizeof(*lengths);++length) {
        agent_clip_t clip={.flash={.read=read_clip},.ready=true,.samples=lengths[length]};
        agent_replay_t separate;
        prepare(&separate,&clip); size_t expected=render(&separate,reference,false);
        assert(expected==clip.samples*3/2);
        memset(header,0xff,sizeof(header)); put32(0,0x31504341); put32(4,1);
        put32(8,AGENT_MIC_RATE); put32(12,(uint32_t)clip.samples);
        put32(16,agent_crc32(source,clip.samples*sizeof(*source)));
        put32(24,agent_crc32(header,24)); put32(28,0x54494d43);
        for(unsigned phase=0;phase<5;++phase) {
            switch_to_input(length*7+phase);
            if(phase) {
                assert(!agent_replay_init(&guarded.work.replay,&clip));
                bool ready=false;
                assert(!agent_replay_prepare(&guarded.work.replay,&ready));
                if(phase>=2) while(!ready) assert(!agent_replay_prepare(&guarded.work.replay,&ready));
                if(phase==3) {
                    size_t count;
                    assert(!agent_replay_render(&guarded.work.replay,output,137,80,&count));
                    assert(count==137);
                }
                switch_to_input(phase*1597u);
            }
            if(phase==4) {
                agent_flash_ops_t flash={.read=read_clip,.write=no_write,.erase=no_erase,.size=16384,.sector=4096};
                assert(!agent_clip_open(&guarded.work.check,&flash));
                assert(guarded.work.check.ready && guarded.work.check.samples==clip.samples);
                clip=guarded.work.check;
            }
            prepare(&guarded.work.replay,&clip);
            assert(render(&guarded.work.replay,output,true)==expected);
            assert(!memcmp(output,reference,expected*sizeof(*output)));
            for(unsigned i=0;i<8;++i)
                assert(guarded.before[i]==0xa5a5a5a5u && guarded.after[i]==0xa5a5a5a5u);
            ++cases;
        }
    }
    printf("audio workspace: %u input/profile/render transitions, bit-exact PCM; shared %zu B, recovered %zu B\n",
        cases,sizeof(guarded.work),sizeof(guarded.work.input));
}
