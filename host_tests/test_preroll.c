#include "preroll.h"
#include "crc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t storage[AGENT_PREROLL_BYTES+2];
static int16_t pcm[512],output[512];
static void frame(int16_t value) { for(unsigned i=0;i<512;++i)pcm[i]=value; }
static void init(agent_preroll_t *s)
{
    memset(s,0,sizeof(*s));memset(storage,0xa5,sizeof(storage));
    assert(agent_preroll_init(s,storage+1,AGENT_PREROLL_BYTES)==AGENT_OK);
}
static void sentinels(void) { assert(storage[0]==0xa5 && storage[sizeof(storage)-1]==0xa5); }

static void chronological(void)
{
    agent_preroll_t s;init(&s);
    assert(agent_preroll_seal(&s)==AGENT_ERR_NOT_FOUND);
    for(unsigned i=1;i<=80;++i) {
        frame((int16_t)i);assert(!agent_preroll_feed(&s,pcm,(uint64_t)i*512));
    }
    assert(s.count==64 && s.next==16);
    assert(!agent_preroll_seal(&s));
    assert(agent_preroll_feed(&s,pcm,81u*512)==AGENT_ERR_BUSY);
    for(unsigned i=17;i<=80;++i) {
        uint64_t end=0;assert(!agent_preroll_read(&s,output,&end));
        assert(end==(uint64_t)i*512);
        for(unsigned j=0;j<512;++j)assert(output[j]==(int16_t)i);
    }
    uint64_t end=123;memset(output,0x33,sizeof(output));
    assert(agent_preroll_read(&s,output,&end)==AGENT_ERR_NOT_FOUND && end==123 && output[0]==0x3333);
    sentinels();
}
static void vector_and_gap(void)
{
    agent_preroll_t s;init(&s);frame(1008);
    pcm[0]=1000;pcm[1]=1001;pcm[2]=1002;pcm[3]=1004;
    assert(!agent_preroll_feed(&s,pcm,512));
    const uint8_t *p=storage+1;
    assert(p[0]==0xe8 && p[1]==3 && p[2]==0 && p[3]==1 && p[4]==0x10 && p[5]==0x21);
    uint64_t end;assert(agent_preroll_read(&s,output,&end)==AGENT_ERR_BUSY);
    agent_preroll_t before=s;
    assert(agent_preroll_feed(&s,pcm,512)==AGENT_ERR_PROTOCOL && !memcmp(&s,&before,sizeof(s)));
    assert(agent_preroll_feed(&s,pcm,513)==AGENT_ERR_ARGUMENT && !memcmp(&s,&before,sizeof(s)));
    assert(!agent_preroll_seal(&s));assert(!agent_preroll_read(&s,output,&end));
    const int16_t expected[]={1000,1000,1002,1004,1008};
    assert(!memcmp(output,expected,sizeof(expected)) && end==512);
    agent_preroll_reset(&s);assert(!agent_preroll_init(&s,storage+1,AGENT_PREROLL_BYTES));
    frame(-100);assert(!agent_preroll_feed(&s,pcm,512));
    frame(200);assert(!agent_preroll_feed(&s,pcm,2048));
    assert(s.count==1 && s.discontinuities==1 && !agent_preroll_seal(&s));
    assert(!agent_preroll_read(&s,output,&end) && end==2048 && output[0]==200);
    agent_preroll_reset(&s);assert(!agent_preroll_init(&s,storage+1,AGENT_PREROLL_BYTES));
    assert(!agent_preroll_feed(&s,pcm,UINT64_MAX-511));assert(!agent_preroll_seal(&s));
    assert(!agent_preroll_read(&s,output,&end) && end==UINT64_MAX-511);
    sentinels();
}
static void corruption(void)
{
    static const unsigned offsets[]={0,2,3,4,259,260,263};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        agent_preroll_t s;init(&s);frame(0);assert(!agent_preroll_feed(&s,pcm,512));
        storage[1+offsets[i]]^=0x80;agent_preroll_t before=s;
        assert(agent_preroll_seal(&s)==AGENT_ERR_CORRUPT && !memcmp(&s,&before,sizeof(s)));
    }
    agent_preroll_t s;init(&s);frame(0);assert(!agent_preroll_feed(&s,pcm,512));
    assert(!agent_preroll_seal(&s));storage[5]^=1;
    agent_preroll_t before=s;uint64_t end=123;memset(output,0x33,sizeof(output));
    assert(agent_preroll_read(&s,output,&end)==AGENT_ERR_CORRUPT);
    assert(!memcmp(&s,&before,sizeof(s)) && end==123 && output[0]==0x3333);
    /* A different arena owner cleared the packet: never silently send zeros. */
    memset(storage+1,0,AGENT_PREROLL_BYTES);
    assert(agent_preroll_read(&s,output,&end)==AGENT_ERR_CORRUPT && end==123);
    sentinels();
}
static void guards_and_tail_state(void)
{
    agent_preroll_t s={0};
    assert(agent_preroll_init(NULL,storage,AGENT_PREROLL_BYTES)==AGENT_ERR_ARGUMENT);
    assert(agent_preroll_init(&s,storage,AGENT_PREROLL_BYTES-1)==AGENT_ERR_ARGUMENT);
    assert(agent_preroll_init(&s,&s,AGENT_PREROLL_BYTES)==AGENT_ERR_ARGUMENT);
    init(&s);frame(0);agent_preroll_t before=s;uint64_t end;
    assert(agent_preroll_init(&s,storage,AGENT_PREROLL_BYTES)==AGENT_ERR_BUSY);
    assert(agent_preroll_feed(&s,NULL,512)==AGENT_ERR_ARGUMENT);
    union { agent_preroll_t state; int16_t input[512]; } alias={0};
    assert(!agent_preroll_init(&alias.state,storage+1,AGENT_PREROLL_BYTES));
    assert(agent_preroll_feed(&alias.state,alias.input,512)==AGENT_ERR_ARGUMENT);
    assert(agent_preroll_feed(&s,(const int16_t *)(storage+2),512)==AGENT_ERR_ARGUMENT);
    assert(!memcmp(&s,&before,sizeof(s)));
    assert(!agent_preroll_feed(&s,pcm,512));assert(!agent_preroll_seal(&s));before=s;
    assert(agent_preroll_read(&s,output,(uint64_t *)output)==AGENT_ERR_ARGUMENT);
    assert(agent_preroll_read(&s,output,&s.last_sample)==AGENT_ERR_ARGUMENT);
    assert(agent_preroll_read(&s,(int16_t *)(storage+2),&end)==AGENT_ERR_ARGUMENT);
    assert(!memcmp(&s,&before,sizeof(s)));
    union { max_align_t align; uint8_t bytes[17024]; } arena;
    agent_preroll_t *tail=(void *)(arena.bytes+AGENT_PREROLL_BYTES);
    memset(tail,0,sizeof(*tail));
    assert(!agent_preroll_init(tail,arena.bytes,sizeof(arena.bytes)));
    assert(!agent_preroll_feed(tail,pcm,512) && !agent_preroll_seal(tail));
    assert(!agent_preroll_read(tail,output,&end) && end==512);
    for(unsigned i=0;i<512;++i)assert(output[i]==0);
    agent_preroll_reset(tail);assert(!tail->data);
    /* Repeated extrema stay within the fixed integer codec and packet bounds. */
    init(&s);for(unsigned i=0;i<512;++i)pcm[i]=i&1?INT16_MIN:INT16_MAX;
    assert(!agent_preroll_feed(&s,pcm,512));assert(!agent_preroll_seal(&s));
    assert(!agent_preroll_read(&s,output,&end) && output[0]==INT16_MAX);
    sentinels();
}
int main(void)
{
    chronological();vector_and_gap();corruption();guards_and_tail_state();
    printf("preroll: four ownership/packet suites passed; storage=%u state=%zu\n",(unsigned)AGENT_PREROLL_BYTES,sizeof(agent_preroll_t));
    return 0;
}
