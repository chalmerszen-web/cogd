#include "kws_temporal_owner.h"
#include <assert.h>
#include <stdio.h>
extern void legacy_reset(kws_temporal_owner_t *,int16_t);
extern bool legacy_step(kws_temporal_owner_t *,int16_t,int16_t,uint64_t,bool);

static void warm(kws_temporal_owner_t *s)
{
    kws_temporal_owner_reset(s,100);
    for(unsigned i=1;i<=64;++i) assert(!kws_temporal_owner_step(s,0,0,i*512,true));
}

int main(void)
{
    kws_temporal_owner_t s,old;warm(&s);legacy_reset(&old,100);
    for(unsigned i=1;i<=64;++i) assert(!legacy_step(&old,0,0,i*512,true));
    for(unsigned i=65;i<=68;++i) {
        int16_t primary=i>=67?500:0,aux=i<=66?500:0;
        assert(!kws_temporal_owner_step(&s,primary,aux,i*512,true));
        bool previous=legacy_step(&old,primary,aux,i*512,true);
        assert(previous==(i==68));
    }
    assert(s.pending==68*512 && s.original.cooldown_until==0);
    assert(!kws_temporal_owner_step(&s,0,0,69*512,true));
    assert(!kws_temporal_owner_step(&s,0,500,70*512,true));
    assert(kws_temporal_owner_step(&s,0,500,71*512,true));
    assert(!s.pending && s.original.cooldown_until==71*512+24000);
    assert(!kws_temporal_owner_step(&s,0,0,72*512,false));
    assert(!s.pending && !s.last_support && s.original.cooldown_until==71*512+24000);
    assert(!kws_temporal_owner_step(&s,500,500,50000,true));
    assert(!s.original.cooldown_until && !s.pending);
    warm(&s);
    for(unsigned i=65;i<=77;++i) {
        assert(!kws_temporal_owner_step(&s,i<=66?500:0,0,i*512,true));
    }
    assert(!s.pending && !s.original.cooldown_until);
    assert(!kws_temporal_owner_step(&s,0,500,78*512,true));
    assert(!kws_temporal_owner_step(&s,0,500,79*512,true));
    /* Two recent votes remain valid when the verifier's peak precedes the
     * primary by one block. Requiring the current score too would reject it. */
    warm(&s);
    assert(!kws_temporal_owner_step(&s,0,500,65*512,true));
    assert(!kws_temporal_owner_step(&s,500,500,66*512,true));
    assert(kws_temporal_owner_step(&s,500,0,67*512,true));
    assert(s.original.cooldown_until==67*512+24000);
    puts("PASS: stale support, recent two-of-three, accepted-only cooldown, disarm, gap and expiry");
    return 0;
}
