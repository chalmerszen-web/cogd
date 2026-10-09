#define _POSIX_C_SOURCE 200809L
#include "voice_stream.h"
#include "audio_board.h"
#include "qianwen.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_t worker;
static void (*entry)(void *);
static void *argument;
static bool task_exists,task_fail,speaker;
static unsigned opens,closes,tasks;
static agent_err_t begin_error;
static atomic_bool cancelled;
static atomic_bool hold_begin,begin_entered,begin_finished,producer_done,music_busy;
static atomic_uint clock_advance;
static atomic_uint http_releases;
static atomic_uint http_forgets;
static atomic_bool http_reader_active,hold_finish;
static pthread_t owner_thread;
static agent_err_t producer_result;
static char text[2049],scratch[18425],spoken[2049];
static size_t spoken_size;
static agent_pcm_sink_t sink;

uint64_t esp_agent_now(void)
{ struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000+atomic_load(&clock_advance); }
uint32_t esp_random(void) { return 1234; }
void esp_agent_http_release(void)
{
    assert(pthread_equal(pthread_self(),owner_thread));
    assert(!atomic_load(&http_reader_active));
    atomic_fetch_add(&http_releases,1);
}
void esp_agent_http_forget(void)
{
    /* Same producer ownership gate, separately count full disposal. */
    esp_agent_http_release();
    atomic_fetch_add(&http_forgets,1);
}
static void *run(void *unused) { (void)unused;entry(argument);return NULL; }
int xTaskCreate(void (*function)(void *),const char *name,unsigned stack,void *ctx,unsigned priority,void *handle)
{
    (void)name;(void)handle;assert(stack==7168 && priority==3 && !task_exists);
    if(task_fail)return 0;
    entry=function;argument=ctx;task_exists=true;++tasks;
    assert(!pthread_create(&worker,NULL,run,NULL));return 1;
}
void vTaskDelay(unsigned ms)
{ struct timespec t={ms/1000,(long)(ms%1000)*1000000};nanosleep(&t,NULL); }
void vTaskDelete(void *task) { assert(!task);pthread_exit(NULL); }
unsigned uxTaskGetStackHighWaterMark(void *task) { assert(!task);return 4096; }
static void join(void) { if(task_exists) {assert(!pthread_join(worker,NULL));task_exists=false;} }
static agent_err_t inspect(void *ctx,agent_audio_state_t *state)
{ (void)ctx;memset(state,0,sizeof(*state));state->playing=atomic_load(&music_busy);return AGENT_OK; }
const agent_audio_ops_t esp_hi_audio_ops={.inspect=inspect};
agent_err_t esp_hi_stream_open(unsigned rate,void *memory,size_t bytes)
{
    assert(!speaker && rate==24000 && memory==scratch+AGENT_QWEN_SCRATCH);
    assert(bytes==((sizeof(scratch)-AGENT_QWEN_SCRATCH)&~(size_t)1));speaker=true;++opens;return AGENT_OK;
}
agent_err_t esp_hi_stream_write(const int16_t *pcm,size_t n,const atomic_bool *flag)
{ assert(speaker && pcm && n);return atomic_load(flag)?AGENT_ERR_CANCELLED:AGENT_OK; }
agent_err_t esp_hi_stream_finish(agent_err_t error)
{ assert(speaker);speaker=false;++closes;return error; }
const agent_ws_ops_t esp_agent_speech_ws={0};
void agent_qwen_init(agent_qwen_t *q,agent_speech_t *s,const agent_ws_ops_t *ws)
{ memset(q,0,sizeof(*q));q->speech=s;q->ws=ws;s->provider_state=q; }
agent_err_t agent_qwen_poll(agent_qwen_t *q,unsigned ms)
{ vTaskDelay(ms);return atomic_load(q->speech->cancelled)?AGENT_ERR_CANCELLED:AGENT_OK; }
static agent_err_t begin(agent_speech_t *s,const char *voice,const agent_pcm_sink_t *out)
{
    assert(s->scratch==scratch && s->capacity==8192 && !strcmp(voice,"test"));sink=*out;
    atomic_store(&begin_entered,true);
    while(atomic_load(&hold_begin)) {
        if(atomic_load(s->cancelled))return AGENT_ERR_CANCELLED;
        vTaskDelay(1);
    }
    atomic_store(&begin_finished,true);return begin_error;
}
static agent_err_t feed(agent_speech_t *s,const char *data,size_t n)
{
    if(atomic_load(s->cancelled))return AGENT_ERR_CANCELLED;
    assert(n && spoken_size+n<sizeof(spoken));memcpy(spoken+spoken_size,data,n);spoken_size+=n;
    spoken[spoken_size]=0;return AGENT_OK;
}
static agent_err_t finish(agent_speech_t *s)
{
    while(atomic_load(&hold_finish) && !atomic_load(&http_releases) && !atomic_load(s->cancelled))
        vTaskDelay(1);
    if(atomic_load(s->cancelled))return AGENT_ERR_CANCELLED;
    const int16_t samples[]={3,2,1};
    agent_err_t error=sink.open(sink.ctx,24000);
    return error?error:sink.write(sink.ctx,samples,3);
}
static void cancel(agent_speech_t *s) { (void)s; }
const agent_tts_ops_t agent_qianwen_tts={.begin=begin,.feed_text=feed,.finish=finish,.cancel=cancel};
static void prepare(void)
{
    join();assert(!speaker);atomic_store(&cancelled,false);
    task_fail=false;begin_error=AGENT_OK;spoken_size=0;spoken[0]=0;
    atomic_store(&hold_begin,false);atomic_store(&begin_entered,false);
    atomic_store(&begin_finished,false);atomic_store(&producer_done,false);
    atomic_store(&music_busy,false);atomic_store(&clock_advance,0);
    atomic_store(&http_reader_active,false);atomic_store(&hold_finish,false);
    esp_agent_voice_stream_prepare(&cancelled,"test",text,sizeof(text),scratch,sizeof(scratch),NULL);
}
static void *producer(void *unused)
{
    (void)unused;producer_result=esp_agent_voice_stream_begin(NULL);
    atomic_store(&producer_done,true);return NULL;
}
static void entered(void)
{
    uint64_t until=esp_agent_now()+2000;
    while(!atomic_load(&begin_entered) && esp_agent_now()<until)vTaskDelay(1);
    assert(atomic_load(&begin_entered));
}
int main(void)
{
    owner_thread=pthread_self();
    prepare();assert(!esp_agent_voice_stream_begin(NULL));
    assert(!esp_agent_voice_stream_begin(NULL) && tasks==1);
    const char ack[]="嗯，让我想想这盏灯的名字。";
    assert(!esp_agent_voice_stream_write(NULL,ack,strlen(ack)));
    assert(!esp_agent_voice_stream_seal());
    /* The producer can return to planning; EOF lets the consumer complete
     * without waiting for the later engine join. */
    join();assert(opens==1 && closes==1 && !strcmp(spoken,ack));
    assert(esp_agent_voice_stream_write(NULL,"late",4)==AGENT_ERR_PROTOCOL);
    assert(!esp_agent_voice_stream_end(NULL,AGENT_OK));
    prepare();assert(!esp_agent_voice_stream_begin(NULL));
    assert(!esp_agent_voice_stream_write(NULL,"原来",6));
    assert(!esp_agent_voice_stream_write(NULL,"叫小星星。",15));
    assert(!esp_agent_voice_stream_end(NULL,AGENT_OK));join();
    assert(!strcmp(spoken,"原来叫小星星。") && opens==2 && closes==2 && tasks==2);
    prepare();task_fail=true;assert(esp_agent_voice_stream_begin(NULL)==AGENT_ERR_MEMORY);
    assert(esp_agent_voice_stream_end(NULL,AGENT_ERR_MEMORY)==AGENT_ERR_MEMORY);
    assert(esp_agent_voice_stream_seal()==AGENT_ERR_PROTOCOL && !task_exists);
    prepare();begin_error=AGENT_ERR_NETWORK;assert(esp_agent_voice_stream_begin(NULL)==AGENT_ERR_NETWORK);
    assert(esp_agent_voice_stream_end(NULL,AGENT_OK)==AGENT_ERR_NETWORK);join();assert(atomic_load(&cancelled));
    prepare();assert(!esp_agent_voice_stream_begin(NULL));
    assert(!esp_agent_voice_stream_write(NULL,ack,strlen(ack)));assert(!esp_agent_voice_stream_seal());
    assert(esp_agent_voice_stream_end(NULL,AGENT_ERR_NETWORK)==AGENT_ERR_NETWORK);join();
    prepare();atomic_store(&cancelled,true);assert(esp_agent_voice_stream_begin(NULL)==AGENT_ERR_CANCELLED);
    assert(esp_agent_voice_stream_end(NULL,AGENT_ERR_CANCELLED)==AGENT_ERR_CANCELLED && !task_exists);
    /* An in-flight provider request can proceed remotely, but its owning
     * producer must not start local HTTP reads before TTS becomes ready. */
    prepare();atomic_store(&hold_begin,true);pthread_t sender;
    assert(!pthread_create(&sender,NULL,producer,NULL));entered();
    assert(!atomic_load(&producer_done) && !atomic_load(&begin_finished));
    atomic_store(&hold_begin,false);assert(!pthread_join(sender,NULL));
    assert(!producer_result && atomic_load(&begin_finished));
    assert(!esp_agent_voice_stream_write(NULL,ack,strlen(ack)));
    assert(!esp_agent_voice_stream_end(NULL,AGENT_OK));join();
    assert(!strcmp(spoken,ack));
    /* Cancellation/timeout propagate before parsing a response and retain
     * ownership until end joins the worker, closing its reserved speaker. */
    for(unsigned timeout=0;timeout<2;++timeout) {
        prepare();atomic_store(&hold_begin,true);
        assert(!pthread_create(&sender,NULL,producer,NULL));entered();
        if(timeout)atomic_store(&clock_advance,20000);else atomic_store(&cancelled,true);
        assert(!pthread_join(sender,NULL));
        assert(producer_result==(timeout?AGENT_ERR_TIMEOUT:AGENT_ERR_CANCELLED));
        assert(esp_agent_voice_stream_end(NULL,producer_result)==producer_result);join();
        assert(!speaker && atomic_load(&cancelled));
    }
    /* An existing song retains the prior asynchronous path, without holding
     * the HTTP reader for the song's duration or stealing its speaker. */
    prepare();atomic_store(&music_busy,true);
    assert(!esp_agent_voice_stream_begin(NULL));
    assert(!atomic_load(&begin_entered) && !speaker);
    assert(!esp_agent_voice_stream_write(NULL,ack,strlen(ack)));
    atomic_store(&music_busy,false);
    assert(!esp_agent_voice_stream_end(NULL,AGENT_OK));join();assert(!strcmp(spoken,ack));
    assert(opens==closes && !speaker);
    /* Progress may end inside HTTP feed, so its end must never close HTTP. */
    assert(!atomic_load(&http_releases));
    prepare();atomic_store(&http_reader_active,true);
    assert(!esp_agent_voice_stream_begin(NULL));
    assert(!esp_agent_voice_stream_write(NULL,ack,strlen(ack)));
    assert(!esp_agent_voice_stream_end(NULL,AGENT_OK));join();
    assert(!atomic_load(&http_releases));
    atomic_store(&http_reader_active,false);
    /* The final producer has returned. A successful drain cannot finish
     * until HTTP has been released in the ON build, catching close-after-join. */
    for(unsigned failure=0;failure<3;++failure) {
        prepare();atomic_store(&http_releases,0);
        atomic_store(&http_forgets,0);
        assert(!esp_agent_voice_stream_begin(NULL));
        assert(!esp_agent_voice_stream_write(NULL,ack,strlen(ack)));
#if AGENT_VOICE_HTTP_RELEASE
        atomic_store(&hold_finish,true);
#endif
        agent_err_t result=failure==1?AGENT_ERR_CANCELLED:failure==2?AGENT_ERR_NETWORK:AGENT_OK;
        assert(esp_agent_voice_answer_end(NULL,result)==result);join();
#if AGENT_VOICE_HTTP_RELEASE
        assert(atomic_load(&http_releases)==1);
#else
        assert(!atomic_load(&http_releases));
#endif
#if AGENT_VOICE_HTTP_FORGET
        assert(atomic_load(&http_forgets)==1);
#else
        assert(!atomic_load(&http_forgets));
#endif
        unsigned released=atomic_load(&http_releases);
        unsigned forgotten=atomic_load(&http_forgets);
        assert(esp_agent_voice_answer_end(NULL,result)==result);
        assert(atomic_load(&http_releases)==released && !speaker);
        assert(atomic_load(&http_forgets)==forgotten);
    }
    prepare();task_fail=true;unsigned released=atomic_load(&http_releases);
    assert(esp_agent_voice_stream_begin(NULL)==AGENT_ERR_MEMORY);
    assert(esp_agent_voice_answer_end(NULL,AGENT_ERR_MEMORY)==AGENT_ERR_MEMORY);
    assert(atomic_load(&http_releases)==released && !task_exists);
    puts("voice_stream: bounded startup before HTTP reads, cancellation/timeout, speaker deferral and joined ownership PASS");
    puts("voice_stream: final-only HTTP release, close-before-drain, progress isolation, errors and repeated end PASS");
}
