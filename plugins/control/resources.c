#include "resources.h"
#include <string.h>

void agent_resources_init(agent_resources_t *r)
{
    memset(r->owners,0,sizeof(r->owners));
    atomic_flag_clear(&r->guard); atomic_init(&r->occupied,0);
}
agent_err_t agent_resources_claim(agent_resources_t *r,unsigned owner,uint32_t mask,uint32_t *added)
{
    if(!r || !owner || (mask >> AGENT_RESOURCE_COUNT) || !added) return AGENT_ERR_ARGUMENT;
    if(atomic_flag_test_and_set(&r->guard)) return AGENT_ERR_BUSY;
    agent_err_t error=AGENT_OK; *added=0;
    for(unsigned i=0;i<AGENT_RESOURCE_COUNT;++i)
        if((mask&AGENT_PIN(i)) && r->owners[i] && r->owners[i]!=owner) { error=AGENT_ERR_BUSY; break; }
    if(!error) {
        for(unsigned i=0;i<AGENT_RESOURCE_COUNT;++i) if(mask&AGENT_PIN(i)) {
            if(!r->owners[i]) *added|=AGENT_PIN(i);
            r->owners[i]=owner;
        }
        atomic_fetch_or(&r->occupied,mask);
    }
    atomic_flag_clear(&r->guard); return error;
}
agent_err_t agent_resources_release(agent_resources_t *r,unsigned owner,uint32_t mask)
{
    if(!r || !owner || (mask >> AGENT_RESOURCE_COUNT)) return AGENT_ERR_ARGUMENT;
    if(atomic_flag_test_and_set(&r->guard)) return AGENT_ERR_BUSY;
    agent_err_t error=AGENT_OK;
    for(unsigned i=0;i<AGENT_RESOURCE_COUNT;++i)
        if((mask&AGENT_PIN(i)) && r->owners[i]!=owner) { error=AGENT_ERR_FORBIDDEN; break; }
    if(!error) {
        for(unsigned i=0;i<AGENT_RESOURCE_COUNT;++i) if(mask&AGENT_PIN(i)) r->owners[i]=0;
        atomic_fetch_and(&r->occupied,~mask);
    }
    atomic_flag_clear(&r->guard); return error;
}
agent_err_t agent_resources_owner(agent_resources_t *r,unsigned index,unsigned *owner)
{
    if(!r || index>=AGENT_RESOURCE_COUNT || !owner) return AGENT_ERR_ARGUMENT;
    if(atomic_flag_test_and_set(&r->guard)) return AGENT_ERR_BUSY;
    *owner=r->owners[index]; atomic_flag_clear(&r->guard); return AGENT_OK;
}
