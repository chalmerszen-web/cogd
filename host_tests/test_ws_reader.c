#include "ws_reader.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *wire;size_t length,at,chunk;
    unsigned calls,stall_every;bool eof,overclaim;
} input_t;
static int read_bytes(void *ctx,void *out,size_t count,unsigned timeout)
{
    input_t *s=ctx;assert(count && timeout<=50);++s->calls;
    if(s->overclaim)return (int)count+1;
    if(s->stall_every && s->calls%s->stall_every==0)return 0;
    if(s->at==s->length)return s->eof?-1:0;
    if(count>s->length-s->at)count=s->length-s->at;
    if(count>s->chunk)count=s->chunk;
    memcpy(out,s->wire+s->at,count);s->at+=count;return (int)count;
}
static size_t pack(uint8_t *out,unsigned opcode,bool fin,const uint8_t *data,size_t n)
{
    out[0]=(uint8_t)(opcode|(fin?128:0));size_t h=n<=125?2:n<=65535?4:10;
    out[1]=(uint8_t)(h==2?n:h==4?126:127);
    if(h>2)for(size_t i=0;i<h-2;++i)out[h-1-i]=(uint8_t)((uint64_t)n>>(8*i));
    if(n)memcpy(out+h,data,n);return h+n;
}
static void roundtrip(size_t length,size_t chunk,unsigned stall,unsigned opcode)
{
    uint8_t *data=malloc(length+1),*wire=malloc(length+10);assert(data && wire);
    for(size_t i=0;i<length;++i)data[i]=(uint8_t)(i*37+11);
    input_t s={.wire=wire,.length=pack(wire,opcode,true,data,length),.chunk=chunk,.stall_every=stall};
    agent_ws_reader_t r={0};size_t used=0;unsigned firsts=0,finals=0,loops=0;
    while(!finals) {
        char out[259];memset(out,0x5a,sizeof(out));agent_ws_chunk_t part;unsigned before=s.calls;
        assert(!agent_ws_reader_next(&r,read_bytes,&s,out+1,sizeof(out)-2,&part,50));
        assert(s.calls-before<=3 && out[0]==0x5a && out[258]==0x5a);
        if(part.first || part.final || part.length) {
            assert(part.opcode==opcode && part.first==(used==0));
            firsts+=part.first;finals+=part.final;assert(used+part.length<=length);
            assert(!memcmp(out+1,data+used,part.length));used+=part.length;
        }
        assert(++loops<1000000);
    }
    assert(used==length && s.at==s.length && firsts==1 && finals==1);
    assert(!r.payload && !r.fragmented);
    agent_ws_chunk_t empty;char out[1];
    assert(!agent_ws_reader_next(&r,read_bytes,&s,out,sizeof(out),&empty,0));
    assert(!empty.length && !empty.first && !empty.final);
    free(data);free(wire);
}
static void fragments(void)
{
    uint8_t wire[300],body[125];memset(body,0x6b,sizeof(body));size_t n=0;
    n+=pack(wire+n,1,false,NULL,0);
    n+=pack(wire+n,0,false,(const uint8_t *)"abc",3);
    n+=pack(wire+n,9,true,body,125);
    n+=pack(wire+n,10,true,NULL,0);
    n+=pack(wire+n,0,true,(const uint8_t *)"def",3);
    n+=pack(wire+n,8,true,(const uint8_t *)"\x03\xe8",2);
    input_t s={.wire=wire,.length=n,.chunk=1,.stall_every=3};agent_ws_reader_t r={0};
    unsigned starts=0,finishes=0,pings=0,pongs=0,closes=0;char joined[7]={0};size_t used=0;
    for(unsigned i=0;!closes;++i) {
        char out[130];agent_ws_chunk_t part;assert(i<1500);
        assert(!agent_ws_reader_next(&r,read_bytes,&s,out,sizeof(out),&part,0));
        if(!(part.first || part.final || part.length))continue;
        if(part.opcode>=8) {
            assert(part.first && part.final);
            if(part.opcode==9) {++pings;assert(part.length==125 && !memcmp(out,body,125));}
            else if(part.opcode==10) {++pongs;assert(!part.length);}
            else {++closes;assert(part.length==2);}
        } else {
            starts+=part.first;finishes+=part.final;
            assert(used+part.length<=6);memcpy(joined+used,out,part.length);used+=part.length;
        }
    }
    assert(starts==3 && finishes==1 && pings==1 && pongs==1 && closes==1 && !strcmp(joined,"abcdef"));
}
static void invalid(const uint8_t *wire,size_t n,agent_err_t expected)
{
    input_t s={.wire=wire,.length=n,.chunk=1,.eof=true};agent_ws_reader_t r={0};
    char out[256];agent_ws_chunk_t part;agent_err_t error=AGENT_OK;
    for(unsigned i=0;!error;++i) {assert(i<30);error=agent_ws_reader_next(&r,read_bytes,&s,out,sizeof(out),&part,0);}
    assert(error==expected);unsigned calls=s.calls;
    assert(agent_ws_reader_next(&r,read_bytes,&s,out,sizeof(out),&part,0)==expected && calls==s.calls);
}
int main(void)
{
    const size_t sizes[]={0,1,125,126,257,2048,65535,65536,90000};
    for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);++i) {
        roundtrip(sizes[i],1,2,1);roundtrip(sizes[i],7,3,2);roundtrip(sizes[i],8192,0,1);
    }
    fragments();
    const uint8_t bad[][10]={
        {0xc1,0},{0x81,0x80},{0x83,0},{0x8b,0},{9,0},{0x89,126,0,126},
        {0x88,1},{0x81,126,0,125},{0x81,127,0,0,0,0,0,0,0,126},
        {0x81,127,0x80,0,0,0,0,0,0,0},{0x81,127,0,0,0,0,0,0x41,0xeb,1},
        {0x80,0},{1,0,0x81,0}
    };
    const size_t lens[]={2,2,2,2,2,4,2,4,10,10,10,2,4};
    for(size_t i=0;i<sizeof(lens)/sizeof(*lens);++i)invalid(bad[i],lens[i],i==9 || i==10?AGENT_ERR_LIMIT:AGENT_ERR_PROTOCOL);
    const uint8_t partial[]={0x81,126,0};invalid(partial,sizeof(partial),AGENT_ERR_NETWORK);
    input_t broken={.overclaim=true};agent_ws_reader_t r={0};agent_ws_chunk_t p;char out[2];
    assert(agent_ws_reader_next(&r,read_bytes,&broken,out,sizeof(out),&p,0)==AGENT_ERR_PROTOCOL);
    puts("ws_reader: fragmented headers/payloads, would-block, interleaved control and malformed frames passed");
    return 0;
}
