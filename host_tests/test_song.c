#include "audio.h"
#include "json.h"
#include "tools.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double rms(const int16_t *pcm,unsigned first,unsigned end)
{
    double sum=0; for(unsigned i=first;i<end;++i) sum+=(double)pcm[i]*pcm[i];
    return sqrt(sum/(end-first));
}
static unsigned played;
static agent_err_t accept_song(void *ctx,const agent_song_t *song)
{ (void)ctx; assert(!agent_song_validate(song)); ++played; return AGENT_OK; }
static agent_err_t audio_status(void *ctx,char *out,size_t cap)
{ (void)ctx; return snprintf(out,cap,"{\"playing\":true}")<(int)cap?AGENT_OK:AGENT_ERR_LIMIT; }
int main(void)
{
    const char *good="{\"v\":1,\"bpm\":128,\"patterns\":[[[69,8,64,100],[0,4,100,100],[69,8,112,100]]],\"sequence\":[[0,-1,-1,-1,127]]}";
    agent_song_t song; assert(!agent_song_parse(good,&song));
    const agent_audio_ops_t audio={.play_song=accept_song,.status=audio_status};
    const agent_tool_ops_t tools={.audio=&audio}; char output[128];
    assert(!agent_tool_invoke(&tools,"device_audio_play_song",good,output,sizeof(output)) && played==1);
    assert(agent_tool_invoke(&tools,"device.audio.play_song","{}",output,sizeof(output)) && played==1);
    assert(agent_song_samples(&song)==90000 && song.note_count==3);
    char roundtrip[4097]; agent_json_writer_t writer;
    agent_json_writer_init(&writer,roundtrip,sizeof(roundtrip));
    assert(!agent_song_write(&song,agent_json_write_bytes,&writer));
    agent_song_t copied; assert(!agent_song_parse(roundtrip,&copied)); assert(!memcmp(&copied,&song,sizeof(song)));
    const char *rest="{\"v\":1,\"bpm\":128,\"patterns\":[[[0,4,0,0],[60,4,80,90]]],\"sequence\":[[0,-1,-1,-1,100]]}";
    assert(!agent_song_parse(rest,&copied));
    const char *bad[]={"", "{}", "[]", "null", "{\"v\":1,}", "{\"v\":1,\"v\":1}",
        "{\"v\":1,\"\\u0076\":1}","{\"v\":1,\"bpm\":128.5}","{\"v\":1,\"bpm\":0128}",
        "{\"v\":1,\"bpm\":9999999999}","{\"pin\":18}","{\"v\\u0", "{\"v\\", "{\"v\":true}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[],\"sequence\":[[0,-1,-1,-1,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[60,33,100,80]]],\"sequence\":[[0,-1,-1,-1,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[60,20,100,80],[64,20,100,80]]],\"sequence\":[[0,-1,-1,-1,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[60,4,100,80]]],\"sequence\":[[-1,-1,-1,0,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[60,4,100,80]]],\"sequence\":[[1,-1,-1,-1,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[60,4,100,80]]],\"sequence\":[[0,-2,-1,-1,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[0,4,0,0]]],\"sequence\":[[0,-1,-1,-1,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[60,4,100,80]]],\"sequence\":[[-1,-1,-1,-1,100]]}",
        "{\"v\":1,\"bpm\":128,\"patterns\":[[[60,4,0,80]]],\"sequence\":[[0,-1,-1,-1,100]]}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        agent_song_t previous=song; assert(agent_song_parse(bad[i],&song)); assert(!memcmp(&song,&previous,sizeof(song)));
    }
    char detail[384];
    assert(agent_song_parse_detail("{\"v\":1,\"bpm\":128,\"patterns\":[[[60,20,100,80],[64,20,100,80]]],\"sequence\":[[0,-1,-1,-1,100]]}",&copied,detail,sizeof(detail))==AGENT_ERR_LIMIT);
    assert(strstr(detail,"[40]") && strstr(detail,"<=32") && strstr(detail,"-1..0") && strstr(detail,"4->2"));
    for(size_t i=0;i<strlen(good);++i) {
        char part[256]; memcpy(part,good,i); part[i]=0; assert(agent_song_parse(part,&copied));
    }
    int16_t *whole=malloc(AGENT_AUDIO_RATE*75*2),*fragmented=malloc(AGENT_AUDIO_RATE*75*2);
    assert(whole && fragmented); agent_song_synth_t synth;
    assert(!agent_song_synth_init(&synth,&song));
    assert(agent_song_render(&synth,whole,100000,80)==90000);
    assert(!agent_song_render(&synth,whole,1,80));
    double weak=rms(whole,1200,3600),strong=rms(whole,33750+1200,33750+3600);
    assert(strong/weak>1.70 && strong/weak<1.80);
    assert(rms(whole,1200,3600)>rms(whole,15000,17000)*3);
    for(unsigned i=22500;i<33750;++i) assert(!whole[i]);
    song.segment_count=16;
    for(unsigned i=1;i<16;++i) song.sequence[i]=song.sequence[0];
    assert(!agent_song_synth_init(&synth,&song) && synth.samples==1440000);
    assert(agent_song_render(&synth,whole,synth.samples,100)==1440000);
    assert(!agent_song_synth_init(&synth,&song));
    unsigned offset=0,seed=47;
    while(offset<synth.samples) {
        seed=seed*1664525+1013904223;
        offset+=(unsigned)agent_song_render(&synth,fragmented+offset,1+seed%877,100);
    }
    assert(!memcmp(whole,fragmented,offset*2)); assert(!whole[0] && !whole[offset-1]);
    song.bpm=60; assert(agent_song_validate(&song)==AGENT_ERR_LIMIT);
    song.bpm=137; assert(!agent_song_synth_init(&synth,&song));
    assert(synth.samples==(uint64_t)16*32*24000*60/(137*4));
    song.pattern_count=16; song.note_count=128; song.bpm=128;
    for(unsigned i=0;i<16;++i) {
        song.patterns[i]=(agent_song_pattern_t){(uint8_t)(i*8),8};
        for(unsigned j=0;j<8;++j) song.notes[i*8+j]=(agent_song_note_t){(uint8_t)(i==15?(j%2?42:36):60+i),4,127,100};
    }
    for(unsigned i=0;i<16;++i) song.sequence[i]=(agent_song_segment_t){{0,1,2,15},127};
    assert(!agent_song_synth_init(&synth,&song));
    agent_json_writer_init(&writer,roundtrip,sizeof(roundtrip));
    assert(!agent_song_write(&song,agent_json_write_bytes,&writer));
    assert(!agent_song_parse(roundtrip,&copied));
    assert(agent_song_render(&synth,whole,synth.samples,100)==1440000);
    for(unsigned i=0;i<1440000;++i) assert(whole[i]>-8192 && whole[i]<8192);
    assert(!agent_song_synth_init(&synth,&song));
    assert(agent_song_render(&synth,whole,50000,0)==50000);
    for(unsigned i=0;i<50000;++i) assert(!whole[i]);
    free(whole); free(fragmented);
    puts("song: transactional bounded grammar, 128-note/four-voice mix, one-minute timing, dynamics, decay, PCM range and fragmentation PASS");
}
