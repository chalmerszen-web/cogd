#include "keyword_pcm.h"
#include "crc.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static keyword_pcm_t stream;
static keyword_pcm_frame_t memory[3];
static int16_t input[KEYWORD_PCM_SAMPLES];
static const int16_t scores[2]={-1234,987};
static const int16_t heads[3]={INT16_MIN,INT16_MAX,123};

static keyword_pcm_frame_t *put(unsigned sequence)
{
    for(unsigned i=0;i<KEYWORD_PCM_SAMPLES;i++)input[i]=(int16_t)(i+sequence);
    keyword_pcm_frame_t *f=keyword_pcm_begin(&stream,input,sequence*32);
    assert(f && f->sequence==sequence);
    assert(!keyword_pcm_peek(&stream)); /* no partial frame is published */
    f->copy_us=7;
    assert(keyword_pcm_commit(&stream,f,8100,KEYWORD_PCM_DETECTED|KEYWORD_PCM_ARMED|KEYWORD_PCM_HEADS,32768+(sequence+1)*512,268,scores,heads));
    return f;
}
static void basics(void)
{
    assert(!keyword_pcm_open(&stream,NULL,sizeof(memory),1));
    assert(!keyword_pcm_open(&stream,(char *)memory+1,sizeof(memory)-1,1));
    assert(!keyword_pcm_open(&stream,memory,sizeof(memory[0]),1));
    assert(!keyword_pcm_open(&stream,memory,sizeof(memory),0));
    assert(!keyword_pcm_open(&stream,memory,sizeof(memory),KEYWORD_PCM_MAX_FRAMES+1));
    assert(keyword_pcm_open(&stream,memory,sizeof(memory),2));
    assert(!keyword_pcm_close(&stream,true));
    for(unsigned n=0;n<2;n++) {
        keyword_pcm_frame_t *f=put(n);
        assert(f==keyword_pcm_peek(&stream));
        assert(f->checksum==agent_crc32(input,sizeof(input)) && !memcmp(input,f->data,sizeof(input)));
        assert(f->score_q8==268 && f->scores_q8[0]==-1234 && f->scores_q8[1]==987 && f->copy_us==7);
        assert(!memcmp(f->heads_q8,heads,sizeof(heads)));
        assert(f->flags==(KEYWORD_PCM_DETECTED|KEYWORD_PCM_ARMED|KEYWORD_PCM_HEADS));
        assert(!keyword_pcm_pop(&stream,n+1));assert(keyword_pcm_pop(&stream,n));
    }
    assert(keyword_pcm_reason(&stream)==KEYWORD_PCM_COMPLETE && !keyword_pcm_active(&stream));
    assert(!keyword_pcm_begin(&stream,input,64));
    assert(keyword_pcm_close(&stream,false));
}
static void full_and_cancel(void)
{
    assert(keyword_pcm_open(&stream,memory,sizeof(memory),4));
    for(unsigned n=0;n<3;n++) {
        keyword_pcm_frame_t *f=keyword_pcm_begin(&stream,input,n*32);assert(f);
        assert(keyword_pcm_commit(&stream,f,8000,0,32768+(n+1)*512,0,scores,NULL));
    }
    assert(!keyword_pcm_begin(&stream,input,96));
    assert(keyword_pcm_reason(&stream)==KEYWORD_PCM_FULL);
    keyword_pcm_halt(&stream,KEYWORD_PCM_TIMEOUT);assert(keyword_pcm_reason(&stream)==KEYWORD_PCM_FULL);
    assert(!keyword_pcm_close(&stream,false));assert(keyword_pcm_close(&stream,true));
    assert(atomic_load(&stream.discarded)==3);
    assert(keyword_pcm_open(&stream,memory,sizeof(memory),2));
    keyword_pcm_frame_t *f=keyword_pcm_begin(&stream,input,0);assert(f);
    keyword_pcm_halt(&stream,KEYWORD_PCM_CANCELLED);
    assert(!keyword_pcm_close(&stream,true));
    assert(!keyword_pcm_commit(&stream,f,8000,0,33280,0,scores,NULL));
    assert(!keyword_pcm_peek(&stream) && atomic_load(&stream.discarded)==1);
    assert(keyword_pcm_close(&stream,false));
}
static void *produce(void *unused)
{
    (void)unused;
    for(unsigned n=0;n<KEYWORD_PCM_MAX_FRAMES;n++) {
        while(atomic_load(&stream.produced)-atomic_load(&stream.consumed)>=stream.slots)sched_yield();
        for(unsigned i=0;i<KEYWORD_PCM_SAMPLES;i++)input[i]=(int16_t)(n+i);
        keyword_pcm_frame_t *f=keyword_pcm_begin(&stream,input,n*32);assert(f);
        assert(keyword_pcm_commit(&stream,f,1,n&1?KEYWORD_PCM_ARMED|KEYWORD_PCM_HEADS:0,32768+(n+1)*512,(int16_t)n,scores,n&1?heads:NULL));
    }
    return NULL;
}
static void concurrent_wrap(void)
{
    assert(keyword_pcm_open(&stream,memory,sizeof(memory),KEYWORD_PCM_MAX_FRAMES));
    pthread_t thread;assert(!pthread_create(&thread,NULL,produce,NULL));
    for(unsigned n=0;n<KEYWORD_PCM_MAX_FRAMES;n++) {
        const keyword_pcm_frame_t *f;
        while(!(f=keyword_pcm_peek(&stream)))sched_yield();
        assert(f->sequence==n && f->score_q8==(int16_t)n && f->sample_end==32768+(n+1)*512);
        assert(f->checksum==agent_crc32(f->data,sizeof(f->data)));
        assert(f->flags==(n&1?KEYWORD_PCM_ARMED|KEYWORD_PCM_HEADS:0));
        if(n&1)assert(!memcmp(f->heads_q8,heads,sizeof(heads)));
        else for(unsigned j=0;j<3;j++)assert(f->heads_q8[j]==0);
        for(unsigned i=0;i<KEYWORD_PCM_SAMPLES;i++) {
            int16_t sample;memcpy(&sample,f->data+2*i,2);assert(sample==(int16_t)(n+i));
        }
        assert(keyword_pcm_pop(&stream,n));
    }
    assert(!pthread_join(thread,NULL));
    assert(keyword_pcm_close(&stream,false));
    keyword_pcm_info_t info;keyword_pcm_info(&stream,&info);
    assert(info.generation==4 && info.produced==2000 && info.consumed==2000 && !info.discarded);
    assert(!info.active && !info.writing && info.reason==KEYWORD_PCM_COMPLETE);
}
int main(void)
{
    basics();full_and_cancel();concurrent_wrap();
    assert(keyword_pcm_open(&stream,memory,sizeof(memory),1));
    keyword_pcm_frame_t *f=keyword_pcm_begin(&stream,input,0);assert(f);
    assert(!keyword_pcm_commit(&stream,f,1,KEYWORD_PCM_HEADS,512,0,scores,NULL));
    assert(!keyword_pcm_peek(&stream));
    assert(keyword_pcm_commit(&stream,f,1,0,512,0,scores,NULL));
    assert(keyword_pcm_pop(&stream,0) && keyword_pcm_close(&stream,false));
    printf("keyword PCM lifecycle, ownership, publication, wrap, CRC and cancellation passed; frame=%zu state=%zu\n",
        sizeof(keyword_pcm_frame_t),sizeof(keyword_pcm_t));return 0;
}
