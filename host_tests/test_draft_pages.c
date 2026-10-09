#include "draft.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BYTES=24*AGENT_DRAFT_PAGE_BYTES, SAMPLES=1+2*BYTES };
static uint8_t linear[BYTES],paged[BYTES],*pages[24];
static int16_t source[SAMPLES],a[SAMPLES],b[SAMPLES];
static unsigned seed=617;
static unsigned random32(void) { seed=seed*1664525u+1013904223u;return seed; }
int main(void)
{
    for(unsigned i=0;i<SAMPLES;++i)source[i]=(int16_t)(random32()>>16);
    for(unsigned i=0;i<24;++i)pages[i]=paged+i*AGENT_DRAFT_PAGE_BYTES;
    agent_speech_draft_t one,many;
    /* Same codec, same full capacity, different/random packet boundaries. */
    for(unsigned trial=0;trial<12;++trial) {
        agent_speech_draft_init(&one,linear,sizeof(linear));
        agent_speech_draft_init_pages(&many,pages,sizeof(paged));
        assert(!agent_speech_draft_write(&one,source,SAMPLES));
        for(size_t at=0;at<SAMPLES;) {
            size_t n=1+random32()%3001;if(n>SAMPLES-at)n=SAMPLES-at;
            assert(!agent_speech_draft_write(&many,source+at,n));at+=n;
        }
        assert(!memcmp(linear,paged,sizeof(linear)) && one.first==many.first);
        size_t got;assert(!agent_speech_draft_read(&one,a,SAMPLES,&got) && got==SAMPLES);
        for(size_t at=0;at<SAMPLES;) {
            size_t n=1+random32()%997;
            assert(!agent_speech_draft_read(&many,b+at,n,&got) && got);at+=got;
        }
        assert(!memcmp(a,b,sizeof(a)));
    }
    agent_speech_draft_reset(&many);assert(many.pages==pages && many.capacity==BYTES);
    assert(!agent_speech_draft_write(&many,source,2049));
    pages[1]=NULL;size_t before=many.samples;
    int encoder=many.encoder,index=many.index;
    assert(agent_speech_draft_write(&many,source+before,2)==AGENT_ERR_MEMORY);
    assert(many.samples==before && many.encoder==encoder && many.index==index);
    pages[1]=paged+AGENT_DRAFT_PAGE_BYTES;
    assert(!agent_speech_draft_write(&many,source+before,2));
    pages[1]=NULL;size_t got=77;
    assert(agent_speech_draft_read(&many,b,2051,&got)==AGENT_ERR_MEMORY);
    assert(!got && !many.read);
    pages[1]=paged+AGENT_DRAFT_PAGE_BYTES;
    assert(!agent_speech_draft_read(&many,b,2051,&got) && got==2051);
    agent_speech_draft_reset(&many);
    assert(!agent_speech_draft_write(&many,source,SAMPLES));
    assert(agent_speech_draft_write(&many,source,1)==AGENT_ERR_FULL);
    assert(many.samples==SAMPLES && many.full);
    assert(agent_speech_draft_read(&many,b,1,&got)==AGENT_ERR_ARGUMENT);
    agent_speech_draft_init_pages(&many,pages,1);
    assert(!agent_speech_draft_write(&many,source,3));
    assert(agent_speech_draft_write(&many,source,1)==AGENT_ERR_FULL);
    puts("Paged draft: byte-identical codec, arbitrary boundaries, unchanged capacity, atomic missing-page failure OK");
}
