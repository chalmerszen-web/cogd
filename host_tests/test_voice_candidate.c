#define _POSIX_C_SOURCE 200809L
#include "voice_candidate.h"
#include "audio_board.h"
#include "realtime.h"
#include "draft.h"
#include "intent.h"
#include "freertos/task.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#if AGENT_TLS_COOPERATE
/* TLS/crypto are covered by the actual SDK-adapter tests. These stubs keep
 * this pthread test about candidate/ASR ownership and task admission. */
void esp_agent_tls_cooperate_stats(unsigned *steps,unsigned *maximum)
{ *steps=0;*maximum=0; }
#endif

static agent_engine_storage_t engine_workspace;
static agent_engine_t engine={AGENT_ENGINE_STORAGE_INIT(engine_workspace)};
enum { CAPTURE_BYTES=ESP_AGENT_CANDIDATE_PREFIX_BYTES+3000+8192 };
static uint8_t *capture_arena;
static bool reuse_requested;
bool esp_agent_voice_reuse_enabled(void) {return reuse_requested;}
static void *cache_pages[24];
static atomic_uint cache_count;
static unsigned cache_peak_pages;
static agent_core_t core;
static atomic_bool suspended,worker_live,opened,generation_closed;
static atomic_uint clock_advance,open_delay;
static pthread_t thread;
static void (*task_entry)(void *);
static void *task_ctx;
static unsigned stage,sends,opens,closes,creates,items,streams,writes,seals,finishes,persists,assistant_turns,reuses;
static unsigned fault;
static unsigned sent_nonce;
static unsigned local_calls;
static unsigned cache_begins,generated,progresses;
static bool spool_active,spool_sealed;
static const atomic_bool *spool_cancel;
static uint64_t first_write_at;
static agent_err_t local_error;
static atomic_bool asr_live;
static unsigned asr_warms,asr_opens,asr_closes,asr_delay;
static bool asr_fault;
static int asr_context;
agent_err_t esp_agent_asr_ws_warm(const atomic_bool *flag)
{
    assert(atomic_load(&worker_live) && !atomic_load(&asr_live));++asr_warms;
    vTaskDelay(asr_delay);
    if(atomic_load(flag))return AGENT_ERR_CANCELLED;
    if(asr_fault)return AGENT_ERR_NETWORK;
    atomic_store(&asr_live,true);return AGENT_OK;
}
static agent_err_t asr_open(void *ctx,const atomic_bool *flag)
{
    assert(ctx==&asr_context && !atomic_load(&asr_live));++asr_opens;
    if(atomic_load(flag))return AGENT_ERR_CANCELLED;
    atomic_store(&asr_live,true);return AGENT_OK;
}
static void asr_close(void *ctx)
{
    assert(ctx==&asr_context && atomic_load(&asr_live));
    atomic_store(&asr_live,false);++asr_closes;
}
const agent_ws_ops_t esp_agent_asr_ws={.open=asr_open,.close=asr_close,.ctx=&asr_context};
agent_err_t esp_agent_voice_fast_light(agent_engine_t *e,const char *text,
    void (*notify)(void *,const char *,const char *))
{
    uint8_t rgb[3];
    assert(e->workspace && !atomic_load(&worker_live) && !atomic_load(&opened));
    assert(!atomic_load(&cache_count) && !streams && !persists && notify);
    assert(agent_speech_light_literal(text,rgb));++local_calls;
    return atomic_load(&e->core->cancelled)?AGENT_ERR_CANCELLED:local_error;
}
static unsigned response_delay,tail_delay,tail_chunks,final_candidates,streamed_samples;
static unsigned http_warms,spool_delay,warm_delay;
static agent_err_t http_warm_error;
#if AGENT_HANDOFF_PROBE
void esp_agent_speech_ws_probe(char *out,size_t capacity)
{ assert(!atomic_load(&worker_live));snprintf(out,capacity,"{}"); }
bool esp_agent_speech_ws_tcp_probe(unsigned index,unsigned side,char *out,size_t capacity)
{ (void)index;(void)side;(void)out;(void)capacity;assert(!atomic_load(&worker_live));return false; }
#endif
agent_err_t esp_agent_http_warm(const atomic_bool *stop)
{
    assert(stop==&core.cancelled && final_candidates==1 && streams==1 && !engine.workspace);
    assert(!local_calls && !persists && !engine.gateway);
    assert(spool_active && first_write_at && !atomic_load(&cache_count));
    ++http_warms;
    vTaskDelay(warm_delay);
    return atomic_load(stop)?AGENT_ERR_CANCELLED:http_warm_error;
}
static uint64_t response_at,cancel_at,tail_at;
static bool live_stream;
static bool too_small,started,configuration,submitted,responding,task_failed,transcript_ended;
static char config[2048],input[512],wire[4096],reply[97];
#if AGENT_WS_CONNECT_TRACE
static unsigned tls_stamp;
void esp_agent_ws_connect_clock(unsigned *begin,unsigned *end)
{ *begin=*end=tls_stamp; }
#if AGENT_TLS_PHASE_TRACE
#include "tls_phase_clock.h"
const unsigned *esp_agent_tls_phase_clock(void)
{
    static unsigned clocks[TLS_PHASE_COUNT];
    assert(!atomic_load(&worker_live) && (!atomic_load(&opened) || reuse_requested));
    for(unsigned i=0;i<4;++i)clocks[i]=tls_stamp;
    return clocks;
}
unsigned esp_agent_tls_verify_count(bool *overflow)
{ assert(!atomic_load(&worker_live));*overflow=false;return 1; }
const tls_verify_detail_t *esp_agent_tls_verify_detail(unsigned row)
{ static const tls_verify_detail_t detail={.key_bits=4096,.wall_ms=17};assert(!row);return &detail; }
#endif
#endif
static size_t at;
static uint8_t sentinel[1024];
uint64_t esp_agent_now(void)
{ struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000+atomic_load(&clock_advance); }
void vTaskDelay(unsigned ms)
{ struct timespec t={.tv_sec=ms/1000,.tv_nsec=(long)(ms%1000)*1000000};nanosleep(&t,NULL); }
unsigned esp_get_free_heap_size(void) {return 65536;}
unsigned uxTaskGetStackHighWaterMark(void *p) {(void)p;return 1024;}
static void *entry(void *unused) {(void)unused;task_entry(task_ctx);assert(false);return NULL;}
TaskHandle_t xTaskCreateStatic(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,StackType_t *mem,StaticTask_t *tcb)
{
    assert(!atomic_load(&worker_live) && stack==6144 && priority==3 && name && mem && tcb);
    if(task_failed)return NULL;
    atomic_store(&worker_live,true);atomic_store(&suspended,false);task_entry=fn;task_ctx=arg;
    assert(!pthread_create(&thread,NULL,entry,NULL));return &thread;
}
void vTaskSuspend(void *p) {(void)p;atomic_store(&suspended,true);pthread_exit(NULL);}
eTaskState eTaskGetState(void *p) {assert(p==&thread);return atomic_load(&suspended)?eSuspended:eRunning;}
void vTaskDelete(void *p)
{ assert(p==&thread && atomic_load(&suspended));assert(!pthread_join(thread,NULL));atomic_store(&worker_live,false); }
agent_err_t esp_hi_voice_live_workspace(void **memory,size_t *capacity)
{
    *memory=capture_arena;*capacity=too_small?8:ESP_AGENT_CANDIDATE_PREFIX_BYTES;
    return AGENT_OK;
}
void *__real_malloc(size_t);
void __real_free(void *);
void *__wrap_malloc(size_t size)
{
    if(size==AGENT_DRAFT_PAGE_BYTES && atomic_load(&worker_live) && stage>=5) {
        assert(atomic_load(&opened));
        unsigned n=atomic_load(&cache_count);
        if(fault==5 || (fault==11 && n==2))return NULL;
        assert(n<24 && !cache_pages[n]);
        void *p=__real_malloc(size);assert(p);cache_pages[n]=p;
        if(n+1>cache_peak_pages)cache_peak_pages=n+1;
        atomic_fetch_add(&cache_count,1);return p;
    }
    return __real_malloc(size);
}
void __wrap_free(void *p)
{
    for(unsigned i=0;p && i<24;++i)if(p==cache_pages[i]) {
        /* Only worker after close or after admitted drain, or joined owner. */
        assert(!atomic_load(&opened) || live_stream || (reuse_requested && !atomic_load(&worker_live)));
        cache_pages[i]=NULL;atomic_fetch_sub(&cache_count,1);break;
    }
    __real_free(p);
}
static agent_err_t restore(bool retain)
{
    if(!retain)esp_agent_voice_candidate_discard();
    assert(!atomic_load(&worker_live) && (!atomic_load(&opened) || retain) && !atomic_load(&cache_count));
    if(retain)assert(esp_agent_voice_candidate_retained());
    assert(!atomic_load(&asr_live));
    if(spool_active)assert(spool_sealed && spool_cancel==&core.cancelled);
    /* Allocate a different arena before freeing the old one. ASan catches
     * any surviving callback/scratch pointer on the following actual turn. */
    uint8_t *next=malloc(CAPTURE_BYTES);assert(next);
    memset(next,0,CAPTURE_BYTES);
    memcpy(next+ESP_AGENT_CANDIDATE_PREFIX_BYTES,capture_arena+ESP_AGENT_CANDIDATE_PREFIX_BYTES,
           CAPTURE_BYTES-ESP_AGENT_CANDIDATE_PREFIX_BYTES);
    memset(capture_arena,0xdd,ESP_AGENT_CANDIDATE_PREFIX_BYTES);
    free(capture_arena);capture_arena=next;
    if(fault==6)return AGENT_ERR_MEMORY;
    return agent_engine_bind_workspace(&engine,&engine_workspace,sizeof(engine_workspace));
}
agent_err_t esp_agent_workspace_restore(void) {return restore(false);}
agent_err_t esp_agent_workspace_restore_retaining_candidate(void) {return restore(true);}
static agent_err_t ws_open(void *ctx,const atomic_bool *cancel)
{
    vTaskDelay(atomic_load(&open_delay));
    (void)ctx;if(fault==1)return AGENT_ERR_TLS;if(atomic_load(cancel))return AGENT_ERR_CANCELLED;
#if AGENT_WS_CONNECT_TRACE
    tls_stamp=(unsigned)esp_agent_now();
#endif
    assert(!atomic_load(&opened));++opens;stage=0;sent_nonce=0;
    configuration=submitted=responding=false;
    atomic_store(&opened,true);return AGENT_OK;
}
static void ws_close(void *ctx)
{ (void)ctx;assert(atomic_exchange(&opened,false));++closes;atomic_store(&generation_closed,true); }
static agent_err_t ws_send(void *ctx,bool binary,char *text,size_t count,const atomic_bool *cancel)
{
    (void)ctx;assert(!binary && !atomic_load(cancel) && atomic_load(&opened));++sends;
    cJSON *r=agent_json_parse(text,count);assert(r);const char *type=agent_json_string(r,"type");
    const char *event_id=agent_json_string(r,"event_id");
    assert(event_id && event_id[0]=='c' && strtoul(event_id+1,NULL,10)>sent_nonce);
    sent_nonce=(unsigned)strtoul(event_id+1,NULL,10);
    if(!strcmp(type,"session.update")) {
        cJSON *session=cJSON_GetObjectItemCaseSensitive(r,"session");
        assert(cJSON_AddStringToObject(session,"model",fault==2?"wrong":AGENT_RT_MODEL));
        char *json=cJSON_PrintUnformatted(session);assert(json && strlen(json)<sizeof(config));
        strcpy(config,json);free(json);configuration=true;
    } else if(!strcmp(type,"conversation.item.create")) {
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(r,"item");
        const char *value=agent_json_string(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(item,"content"),0),"text");
        assert(value && strlen(value)<sizeof(input));strcpy(input,value);submitted=true;++items;
        stage=2;responding=false;transcript_ended=false;
    } else {
        assert(!strcmp(type,"response.create") && !atomic_load(&cache_count));
        assert(stage==2 && submitted); /* No intervening acknowledgement read. */
        responding=true;++creates;response_at=esp_agent_now()+response_delay;
    }
    cJSON_Delete(r);return AGENT_OK;
}
static agent_err_t ws_read(void *ctx,char *data,size_t capacity,agent_ws_chunk_t *part,unsigned timeout,const atomic_bool *cancel)
{
    (void)ctx;if(atomic_load(cancel))return AGENT_ERR_CANCELLED;
    if(cancel_at && esp_agent_now()>=cancel_at) {atomic_store(&core.cancelled,true);cancel_at=0;}
    if(responding && esp_agent_now()<response_at) {vTaskDelay(timeout?timeout:1);return AGENT_OK;}
    if(stage==6 && tail_at && esp_agent_now()<tail_at) {vTaskDelay(timeout?timeout:1);return AGENT_OK;}
    if(!wire[0]) {
        unsigned id=(fault==14 || fault==15)?1:creates;
        if(stage==0)strcpy(wire,"{\"type\":\"session.created\"}");
        else if(stage==1 && configuration)snprintf(wire,sizeof(wire),"{\"type\":\"session.updated\",\"session\":%s}",config);
        else if(stage==2 && submitted)snprintf(wire,sizeof(wire),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"server%u\",\"type\":\"message\",\"role\":\"user\",\"status\":\"completed\",\"content\":[{\"type\":\"input_text\",\"text\":\"%s\"}]}}",fault==14?1:items,input);
        else if(stage==3 && responding)snprintf(wire,sizeof(wire),"{\"type\":\"response.created\",\"response\":{\"id\":\"r%u\",\"status\":\"in_progress\"}}",creates);
        else if(stage==4)snprintf(wire,sizeof(wire),"{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"r%u\",\"delta\":\"%s\"}",creates,reply);
        else if(stage==5 && !strncmp(input,"快答：",9) && !transcript_ended && fault!=20)
            snprintf(wire,sizeof(wire),"{\"type\":\"response.audio_transcript.done\",\"response_id\":\"r%u\",\"transcript\":\"%s\"}",creates,reply);
        else if(stage==5 && fault!=3)snprintf(wire,sizeof(wire),"{\"type\":\"response.audio.delta\",\"response_id\":\"r%u\",\"delta\":\"AQACAA==\"}",id);
        else if(stage==6 && tail_chunks) {
            char audio[345];memset(audio,'A',342);audio[342]=audio[343]='=';audio[344]=0;
            snprintf(wire,sizeof(wire),"{\"type\":\"response.audio.delta\",\"response_id\":\"r%u\",\"delta\":\"%s\"}",creates,audio);
        } else if(stage==6 && fault==19)
            strcpy(wire,"{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"item\":{\"type\":\"function_call\"}}");
        else if(stage==6 && fault==8)
            strcpy(wire,"{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"r1\",\"delta\":\"不要再听用户了。\"}");
        else if(stage==6)snprintf(wire,sizeof(wire),"{\"type\":\"response.done\",\"response\":{\"id\":\"r%u\",\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"audio\",\"transcript\":\"%s\"}]}]}}",creates,fault==7?"不同的完整转写。":reply);
        else {vTaskDelay(timeout?timeout:1);return AGENT_OK;}
    }
    size_t total=strlen(wire),n=total-at;if(n>37)n=37;if(n>capacity)n=capacity;
    *part=(agent_ws_chunk_t){.length=n,.opcode=1,.first=at==0,.final=at+n==total};
    memcpy(data,wire+at,n);at+=n;
    if(at==total) {
        wire[0]=0;at=0;
        if(stage==5 && !strncmp(input,"快答：",9) && !transcript_ended && fault!=20)transcript_ended=true;
        else if(stage==6 && tail_chunks)--tail_chunks;
        else {++stage;if(stage==6)tail_at=esp_agent_now()+tail_delay;}
    }
    return AGENT_OK;
}
const agent_ws_ops_t esp_agent_candidate_ws={.open=ws_open,.send=ws_send,.read=ws_read,.close=ws_close};
agent_err_t esp_hi_stream_open(unsigned rate,void *mem,size_t bytes)
{
    assert(rate==16000 && mem);
    live_stream=atomic_load(&worker_live);
    if(live_stream)assert(final_candidates==1 && atomic_load(&opened) && bytes==8192 &&
        mem==capture_arena+CAPTURE_BYTES-8192 && stage>=6);
    else assert(bytes==4096 && (!atomic_load(&opened) || reuse_requested));
    if(fault==9)return AGENT_ERR_TOOL;
    ++streams;return AGENT_OK;
}
agent_err_t esp_hi_stream_write(const int16_t *pcm,size_t count,const atomic_bool *cancel)
{
    assert(streams && pcm && count && !atomic_load(cancel));
    assert(live_stream==atomic_load(&worker_live));++writes;streamed_samples+=(unsigned)count;
    if(!first_write_at)first_write_at=esp_agent_now();
    if(spool_active)assert(!spool_sealed && spool_cancel==&core.cancelled);
    if(fault==10) {atomic_store(&core.cancelled,true);vTaskDelay(10);return AGENT_ERR_CANCELLED;}
    return fault==4?AGENT_ERR_TOOL:AGENT_OK;
}
agent_err_t esp_hi_stream_cache_begin(const atomic_bool *cancel,bool deferred)
{
    assert(cancel==&core.cancelled && !atomic_load(cancel) && !deferred && !spool_active);
    ++cache_begins;if(fault==16)return AGENT_ERR_FULL;
    vTaskDelay(spool_delay);
    spool_active=true;spool_cancel=cancel;return AGENT_OK;
}
agent_err_t esp_hi_stream_cache_seal(void)
{
    assert(!atomic_load(&worker_live) && spool_active && !spool_sealed);++seals;
    if(fault==17)return AGENT_ERR_FULL;
    spool_sealed=true;return AGENT_OK;
}
agent_err_t esp_hi_stream_finish(agent_err_t result)
{
    assert(!atomic_load(&worker_live) && (!atomic_load(&opened) || reuse_requested));++finishes;
    spool_active=spool_sealed=false;spool_cancel=NULL;
    return !result && fault==18?AGENT_ERR_TOOL:result;
}
agent_err_t __wrap_agent_context_emit(agent_context_t *ctx,const char *kind,const char *role,const char *content,char *id,size_t size)
{
    (void)ctx;(void)role;(void)id;(void)size;assert(!atomic_load(&worker_live) && (!atomic_load(&opened) || reuse_requested));
    assert(kind && content);++persists;if(!strcmp(kind,"turn"))++assistant_turns;
    cJSON *record=agent_json_parse(content,strlen(content));assert(record);
    if(!strcmp(kind,"error")) {
        assert(!strcmp(agent_json_string(record,"route"),"text_candidate"));
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(record,"partial")));
        assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(record,"effects")));
    }
    cJSON_Delete(record);
    if(fault==12 && !strcmp(kind,"message"))return AGENT_ERR_FULL;
    if(fault==13 && !strcmp(kind,"turn"))return AGENT_ERR_CORRUPT;
    return AGENT_OK;
}
static void notify(void *ctx,const char *event,const char *text)
{
    (void)ctx;
#if AGENT_TLS_PHASE_TRACE
    if(!strcmp(event,"candidate_tls")) {
        assert(!atomic_load(&worker_live) && (!atomic_load(&opened) || reuse_requested) && tls_stamp && fault!=1);
        cJSON *r=agent_json_parse(text,strlen(text));
        assert(cJSON_IsArray(r) && cJSON_GetArraySize(r)==TLS_PHASE_COUNT);
        assert(cJSON_GetArrayItem(r,TLS_CONFIG_BEGIN)->valuedouble==tls_stamp);
        cJSON_Delete(r);
    }
    if(!strcmp(event,"candidate_tls_verify")) {
        assert(!atomic_load(&worker_live));cJSON *r=agent_json_parse(text,strlen(text));
        assert(r && cJSON_GetObjectItemCaseSensitive(r,"bits")->valueint==4096);
        assert(cJSON_GetObjectItemCaseSensitive(r,"wall_ms")->valueint==17);cJSON_Delete(r);
    }
#endif
    if(!strcmp(event,"candidate_begin"))started=true;
    if(!strcmp(event,"candidate_final"))++final_candidates;
    if(!strcmp(event,"candidate_play") && atomic_load(&worker_live))assert(final_candidates==1);
    if(!strcmp(event,"progress_generated")) {assert(text && !strcmp(text,reply));++generated;}
    if(!strcmp(event,"progress_start")) {assert(generated==1 && text && !strcmp(text,reply));++progresses;}
    if(!strcmp(event,"candidate_stats")) {
        assert(!atomic_load(&worker_live) && (!atomic_load(&opened) || reuse_requested));
        cJSON *r=agent_json_parse(text,strlen(text));assert(r);
        bool reused=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"reused"));
        if(reused) {assert(reuse_requested);++reuses;}
        const cJSON *begin=cJSON_GetObjectItemCaseSensitive(r,"started_ms");
        const cJSON *open=cJSON_GetObjectItemCaseSensitive(r,"opened_ms");
        const cJSON *created=cJSON_GetObjectItemCaseSensitive(r,"created_ms");
        const cJSON *ready=cJSON_GetObjectItemCaseSensitive(r,"ready_ms");
        assert(cJSON_IsNumber(begin) && cJSON_IsNumber(open) && cJSON_IsNumber(ready));
        if(open->valuedouble)assert(open->valuedouble>=begin->valuedouble);
        if(fault==1 || task_failed)assert(!open->valuedouble);
        if(ready->valuedouble)assert((reused || open->valuedouble) && ready->valuedouble>=open->valuedouble);
        const cJSON *cache=cJSON_GetObjectItemCaseSensitive(r,"cache_peak");
        assert(cJSON_IsNumber(cache) && cache->valuedouble==cache_peak_pages*AGENT_DRAFT_PAGE_BYTES);
#if AGENT_WS_CONNECT_TRACE
        assert(cJSON_IsNumber(created));
        if(created->valuedouble)assert(open->valuedouble && created->valuedouble>=open->valuedouble);
        if(ready->valuedouble)assert((reused || created->valuedouble) && ready->valuedouble>=created->valuedouble);
        const cJSON *tls_begin=cJSON_GetObjectItemCaseSensitive(r,"tls_begin_ms");
        const cJSON *tls_end=cJSON_GetObjectItemCaseSensitive(r,"tls_end_ms");
        assert(cJSON_IsNumber(tls_begin) && cJSON_IsNumber(tls_end));
        if(open->valuedouble) {
            assert(tls_begin->valuedouble>=begin->valuedouble);
            assert(tls_end->valuedouble>=tls_begin->valuedouble && tls_end->valuedouble<=open->valuedouble);
        } else assert(!tls_begin->valuedouble && !tls_end->valuedouble);
#else
        assert(!created);
        assert(!cJSON_GetObjectItemCaseSensitive(r,"tls_begin_ms"));
#endif
        cJSON_Delete(r);
    }
}
static void init(void)
{
    assert(!atomic_load(&worker_live) && !atomic_load(&opened) && !atomic_load(&cache_count));
    assert(!spool_active && !spool_cancel);
    assert(!atomic_load(&asr_live));asr_warms=asr_opens=asr_closes=asr_delay=0;asr_fault=false;
    assert(!esp_agent_voice_candidate_idle_state());
    atomic_store(&clock_advance,0);atomic_store(&open_delay,0);
    memset(&engine,0,sizeof(engine)); assert(!agent_engine_bind_workspace(&engine,&engine_workspace,sizeof(engine_workspace))); memset(&engine_workspace,0,sizeof(engine_workspace));memset(&core,0,sizeof(core));atomic_init(&core.cancelled,false);engine.core=&core;
    stage=sends=opens=closes=creates=items=streams=writes=seals=finishes=assistant_turns=persists=fault=local_calls=reuses=0;at=0;
    reuse_requested=false;
    local_error=AGENT_OK;
    cache_begins=generated=progresses=0;first_write_at=0;
    response_delay=tail_delay=tail_chunks=final_candidates=streamed_samples=0;response_at=cancel_at=tail_at=0;
    http_warms=0;http_warm_error=AGENT_OK;
    spool_delay=warm_delay=0;
    cache_peak_pages=0;
    atomic_store(&generation_closed,false);live_stream=false;
    configuration=submitted=responding=too_small=started=task_failed=false;
    config[0]=input[0]=wire[0]=0;strcpy(reply,"我是小言。");
    free(capture_arena);capture_arena=malloc(CAPTURE_BYTES);assert(capture_arena);
    memset(capture_arena,0,CAPTURE_BYTES);
    memset(capture_arena+ESP_AGENT_CANDIDATE_PREFIX_BYTES,0xa5,sizeof(sentinel));memset(sentinel,0xa5,sizeof(sentinel));
    assert(!agent_engine_bind_workspace(&engine,NULL,0));
}
static void begin(const char *partial)
{
    esp_agent_voice_candidate_begin(&core.cancelled,notify);assert(started);
    esp_agent_voice_candidate_ready();esp_agent_voice_candidate_revision(1);
    /* Simulate the independent ASR owner claiming, using and closing before
     * final capture admission. No ASR socket survives into candidate finish. */
    assert(!esp_agent_voice_candidate_asr_open(&asr_context,&core.cancelled));
    asr_close(&asr_context);
    esp_agent_voice_candidate_preview(partial);
}
static void preserved(void)
{
    assert(!memcmp(capture_arena+ESP_AGENT_CANDIDATE_PREFIX_BYTES,sentinel,sizeof(sentinel)));
}
static agent_err_t finish(agent_engine_t *e,const char *text,agent_err_t result,bool *handled)
{
    return esp_agent_voice_candidate_finish(e,text,result,
        capture_arena+CAPTURE_BYTES-8192,handled);
}
static void idle_wait(unsigned state)
{
    uint64_t end=esp_agent_now()+2000;
    while(esp_agent_voice_candidate_idle_state()!=state && esp_agent_now()<end)vTaskDelay(2);
    assert(esp_agent_voice_candidate_idle_state()==state);
}
static agent_err_t warm(void)
{
    return esp_agent_voice_candidate_warm(capture_arena,ESP_AGENT_CANDIDATE_PREFIX_BYTES,&core.cancelled,notify);
}
static void worker_stopped(void)
{
    uint64_t end=esp_agent_now()+2500;
    while(!atomic_load(&suspended) && esp_agent_now()<end)vTaskDelay(2);
    assert(atomic_load(&suspended));
}
static void asr_connection_order(void)
{
    bool handled;
    esp_agent_voice_candidate_asr_notice(NULL);
    esp_agent_voice_candidate_asr_notice("asr_connected");
    assert(!atomic_load(&worker_live));
    init();
    esp_agent_voice_candidate_begin(&core.cancelled,notify);
    esp_agent_voice_candidate_revision(1);
    esp_agent_voice_candidate_preview("请记住");
    esp_agent_voice_candidate_asr_notice("capture_begin");
    esp_agent_voice_candidate_asr_notice(NULL);
    assert(!atomic_load(&worker_live));
    esp_agent_voice_candidate_asr_notice("asr_connect");
#if AGENT_TLS_COOPERATE
    assert(!atomic_load(&worker_live));
#else
    assert(atomic_load(&worker_live));
#endif
    esp_agent_voice_candidate_asr_notice("asr_connected");
    assert(atomic_load(&worker_live));
    esp_agent_voice_candidate_asr_notice("asr_connected");
    esp_agent_voice_candidate_asr_notice("asr_connect");
    esp_agent_voice_candidate_asr_notice("asr_started");
    assert(finish(&engine,"",AGENT_ERR_NETWORK,&handled)==AGENT_ERR_NETWORK);
    assert(!handled && !atomic_load(&worker_live) && !atomic_load(&opened) && !streams && !persists);
    preserved();
    esp_agent_voice_candidate_asr_notice("asr_connected");
    assert(!atomic_load(&worker_live));
    /* Cancellation before connecting must not start an orphan worker. */
    init();esp_agent_voice_candidate_begin(&core.cancelled,notify);
    atomic_store(&core.cancelled,true);
    esp_agent_voice_candidate_asr_notice("asr_connect");
    esp_agent_voice_candidate_asr_notice("asr_connected");
    assert(!atomic_load(&worker_live));
    assert(finish(&engine,"",AGENT_ERR_CANCELLED,&handled)==AGENT_ERR_CANCELLED);
    assert(!handled && !streams && !persists);preserved();
    /* Failed ASR never publishes connected; its borrower still joins cleanly. */
    init();esp_agent_voice_candidate_begin(&core.cancelled,notify);
    esp_agent_voice_candidate_asr_notice("asr_connect");
    esp_agent_voice_candidate_asr_notice("asr_failed");
#if AGENT_TLS_COOPERATE
    assert(!atomic_load(&worker_live));
#endif
    assert(finish(&engine,"",AGENT_ERR_NETWORK,&handled)==AGENT_ERR_NETWORK);
    assert(!handled && !atomic_load(&worker_live) && !atomic_load(&opened) && !streams && !persists);
    preserved();
}
static void next_capture(void)
{
    final_candidates=cache_peak_pages=0;live_stream=false;
    assert(!agent_engine_bind_workspace(&engine,NULL,0));
}
static void retained_first(void)
{
    bool handled=false;init();reuse_requested=true;begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled) && handled);
    assert(esp_agent_voice_candidate_idle_state()==ESP_AGENT_CANDIDATE_PARKED);
    assert(esp_agent_voice_candidate_retained() && opens==1 && !closes);
    assert(!atomic_load(&worker_live) && !atomic_load(&cache_count) && atomic_load(&opened));
}
static void contextual_receipt(void)
{
    bool handled;
    static const char *preview[]={"我刚给这盏灯","我想知道呢盞燈嘅名字"};
    static const char *final[]={"我刚给这盏灯取的名字是什么？","我想知道呢盞燈嘅名字係乜嘢？"};
    static const char *answer[]={"嗯，这盏灯，我想想。","嗯，呢盞燈，我諗諗先。"};
    for(unsigned language=0;language<2;++language) {
        init();strcpy(reply,answer[language]);response_delay=70;tail_delay=650;tail_chunks=4;
        begin(preview[language]);assert(!streams && !progresses);
        esp_agent_voice_candidate_revision(2);esp_agent_voice_candidate_preview(final[language]);
        assert(!finish(&engine,final[language],AGENT_OK,&handled));
        assert(!handled && streams==1 && live_stream && creates==1 && !persists && !local_calls);
        assert(!strncmp(input,"待续：",9) && !strcmp(input+9,preview[language]) && generated==1 && progresses==1);
        assert(esp_agent_voice_candidate_ack()->pending && spool_cancel==&core.cancelled);
        assert(!esp_agent_voice_candidate_ack_join(AGENT_OK) && finishes==1);preserved();
    }
    /* Neither a factual reply nor a plausible but invented topic is audible.
     * Complete original input, never the preview, still goes to the Agent. */
    const char *rejected[]={"嗯，小星星，我想想。","嗯，天气，我想想。","名字就是星星。",
        "嗯，这盏灯，已经开好了。","嗯，这盏灯，我想想。完成了。"};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i) {
        init();strcpy(reply,rejected[i]);begin(preview[0]);worker_stopped();
#if AGENT_LOCAL_FIRST_PREFETCH
        /* A predicted but well-shaped topic may wait silently. Its content
         * cannot be grounded until final capture; no audio may escape. */
        assert(!streams);
#else
        assert(!streams && !atomic_load(&cache_count));
#endif
        assert(!finish(&engine,final[0],AGENT_OK,&handled));
        assert(!handled && !streams && !persists && creates==1 && closes==1);preserved();
    }
    const char *changed[]={"我刚给另一盏灯取的名字是什么？","我刚给这盏灯，不对，先别回答。",
        "我刚给这盏灯，取消。","我刚给这盏灯，用粤语回答。"};
    for(unsigned i=0;i<sizeof(changed)/sizeof(*changed);++i) {
        init();strcpy(reply,answer[0]);begin(preview[0]);worker_stopped();
        assert(!finish(&engine,changed[i],AGENT_OK,&handled));
        assert(!handled && !streams && !persists && creates==1 && closes==1);preserved();
    }
    init();strcpy(reply,answer[0]);begin(preview[0]);worker_stopped();
    assert(finish(&engine,final[0],AGENT_ERR_TIMEOUT,&handled)==AGENT_ERR_TIMEOUT);
    assert(!handled && !streams && !persists);preserved();
}
static void short_answers(void)
{
    bool handled;
    const char *question="为什么天空是蓝色的？";
    const char *answer="因为蓝光更容易被空气散射。";
    init();strcpy(reply,answer);response_delay=70;tail_delay=650;tail_chunks=4;
    begin(question);assert(!streams && !persists && !local_calls);
    esp_agent_voice_candidate_revision(2);esp_agent_voice_candidate_preview(question);
    assert(!finish(&engine,question,AGENT_OK,&handled) && handled);
    assert(streams==1 && live_stream && creates==1 && persists==2 && assistant_turns==1 && !local_calls);
    assert(!cache_begins && !generated && !progresses && !esp_agent_voice_candidate_ack()->pending);
    assert(!strcmp(input,"快答：为什么天空是蓝色的？"));preserved();
    /* Some providers send the end marker only after all PCM. An admitted
     * single sentence may stream first; completion still validates output. */
    init();strcpy(reply,answer);fault=20;tail_delay=250;begin(question);
    assert(!finish(&engine,question,AGENT_OK,&handled) && handled && live_stream && first_write_at<response_at+250);preserved();
    const char *changed[]={"为什么天空是蓝色的？请解释详细一点。","为什么天空是红色的？",
        "为什么天空是蓝色的？再把灯改成绿色。"};
    for(unsigned i=0;i<sizeof(changed)/sizeof(*changed);++i) {
        init();strcpy(reply,answer);begin(question);worker_stopped();
        assert(!streams && !persists);
        assert(!finish(&engine,changed[i],AGENT_OK,&handled));
        assert(!handled && !streams && !persists && !local_calls && creates==1);preserved();
    }
    init();strcpy(reply,answer);fault=8;tail_delay=200;begin(question);
    assert(finish(&engine,question,AGENT_OK,&handled)==AGENT_ERR_PROTOCOL);
    assert(handled && streams==1 && !assistant_turns && creates==1);preserved();
    puts("Short answer: immutable full-question admission, transcript-final streaming, revision discard and no action/replay OK");
}
static void unpunctuated_receipt_stream(void)
{
    const char *preview[]={"现在灯是什么颜色","而家燈係乜嘢顏色"};
    const char *reply_text[]={"嗯，现在灯是什么颜色，我想想","嗯，而家燈係乜嘢顏色，我諗諗先"};
    const char *final_text[]={"现在灯是什么颜色？请简单回答。","而家燈係乜嘢顏色？"};
    for(unsigned language=0;language<2;++language) {
        init();strcpy(reply,reply_text[language]);response_delay=70;tail_delay=700;tail_chunks=500;
        begin(preview[language]);assert(!streams && !persists && !local_calls);
        uint64_t began=esp_agent_now();bool handled=false;
        assert(!finish(&engine,final_text[language],AGENT_OK,&handled));
        assert(!handled && live_stream && streams==1 && !persists && !local_calls);
        assert(first_write_at>=began && first_write_at-began<500 && streamed_samples==64002);
        assert(cache_peak_pages==1 && esp_agent_voice_candidate_ack()->pending);
        assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));preserved();
    }
    /* Complete receipt text never overrides changed final input. */
    init();strcpy(reply,reply_text[0]);tail_delay=50;begin(preview[0]);worker_stopped();
    bool handled=false;
    assert(!finish(&engine,"不用问灯了，请介绍你自己。",AGENT_OK,&handled));
    assert(!handled && !streams && !persists && !local_calls);preserved();
}
static void receipt_stream(void)
{
    static const char *partial[]={"请记住","唔該記低"};
    static const char *final[]={"请记住灯的名字是星星。","唔該記低燈嘅名係星星。"};
    static const char *receipt[]={"嗯，我来记一下。","嗯，我嚟記低先。"};
    bool handled;
    /* Timely PCM with a slow response.done must become audible within the
     * original500ms admission window, not be discarded while awaiting EOF.
     * The source arena is poisoned/freed by restore before pending playback
     * joins, so the spool cancellation pointer cannot point into that arena. */
    for(unsigned language=0;language<2;++language) {
        init();strcpy(reply,receipt[language]);response_delay=70;tail_delay=700;tail_chunks=8;
        begin(partial[language]);assert(!streams && !progresses);
        uint64_t began=esp_agent_now();
        assert(!finish(&engine,final[language],AGENT_OK,&handled));
        assert(!handled && live_stream && first_write_at>=began && first_write_at-began<500);
        assert(esp_agent_now()-began>=700 && streams==1 && cache_begins==1 && seals==1 && !finishes);
        assert(generated==1 && progresses==1 && final_candidates==1 && !persists && !local_calls);
        assert(creates==1 && closes==1 && !atomic_load(&cache_count));preserved();
        const esp_agent_voice_ack_t *ack=esp_agent_voice_candidate_ack();
        assert(http_warms==1);
        assert(ack->pending && !ack->spoken && !strcmp(ack->text,receipt[language]));
        assert(!strcmp(ack->language,language?"yue":"zh"));
        assert(spool_cancel==&core.cancelled && !atomic_load(spool_cancel));
        assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));
        assert(!ack->pending && ack->spoken && finishes==1);
        assert(!esp_agent_voice_candidate_ack_join(AGENT_OK) && finishes==1);
    }
    /* No first PCM within the original deadline: keep the no-speech fallback,
     * without increasing the wait or replaying the candidate request. */
    init();strcpy(reply,receipt[0]);response_delay=900;begin(partial[0]);
    uint64_t began=esp_agent_now();
    assert(!finish(&engine,final[0],AGENT_OK,&handled));
    assert(esp_agent_now()-began<800 && !handled && !streams && !cache_begins && !persists);
    assert(creates==1 && closes==1);preserved();
    /* Once admitted, a late changed transcript, tool, write/spool/seal error,
     * or cancellation owns the failed turn. Never replay a partial receipt
     * or persist it as a completed assistant answer. */
    const unsigned failures[]={4,7,8,10,16,17,19};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i) {
        init();fault=failures[i];strcpy(reply,receipt[0]);tail_delay=30;begin(partial[0]);
        agent_err_t result=finish(&engine,final[0],AGENT_OK,&handled);
        assert(result && handled && live_stream && streams==1 && cache_begins==1 && finishes==1);
        assert(!spool_active && !esp_agent_voice_candidate_ack()->pending);
        assert(persists==2 && !assistant_turns && !local_calls && creates==1 && closes==1);
        assert(generated==(fault==16?0u:1u) && progresses==generated);
        assert(!atomic_load(&cache_count));preserved();
    }
    init();strcpy(reply,receipt[0]);tail_delay=2000;cancel_at=esp_agent_now()+100;
    begin(partial[0]);began=esp_agent_now();
    assert(finish(&engine,final[0],AGENT_OK,&handled)==AGENT_ERR_CANCELLED);
    assert(handled && streams==1 && finishes==1 && !seals && esp_agent_now()-began<700);preserved();
    /* A sealed receipt must still be joined when engine restoration or
     * eventual board playback fails. Joined cancellation remains stable. */
    for(unsigned failure=6;failure<=18;failure+=12) {
        init();fault=failure;strcpy(reply,receipt[0]);tail_delay=30;begin(partial[0]);
        agent_err_t result=finish(&engine,final[0],AGENT_OK,&handled);
        assert(result==(failure==6?AGENT_ERR_MEMORY:AGENT_OK));
        assert(handled==(failure==6) && esp_agent_voice_candidate_ack()->pending);
        assert(esp_agent_voice_candidate_ack_join(result)==(failure==6?AGENT_ERR_MEMORY:AGENT_ERR_TOOL));
        assert(!esp_agent_voice_candidate_ack()->pending && !esp_agent_voice_candidate_ack()->spoken && finishes==1);
        preserved();
    }
    /* Changed/cancelled/incomplete final input never grants speculative PCM
     * access to the speaker, even though its complete sentence is cached. */
    const char *reject[]={"请记住灯的名字是星星，不对，不用记了。","介绍你自己。","请记住。"};
    for(unsigned i=0;i<sizeof(reject)/sizeof(*reject);++i) {
        init();strcpy(reply,receipt[0]);tail_delay=700;begin(partial[0]);
        assert(!finish(&engine,reject[i],AGENT_OK,&handled));
        assert(!handled && !streams && !cache_begins && !persists && !progresses);preserved();
    }
    for(unsigned scenario=0;scenario<2;++scenario) {
        init();strcpy(reply,receipt[0]);tail_delay=100;
        if(scenario==0)engine.gateway=true;
        else http_warm_error=AGENT_ERR_TIMEOUT;
        begin(partial[0]);
        assert(!finish(&engine,final[0],AGENT_OK,&handled));
        assert(!handled && http_warms==scenario && !local_calls && !persists);
        assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));preserved();
    }
    puts("Task receipt: final admission, bounded HTTP warm once, Gateway skip, warm failure fallback, no early effects, joined ownership OK");
}
static void receipt_handoff(void)
{
    for(unsigned scenario=0;scenario<4;++scenario) {
        bool handled=false;init();strcpy(reply,"嗯，我来记一下。");tail_delay=700;tail_chunks=8;
        spool_delay=80;warm_delay=100;
        begin("请记住");uint64_t began=esp_agent_now();
        assert(!esp_agent_voice_candidate_stage(&engine,"请记住灯的名字是星星。",AGENT_OK,
            capture_arena+CAPTURE_BYTES-8192,&handled));
        assert(esp_agent_voice_candidate_deferred() && !handled && !engine.workspace);
        assert(esp_agent_now()-began<500 && atomic_load(&worker_live) && streams==1 && !seals);
        assert(esp_agent_now()-began>=180 && http_warms==1 && !persists && final_candidates==1 && !local_calls);
        agent_err_t error=scenario==1?AGENT_ERR_NETWORK:AGENT_OK;
        if(scenario==2)atomic_store(&core.cancelled,true);
        if(scenario==3) {
            atomic_fetch_add(&clock_advance,10200);worker_stopped();
        }
        agent_err_t result=finish(&engine,"请记住灯的名字是星星。",error,&handled);
        assert(!esp_agent_voice_candidate_deferred() && !atomic_load(&worker_live) && engine.workspace);
        assert(!persists && final_candidates==1 && http_warms==1);preserved();
        if(!scenario) {
            assert(!result && !handled && seals==1 && esp_agent_voice_candidate_ack()->pending);
            assert(!esp_agent_voice_candidate_ack_join(AGENT_OK) && finishes==1);
        } else assert(result && handled && finishes==1 && !seals);
    }
    for(unsigned scenario=0;scenario<2;++scenario) {
        bool handled=false;init();strcpy(reply,"嗯，我来记一下。");tail_delay=150;tail_chunks=8;
        if(scenario)warm_delay=350;else http_warm_error=AGENT_ERR_TLS;
        begin("请记住");
        assert(!esp_agent_voice_candidate_stage(&engine,"请记住灯的名字是星星。",AGENT_OK,
            capture_arena+CAPTURE_BYTES-8192,&handled));
        assert(!esp_agent_voice_candidate_deferred() && !handled && engine.workspace && http_warms==1);
        assert(!atomic_load(&worker_live) && !persists && !local_calls);
        assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));preserved();
    }
    puts("Receipt handoff: live task until single join, no re-admission/history, original deadline and error/cancel cleanup OK");
}
static void reuse_lifetime(void)
{
    bool handled;
    retained_first();
    next_capture();assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled) && handled);
    assert(opens==1 && !closes && reuses==1 && esp_agent_voice_candidate_retained());
    next_capture();begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled) && handled);
    assert(opens==1 && closes==1 && reuses==2 && creates==3 && sends==7);
    assert(!esp_agent_voice_candidate_retained() && !esp_agent_voice_candidate_idle_state());
    next_capture();begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled) && handled);
    assert(opens==2 && creates==4 && reuses==2 && sends==10);
    esp_agent_voice_candidate_discard();assert(closes==2);preserved();

    /* Cancellation, lease expiry, setting changes and ordinary engine work
     * revoke parked ownership before another transport user can enter. */
    for(unsigned why=0;why<4;++why) {
        retained_first();
        if(why==0)esp_agent_voice_candidate_cancel();
        if(why==1)atomic_store(&clock_advance,31000);
        if(why==2)reuse_requested=false;
        assert(esp_agent_voice_candidate_retained()==(why==3));
        assert(!esp_agent_workspace_restore());
        assert(!atomic_load(&opened) && closes==1 && !esp_agent_voice_candidate_idle_state());
    }
    for(unsigned failure=14;failure<=15;++failure) {
        retained_first();next_capture();fault=failure;begin("介绍");worker_stopped();
        assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
        assert(!handled && streams==1 && persists==2 && opens==1 && closes==1);
        assert(!esp_agent_voice_candidate_retained());
    }
    retained_first();next_capture();begin("介绍");worker_stopped();
    assert(!finish(&engine,"改为查询天气。",AGENT_OK,&handled));
    assert(!handled && streams==1 && closes==1 && !esp_agent_voice_candidate_retained());
    retained_first();next_capture();streams=persists=0;begin("介绍");worker_stopped();
    assert(!finish(&engine,"请把灯设为绿色。",AGENT_OK,&handled));
    assert(handled && local_calls==1 && !streams && closes==1);
    for(unsigned failure=6;failure<=13;failure+=6) {
        retained_first();next_capture();fault=failure;begin("介绍");
        assert(finish(&engine,"介绍你自己。",AGENT_OK,&handled)==
            (failure==6?AGENT_ERR_MEMORY:AGENT_ERR_FULL));
        assert(handled && closes==1 && !esp_agent_voice_candidate_retained());
    }
    retained_first();next_capture();task_failed=true;begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(!handled && closes==1 && !esp_agent_voice_candidate_retained());
    retained_first();next_capture();assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);
    esp_agent_voice_candidate_cancel();idle_wait(ESP_AGENT_CANDIDATE_EXPIRED);
    esp_agent_voice_candidate_discard();assert(closes==1 && !atomic_load(&asr_live));
    puts("Candidate reuse: three turns, different freed arenas, one handshake, stale IDs/PCM, expiry, cancel, fallback and restore failures OK");
}
#if AGENT_HANDOFF_PROBE
static void diagnostic_handoff(void)
{
    const char *input_text="请记住灯的名字是星星。";
    for(unsigned scenario=0;scenario<7;++scenario) {
        bool handled=false;init();strcpy(reply,"嗯，我来记一下。");tail_chunks=8;
        assert(!esp_agent_voice_candidate_probe(capture_arena,ESP_AGENT_CANDIDATE_PREFIX_BYTES,
            &core.cancelled,notify,"请记住",scenario==3,true));
        assert(atomic_load(&worker_live) && !streams && !asr_opens && !asr_warms);
        assert(atomic_load(&cache_count) && !persists);
        if(scenario==5)http_warm_error=AGENT_ERR_TLS;
        assert(!(scenario==6?esp_agent_voice_candidate_finish:esp_agent_voice_candidate_stage)(
            &engine,scenario==1?"介绍你自己。":input_text,
            AGENT_OK,capture_arena+CAPTURE_BYTES-8192,&handled));
        if(scenario==1 || scenario==5 || scenario==6) {
            assert(!handled && !esp_agent_voice_candidate_deferred() && engine.workspace);
            assert(!atomic_load(&worker_live));
            if(scenario==1)assert(!streams && !http_warms && !persists);
            else assert(streams==1 && http_warms==1 && !esp_agent_voice_candidate_ack_join(AGENT_OK));
            preserved();continue;
        }
        assert(!handled && esp_agent_voice_candidate_deferred() && !engine.workspace);
        assert(atomic_load(&worker_live) && http_warms==1 && streams==1 && !seals);
        assert(!atomic_load(&cache_count) && final_candidates==1 && !persists);
        if(!scenario) {
            uint64_t hold=esp_agent_now();vTaskDelay(650);
            assert(esp_agent_now()-hold>=650 && atomic_load(&worker_live));
            assert(!seals && creates==1 && !persists); /* join alone releases the barrier */
        }
        if(scenario==2)atomic_store(&core.cancelled,true);
        if(scenario==4)atomic_fetch_add(&clock_advance,10200);
        agent_err_t result=finish(&engine,input_text,AGENT_OK,&handled);
        assert(!esp_agent_voice_candidate_deferred() && engine.workspace && !atomic_load(&worker_live));
        assert(final_candidates==1 && !persists && creates==1 && closes==1);
        if(!scenario) {
            assert(!result && !handled && seals==1 && esp_agent_voice_candidate_ack()->pending);
            assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));
        } else {
            assert(result==(scenario==3?AGENT_ERR_PROTOCOL:AGENT_ERR_CANCELLED));
            assert(handled && !seals && finishes==1 && !esp_agent_voice_candidate_ack()->pending);
        }
        preserved();
    }
    init();fault=1;bool handled=false;
    assert(esp_agent_voice_candidate_probe(capture_arena,ESP_AGENT_CANDIDATE_PREFIX_BYTES,
        &core.cancelled,notify,"请记住",false,true)==AGENT_ERR_TLS);
    assert(finish(&engine,input_text,AGENT_ERR_TLS,&handled)==AGENT_ERR_TLS);
    assert(!atomic_load(&worker_live) && !streams && !persists && !handled);preserved();
    puts("Diagnostic handoff barriers: real worker lifecycle, revoke, cancel, injected failure, original deadline and warm failure OK");
}
static void diagnostic_unheld(void)
{
    for(unsigned scenario=0;scenario<3;++scenario) {
        init();strcpy(reply,"嗯，我来记一下。");tail_chunks=8;tail_delay=40;
        assert(!esp_agent_voice_candidate_probe(capture_arena,ESP_AGENT_CANDIDATE_PREFIX_BYTES,
            &core.cancelled,notify,"请记住",false,false));
        /* Receive must finish without final input releasing a probe barrier.
         * No speculative audio, tools or persistence may escape the gate. */
        worker_stopped();assert(atomic_load(&cache_count) && !streams && !persists && !local_calls);
        assert(!tail_chunks && closes==1);
        if(scenario==2)atomic_store(&core.cancelled,true);
        bool handled=false;
        agent_err_t result=finish(&engine,scenario==1?"介绍你自己。":"请记住灯的名字是星星。",
                                 AGENT_OK,&handled);
        assert(result==(scenario==2?AGENT_ERR_CANCELLED:AGENT_OK));
        assert(!handled && engine.workspace && !atomic_load(&worker_live) && !persists && !local_calls);
        if(!scenario) {
            assert(streams==1 && seals==1 && esp_agent_voice_candidate_ack()->pending);
            assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));
        } else assert(!streams && !esp_agent_voice_candidate_ack()->pending);
        preserved();
    }
    puts("Unheld diagnostic: receive completes independently, final admission, revision and cancellation remain required OK");
}
#endif
int main(void)
{
    bool handled;
    asr_connection_order();
    unpunctuated_receipt_stream();
#if AGENT_HANDOFF_PROBE
    diagnostic_handoff();
    diagnostic_unheld();
#endif
    receipt_handoff();
    /* Idle warms only protocol state. No input, generation, cache or playback.
     * Claim uses the same worker/socket, and capture metadata stays disjoint. */
    init();assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);
    assert(warm()==AGENT_ERR_BUSY && !creates && !streams && !atomic_load(&cache_count));
    vTaskDelay(50);preserved();begin("介绍");
    assert(!esp_agent_voice_candidate_idle_state());
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(handled && creates==1 && closes==1 && streams==1);preserved();
    assert(cache_peak_pages==1);
    assert(asr_warms==1 && !asr_opens && asr_closes==1);
    /* Failed speculation gets one fresh connection before any ASR input;
     * there is no automatic replay of a committed/uploaded turn. */
    init();asr_fault=true;assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);
    begin("介绍");assert(asr_warms==1 && asr_opens==1 && asr_closes==1);
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));preserved();
    /* In-progress warm ownership transfers only after its publication. */
    init();asr_delay=70;assert(!warm());
    uint64_t transfer_at=esp_agent_now();begin("介绍");
    assert(esp_agent_now()-transfer_at>=70 && asr_warms==1 && !asr_opens);
    assert(esp_agent_voice_candidate_asr_open(&asr_context,&core.cancelled)==AGENT_ERR_BUSY);
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));preserved();
    /* Cancellation before adoption joins the warm task and closes its unused
     * ASR handle; the worker never closes a handle owned by live ASR. */
    init();asr_delay=70;assert(!warm());
    esp_agent_voice_candidate_begin(&core.cancelled,notify);
    atomic_store(&core.cancelled,true);
    assert(esp_agent_voice_candidate_asr_open(&asr_context,&core.cancelled)==AGENT_ERR_CANCELLED);
    assert(finish(&engine,"",AGENT_ERR_CANCELLED,&handled)==AGENT_ERR_CANCELLED);
    assert(!atomic_load(&asr_live) && !asr_opens && !streams);preserved();
    /* Wake may arrive during TLS. Claim is asynchronous; it must not join or
     * wait for setup before recording/ASR can proceed. */
    init();atomic_store(&open_delay,200);assert(!warm());
    uint64_t claim_begin=esp_agent_now();begin("介绍");
    assert(esp_agent_now()-claim_begin<100);
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(handled && creates==1 && closes==1);preserved();
    init();assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);
    esp_agent_voice_candidate_cancel();idle_wait(ESP_AGENT_CANDIDATE_EXPIRED);
    esp_agent_voice_candidate_discard();
    assert(!esp_agent_voice_candidate_idle_state() && closes==1 && !creates && !streams);preserved();
    /* Expiry frees the worker before the same arena can be used cold. */
    init();assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);
    atomic_store(&clock_advance,31000);idle_wait(ESP_AGENT_CANDIDATE_EXPIRED);
    esp_agent_voice_candidate_discard();assert(!creates && closes==1);preserved();
    init();fault=1;assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_EXPIRED);
    esp_agent_voice_candidate_discard();assert(!creates && !closes);preserved();
    init();task_failed=true;assert(warm()==AGENT_ERR_MEMORY);
    assert(esp_agent_voice_candidate_idle_state()==ESP_AGENT_CANDIDATE_EXPIRED);
    esp_agent_voice_candidate_discard();assert(!creates && !closes);preserved();
    /* Direct replacement requires joining even if the worker is still open. */
    init();assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);
    esp_agent_voice_candidate_discard();assert(!creates && closes==1);preserved();
    init();assert(!warm());idle_wait(ESP_AGENT_CANDIDATE_READY);begin("请把灯");
    esp_agent_voice_candidate_revision(2);
    assert(!finish(&engine,"不对，改成绿色。",AGENT_OK,&handled));
    assert(!handled && !streams && !persists && closes==1);preserved();
    init();strcpy(reply,"嗯，我来记一下。");begin("请记住");
    worker_stopped();assert(atomic_load(&cache_count) && !streams && !persists);
    esp_agent_voice_candidate_revision(2);
    esp_agent_voice_candidate_preview("请记住。\n灯的名字是星星。\n");
    assert(!streams && !persists);
    assert(!finish(&engine,"请记住。\n灯的名字是星星。\n",AGENT_OK,&handled));
    assert(!handled && streams==1 && writes==1 && seals==1 && !persists && creates==1 && closes==1);
    preserved();assert(esp_agent_voice_candidate_ack()->pending);
    assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));assert(finishes==1);
    assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));assert(finishes==1);
    init();strcpy(reply,"嗯，我来记一下。");begin("请记住");worker_stopped();
    esp_agent_voice_candidate_revision(2);
    assert(!finish(&engine,"请记住。\n不对，不用记了。",AGENT_OK,&handled));
    assert(!handled && !streams && !persists && !local_calls && !atomic_load(&cache_count));preserved();
    /* Final literal uses the original local tool path only after the worker
     * joins, cache releases and the engine arena is restored. Never play the
     * old preparation sentence or send the effect to a second owner. */
    init();strcpy(reply,"嗯，我来调整一下灯光。");begin("请把灯");worker_stopped();
    assert(atomic_load(&cache_count) && !streams && !persists);
    esp_agent_voice_candidate_revision(2);
    esp_agent_voice_candidate_preview("不对，不要蓝色，改成绿色。");
    assert(!streams && atomic_load(&cache_count));
    assert(!finish(&engine,"请把灯调成蓝色。不对，不要蓝色，改成绿色。\n",AGENT_OK,&handled));
    assert(!engine.clarify_only);
    assert(handled && local_calls==1 && !streams && !seals && !persists && creates==1 && !atomic_load(&cache_count));
    assert(!esp_agent_voice_candidate_ack()->pending);preserved();
    assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));assert(!finishes);
    const char *bad_receipts[]={"我已经把灯调成蓝色了。","嗯，我来调整蓝色灯光。"};
    for(unsigned i=0;i<sizeof(bad_receipts)/sizeof(*bad_receipts);++i) {
        init();strcpy(reply,bad_receipts[i]);begin("请把灯");worker_stopped();
        esp_agent_voice_candidate_revision(2);
        assert(!finish(&engine,"请把灯设为蓝色，不对，改成绿色。",AGENT_OK,&handled));
        assert(handled && local_calls==1 && !streams && !persists && !atomic_load(&cache_count));preserved();
    }
    init();strcpy(reply,"嗯，我来调整一下灯光。");begin("请把灯");worker_stopped();
    esp_agent_voice_candidate_revision(2);esp_agent_voice_candidate_preview("不对，取消。");
    assert(!finish(&engine,"请把灯设为蓝色，不对，取消。",AGENT_OK,&handled));
    assert(!handled && !streams && !persists && !atomic_load(&cache_count));preserved();
    init();strcpy(reply,"嗯，我来调整一下灯光。");begin("请把灯");worker_stopped();
    assert(!finish(&engine,"请把灯调成蓝色。不对。\n",AGENT_OK,&handled));
    assert(!handled && engine.clarify_only && !streams && !persists && !atomic_load(&cache_count));preserved();
    const char *missing[]={"记住。\n","请记住，","唔該記低。","请把灯调成。"};
    for(unsigned i=0;i<sizeof(missing)/sizeof(*missing);++i) {
        init();strcpy(reply,"嗯，我来记一下。");begin("请记住");worker_stopped();
        assert(!finish(&engine,missing[i],AGENT_OK,&handled));
        assert(!handled && engine.clarify_only && !streams && !persists && !local_calls);
        assert(!atomic_load(&cache_count));preserved();
    }
    init();begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(handled && creates==1 && streams==1 && finishes==1 && persists==2);
    for(unsigned failure=12;failure<=13;++failure) {
        init();fault=failure;begin("介绍");
        agent_err_t result=finish(&engine,"介绍你自己。",AGENT_OK,&handled);
        assert(result==(failure==12?AGENT_ERR_FULL:AGENT_ERR_CORRUPT));
        assert(handled && creates==1 && streams==1 && finishes==1);
        assert(persists==(failure==12?2u:3u) && assistant_turns==(failure==13));
        preserved();
    }
    /* The admitted sentence starts before response.done; a long tail bypasses
     * the24KiB speculative cache. Persist only after a validated completion. */
    init();tail_delay=2000;tail_chunks=512;begin("介绍");
    uint64_t stream_begin=esp_agent_now();
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(esp_agent_now()-stream_begin>=2000 && handled && live_stream && creates==1 &&
        streamed_samples==65538 && finishes==1 && assistant_turns==1);preserved();
    assert(cache_peak_pages==1 && !atomic_load(&cache_count));
    for(unsigned late=7;late<=10;++late) {
        init();fault=late;tail_delay=30;begin("介绍");
        agent_err_t result=finish(&engine,"介绍你自己。",AGENT_OK,&handled);
        if(late==7 || late==8)assert(result==AGENT_ERR_PROTOCOL);
        if(late==9)assert(!result && !handled && !streams && !persists);
        else assert(result && handled && streams==1 && finishes==1 && persists==2 && !assistant_turns);
        assert(creates==1 && closes==1);preserved();
    }
    /* A response already completed before final ASR keeps the joined-cache
     * path; speculative PCM must remain inaudible until that admission. */
    init();begin("介绍");
    uint64_t close_limit=esp_agent_now()+2000;
    while(!atomic_load(&generation_closed) && esp_agent_now()<close_limit)vTaskDelay(2);
    assert(atomic_load(&generation_closed) && !streams);
    worker_stopped();assert(atomic_load(&cache_count)==1);
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(handled && !live_stream && streams==1 && assistant_turns==1);preserved();
    /* A completed longer candidate retains only actual pages; the full24KiB
     * bound is unchanged. Over-capacity or allocation failure never truncates
     * a reply into an accepted one. Failure frees pages before final capture. */
    init();tail_chunks=128;begin("介绍");worker_stopped();
    assert(atomic_load(&cache_count)==9 && !streams);
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(handled && streamed_samples==16386 && !atomic_load(&cache_count));preserved();
    init();tail_chunks=400;begin("介绍");worker_stopped();
    assert(!atomic_load(&cache_count) && cache_peak_pages==24 && !streams);
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(!handled && !streams && !persists);preserved();
    init();fault=11;tail_chunks=32;begin("介绍");worker_stopped();
    assert(!atomic_load(&cache_count) && cache_peak_pages==2 && !streams);
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(!handled && !streams && !persists);preserved();
    init();tail_delay=2000;begin("介绍");
    uint64_t page_wait=esp_agent_now()+2000;
    while(!atomic_load(&cache_count) && esp_agent_now()<page_wait)vTaskDelay(2);
    assert(atomic_load(&cache_count));esp_agent_voice_candidate_preview("不对，改成调灯。");
    worker_stopped();assert(!atomic_load(&cache_count) && !streams);
    assert(!finish(&engine,"不对，改成调灯。",AGENT_OK,&handled));
    assert(!handled && !streams && !persists);preserved();
    /* A late valid simple answer survives the old speculative cutoff. */
    init();response_delay=700;begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(handled && final_candidates==1 && creates==1 && closes==1 && streams==1 && persists==2);preserved();
    init();response_delay=700;strcpy(reply,"嗯，我来调整一下灯光。");begin("请把灯");
    assert(!finish(&engine,"请把灯设为蓝色。",AGENT_OK,&handled));
    assert(handled && local_calls==1 && !final_candidates && !streams && !persists && creates<=1);preserved();
    /* A failed local effect/playback owns the turn and cannot fall through
     * to the full Agent. Capture failures never enter this path. */
    for(unsigned failure=0;failure<4;++failure) {
        init();begin("请把灯");
        local_error=failure==0?AGENT_ERR_TOOL:failure==1?AGENT_ERR_NETWORK:AGENT_ERR_CANCELLED;
        if(failure==2)atomic_store(&core.cancelled,true);
        agent_err_t input_error=failure==3?AGENT_ERR_TIMEOUT:AGENT_OK;
        agent_err_t result=finish(&engine,"请把灯设为绿色。",input_error,&handled);
        assert(result==(failure==3?input_error:local_error));
        assert(handled==(failure<3) && local_calls==(failure<2?1u:0u));
        assert(!streams && !persists && !atomic_load(&cache_count));preserved();
    }
    init();response_delay=4000;cancel_at=esp_agent_now()+100;begin("介绍");
    uint64_t cancel_start=esp_agent_now();
    assert(finish(&engine,"介绍你自己。",AGENT_OK,&handled)==AGENT_ERR_CANCELLED);
    assert(esp_agent_now()-cancel_start<1000 && !handled && !streams && !persists);preserved();
    init();begin("介绍");
    assert(finish(&engine,"介绍你自己。",AGENT_ERR_PROTOCOL,&handled)==AGENT_ERR_PROTOCOL);
    assert(!handled && !final_candidates && !streams && !persists);preserved();
    for(unsigned test=0;test<8;++test) {
        init();if(test<4)fault=test+1;
        if(test==7)task_failed=true;
        begin("介绍");
        if(test==4)esp_agent_voice_candidate_revision(2);
        if(test==5)atomic_store(&core.cancelled,true);
        agent_err_t result=finish(&engine,test==6?"改为查询天气。":"介绍你自己。",AGENT_OK,&handled);
        if(test==3)assert(result==AGENT_ERR_TOOL && handled && streams==1 && finishes==1 && persists==2);
        else {assert(result==(test==5?AGENT_ERR_CANCELLED:AGENT_OK));assert(!handled && !streams && !persists);preserved();}
        assert(!atomic_load(&worker_live) && !atomic_load(&opened) && creates<=1);
    }
    init();too_small=true;esp_agent_voice_candidate_begin(&core.cancelled,notify);
    assert(!finish(&engine,"介绍你自己",AGENT_OK,&handled));assert(!handled && !started && !streams);
    init();fault=5;begin("介绍");
    assert(!finish(&engine,"介绍你自己。",AGENT_OK,&handled));
    assert(!handled && !streams && creates==1 && closes==1 && engine.workspace && !atomic_load(&cache_count));preserved();
    init();begin("");
    assert(!finish(&engine,"今天天气好吗？",AGENT_OK,&handled));
    assert(!handled && !creates && !streams && !atomic_load(&cache_count) && engine.workspace);preserved();
    init();fault=6;begin("介绍");
    assert(finish(&engine,"介绍你自己。",AGENT_OK,&handled)==AGENT_ERR_MEMORY);
    assert(handled && streams==1 && !persists && !engine.workspace && !atomic_load(&cache_count));preserved();
    contextual_receipt();short_answers();receipt_stream();reuse_lifetime();free(capture_arena);capture_arena=NULL;
    puts("Candidate adapter: final-input sentence streaming, bounded cache/tail, late failure without replay, cancel, joined ownership and complete-only persistence OK");
    return 0;
}
