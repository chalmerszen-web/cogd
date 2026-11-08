#include "aoq_tts_play.h"
#include "aoq_tts_trial.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t now,start;
    size_t bytes,written,read,played;
    unsigned copy_calls,opens,writes,finishes,seals,waits;
    unsigned allocations,frees,fail_allocation;
    int copy_error,open_error,write_error,finish_error;
    unsigned error_write_number,cancel_wait;
    bool active,joined,external_cancel,stall,counts_invalid,short_after_join,cancel_in_join,reenter;
    uint64_t join_delay;
    void* ring;
    size_t ring_bytes;
    p4_aoq_play_ops_t* ops;
} fake_t;
static fake_t f;
static unsigned cases;
static int16_t source_sample(size_t i){return (int16_t)(123+i%1234);}
void* heap_caps_malloc(size_t n,unsigned flags){
    assert(flags==3);++f.allocations;if(f.allocations==f.fail_allocation)return NULL;
    return calloc(1,n);
}
void heap_caps_free(void* p){assert(!f.active);++f.frees;free(p);}
int p4_aoq_tts_trial_copy_pcm(uint64_t generation,void* p,size_t capacity,size_t* written){
    ++f.copy_calls;assert(p&&written&&capacity==P4_AOQ_PLAY_PCM_MAX_BYTES);*written=0;
    if(generation!=42)return 14;
    if(f.copy_error)return f.copy_error;
    *written=f.bytes;
    if(f.bytes<=capacity){for(size_t i=0;i<f.bytes/2;++i)((int16_t*)p)[i]=source_sample(i);}
    return 0;
}
static int open_stream(void* ctx,unsigned rate,void* p,size_t n){
    assert(ctx==&f&&rate==24000&&p&&n==P4_AOQ_PLAY_RING_BYTES);++f.opens;
    if(f.reenter){p4_aoq_play_report_t nested;assert(p4_aoq_tts_play(42,f.ops,&nested)==3&&!nested.stream_opened);}
    if(f.open_error)return f.open_error;
    f.active=true;f.ring=p;f.ring_bytes=n;f.start=f.now;return 0;
}
static unsigned space(void* ctx){assert(ctx==&f&&f.active);return f.stall?0:(unsigned)(f.ring_bytes/2-(f.written-f.read));}
static int write_stream(void* ctx,const int16_t* p,size_t n){
    assert(ctx==&f&&f.active&&n&&n<=space(ctx)&&n<=960);++f.writes;
    if(f.error_write_number==f.writes)return f.write_error;
    for(size_t i=0;i<n;++i){assert(p[i]==source_sample(f.written+i));((int16_t*)f.ring)[(f.written+i)%(f.ring_bytes/2)]=p[i];}
    f.written+=n;return 0;
}
static unsigned remaining(void* ctx){assert(ctx==&f);return (unsigned)(f.written-f.read);}
static unsigned played(void* ctx){assert(ctx==&f);return f.counts_invalid?(unsigned)(f.bytes/2+1):(unsigned)f.played;}
static void seal(void* ctx){assert(ctx==&f&&f.active&&f.written==f.bytes/2);++f.seals;}
static int finish(void* ctx,int error){
    assert(ctx==&f&&f.active&&f.frees==0);++f.finishes;
    // The borrowed ring must still exist until this joining callback returns.
    assert(f.ring&&f.ring_bytes==P4_AOQ_PLAY_RING_BYTES);
    if(!error)assert(f.seals==1&&remaining(ctx)==0&&f.played==f.bytes/2);
    volatile int16_t last=((int16_t*)f.ring)[0];(void)last;
    if(f.cancel_in_join)p4_aoq_tts_play_cancel();
    f.now+=f.join_delay;f.active=false;f.joined=true;
    if(f.short_after_join&&f.played)--f.played;
    return error?error:f.finish_error;
}
static bool cancelled(void* ctx){assert(ctx==&f);return f.external_cancel;}
static uint64_t now(void* ctx){assert(ctx==&f);return f.now;}
static void wait(void* ctx,unsigned ms){
    assert(ctx==&f&&ms==5);f.now+=ms;++f.waits;
    if(f.cancel_wait&&f.waits==f.cancel_wait)p4_aoq_tts_play_cancel();
    if(f.stall)return;
    // Ring consumption leads DAC playback by 100ms. Completion cannot use
    // remaining()==0 alone, and must continue waiting before finish(OK).
    size_t count=24*ms;if(count>f.written-f.read)count=f.written-f.read;f.read+=count;
    const uint64_t elapsed=f.now-f.start;
    const size_t dac=elapsed>100?(size_t)((elapsed-100)*24):0;
    f.played=dac<f.read?dac:f.read;
}
static p4_aoq_play_ops_t operations={&f,open_stream,space,write_stream,remaining,played,seal,finish,cancelled,now,wait};
static void reset(void){memset(&f,0,sizeof(f));f.now=1000;f.bytes=48000;f.ops=&operations;}
static void released(unsigned successful_allocations){assert(!f.active&&f.frees==successful_allocations);++cases;}
static int separate_cache(uint64_t generation,void* p,size_t capacity,size_t* written){
    if(generation!=90){*written=0;return 23;}
    return p4_aoq_tts_trial_copy_pcm(42,p,capacity,written);
}
int main(void){
    p4_aoq_play_report_t r;
    reset();assert(p4_aoq_tts_play(42,&operations,&r)==0&&r.complete&&r.joined&&r.sealed);
    assert(r.accepted_samples==24000&&r.written_samples==24000&&r.played_samples==24000&&!r.remaining_samples);
    assert(f.finishes==1&&r.playback_ms>=1100&&r.elapsed_ms==r.playback_ms);released(2);
    reset();f.reenter=true;assert(!p4_aoq_tts_play(42,&operations,&r)&&f.copy_calls==1);released(2);
    reset();f.bytes=P4_AOQ_PLAY_PCM_MAX_BYTES;assert(!p4_aoq_tts_play(42,&operations,&r)&&r.accepted_samples==262144&&r.played_samples==262144);released(2);
    reset();assert(p4_aoq_tts_play(43,&operations,&r)==14&&!r.cache_copied&&!f.opens&&!f.finishes);released(1);
    reset();f.copy_error=3;assert(p4_aoq_tts_play(42,&operations,&r)==3&&!f.opens);released(1);
    reset();f.bytes=3;assert(p4_aoq_tts_play(42,&operations,&r)==7&&!f.opens);released(1);
    reset();f.bytes=0;assert(p4_aoq_tts_play(42,&operations,&r)==7&&!f.opens);released(1);
    reset();f.bytes=P4_AOQ_PLAY_PCM_MAX_BYTES+2;assert(p4_aoq_tts_play(42,&operations,&r)==7&&!f.opens);released(1);
    reset();f.fail_allocation=1;assert(p4_aoq_tts_play(42,&operations,&r)==19&&!f.opens);released(0);
    reset();f.fail_allocation=2;assert(p4_aoq_tts_play(42,&operations,&r)==19&&!f.opens);released(1);
    reset();f.open_error=3;assert(p4_aoq_tts_play(42,&operations,&r)==3&&!r.stream_opened&&!r.joined&&!f.finishes);released(2);
    reset();f.error_write_number=2;f.write_error=8;assert(p4_aoq_tts_play(42,&operations,&r)==8&&r.joined&&!r.complete&&r.written_samples==960&&f.finishes==1);released(2);
    reset();f.external_cancel=true;assert(p4_aoq_tts_play(42,&operations,&r)==4&&r.cancelled&&!r.joined&&!f.copy_calls);released(0);
    reset();f.cancel_wait=3;assert(p4_aoq_tts_play(42,&operations,&r)==4&&r.cancelled&&r.joined&&!r.complete&&f.finishes==1);released(2);
    reset();f.stall=true;assert(p4_aoq_tts_play(42,&operations,&r)==15&&r.timed_out&&r.joined&&!r.written_samples&&r.playback_ms==5000);released(2);
    reset();f.finish_error=8;assert(p4_aoq_tts_play(42,&operations,&r)==8&&r.joined&&!r.complete);released(2);
    reset();f.short_after_join=true;assert(p4_aoq_tts_play(42,&operations,&r)==7&&r.joined&&!r.complete&&r.played_samples==23999);released(2);
    reset();f.cancel_in_join=true;assert(p4_aoq_tts_play(42,&operations,&r)==4&&r.joined&&r.cancelled);released(2);
    reset();f.counts_invalid=true;assert(p4_aoq_tts_play(42,&operations,&r)==7&&r.joined&&!r.complete);released(2);
    reset();f.join_delay=10000;assert(!p4_aoq_tts_play(42,&operations,&r)&&r.complete&&r.join_ms==10000&&r.elapsed_ms>5000);released(2);
    reset();assert(p4_aoq_tts_play(0,&operations,&r)==1&&!f.allocations);
    p4_aoq_play_ops_t bad=operations;bad.finish=NULL;assert(p4_aoq_tts_play(42,&bad,&r)==1&&!f.allocations);
    assert(p4_aoq_tts_play(42,&operations,NULL)==1&&!f.allocations);++cases;
    reset();assert(!p4_aoq_cached_play(separate_cache,90,&operations,&r)&&r.complete&&r.generation==90&&f.copy_calls==1);released(2);
    reset();assert(p4_aoq_cached_play(separate_cache,42,&operations,&r)==23&&!f.opens&&!f.copy_calls);released(1);
    reset();assert(p4_aoq_cached_play(NULL,90,&operations,&r)==1&&!f.allocations);++cases;
    printf("PASS %u playback bridge lifecycle cases; real bridge, mocked cache/allocator/audio/clock; no hardware/cloud\n",cases);
}
