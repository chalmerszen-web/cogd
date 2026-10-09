#include "ws_connect_clock.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct esp_transport_item_t { unsigned id; } candidate={1},asr={2};
static unsigned clock_ms=100,calls;
static int answer;
uint64_t esp_agent_now(void) { return clock_ms++; }
int __wrap_esp_transport_connect(esp_transport_handle_t,const char *,int,int);
int esp_transport_connect(esp_transport_handle_t t,const char *host,int port,int timeout)
{
    assert((t==&candidate || t==&asr || !t) && !strcmp(host,"fixture"));
    assert(port==443 && timeout==4000);++calls;clock_ms+=10;return answer;
}
static int connect_to(esp_transport_handle_t t)
{ return __wrap_esp_transport_connect(t,"fixture",443,4000); }
static void expect(unsigned begin,unsigned end)
{
    unsigned b,e;esp_agent_ws_connect_clock(&b,&e);assert(b==begin && e==end);
}
int main(void)
{
    esp_agent_ws_connect_watch(&candidate);expect(0,0);
    assert(!connect_to(&asr));expect(0,0);assert(clock_ms==110);
    assert(!connect_to(&candidate));expect(110,121);assert(calls==2);
    esp_agent_ws_connect_watch(NULL);
    assert(!connect_to(&candidate));expect(110,121);assert(calls==3);
    /* A fresh attempt clears history; failures retain the original return. */
    esp_agent_ws_connect_watch(&candidate);expect(0,0);answer=-1;
    assert(connect_to(&candidate)==-1);expect(132,143);assert(calls==4);
    esp_agent_ws_connect_watch(NULL);
    assert(connect_to(NULL)==-1);expect(132,143);assert(calls==5);
    puts("Connect clock: one unchanged delegation, owner isolation, cleanup, failure and fresh attempt OK");
    return 0;
}
