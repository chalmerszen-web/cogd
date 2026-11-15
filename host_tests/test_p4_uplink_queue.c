#include "p4_uplink_queue.h"
#include "p4_local_turn.h"
#include "p4_input_media_clock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void backpressure(void)
{
    p4_uplink_queue_t *q=calloc(1,sizeof(*q));assert(q);
    int16_t pcm[320];p4_probe_bound_t probe={0};
    for(unsigned round=0;round<5;++round){
        for(unsigned i=0;i<P4_UPLINK_QUEUE_FRAMES;++i){
            for(unsigned j=0;j<320;++j)pcm[j]=(int16_t)(i*320+j);
            probe.start_cursor=(round*96+i)*320;probe.end_cursor=probe.start_cursor+320;
            assert(p4_uplink_queue_push(q,pcm,probe.end_cursor,probe.start_cursor*1000u/16,&probe));
        }
        assert(!p4_uplink_queue_push(q,pcm,probe.end_cursor+320,0,&probe));
        for(unsigned i=0;i<P4_UPLINK_QUEUE_FRAMES;++i){
            p4_uplink_frame_t *f=p4_uplink_queue_head(q);assert(f);
            /* A transport BUSY result leaves the exact processed frame and
             * metadata untouched, including across queue wraparound. */
            assert(p4_uplink_queue_head(q)==f);
            assert(f->end_cursor==(round*96+i+1)*320);
            assert(f->probe.start_cursor+320==f->end_cursor);
            for(unsigned j=0;j<320;++j)assert(f->pcm[j]==(int16_t)(i*320+j));
            p4_uplink_queue_pop(q);
        }
        assert(!p4_uplink_queue_head(q));
    }
    assert(p4_uplink_queue_push(q,pcm,320,0,&probe));
    assert(!p4_uplink_queue_push(q,pcm,960,0,&probe));
    p4_uplink_queue_reset(q);assert(!p4_uplink_queue_head(q));free(q);
}
static void endpoint_before_drain(void)
{
    p4_uplink_queue_t *q=calloc(1,sizeof(*q));assert(q);
    p4_local_turn_t turn={0};p4_local_turn_seed(&turn,0,320);
    p4_input_media_clock_t clock={0};p4_probe_bound_t probe={0};
    int16_t pcm[320]={0};unsigned source=0,sent=0,endpoint_at=0,commit_at=0;
    unsigned cue_count=0,pending_at_cue=0;
    /* 1.2 seconds retained onset precedes real-time input. A 600 ms pause
     * inside the sentence must not endpoint the original 1200 ms rule. */
    for(unsigned now=0;now<8000;now+=10){
        unsigned available=19200+now*16;
        for(unsigned batch=0;batch<4 && !turn.ended && source+320<=available &&
            p4_uplink_queue_count(q)<P4_UPLINK_QUEUE_FRAMES;++batch){
            source+=320;
            bool voice=source<=32000 || (source>41600 && source<=48000);
            p4_local_event_t event=p4_local_turn_feed(&turn,source,voice,true,1200);
            pcm[0]=(int16_t)(source/320);
            assert(p4_uplink_queue_push(q,pcm,source,(uint64_t)source*1000/16,&probe));
            if(event==P4_LOCAL_END){
                ++cue_count;endpoint_at=now;pending_at_cue=p4_uplink_queue_count(q);
                assert(source==48000+19200);
            }
        }
        if(now%20==0){
            p4_uplink_frame_t *f=p4_uplink_queue_head(q);
            if(f){assert(f->end_cursor==sent+320);sent=f->end_cursor;
                p4_input_media_clock_accept(&clock,now,320);p4_uplink_queue_pop(q);}
        }
        if(turn.ended && sent>=turn.analysed && !p4_input_media_clock_wait(&clock,now)){
            commit_at=now;break;
        }
    }
    assert(cue_count==1 && pending_at_cue>=40);
    assert(commit_at>endpoint_at+800 && sent==source && !p4_uplink_queue_count(q));
    printf("endpoint=%u ms commit=%u ms overlap=%u ms queued_at_cue=%u\n",
        endpoint_at,commit_at,commit_at-endpoint_at,pending_at_cue);
    free(q);
}
int main(void){backpressure();endpoint_before_drain();puts("uplink queue PASS");return 0;}
