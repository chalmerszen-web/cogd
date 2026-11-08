#include "p4_near_onset.h"
#include <assert.h>
#include <limits.h>
int main(void)
{
    p4_near_onset_t s={0};
    p4_near_onset_observe(&s,100,false);assert(!s.active);
    /* Actual candidate-54 eligible frames, relative to the first frame. */
    const unsigned frames[]={620,906,928,988,1033,1073,1092,1384,1632,1662,1738,1780,1827,1865,1888,1917};
    for(unsigned i=0;i<sizeof(frames)/sizeof(frames[0]);++i)
        p4_near_onset_observe(&s,275077+frames[i],true);
    assert(s.active && s.first==275697 && s.last==276994);
    p4_near_onset_observe(&s,277500,false);assert(s.first==275697);
    p4_near_onset_observe(&s,278194,true);assert(s.first==275697);
    p4_near_onset_observe(&s,279395,true);assert(s.first==279395);
    /* Recorded NLP detector has a 992 ms dropout inside a complete phrase.
     * Retain the first syllable; the voice-confirmation votes remain separate. */
    s=(p4_near_onset_t){0};
    p4_near_onset_observe(&s,35924,true);
    p4_near_onset_observe(&s,36258,true);
    p4_near_onset_observe(&s,37250,true);
    assert(s.first==35924);
    s=(p4_near_onset_t){0};
    p4_near_onset_observe(&s,UINT_MAX-99,true);
    p4_near_onset_observe(&s,200,true);assert(s.first==UINT_MAX-99);
    p4_near_onset_observe(&s,1401,true);assert(s.first==1401);
    /* Actual prefix-loss trace: early linear speech survived but the NLP
     * admission began 931 ms later. A hint never changes the strict track. */
    s=(p4_near_onset_t){0};p4_near_onset_t hint={0};
    p4_near_onset_hint(&hint,85578,true);
    p4_near_onset_observe(&s,86509,true);
    p4_near_onset_hint(&hint,86793,true);
    assert(s.first==86509 && hint.first==85578);
    assert(p4_near_onset_select(&s,&hint,86853)==85578);
    assert(p4_near_onset_select(&s,&hint,88579)==86509); /* total age */
    hint=(p4_near_onset_t){.first=86500,.last=86500,.active=true};
    assert(p4_near_onset_select(&s,&hint,88501)==86509); /* inactive gap */
    p4_near_onset_hint(&hint,88501,true);assert(hint.first==88501);
    assert(p4_near_onset_select(&s,&hint,88502)==86509); /* never move later */
    hint=(p4_near_onset_t){0};
    p4_near_onset_hint(&hint,100,false);assert(!hint.active);
    s=(p4_near_onset_t){.first=200,.last=200,.active=true};
    p4_near_onset_hint(&hint,UINT_MAX-99,true);
    p4_near_onset_hint(&hint,200,true);
    assert(p4_near_onset_select(&s,&hint,300)==UINT_MAX-99);
    return 0;
}
