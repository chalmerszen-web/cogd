#include "progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* This vector was decoded with FFmpeg's independent adpcm_ima_wav decoder,
 * not this implementation. It crosses headers and exercises both saturation
 * rails and step-index extremes. RIFF fixture/command live with the manifest. */
static void known_vector(void)
{
    static const uint8_t data[]={
        0xe8,0x03,0x00,0x00,0x10,0x32,0x54,0x76,
        0x18,0xfc,0x58,0x00,0xff,0x77,0x08,0x80
    };
    static const int16_t expected[]={1000,1000,1002,1006,1012,1019,1031,1052,
        1095,-1000,-32768,-32768,28670,32767,28672,32396,32767,29690};
    agent_progress_t state={.data=data,.bytes=sizeof(data),.samples=18,.block_bytes=8};
    int16_t pcm[20];
    assert(agent_progress_render(&state,pcm,20)==18);
    assert(!memcmp(pcm,expected,sizeof(expected)));
    assert(agent_progress_render(&state,pcm,20)==0);
    state=(agent_progress_t){.data=data,.bytes=sizeof(data),.samples=18,.block_bytes=8};
    for(unsigned i=0;i<18;++i) {
        assert(agent_progress_render(&state,pcm,1)==1);
        assert(pcm[0]==expected[i]);
    }
}

static uint32_t pcm_crc(const int16_t *pcm,size_t count)
{
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<count;++i) for(unsigned byte=0;byte<2;++byte) {
        crc^=((uint16_t)pcm[i]>>(byte*8))&255u;
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0);
    }
    return ~crc;
}

static void asset(bool cantonese,const char *dump)
{
    static int16_t whole[40000],pieces[40000];
    /* Frozen independent FFmpeg reference PCM for the checked-in assets. */
    const unsigned samples=cantonese?19762u:18858u;
    const uint32_t reference_crc=cantonese?UINT32_C(0xbdafb4c9):UINT32_C(0x1d19c1a7);
    assert(agent_progress_samples(cantonese)==samples);
    assert(samples<=sizeof(whole)/sizeof(*whole));
    agent_progress_t state;
    agent_progress_init(&state,cantonese);
    assert(agent_progress_render(&state,whole,40000)==samples);
    assert(agent_progress_render(&state,whole,40000)==0);
    assert(pcm_crc(whole,samples)==reference_crc);
    uint32_t seed=71;
    for(unsigned pass=0;pass<100;++pass) {
        agent_progress_init(&state,cantonese);
        unsigned at=0;
        while(at<samples) {
            seed=seed*UINT32_C(1664525)+UINT32_C(1013904223);
            size_t requested=1+seed%601u;
            if(requested>samples-at) requested=samples-at;
            size_t n=agent_progress_render(&state,pieces+at,requested);
            assert(n==requested);at+=(unsigned)n;
        }
        assert(!memcmp(whole,pieces,samples*sizeof(*whole)));
        assert(agent_progress_render(&state,pieces,1)==0);
    }
    if(dump) {
        FILE *file=fopen(dump,"wb");assert(file);
        for(unsigned i=0;i<samples;++i) {
            const uint8_t bytes[]={(uint8_t)whole[i],(uint8_t)((uint16_t)whole[i]>>8)};
            assert(fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes));
        }
        assert(fclose(file)==0);
    }
    printf("progress %s samples=%u crc32=%08x bytes=%u state=%zu\n",
        cantonese?"yue":"zh",samples,(unsigned)reference_crc,state.bytes,sizeof(state));
}

static void search_assets(void)
{
    static int16_t whole[24000],pieces[24000];
    static const unsigned lengths[]={20395,23090};
    static const uint32_t golden[]={UINT32_C(0x05717d00),UINT32_C(0xf7793c03)};
    for(unsigned yue=0;yue<2;++yue) {
        agent_progress_t s;
        assert(agent_progress_search_samples(yue)==lengths[yue]);
        assert(agent_progress_search_text(yue)[0]);
        agent_progress_search_init(&s,yue);
        assert(agent_progress_render(&s,whole,24000)==lengths[yue]);
        assert(pcm_crc(whole,lengths[yue])==golden[yue]);
        agent_progress_search_init(&s,yue);
        for(unsigned at=0;at<lengths[yue];) {
            size_t got=agent_progress_render(&s,pieces+at,1+at%237);
            assert(got && got<=lengths[yue]-at);at+=(unsigned)got;
        }
        assert(!memcmp(whole,pieces,lengths[yue]*sizeof(*whole)));
        assert(!agent_progress_render(&s,pieces,1));
    }
}
static void invalid(void)
{
    int16_t pcm[8];agent_progress_t state,before;
    agent_progress_init(&state,false);before=state;
    agent_progress_init(NULL,false);
    assert(agent_progress_render(NULL,pcm,8)==0);
    assert(agent_progress_render(&state,NULL,8)==0);
    assert(agent_progress_render(&state,pcm,0)==0);
    assert(!memcmp(&state,&before,sizeof(state)));
    static const uint8_t bad_index[]={0,0,89,0};
    state=(agent_progress_t){.data=bad_index,.bytes=4,.samples=9,.block_bytes=8};
    assert(agent_progress_render(&state,pcm,8)==0);
    state.bytes=3;
    assert(agent_progress_render(&state,pcm,8)==0);
    static const uint8_t valid_header[]={0,0,0,0};
    state=(agent_progress_t){.data=valid_header,.bytes=4,.samples=9,.block_bytes=8};
    assert(agent_progress_render(&state,pcm,8)==1);
    assert(agent_progress_render(&state,pcm,8)==0);
    state.block_bytes=0;
    assert(agent_progress_render(&state,pcm,8)==0);
}

static void completion_assets(void)
{
    static int16_t whole[28000],pieces[28000];
    static const unsigned lengths[]={18038,15424};
    /* Independent FFmpeg decode, completion16-assets-01185-02/manifest.json. */
    static const uint32_t golden[]={UINT32_C(0xf885862c),UINT32_C(0xbca453ee)};
    assert(AGENT_PROGRESS_LIGHT_RATE==16000);
    for(unsigned yue=0;yue<2;++yue) {
        agent_progress_t s;agent_progress_light_init(&s,yue);
        assert(agent_progress_light_text(yue)[0] && s.samples==lengths[yue]);
        assert(agent_progress_render(&s,whole,28000)==lengths[yue]);
        assert(pcm_crc(whole,lengths[yue])==golden[yue]);
        agent_progress_light_init(&s,yue);
        for(unsigned at=0;at<lengths[yue];) {
            size_t n=agent_progress_render(&s,pieces+at,1+at%239);
            assert(n && n<=lengths[yue]-at);at+=(unsigned)n;
        }
        assert(!memcmp(whole,pieces,lengths[yue]*sizeof(*whole)));
        assert(!agent_progress_render(&s,pieces,1));
    }
}

static void memory_assets(void)
{
    static int16_t pcm[14000];
    const unsigned lengths[]={13613,13079};
    const uint32_t golden[]={UINT32_C(0x4bc96dd5),UINT32_C(0x8760558c)};
    for(unsigned yue=0;yue<2;++yue) {
        agent_progress_t s;agent_progress_memory_init(&s,yue);
        assert(s.samples==lengths[yue] && agent_progress_memory_samples(yue)==s.samples);
        assert(agent_progress_memory_text(yue)[0]);
        assert(agent_progress_render(&s,pcm,14000)==lengths[yue]);
        assert(pcm_crc(pcm,lengths[yue])==golden[yue]);
        assert(!agent_progress_render(&s,pcm,1));
    }
}

int main(int argc,char **argv)
{
    _Static_assert(sizeof(agent_progress_t)<=48,"Small fixed progress decoder state");
    assert(argc==1 || argc==3);
    known_vector();invalid();search_assets();completion_assets();memory_assets();
    asset(false,argc==3?argv[1]:NULL);
    asset(true,argc==3?argv[2]:NULL);
    return 0;
}
