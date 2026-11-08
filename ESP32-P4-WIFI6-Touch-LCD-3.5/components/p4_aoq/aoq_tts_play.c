#include "aoq_tts_play.h"
#include "aoq_tts_trial.h"
#include "esp_heap_caps.h"
#include <stdatomic.h>
#include <string.h>

enum { Argument=1,Busy=3,Cancelled=4,Limit=5,Protocol=7,Timeout=15,Memory=19 };
enum { Rate=24000,ChunkSamples=960,WaitMs=5 };
static atomic_flag play_owner=ATOMIC_FLAG_INIT;
static atomic_uint cancel_epoch=0;

static void wipe(void* memory,size_t bytes){
    volatile unsigned char* p=memory;while(bytes--)*p++=0;
}
static bool valid_ops(const p4_aoq_play_ops_t* o){
    return o&&o->open&&o->space&&o->write&&o->remaining&&o->played&&
        o->seal&&o->finish&&o->cancelled&&o->now_ms&&o->wait_ms;
}
static int progress(const p4_aoq_play_ops_t* o,unsigned epoch,uint64_t started,uint64_t budget){
    if(atomic_load_explicit(&cancel_epoch,memory_order_relaxed)!=epoch||o->cancelled(o->ctx))return Cancelled;
    const uint64_t now=o->now_ms(o->ctx);
    if(now<started||now-started>=budget)return Timeout;
    return 0;
}
static void snapshot(const p4_aoq_play_ops_t* o,p4_aoq_play_report_t* r){
    r->remaining_samples=o->remaining(o->ctx);r->played_samples=o->played(o->ctx);
}
void p4_aoq_tts_play_cancel(void){
    atomic_fetch_add_explicit(&cancel_epoch,1,memory_order_relaxed);
}

int p4_aoq_cached_play(p4_aoq_pcm_copy_fn copy_pcm,uint64_t generation,const p4_aoq_play_ops_t* supplied,p4_aoq_play_report_t* report){
    const unsigned epoch=atomic_load_explicit(&cancel_epoch,memory_order_relaxed);
    if(!report)return Argument;
    memset(report,0,sizeof(*report));report->generation=generation;report->stage="argument";
    if(!copy_pcm||!generation||!valid_ops(supplied)){report->error=Argument;return Argument;}
    if(atomic_flag_test_and_set_explicit(&play_owner,memory_order_acquire)){
        report->stage="play_busy";report->error=Busy;return Busy;
    }
    const p4_aoq_play_ops_t ops=*supplied;
    const uint64_t started=ops.now_ms(ops.ctx);
    uint64_t budget=4000;
    int error=progress(&ops,epoch,started,budget);
    int16_t* source=NULL;void* ring=NULL;size_t bytes=0;
    if(error){report->stage="cancelled_before_copy";goto cleanup;}
    report->stage="copying_exact_generation";
    source=heap_caps_malloc(P4_AOQ_PLAY_PCM_MAX_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!source){error=Memory;goto cleanup;}
    report->copy_error=copy_pcm(generation,source,P4_AOQ_PLAY_PCM_MAX_BYTES,&bytes);
    if(report->copy_error){error=report->copy_error;goto cleanup;}
    if(!bytes||(bytes%sizeof(*source))||bytes>P4_AOQ_PLAY_PCM_MAX_BYTES){error=Protocol;report->stage="cached_pcm_invalid";goto cleanup;}
    report->cache_copied=true;report->accepted_samples=bytes/sizeof(*source);
    budget=((uint64_t)report->accepted_samples*1000+Rate-1)/Rate+4000;
    error=progress(&ops,epoch,started,budget);if(error)goto cleanup;
    ring=heap_caps_malloc(P4_AOQ_PLAY_RING_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!ring){error=Memory;report->stage="ring_memory";goto cleanup;}
    report->stage="opening_existing_audio_owner";
    report->open_error=ops.open(ops.ctx,Rate,ring,P4_AOQ_PLAY_RING_BYTES);
    if(report->open_error){error=report->open_error;goto cleanup;}
    report->stream_opened=true;report->stage="writing_pcm";
    while(report->written_samples<report->accepted_samples){
        error=progress(&ops,epoch,started,budget);if(error)break;
        const unsigned space=ops.space(ops.ctx);
        if(space>P4_AOQ_PLAY_RING_BYTES/sizeof(*source)){error=Protocol;report->stage="stream_space_invalid";break;}
        size_t count=report->accepted_samples-report->written_samples;
        if(count>space)count=space;
        if(count>ChunkSamples)count=ChunkSamples;
        if(!count){ops.wait_ms(ops.ctx,WaitMs);continue;}
        error=ops.write(ops.ctx,source+report->written_samples,count);
        if(error)break;
        report->written_samples+=count;
    }
    if(!error){
        error=progress(&ops,epoch,started,budget);
        if(!error){ops.seal(ops.ctx);report->sealed=true;report->stage="waiting_dac_progress";}
    }
    while(!error){
        error=progress(&ops,epoch,started,budget);if(error)break;
        snapshot(&ops,report);
        if(report->played_samples>report->written_samples||report->remaining_samples>report->written_samples){
            error=Protocol;report->stage="stream_counts_invalid";break;
        }
        if(!report->remaining_samples&&report->played_samples>=report->accepted_samples)break;
        ops.wait_ms(ops.ctx,WaitMs);
    }

cleanup:
    {
        const uint64_t join_start=ops.now_ms(ops.ctx);
        report->playback_ms=join_start>=started?join_start-started:0;
        if(report->stream_opened){
            /* Even error returns must join the existing audio task. Never free
             * the ring based on remaining()==0, played(), or a timeout alone. */
            report->finish_error=ops.finish(ops.ctx,error);
            report->joined=true;
            if(!error)error=report->finish_error;
            snapshot(&ops,report);
            if(!error&&(atomic_load_explicit(&cancel_epoch,memory_order_relaxed)!=epoch||ops.cancelled(ops.ctx)))error=Cancelled;
            if(!error&&(report->written_samples!=report->accepted_samples||
                       report->remaining_samples||report->played_samples!=report->accepted_samples)){
                error=Protocol;report->stage="joined_counts_incomplete";
            }
        }
        const uint64_t end=ops.now_ms(ops.ctx);
        report->join_ms=end>=join_start?end-join_start:0;report->elapsed_ms=end>=started?end-started:0;
        /* No callback or audio task may hold either buffer beyond joined. */
        if(ring){wipe(ring,P4_AOQ_PLAY_RING_BYTES);heap_caps_free(ring);}
        if(source){wipe(source,bytes<=P4_AOQ_PLAY_PCM_MAX_BYTES?bytes:P4_AOQ_PLAY_PCM_MAX_BYTES);heap_caps_free(source);}
        report->error=error;report->cancelled=error==Cancelled;report->timed_out=error==Timeout;
        report->complete=!error&&report->cache_copied&&report->sealed&&report->joined;
        if(report->complete)report->stage="played_and_joined";
        else if(report->cancelled)report->stage=report->joined?"cancelled_and_joined":"cancelled_before_open";
        else if(report->timed_out)report->stage=report->joined?"timeout_cleanup_complete":"timeout_before_open";
        atomic_flag_clear_explicit(&play_owner,memory_order_release);
        return error;
    }
}

int p4_aoq_tts_play(uint64_t generation,const p4_aoq_play_ops_t* supplied,p4_aoq_play_report_t* report){
    return p4_aoq_cached_play(p4_aoq_tts_trial_copy_pcm,generation,supplied,report);
}
