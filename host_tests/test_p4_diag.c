#include "p4_diag_format.h"
#include "p4_storage_layout.h"
#include <assert.h>
#include <stdio.h>

static uint8_t page[4096],flash[4][4096],torn[4096];
static uint64_t recover(void) {
    uint64_t max=0;p4_diag_header_t h;
    for(unsigned i=0;i<4;i++)if(p4_diag_valid(flash[i],&h) && h.sequence>max)max=h.sequence;
    return max;
}
int main(void) {
    memset(flash,0xff,sizeof(flash));p4_diag_header_t h;
    assert(!p4_diag_valid(flash[0],&h));
    for(unsigned seq=1;seq<=10;seq++) {
        memset(page,0xff,sizeof(page));uint32_t used=64;
        assert(p4_diag_append(page,&used,123456,2,0,"test",4));
        h=(p4_diag_header_t){.sequence=seq,.used=used,.boot=seq/4+1,.uptime_ms=123999,.unix_ms=1794300000000ULL};
        p4_diag_seal(page,&h);assert(p4_diag_valid(page,&h));
        /* Every possible interrupted write, including partial commit, is invalid. */
        for(unsigned cut=0;cut<4096;cut++) {
            memset(torn,0xff,sizeof(torn));memcpy(torn,page,cut);
            assert(!p4_diag_valid(torn,&h));
        }
        memcpy(flash[(seq-1)%4],page,4096);assert(recover()==seq);
    }
    /* Incomplete erase/program of the next sector preserves the other records. */
    memset(flash[2],0xff,4096);memcpy(flash[2],page,100);assert(recover()==10);
    for(unsigned at=0;at<4096;at++) {
        memcpy(torn,page,4096);torn[at]^=1;assert(!p4_diag_valid(torn,&h));
    }
    memset(page,0xff,4096);uint32_t used=64;char text[P4_DIAG_TEXT];memset(text,'x',sizeof(text));
    for(unsigned i=0;i<5;i++)assert(p4_diag_append(page,&used,i,1,0,text,sizeof(text)));
    uint32_t before=used;assert(!p4_diag_append(page,&used,9,1,0,text,sizeof(text)));assert(used==before);
    assert(!p4_diag_append(page,&used,9,1,0,text,P4_DIAG_TEXT+1));
    assert(p4_media_cache_end(0x780000)==0x680000);
    assert(0x880000+p4_media_cache_end(0x780000)==0xf00000);
    assert(p4_media_cache_end(0x100000)==0);
    assert((P4_DIAG_BYTES/P4_DIAG_PAGE)*P4_DIAG_INTERVAL_MS>=600000);
    puts("PASS journal wrap, reboot recovery, every torn-write boundary, CRC, bounds, storage separation");
    return 0;
}
