#include "voice.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double response(unsigned hz)
{
    agent_voice_t filter; agent_voice_init(&filter);
    double input=0,output=0;
    for(unsigned i=0;i<32000;++i) {
        int16_t x=(int16_t)(8000*sin(6.283185307179586*hz*i/AGENT_MIC_RATE));
        int16_t y=agent_voice_filter(&filter,x);
        if(i>=16000) { input+=(double)x*x; output+=(double)y*y; }
    }
    return sqrt(output/input);
}
static int16_t tone[1600],whole[2400],pieces[2400];
static double cue_response(unsigned hz)
{
    agent_biquad_t state={0}; double input=0,output=0;
    for(unsigned i=0;i<16000;++i) {
        int16_t x=(int16_t)(12000*sin(6.283185307179586*hz*i/16000));
        int16_t y=agent_voice_reject_cue(&state,x);
        if(i>=640) { input+=(double)x*x; output+=(double)y*y; }
    }
    return sqrt(output/input);
}
static agent_err_t read_clip(void *ctx,size_t at,void *out,size_t n)
{
    if(ctx) return fseek(ctx,(long)at,SEEK_SET) || fread(out,1,n,ctx)!=n?AGENT_ERR_STORAGE:AGENT_OK;
    assert(at>=32 && at+n<=32+sizeof(tone));
    memcpy(out,(const char *)tone+at-32,n); return AGENT_OK;
}
static void prepare(agent_replay_t *r,agent_clip_t *clip)
{
    assert(!agent_replay_init(r,clip)); bool ready=false;
    while(!ready) assert(!agent_replay_prepare(r,&ready));
}
static void noise_test(void)
{
    static agent_noise_t n;
    int16_t frame[128],input[64],output[64]; unsigned random=41;
    agent_noise_init(&n);
    for(unsigned f=0;f<64;++f) {
        for(unsigned i=0;i<128;++i) { random=random*1664525u+1013904223u; frame[i]=(int16_t)((int)(random>>20)-2048); }
        agent_noise_profile(&n,frame);
    }
    agent_noise_ready(&n);
    double quiet_in=0,quiet_out=0,tone_in=0,tone_out=0;
    for(unsigned f=0;f<700;++f) {
        for(unsigned i=0;i<64;++i) {
            random=random*1664525u+1013904223u;
            int32_t x=(int)(random>>20)-2048;
            if(f>=250 && f<450) x+=(int32_t)(8000*sin(6.283185307179586*1000*(f*64+i)/16000));
            input[i]=(int16_t)x;
        }
        agent_noise_process(&n,input,output);
        for(unsigned i=0;i<64;++i) {
            if(f>550) { quiet_in+=(double)input[i]*input[i]; quiet_out+=(double)output[i]*output[i]; }
            if(f>300 && f<400) { tone_in+=(double)input[i]*input[i]; tone_out+=(double)output[i]*output[i]; }
        }
    }
    printf("noise: quiet amplitude %.3f, speech-tone amplitude %.3f, state %zu bytes\n",sqrt(quiet_out/quiet_in),sqrt(tone_out/tone_in),sizeof(n));
    assert(quiet_out<quiet_in*.16 && tone_out>tone_in*.65);
    /* Full-scale adversarial input must remain bounded, including overlap sums. */
    for(unsigned f=0;f<1000;++f) {
        for(unsigned i=0;i<64;++i) { random=random*1664525u+1013904223u; input[i]=(int16_t)(random>>16); }
        agent_noise_process(&n,input,output);
    }
}
int main(int argc,char **argv)
{
    noise_test();
    /* Reject the known cue while retaining nearby speech bands, after the
     * existing 40-ms VAD settling window. These are response properties. */
    assert(cue_response(1320)<.002 && cue_response(1290)<.5 && cue_response(1350)<.5);
    assert(cue_response(700)>.98 && cue_response(1000)>.95 && cue_response(2000)>.98);
    agent_biquad_t cue={0}; unsigned cue_seed=17;
    for(unsigned i=0;i<100000;++i) {
        cue_seed=cue_seed*1664525u+1013904223u;
        agent_voice_reject_cue(&cue,(int16_t)(cue_seed>>16));
    }
    assert(response(50)<.08 && response(100)<.01 && response(200)<.01);
    assert(response(1000)>.95 && response(1000)<1.01 && response(7000)<.03);
    agent_voice_t filter; agent_voice_init(&filter);
    for(unsigned i=0;i<16000;++i) {
        int16_t y=agent_voice_filter(&filter,20000);
        if(i>8000) assert(y>=-2 && y<=2);
    }
    unsigned seed=1;
    for(unsigned i=0;i<100000;++i) { seed=seed*1664525u+1013904223u; agent_voice_filter(&filter,(int16_t)(seed>>16)); }
    for(unsigned i=0;i<1600;++i) tone[i]=(int16_t)(8000*sin(6.283185307179586*1000*i/16000));
    agent_clip_t clip={.flash={.read=read_clip},.ready=true,.samples=1600};
    agent_replay_t replay; prepare(&replay,&clip); size_t count;
    assert(!agent_replay_render(&replay,whole,2400,20,&count) && count==2400);
    assert(!agent_replay_render(&replay,whole,2400,20,&count) && !count);
    prepare(&replay,&clip); size_t total=0;
    while(total<2400) {
        seed=seed*1664525u+1013904223u; size_t n=seed%257+1; if(n>2400-total) n=2400-total;
        assert(!agent_replay_render(&replay,pieces+total,n,20,&count)); total+=count;
    }
    assert(!memcmp(whole,pieces,sizeof(whole)) && whole[0]==0 && whole[2399]==0);
    unsigned crossings=0;
    for(unsigned i=240;i<2160;++i) if(whole[i-1]<=0 && whole[i]>0) ++crossings;
    assert(crossings>=79 && crossings<=81); /* 80 ms at 1 kHz; correct 16->24 kHz duration. */
    prepare(&replay,&clip); assert(!agent_replay_render(&replay,whole,2400,0,&count));
    for(unsigned i=0;i<2400;++i) assert(!whole[i]);
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); assert(f); assert(!fseek(f,32,SEEK_SET));
        agent_voice_init(&filter); uint8_t raw[2]; unsigned n=0; double in=0,out=0;
        while(fread(raw,1,2,f)==2) {
            int16_t x=(int16_t)((uint16_t)raw[0]|(uint16_t)raw[1]<<8),y=agent_voice_filter(&filter,x);
            /* Compare the identical final second; no speech/content inference. */
            if(n>=64000) { in+=(double)x*x; out+=(double)y*y; }
            ++n;
        }
        fclose(f); assert(n==80000);
        printf("same-clip final-second RMS: before=%.1f after=%.1f change=%.2f dB\n",sqrt(in/16000),sqrt(out/16000),10*log10(out/in));
    }
    if(argc==3) {
        FILE *source=fopen(argv[1],"rb"),*destination=fopen(argv[2],"wb"); assert(source && destination);
        assert(!fseek(source,0,SEEK_END)); long bytes=ftell(source); assert(bytes>32 && !((bytes-32)%2));
        agent_clip_t recorded={.flash={.read=read_clip,.ctx=source},.ready=true,.samples=(size_t)(bytes-32)/2};
        prepare(&replay,&recorded); int16_t block[240]; size_t written=0;
        do {
            assert(!agent_replay_render(&replay,block,240,100,&count));
            assert(fwrite(block,sizeof(*block),count,destination)==count); written+=count;
        } while(count);
        assert(written==recorded.samples*3/2); fclose(source); fclose(destination);
        printf("rendered %zu samples at 24 kHz locally\n",written);
    }
    puts("voice: hum rejection, passband, DC, bounded state, fragmented resampling and envelope passed");
}
