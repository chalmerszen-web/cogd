#include "sync_kws_temporal_owner.h"
#include <assert.h>
#include <stdio.h>
static kws_temporal_owner_t state;
static uint64_t end;
static bool step(int primary,int verifier,bool armed) {
    end+=512;
    return kws_temporal_owner_step(&state,(int16_t)primary,(int16_t)verifier,end,armed);
}
static void ready(void) {
    kws_temporal_owner_reset(&state,268);end=0;
    for(unsigned i=0;i<64;++i)assert(!step(-1000,-1000,true));
}
int main(void) {
    ready();assert(!step(1000,-1000,true));assert(!step(1000,-1000,true));
    for(unsigned i=0;i<4;++i)assert(!step(-1000,-1000,true));
    assert(!step(-1000,1000,true));assert(!step(-1000,1000,true));
    assert(!state.original.cooldown_until); /* stale primary cannot confirm */
    for(unsigned i=0;i<4;++i)assert(!step(-1000,-1000,true));
    assert(!step(1000,1000,true));assert(step(1000,1000,true));
    assert(state.original.cooldown_until==end+24000); /* rejected peak cost no cooldown */
    for(unsigned i=0;i<10;++i)assert(!step(1000,1000,true));
    ready();assert(!step(-1000,1000,true));assert(!step(-1000,1000,true));
    for(unsigned i=0;i<4;++i)assert(!step(-1000,-1000,true));
    assert(!step(1000,-1000,true));assert(!step(1000,-1000,true));
    assert(!state.original.cooldown_until); /* stale verifier also cannot confirm */
    ready();assert(!step(1000,1000,false));assert(!state.primary_votes && !state.verify_votes);
    assert(!step(1000,1000,true));assert(step(1000,1000,true));
    ready();assert(!step(1000,1000,true));end+=512;
    assert(!step(1000,1000,true));assert(!state.original.cooldown_until);
    puts("PASS: concurrent support, stale primary/verifier rejection, accepted-only cooldown, disarm and gap reset");
    return 0;
}
