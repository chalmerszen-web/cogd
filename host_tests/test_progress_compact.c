#include "progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A cue must finish in 100 ms at the existing 16-kHz owner rate and produce
 * identical output when cancellation/scheduling divides its PCM writes. */
static void cue(void (*init)(agent_progress_t *,bool),bool yue)
{
    int16_t whole[1602],pieces[1602];
    for(unsigned i=0;i<1602;++i) whole[i]=pieces[i]=12345;
    agent_progress_t state;
    init(&state,yue);
    assert(state.samples==1600);
    assert(agent_progress_render(&state,whole+1,1600)==1600);
    assert(state.position==1600 && !agent_progress_render(&state,whole+1,1));
    assert(whole[0]==12345 && whole[1601]==12345);
    assert(whole[1]==0 && whole[1600]==0);
    bool positive=false,negative=false;
    for(unsigned i=1;i<=1600;++i) {
        assert(whole[i]>=-1600 && whole[i]<=1600);
        positive|=whole[i]>0;negative|=whole[i]<0;
    }
    assert(positive && negative);
    init(&state,yue);
    for(size_t done=0;done<1600;) {
        size_t request=1+done%239;
        if(request>1600-done) request=1600-done;
        size_t got=agent_progress_render(&state,pieces+1+done,request);
        assert(got==request);done+=got;
    }
    assert(!memcmp(whole,pieces,sizeof(whole)));
    init(&state,yue);
    agent_progress_t before=state;
    assert(!agent_progress_render(&state,NULL,20));
    assert(!agent_progress_render(&state,pieces,0));
    assert(!memcmp(&before,&state,sizeof(state)));
    assert(!agent_progress_render(NULL,pieces,20));
    init(NULL,yue);
    assert(agent_progress_render(&state,pieces,239)==239);
    init(&state,yue); /* A cancelled owner may start an independent new cue. */
    assert(state.position==0);
    assert(agent_progress_render(&state,pieces,239)==239);
    assert(!memcmp(whole+1,pieces,239*sizeof(*pieces)));
}
int main(void)
{
    for(unsigned yue=0;yue<2;++yue) {
        assert(agent_progress_samples(yue)==1600);
        assert(agent_progress_search_samples(yue)==1600);
        assert(agent_progress_memory_samples(yue)==1600);
        assert(agent_progress_text(yue)[0] && agent_progress_search_text(yue)[0]);
        assert(agent_progress_memory_text(yue)[0] && agent_progress_light_text(yue)[0]);
        cue(agent_progress_init,yue);cue(agent_progress_search_init,yue);
        cue(agent_progress_memory_init,yue);cue(agent_progress_light_init,yue);
    }
    puts("C3 compact progress: bounded cue, chunking, restart and output bounds passed");
    return 0;
}
