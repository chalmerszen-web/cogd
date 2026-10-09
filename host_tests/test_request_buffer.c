#include "transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char input[24577],output[sizeof(input)],scratch[4096];
static size_t used,chunk,maximum;
static unsigned calls,fail_at,cancel_at;
static atomic_bool cancelled;

static agent_err_t sink(void *ctx,const char *data,size_t n)
{
    (void)ctx;assert(n && n<=4096 && used+n<=sizeof(output));++calls;
    if(calls==fail_at)return AGENT_ERR_NETWORK;
    memcpy(output+used,data,n);used+=n;if(n>maximum)maximum=n;
    if(calls==cancel_at)atomic_store(&cancelled,true);
    return AGENT_OK;
}
static agent_err_t produce(void *ctx,agent_write_fn write,void *target)
{
    (void)ctx;
    for(size_t at=0;at<sizeof(input);) {
        size_t n=chunk?chunk:1+(at*17)%7001;if(n>sizeof(input)-at)n=sizeof(input)-at;
        agent_err_t error=write(target,input+at,n);if(error)return error;at+=n;
    }
    return AGENT_OK;
}
static void reset(void)
{ used=maximum=0;calls=fail_at=cancel_at=0;atomic_store(&cancelled,false);memset(output,0,sizeof(output)); }
int main(void)
{
    for(size_t i=0;i<sizeof(input);++i)input[i]=(char)(i*13);
    agent_http_request_t r={.produce=produce,.length=sizeof(input),.cancelled=&cancelled};
    const size_t sizes[]={1,7,1024,4096},fragments[]={0,1,3,1023,1024,4096,9000};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);++i)
        for(unsigned j=0;j<sizeof(fragments)/sizeof(*fragments);++j) {
            reset();chunk=fragments[j];
            assert(!agent_http_write_buffered(&r,sink,NULL,scratch,sizes[i]));
            assert(used==sizeof(input) && !memcmp(input,output,used));
        }
    reset();chunk=4096;assert(!agent_http_write_buffered(&r,sink,NULL,scratch,1024));
    assert(calls==7 && maximum==4096); /* Same bytes; previously 25 writes. */
    reset();chunk=0;fail_at=3;
    assert(agent_http_write_buffered(&r,sink,NULL,scratch,1024)==AGENT_ERR_NETWORK);
    assert(calls==fail_at && used && !memcmp(input,output,used));
    reset();chunk=4096;cancel_at=2;
    assert(agent_http_write_buffered(&r,sink,NULL,scratch,1024)==AGENT_ERR_CANCELLED);
    assert(calls==2 && used==8192 && !memcmp(input,output,used));
    reset();atomic_store(&cancelled,true);
    assert(agent_http_write_buffered(&r,sink,NULL,scratch,1024)==AGENT_ERR_CANCELLED && !calls);
    reset();r.length--;
    assert(agent_http_write_buffered(&r,sink,NULL,scratch,1024)==AGENT_ERR_LIMIT);
    reset();r.length+=2;
    assert(agent_http_write_buffered(&r,sink,NULL,scratch,1024)==AGENT_ERR_PROTOCOL);
    reset();r.length=sizeof(input);r.produce=NULL;r.body=input;
    assert(!agent_http_write_buffered(&r,sink,NULL,scratch,1024));
    assert(used==sizeof(input) && !memcmp(input,output,used));
    puts("Buffered HTTP: byte-exact mixed fragments, 4 KiB direct slices, cancellation, send failure and Content-Length PASS");
}
