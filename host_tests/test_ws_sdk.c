/* Compile the actual pinned SDK source, not a rewritten model of its reader.
 * No network, credentials, TLS implementation or hardware is exercised here. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include "ws_reader.h"
#include "ws_sdk_hash.h"
#include AGENT_IDF_WS_SOURCE

typedef struct {
    const uint8_t *wire;
    size_t length,at,chunk,stall_at;
    unsigned stalls,reads,polls,writes;
    bool eof;
} source_t;

void test_sdk_log(const char *tag,const char *format,...)
{ (void)tag;(void)format; }
void *esp_transport_get_context_data(esp_transport_handle_t t) { return t->data; }
int esp_transport_read(esp_transport_handle_t t,char *out,int capacity,int timeout)
{
    source_t *s=t->data;(void)timeout;assert(capacity>0);++s->reads;
    if(s->at==s->stall_at && !s->stalls) {++s->stalls;return 0;}
    if(s->at==s->length)return s->eof?-1:0;
    size_t n=(size_t)capacity;
    if(n>s->chunk)n=s->chunk;
    if(n>s->length-s->at)n=s->length-s->at;
    if(!s->stalls && s->at<s->stall_at && n>s->stall_at-s->at)n=s->stall_at-s->at;
    memcpy(out,s->wire+s->at,n);s->at+=n;return (int)n;
}
int esp_transport_poll_read(esp_transport_handle_t t,int timeout)
{ source_t *s=t->data;(void)timeout;++s->polls;return s->at<s->length || s->eof; }
int esp_transport_poll_write(esp_transport_handle_t t,int timeout)
{ (void)t;(void)timeout;return 1; }
int esp_transport_write(esp_transport_handle_t t,const char *out,int length,int timeout)
{ source_t *s=t->data;(void)out;(void)timeout;++s->writes;return length; }
int esp_transport_poll_connection_closed(esp_transport_handle_t t,int timeout)
{ (void)t;(void)timeout;return 1; }
static int source_read(void *ctx,void *out,size_t capacity,unsigned timeout)
{ return esp_transport_read(ctx,out,(int)capacity,(int)timeout); }

static size_t pack(uint8_t *wire,unsigned opcode,const uint8_t *payload,size_t n)
{
    wire[0]=(uint8_t)(128u|opcode);size_t header=n<=125?2:n<=65535?4:10;
    wire[1]=(uint8_t)(header==2?n:header==4?126:127);
    for(size_t i=0;i<header-2;++i)wire[header-1-i]=(uint8_t)((uint64_t)n>>(8*i));
    memcpy(wire+header,payload,n);return header+n;
}

static void incremental(source_t *source,const uint8_t *payload,size_t length,unsigned opcode)
{
    struct esp_transport_item_t parent={.data=source};agent_ws_reader_t reader={0};
    size_t used=0;unsigned firsts=0,finals=0;
    for(unsigned i=0;!finals;++i) {
        assert(i<100000);char bytes[259];memset(bytes,0x5a,sizeof(bytes));agent_ws_chunk_t part;
        assert(!agent_ws_reader_next(&reader,source_read,&parent,bytes+1,257,&part,100));
        assert(bytes[0]==0x5a && bytes[258]==0x5a);
        if(part.first || part.final || part.length) {
            assert(part.opcode==opcode && used+part.length<=length);
            assert(!memcmp(bytes+1,payload+used,part.length));used+=part.length;
            firsts+=part.first;finals+=part.final;
        }
    }
    assert(used==length && firsts==1 && finals==1 && source->at==source->length);
}

static void header_timeout(size_t length,size_t boundary)
{
    uint8_t *payload=malloc(length),*wire=malloc(length+10);assert(payload && wire);
    for(size_t i=0;i<length;++i)payload[i]=(uint8_t)(i*37u+11u);
    size_t size=pack(wire,1,payload,length);
    source_t source={.wire=wire,.length=size,.chunk=257,.stall_at=boundary};
    struct esp_transport_item_t parent={.data=&source};
    transport_ws_t state={.parent=&parent};
    struct esp_transport_item_t ws={.data=&state};char bytes[257];
    /* A timeout in a partly consumed header is fatal in the actual SDK. */
    assert(ws_read(&ws,bytes,sizeof(bytes),100)==-1);
    assert(source.at==boundary && source.stalls==1 && state.frame_state.bytes_remaining==0);
    source.at=source.stalls=source.reads=source.polls=0;
    incremental(&source,payload,length,1);
    free(payload);free(wire);
}

static void payload_timeout(void)
{
    uint8_t payload[126],wire[130];memset(payload,0x66,sizeof(payload));
    source_t source={.wire=wire,.length=pack(wire,1,payload,sizeof(payload)),.chunk=257,.stall_at=11};
    struct esp_transport_item_t parent={.data=&source};transport_ws_t state={.parent=&parent};
    struct esp_transport_item_t ws={.data=&state};char bytes[257];
    assert(ws_read(&ws,bytes,sizeof(bytes),100)==7);
    assert(state.frame_state.bytes_remaining==119);
    assert(ws_read(&ws,bytes,sizeof(bytes),100)==0);
    assert(state.frame_state.bytes_remaining==119);
    assert(ws_read(&ws,bytes,sizeof(bytes),100)==119);
    source.at=source.stalls=source.reads=source.polls=0;
    incremental(&source,payload,sizeof(payload),1);
}

static void short_ping(void)
{
    uint8_t payload[125],wire[127];memset(payload,0x6b,sizeof(payload));
    source_t source={.wire=wire,.length=pack(wire,9,payload,sizeof(payload)),.chunk=7,.stall_at=SIZE_MAX};
    struct esp_transport_item_t parent={.data=&source};transport_ws_t state={.parent=&parent};
    struct esp_transport_item_t ws={.data=&state};char bytes[257];
    /* Automatic control-frame handling expects one read to return its entire
     * payload. A legal seven-byte fragment produces -1 without TLS failure. */
    assert(ws_read(&ws,bytes,sizeof(bytes),100)==-1);
    assert(source.at==9 && source.stalls==0 && source.writes==0);
    source.at=source.reads=source.polls=0;
    incremental(&source,payload,sizeof(payload),9);
}

static void retained_upgrade_bytes(void)
{
    uint8_t payload[126],wire[130];memset(payload,0x62,sizeof(payload));
    size_t size=pack(wire,1,payload,sizeof(payload));
    for(size_t prefix=1;prefix<size;++prefix) {
        source_t source={.wire=wire,.length=size,.at=prefix,.chunk=257,.stall_at=SIZE_MAX};
        struct esp_transport_item_t parent={.data=&source};
        transport_ws_t state={.parent=&parent,.buffer=malloc(prefix),.buffer_len=prefix};
        assert(state.buffer);memcpy(state.buffer,wire,prefix);
        struct esp_transport_item_t ws={.data=&state};char bytes[257];size_t used=0;
        while(used<sizeof(payload)) {
            int n=ws_read(&ws,bytes,sizeof(bytes),100);
            assert(n>0 && used+(size_t)n<=sizeof(payload));
            assert(!memcmp(bytes,payload+used,(size_t)n));used+=(size_t)n;
        }
        assert(!state.buffer_len && !state.frame_state.bytes_remaining && source.at==size);
        free(state.buffer);
    }
}

int main(void)
{
    header_timeout(125,1);
    for(size_t i=1;i<4;++i)header_timeout(126,i);
    for(size_t i=1;i<10;++i)header_timeout(65536,i);
    payload_timeout();short_ping();retained_upgrade_bytes();
    printf("{\"sdk_sha256\":\"%s\",\"header_timeout_cases\":13,\"short_ping_cases\":1,"
           "\"incremental_recovered\":14,\"payload_timeout_preserved\":true,"
           "\"upgrade_prefix_cases\":129,\"device_cause_proven\":false}\n",AGENT_IDF_WS_SHA256);
    return 0;
}
