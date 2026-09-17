#include "phase.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
phase_profile_t capture_profile;
phase_stamp_t phase_enter(void)
{
    return (phase_stamp_t){capture_profile.active?phase_clock():0,capture_profile.active};
}
void phase_leave(unsigned which,phase_stamp_t stamp)
{
    if(!stamp.active || !capture_profile.active || which>=PH_COUNT)return;
    unsigned elapsed=phase_clock()-stamp.clock;
    phase_counter_t *c=capture_profile.local+which;
    if(c->calls==UINT_MAX || elapsed>UINT_MAX-c->cycles) {capture_profile.overflow=true;return;}
    ++c->calls;c->cycles+=elapsed;if(elapsed>c->maximum)c->maximum=elapsed;
}
void phase_start(void)
{
    /* An odd epoch hides an in-progress capture. Atomics are used only for
     * the frozen USB snapshot, avoiding atomic critical sections per sample. */
    unsigned epoch=atomic_load(&capture_profile.epoch);
    atomic_store(&capture_profile.epoch,(epoch&1u)?epoch+2u:epoch+1u);
    memset(capture_profile.local,0,sizeof(capture_profile.local));
    capture_profile.overflow=false;capture_profile.active=true;
}
void phase_finish(void)
{
    if(!capture_profile.active)return;
    capture_profile.active=false;
    for(unsigned i=0;i<PH_COUNT;++i) {
        atomic_store(&capture_profile.published[i][0],capture_profile.local[i].calls);
        atomic_store(&capture_profile.published[i][1],capture_profile.local[i].cycles);
        atomic_store(&capture_profile.published[i][2],capture_profile.local[i].maximum);
    }
    atomic_store(&capture_profile.published_overflow,capture_profile.overflow);
    atomic_fetch_add(&capture_profile.epoch,1);
}
agent_err_t phase_status(char *out,size_t cap)
{
    if(!out || !cap)return AGENT_ERR_ARGUMENT;
    unsigned epoch=atomic_load(&capture_profile.epoch);
    if(epoch&1u)return AGENT_ERR_BUSY;
    int wrote=snprintf(out,cap,"{\"profile\":\"capture-coarse-v1\",\"epoch\":%u,\"complete\":%s,"
        "\"overflow\":%s,\"clock_hz\":%u,\"unmeasured_rows\":[3,4,5],\"rows\":[",epoch,epoch?"true":"false",
        atomic_load(&capture_profile.published_overflow)?"true":"false",phase_hz());
    if(wrote<0 || (size_t)wrote>=cap)return AGENT_ERR_LIMIT;
    size_t n=(size_t)wrote;
    for(unsigned i=0;i<PH_COUNT;++i) {
        wrote=snprintf(out+n,cap-n,"%s[%u,%u,%u]",i?",":"",atomic_load(&capture_profile.published[i][0]),
            atomic_load(&capture_profile.published[i][1]),atomic_load(&capture_profile.published[i][2]));
        if(wrote<0 || (size_t)wrote>=cap-n)return AGENT_ERR_LIMIT;
        n+=(size_t)wrote;
    }
    if(n+3>cap)return AGENT_ERR_LIMIT;
    memcpy(out+n,"]}",3);
    return atomic_load(&capture_profile.epoch)==epoch?AGENT_OK:AGENT_ERR_BUSY;
}
