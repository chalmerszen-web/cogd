#include "kws_word_event.h"
#include <assert.h>
#include <stdio.h>

static uint64_t end;
static bool feed(kws_word_event_t *s,bool wake,bool armed)
{
    const int16_t values[2] = {wake ? -4096 : 4096,wake ? 4096 : -4096};
    end += 512;return kws_word_event_step(s,values,end,armed);
}

int main(void)
{
    _Static_assert(sizeof(kws_word_event_t)<=128,"Word event storage budget");
    kws_word_event_t s;kws_word_event_reset(&s);
    assert(!feed(&s,true,true));assert(!feed(&s,false,true));assert(feed(&s,false,true));
    for (unsigned i=0;i<5000;++i) assert(!feed(&s,false,true));
    assert(!feed(&s,true,true));assert(!feed(&s,false,true));assert(feed(&s,false,true));
    kws_word_event_reset(&s);assert(!feed(&s,true,true));end += 512;
    assert(!feed(&s,false,true));assert(!feed(&s,false,true));
    assert(!feed(&s,true,true));assert(!feed(&s,false,false));assert(!feed(&s,false,true));
    int16_t equal[2]={0,0};
    kws_word_event_reset(&s);
    for (unsigned i=0;i<500;++i) {end += 512;assert(!kws_word_event_step(&s,equal,end,true));}
    assert(!kws_word_event_step(&s,NULL,end,true));assert(!kws_word_event_step(&s,equal,1,true));
    assert(!kws_word_event_step(NULL,equal,end,true));
    unsigned seed=462;
    for (unsigned i=0;i<8192;++i) {
        int16_t frame[2];
        for (unsigned k=0;k<2;++k) {seed=seed*1664525u+1013904223u;frame[k]=(int16_t)(seed>>16);}
        end += 512;(void)kws_word_event_step(&s,frame,end,true);
        assert(s.count<=8 && s.next<8);
    }
    printf("word event workspace %zu; continuous/gap/disarm/flat/extreme passed\n",sizeof(s));
    return 0;
}
