#include "ws_connect_clock.h"
#include "runtime.h"
#include <stdatomic.h>
#if AGENT_TLS_PHASE_TRACE
#include "tls_phase_clock.h"
#endif

static _Atomic(esp_transport_handle_t) watched;
/* Only the registered handle's sole task writes/reads these two fields.
 * Other connection tasks compare the atomic handle and never touch them. */
static unsigned connect_began,connect_ended;

void esp_agent_ws_connect_watch(esp_transport_handle_t tls)
{
    if(tls)connect_began=connect_ended=0;
    atomic_store_explicit(&watched,tls,memory_order_release);
}
void esp_agent_ws_connect_clock(unsigned *began,unsigned *ended)
{ *began=connect_began;*ended=connect_ended; }

int __real_esp_transport_connect(esp_transport_handle_t,const char *,int,int);
int __wrap_esp_transport_connect(esp_transport_handle_t t,const char *host,int port,int timeout)
{
    /* The SDK's unchanged WS.connect calls its TLS parent through this
     * linker wrapper. No extra handle, allocation, I/O, timeout or retry. */
    if(!t || t!=atomic_load_explicit(&watched,memory_order_acquire))
        return __real_esp_transport_connect(t,host,port,timeout);
    connect_began=(unsigned)esp_agent_now();
#if AGENT_TLS_PHASE_TRACE
    esp_agent_tls_phase_watch(true);
#endif
    int result=__real_esp_transport_connect(t,host,port,timeout);
#if AGENT_TLS_PHASE_TRACE
    esp_agent_tls_phase_watch(false);
#endif
    connect_ended=(unsigned)esp_agent_now();
    return result;
}
