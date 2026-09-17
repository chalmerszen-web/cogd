#include "phase.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static unsigned clock_value;
unsigned phase_clock(void) {return clock_value;}
unsigned phase_hz(void) {return 160000000u;}
int main(void)
{
    char out[1024];assert(!phase_status(out,sizeof(out)) && strstr(out,"\"complete\":false"));
    phase_stamp_t inactive=phase_enter();assert(!inactive.active);
    phase_start();assert(phase_status(out,sizeof(out))==AGENT_ERR_BUSY);
    phase_leave(PH_READ,inactive);assert(!capture_profile.local[PH_READ].calls);
    clock_value=UINT_MAX-3;phase_stamp_t a=phase_enter();clock_value=7;phase_leave(PH_READ,a);
    assert(capture_profile.local[PH_READ].calls==1 && capture_profile.local[PH_READ].cycles==11);
    clock_value=10;a=phase_enter();clock_value=20;phase_stamp_t b=phase_enter();
    clock_value=30;phase_leave(PH_SPECTRAL,b);clock_value=50;phase_leave(PH_MIC,a);
    assert(capture_profile.local[PH_MIC].cycles==40 && capture_profile.local[PH_SPECTRAL].cycles==10);
    phase_leave(PH_COUNT,a);phase_finish();
    assert(!phase_status(out,sizeof(out)) && strstr(out,"\"epoch\":2") && strstr(out,"\"complete\":true"));
    assert(strstr(out,"\"unmeasured_rows\":[3,4,5]"));
    assert(!capture_profile.local[PH_SOURCE].calls && !capture_profile.local[PH_METER].calls && !capture_profile.local[PH_DECIMATE].calls);
    char frozen[1024];memcpy(frozen,out,sizeof(out));phase_leave(PH_READ,a);phase_finish();
    assert(!phase_status(out,sizeof(out)) && !strcmp(frozen,out));
    assert(phase_status(out,1)==AGENT_ERR_LIMIT && phase_status(NULL,10)==AGENT_ERR_ARGUMENT);
    phase_start();assert(!capture_profile.local[PH_READ].calls);
    capture_profile.local[PH_READ].cycles=UINT_MAX;clock_value=1;a=phase_enter();clock_value=2;phase_leave(PH_READ,a);
    assert(capture_profile.overflow && capture_profile.local[PH_READ].cycles==UINT_MAX);
    phase_finish();assert(!phase_status(out,sizeof(out)) && strstr(out,"\"overflow\":true"));
    for(unsigned i=0;i<1000;++i) {
        phase_start();clock_value=i;a=phase_enter();clock_value+=i;phase_leave(PH_EMPTY,a);phase_finish();
        assert(atomic_load(&capture_profile.published[PH_EMPTY][0])==1);
        assert(atomic_load(&capture_profile.published[PH_EMPTY][1])==i);
        assert(atomic_load(&capture_profile.published[PH_EMPTY][2])==i);
        assert(!phase_status(out,sizeof(out)));
    }
    puts("{\"passed\":true,\"lifecycles\":1000,\"wrap_nested_inactive_overflow_capacity\":true}");
    return 0;
}
