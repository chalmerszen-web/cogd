#include "phase.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
static unsigned ticks;
static atomic_bool done;
unsigned phase_clock(void) {return ticks;}
unsigned phase_hz(void) {return 160000000u;}
static void *writer(void *unused)
{
    (void)unused;
    for(unsigned g=1;g<=10000;++g) {
        phase_start();
        for(unsigned i=0;i<PH_COUNT;++i) {
            if(i==PH_DECIMATE || i==PH_METER || i==PH_SOURCE)continue;
            phase_stamp_t stamp=phase_enter();ticks+=g+i;phase_leave(i,stamp);
        }
        phase_finish();if(g%31==0)sched_yield();
    }
    atomic_store(&done,true);return NULL;
}
int main(void)
{
    pthread_t worker;assert(!pthread_create(&worker,NULL,writer,NULL));
    unsigned verified=0,busy=0;
    do {
        char out[1024];agent_err_t error=phase_status(out,sizeof(out));
        if(error==AGENT_ERR_BUSY) {++busy;continue;}
        assert(error==AGENT_OK);
        unsigned epoch;assert(sscanf(out,"{\"profile\":\"capture-coarse-v1\",\"epoch\":%u",&epoch)==1);
        assert(!(epoch&1u));const char *p=strstr(out,"\"rows\":[");assert(p);p+=8;
        for(unsigned i=0;i<PH_COUNT;++i) {
            unsigned calls,cycles,maximum;int n=0;
            assert(sscanf(p,"[%u,%u,%u]%n",&calls,&cycles,&maximum,&n)==3 && n>0);
            bool observed=epoch && i!=PH_DECIMATE && i!=PH_METER && i!=PH_SOURCE;
            unsigned expected=observed?epoch/2+i:0;
            assert(calls==(observed?1u:0u) && cycles==expected && maximum==expected);
            p+=n;if(i+1<PH_COUNT)assert(*p++==',');
        }
        assert(!strcmp(p,"]}"));++verified;
    } while(!atomic_load(&done));
    assert(!pthread_join(worker,NULL));assert(verified>0);
    printf("{\"passed\":true,\"publications\":10000,\"coherent_snapshots\":%u,\"busy_snapshots\":%u}\n",verified,busy);
    return 0;
}
