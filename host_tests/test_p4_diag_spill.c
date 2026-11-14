#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "p4_diag_spill.h"
int main(void){
    p4_diag_critical_queue_t *q=calloc(1,sizeof(*q));assert(q);
    uint8_t page[P4_DIAG_PAGE];memset(page,0xff,sizeof(page));uint32_t used=64;
    char text[P4_DIAG_TEXT];memset(text,'x',sizeof(text));
    for(unsigned i=0;i<5;++i)assert(p4_diag_append(page,&used,i,6,0,text,sizeof(text)));
    unsigned before=used;assert(!p4_diag_append(page,&used,5,6,0,text,sizeof(text)));assert(before==used);
    for(unsigned i=0;i<16;++i)assert(p4_diag_critical_push(q,5+i,i&1,text,sizeof(text)));
    assert(!p4_diag_critical_push(q,99,0,text,sizeof(text)));assert(q->count==16 && q->peak==16);
    assert(p4_diag_critical_drain(q,page,&used)==0 && q->count==16);
    unsigned drained=0,pages=0;
    while(q->count){memset(page,0xff,sizeof(page));used=64;unsigned n=p4_diag_critical_drain(q,page,&used);assert(n && n<=5);
        p4_diag_header_t h={.sequence=++pages,.used=used};p4_diag_seal(page,&h);assert(p4_diag_valid(page,&h));
        for(unsigned at=64;at<used;at+=12+P4_DIAG_TEXT){uint64_t t;uint16_t size;memcpy(&t,page+at,8);memcpy(&size,page+at+8,2);
            assert(t==5+drained && size==P4_DIAG_TEXT && page[at+10]==6 && page[at+11]==(drained&1));assert(!memcmp(page+at+12,text,sizeof(text)));++drained;}}
    assert(drained==16 && pages==4);
    for(unsigned wrap=0;wrap<128;++wrap){assert(p4_diag_critical_push(q,wrap,0,"ok",2));used=64;assert(p4_diag_critical_drain(q,page,&used)==1 && used==78 && !q->count);}
    assert(!p4_diag_critical_push(q,0,0,text,P4_DIAG_TEXT+1));free(q);
    puts("PASS full-page overflow,16-record bounded queue,four sealed pages,FIFO timestamps/payload,explicit rejection,and128 wraps");return 0;
}
