#include "voice_candidate.h"
#include "candidate.h"
#include "prefetch.h"
#include "voice_history.h"
#include "audio_board.h"
#include "runtime.h"
#if AGENT_TLS_COOPERATE
#include "tls_cooperate.h"
#endif
#if AGENT_WS_CONNECT_TRACE
#include "ws_connect_clock.h"
#endif
#if AGENT_TLS_PHASE_TRACE
#include "tls_phase_clock.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern const agent_ws_ops_t esp_agent_candidate_ws;
extern const agent_ws_ops_t esp_agent_asr_ws;
enum { CACHE_BYTES=24576, WORKER_STACK_BYTES=6144,
       PREVIEW_WAIT_MS=500, FINAL_ANSWER_WAIT_MS=1800, FINAL_STREAM_WAIT_MS=10000 };
typedef struct {
    agent_candidate_gate_t gate;
    agent_ws_ops_t transport;
    agent_speech_t speech;
    agent_realtime_t session;
    agent_prefetch_t reply;
    atomic_bool done,play_permitted,streaming,request_ready;
    atomic_uint claimed_at;
    bool warmed,reused;
    unsigned cancel_generation;
    void *pcm;
#if AGENT_LOCAL_FIRST_PREFETCH
    /* Runtime's independent input workspace stays immutable through the
     * worker join, including deferred HTTP handoff. Publish with permission;
     * never read ASR-owned final bytes while capture can still revise them. */
    const char *final_input;
#endif
    agent_err_t result;
    unsigned started_at,opened_at,ready_at,request_at,complete_at,stack_free,heap_low,final_at;
#if AGENT_WS_CONNECT_TRACE
    unsigned created_at,tls_began,tls_ended;
#endif
    uint8_t *cache[CACHE_BYTES/AGENT_DRAFT_PAGE_BYTES];
    unsigned cache_peak;
    char scratch[AGENT_RT_SCRATCH];
    StaticTask_t tcb;
    _Alignas(16) StackType_t stack[WORKER_STACK_BYTES/sizeof(StackType_t)];
} candidate_work_t;
_Static_assert(sizeof(candidate_work_t)+15<=ESP_AGENT_CANDIDATE_PREFIX_BYTES,"Candidate overlaps live source records");
_Static_assert(sizeof(StackType_t)==1,"IDF task stack sizes are bytes");
/* Small coordinator survives after the engine reclaims the borrowed arena. */
static struct {
    candidate_work_t *work;
    TaskHandle_t task;
    const atomic_bool *cancelled;
    void (*event)(void *,const char *,const char *);
    esp_agent_voice_ack_t ack;
    bool warm,asr_claimed,deferred;
    atomic_int asr_result; /* -1: connecting; nonnegative agent_err_t: published. */
    unsigned text_acked_at; /* Written by worker, read only after join. */
    /* Value-only diagnostics: sole worker writes, coordinator reads after
     * join. Keep native-pointer host layouts within the same capture prefix. */
    struct { unsigned polls,poll_begin,poll_end,pcm_begin,pcm_end; } clock;
#if AGENT_HANDOFF_PROBE
    bool probe,probe_failure,probe_hold;
    atomic_bool probe_prepared,probe_release;
    char last_event[72];
    unsigned last_event_at,protocol_events;
#endif
} owner;
/* Command/status tasks never dereference the borrowed workspace. */
static atomic_uint cancel_generation;
static atomic_uint idle_state;
#if AGENT_RT_TEXT_REUSE
/* Only value state survives arena reclamation. No borrowed pointer or task
 * survives here; the fixed primary transport slot owns its own TLS storage. */
static struct {
    agent_realtime_t session;
    uint64_t until;
    unsigned generation,nonce;
} retained;
static void retained_close(void)
{
    if(retained.session.opened)esp_agent_candidate_ws.close(esp_agent_candidate_ws.ctx);
    memset(&retained,0,sizeof(retained));
}
#endif
bool esp_agent_voice_candidate_retained(void)
{
#if AGENT_RT_TEXT_REUSE
    return retained.session.opened && esp_agent_now()<retained.until &&
        retained.generation==atomic_load(&cancel_generation) && esp_agent_voice_reuse_enabled();
#else
    return false;
#endif
}
unsigned esp_agent_voice_candidate_idle_state(void) { return atomic_load(&idle_state); }
void esp_agent_voice_candidate_cancel(void) { atomic_fetch_add(&cancel_generation,1); }

static void close_session(candidate_work_t *w)
{
    if(w->session.opened) {w->session.ws->close(w->session.ws->ctx);w->session.opened=false;}
}
static bool park(candidate_work_t *w)
{
#if AGENT_RT_TEXT_REUSE
    if(w->session.text_reuse && w->session.opened &&
       w->cancel_generation==atomic_load(&cancel_generation) && !atomic_load(owner.cancelled) &&
       !agent_realtime_next_text_turn(&w->session)) {
        configASSERT(!retained.session.opened);
        retained.session=w->session;
        retained.session.speech=NULL;retained.session.ws=NULL;
        retained.session.sink=(agent_pcm_sink_t){0};
        retained.session.on_event=NULL;retained.session.event_ctx=NULL;
        retained.until=esp_agent_now()+30000;retained.generation=w->cancel_generation;
        retained.nonce=w->speech.nonce;
        w->session.opened=false; /* Move the sole transport ownership. */
        atomic_store(&idle_state,ESP_AGENT_CANDIDATE_PARKED);
        return true;
    }
#else
    (void)w;
#endif
    return false;
}

/* Preserve the actual owner/operations; only timestamp successful completion
 * of DNS/TCP/TLS/HTTP upgrade. No read, reconnect or log is added here. The
 * coordinator cannot release this worker's arena before it has joined. */
static agent_err_t timed_open(void *ctx,const atomic_bool *cancelled)
{
    agent_err_t error=esp_agent_candidate_ws.open(ctx,cancelled);
    if(!error) {
        owner.work->opened_at=(unsigned)esp_agent_now();
#if AGENT_WS_CONNECT_TRACE
        esp_agent_ws_connect_clock(&owner.work->tls_began,&owner.work->tls_ended);
#endif
    }
    return error;
}

static const char instructions[]=
    "ESP-HI助手小言。默认普通话，仅要求时用粤语，一句≤14字。"
    "快答：后是常识问题，直接回答一句≤26字，不复述问题、不说等待、不编造设备状态；"
    "问句是粤语就用粤语答。不调用工具。不足以确定就说不确定。"
#if AGENT_LOCAL_FIRST_PREFETCH
    "先看标记：待续：是未完转写；记下：是请记住之后的未完内容，不是执行灯光动作。提炼自然的话题短语，"
#else
    "先看标记：输入以待续：开头时，后文是未完转写，只摘原文2至8个汉字作话题，"
#endif
    "只说：嗯，{话题}，我想想。粤语用：嗯，{話題}，我諗諗先。"
#if AGENT_LOCAL_FIRST_PREFETCH
    "话题2至8字，尽量简短；省去我给、请等开头，不照念半截句。可补完截断词，"
    "播放前会核对完整转写；其余实词沿用原文且顺序不变，可加一个的或嘅。"
    "不用我给或半截单字动词作结尾，采用完整词语。例：记下：我给这盏灯取→嗯，给这盏灯取名，我想想。"
    "记下：我喜欢蓝色→嗯，喜欢蓝色，我想想。记下：呢盞燈嘅名→嗯，燈嘅名，我諗諗先。"
    "不预测具体名字、数值或设备状态，不另加话题。"
#else
    "话题总长2至8字；可省略原文字词，可加一个连接词的或嘅，实词不可新增或换序。"
#endif
    "不回答问题、不承诺动作。"
    #if AGENT_LOCAL_FIRST_PREFETCH
    "没有待续或记下标记才按以下规则："
#else
    "没有待续标记才按以下规则："
#endif
    "介绍直接答；动作未执行，只表准备，禁报完成、参数或流程。"
    "灯光：嗯，我来调整一下灯光。记忆：嗯，我来记一下。";
static void event(const char *name,const char *text)
{ if(owner.event)owner.event(NULL,name,text); }
static bool direct_answer(unsigned kind)
{ return kind==AGENT_GUESS_SELF || kind==AGENT_GUESS_ANSWER; }
static uint64_t now_ms(void *ctx) { (void)ctx;return esp_agent_now(); }
static agent_err_t cache_open(void *ctx,unsigned rate)
{ (void)ctx;return rate==AGENT_RT_OUTPUT_RATE?AGENT_OK:AGENT_ERR_PROTOCOL; }
static void cache_release(candidate_work_t *w)
{
    for(unsigned i=0;i<CACHE_BYTES/AGENT_DRAFT_PAGE_BYTES;++i) {
        free(w->cache[i]);w->cache[i]=NULL;
    }
}
static agent_err_t cache_write_inner(void *ctx,const int16_t *pcm,size_t count)
{
    candidate_work_t *w=ctx;
    if(w->reply.released)return esp_hi_stream_write(pcm,count,&w->gate.cancelled);
    if(!w->reply.invalid && !w->reply.unusable &&
       count<=1+2*CACHE_BYTES-w->reply.audio.samples) {
        size_t bytes=(w->reply.audio.samples+count)/2;
        unsigned pages=(unsigned)((bytes+AGENT_DRAFT_PAGE_BYTES-1)/AGENT_DRAFT_PAGE_BYTES);
        for(unsigned i=0;i<pages;++i)if(!w->cache[i]) {
            w->cache[i]=malloc(AGENT_DRAFT_PAGE_BYTES);
            if(!w->cache[i])return AGENT_ERR_MEMORY;
            w->cache_peak=(i+1)*AGENT_DRAFT_PAGE_BYTES;
        }
    }
    return agent_prefetch_pcm(&w->reply,pcm,count);
}
static agent_err_t cache_write(void *ctx,const int16_t *pcm,size_t count)
{
    owner.clock.pcm_begin=(unsigned)esp_agent_now();
    agent_err_t error=cache_write_inner(ctx,pcm,count);
    owner.clock.pcm_end=(unsigned)esp_agent_now();return error;
}
static agent_err_t drain(candidate_work_t *w)
{
    int16_t pcm[240];size_t count;
    for(;;) {
        agent_err_t error=agent_speech_draft_read(&w->reply.audio,pcm,240,&count);
        if(error || !count)return error;
        error=esp_hi_stream_write(pcm,count,w->reply.released?&w->gate.cancelled:owner.cancelled);
        if(error)return error;
    }
}
static void remember(candidate_work_t *w)
{
    strcpy(owner.ack.text,w->reply.text);
    strcpy(owner.ack.language,(atomic_load(&w->gate.request)&AGENT_CANDIDATE_YUE)?"yue":"zh");
    event("candidate_play",owner.ack.text);
}
static agent_err_t start_stream(candidate_work_t *w,void *memory,size_t bytes)
{
    agent_err_t error=esp_hi_stream_open(AGENT_RT_OUTPUT_RATE,memory,bytes);
    if(error)return error;
    /* Publish ownership even if spool setup fails, so the joined coordinator
     * closes the speaker before reclaiming either borrowed buffer. */
    atomic_store_explicit(&w->streaming,true,memory_order_release);
    bool task=!direct_answer(w->reply.guess);
    /* Playback may outlive this worker's arena. The cancellation pointer must
     * belong to the turn, never to the speculative gate in that arena. */
    if(task)error=esp_hi_stream_cache_begin(owner.cancelled,false);
    if(!error) {
        remember(w);
        if(task) {event("progress_generated",owner.ack.text);event("progress_start",owner.ack.text);}
    }
    return error;
}
#if AGENT_HANDOFF_PROBE
static void control(candidate_work_t *w);
static agent_err_t probe_barrier(candidate_work_t *w,bool drained)
{
    if(!owner.probe)return AGENT_OK;
    if(!drained && !atomic_load_explicit(&owner.probe_prepared,memory_order_acquire)) {
        char value[96];snprintf(value,sizeof(value),"{\"response_done\":%s,\"output_complete\":%s,\"samples\":%u}",
            w->session.done?"true":"false",w->reply.complete?"true":"false",(unsigned)w->reply.audio.samples);
        event("handoff_probe_prepared",value);
        atomic_store_explicit(&owner.probe_prepared,true,memory_order_release);
    } else if(drained)event("handoff_probe_drained",NULL);
    if(!owner.probe_hold)return AGENT_OK;
    while(!atomic_load_explicit(drained?&owner.probe_release:&w->play_permitted,memory_order_acquire)) {
        control(w);
        if(atomic_load(&w->gate.cancelled))return AGENT_ERR_CANCELLED;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if(atomic_load(&w->gate.cancelled))return AGENT_ERR_CANCELLED;
    return drained && owner.probe_failure?AGENT_ERR_PROTOCOL:AGENT_OK;
}
#endif
static bool reply_valid(candidate_work_t *w)
{
#if AGENT_LOCAL_FIRST_PREFETCH
    if((atomic_load_explicit(&w->gate.request,memory_order_acquire)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK) {
        if(!atomic_load_explicit(&w->play_permitted,memory_order_acquire))
            return agent_speech_guess_reply(AGENT_GUESS_THINK,w->reply.text); /* Silent cache only. */
        return agent_candidate_reply_final(&w->gate,w->final_input,w->reply.text);
    }
#endif
    return agent_candidate_reply(&w->gate,w->reply.text);
}
static agent_err_t release_ready(candidate_work_t *w)
{
    /* Worker alone owns parser/cache/speaker writes. Main publishes the
     * disjoint ASR tail after joining capture and admitting final input.
     * Never drain inside a JSON callback or across a partial WS message. */
#if AGENT_HANDOFF_PROBE
    if(owner.probe && !w->reply.released && !w->session.message &&
       agent_prefetch_output_ready(&w->reply) && reply_valid(w)) {
        agent_err_t error=probe_barrier(w,false);if(error)return error;
    }
#endif
    if(w->reply.released || !atomic_load_explicit(&w->play_permitted,memory_order_acquire) ||
       w->session.message || !agent_prefetch_output_ready(&w->reply) ||
       !reply_valid(w))return AGENT_OK;
    if(atomic_load(&w->gate.cancelled))return AGENT_ERR_CANCELLED;
    if(w->reply.text_length>ESP_AGENT_ACK_MAX)return AGENT_ERR_LIMIT;
    agent_err_t error=start_stream(w,w->pcm,AGENT_QWEN_SCRATCH);
    if(error)return error;
    w->reply.released=true;
    error=drain(w);cache_release(w);
    /* Streaming alone means speaker ownership, including partial setup.
     * Publish the smaller memory footprint and immutable receipt only after
     * spool setup and prebuffer release have both succeeded. */
    if(!error)atomic_store_explicit(&w->request_ready,true,memory_order_release);
#if AGENT_HANDOFF_PROBE
    if(!error)error=probe_barrier(w,true);
#endif
    return error;
}
static agent_err_t receive(void *ctx,const cJSON *root)
{
    candidate_work_t *w=ctx;
#if AGENT_HANDOFF_PROBE
    const char *last=agent_json_string(root,"type");
    if(last && strlen(last)<sizeof(owner.last_event) &&
       strspn(last,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.")==strlen(last))
        strcpy(owner.last_event,last);
    else strcpy(owner.last_event,"unknown");
    owner.last_event_at=(unsigned)esp_agent_now();++owner.protocol_events;
#endif
    if(w->session.text_ready && !owner.text_acked_at)owner.text_acked_at=(unsigned)esp_agent_now();
#if AGENT_WS_CONNECT_TRACE
    const char *type=agent_json_string(root,"type");
    if(type && !strcmp(type,"session.created"))w->created_at=(unsigned)esp_agent_now();
#endif
    return agent_prefetch_event(&w->reply,root);
}
static void heap_sample(candidate_work_t *w)
{ unsigned value=esp_get_free_heap_size();if(value<w->heap_low)w->heap_low=value; }
static void control(candidate_work_t *w)
{
    unsigned claimed=atomic_load_explicit(&w->claimed_at,memory_order_acquire);
    if(claimed) {
        uint64_t now=esp_agent_now();unsigned elapsed=(unsigned)now-claimed;
        w->session.deadline_cap=now+(elapsed<14000?14000-elapsed:0);
    }
    if(atomic_load(&cancel_generation)!=w->cancel_generation || (claimed && atomic_load(owner.cancelled)))
        atomic_store(&w->gate.cancelled,true);
    /* During the request send the coordinator can be inside HTTP. Preserve
     * the ORIGINAL admission deadline in the worker; resuming never renews it. */
    if(atomic_load_explicit(&w->play_permitted,memory_order_acquire) &&
       atomic_load(&w->streaming) && (unsigned)esp_agent_now()-w->final_at>=FINAL_STREAM_WAIT_MS)
        atomic_store(&w->gate.cancelled,true);
}
static agent_err_t poll(candidate_work_t *w)
{
    control(w);
    ++owner.clock.polls;owner.clock.poll_begin=(unsigned)esp_agent_now();
    agent_err_t error=agent_realtime_poll(&w->session,20);
    owner.clock.poll_end=(unsigned)esp_agent_now();return error;
}
static void worker(void *ctx)
{
    candidate_work_t *w=ctx;
    w->started_at=(unsigned)esp_agent_now();
    w->speech=(agent_speech_t){.scratch=w->scratch,.capacity=sizeof(w->scratch),
        .cancelled=&w->gate.cancelled,.now_ms=now_ms};
    agent_pcm_sink_t sink={.open=cache_open,.write=cache_write,.ctx=w};
    w->transport=esp_agent_candidate_ws;w->transport.open=timed_open;
    agent_realtime_init(&w->session,&w->speech,&w->transport,&sink,receive,w);
#if AGENT_RT_TEXT_REUSE
    if(esp_agent_voice_candidate_retained()) {
        w->speech.nonce=retained.nonce;
        w->session=retained.session;memset(&retained,0,sizeof(retained));
        w->session.speech=&w->speech;w->session.ws=&w->transport;w->session.sink=sink;
        w->session.on_event=receive;w->session.event_ctx=w;
        w->session.deadline=esp_agent_now()+120000;w->reused=true;
    } else retained_close();
    w->session.text_reuse=esp_agent_voice_reuse_enabled();
#endif
    w->session.deadline_cap=esp_agent_now()+(w->warmed?30000:14000);
    w->heap_low=esp_get_free_heap_size();
    control(w);
    if(w->warmed) {
        /* Reuse this worker and the already reserved ASR slot, not a second
         * task/buffer. Only connect: the ASR owner consumes session.created,
         * configures and uploads after the release/acquire transfer. */
        agent_err_t warmed=esp_agent_asr_ws_warm(&w->gate.cancelled);
        event("asr_warm",agent_err_name(warmed));heap_sample(w);
        atomic_store_explicit(&owner.asr_result,warmed,memory_order_release);
        control(w);
    }
    agent_err_t error=w->reused?agent_realtime_poll(&w->session,0):
        agent_realtime_begin_text(&w->session,instructions);
    heap_sample(w);if(!error) {
        w->ready_at=(unsigned)esp_agent_now();
        unsigned state=ESP_AGENT_CANDIDATE_WARMING;
        atomic_compare_exchange_strong(&idle_state,&state,ESP_AGENT_CANDIDATE_READY);
    }
    unsigned request=0;
    while(!error && !(request=atomic_load_explicit(&w->gate.request,memory_order_acquire))) {
        error=poll(w);
        if(!error)vTaskDelay(pdMS_TO_TICKS(2));
    }
    control(w);if(!error && atomic_load(&w->gate.cancelled))error=AGENT_ERR_CANCELLED;
    /* Preserve the24KiB limit, but allocate only pages of received audio.
     * No PCM pages are needed by connection setup or the text request. */
    if(!error)heap_sample(w);
    if(!error)error=agent_realtime_user_text(&w->session,agent_candidate_text(&w->gate));
    control(w);if(!error && atomic_load(&w->gate.cancelled))error=AGENT_ERR_CANCELLED;
    if(!error) {
        w->reply.requested=true;w->reply.guess=request&AGENT_CANDIDATE_KIND;
        w->request_at=(unsigned)esp_agent_now();error=agent_realtime_create_response(&w->session);
    }
    while(!error && !w->reply.complete) {
        error=poll(w);heap_sample(w);
        if(!error && w->reply.unusable)error=AGENT_ERR_LIMIT;
        if(!error)error=release_ready(w);
    }
    if(!error && (!w->session.done || !agent_prefetch_output_ready(&w->reply) ||
       !reply_valid(w) ||
       w->reply.text_length>ESP_AGENT_ACK_MAX))error=AGENT_ERR_PROTOCOL;
    /* A fully generated direct reply may transfer to the joined coordinator.
     * Final input and speaker completion still must authorize retention. */
    bool keep=false;
#if AGENT_RT_TEXT_REUSE
    keep=!error && !atomic_load(&w->gate.cancelled) && w->session.text_reuse &&
        w->session.input_count<3 && direct_answer(request&AGENT_CANDIDATE_KIND);
#endif
    if(!keep)close_session(w);
    if(error || atomic_load(&w->gate.cancelled))cache_release(w);
    w->result=error;w->complete_at=(unsigned)esp_agent_now();
    w->stack_free=uxTaskGetStackHighWaterMark(NULL);
    atomic_store_explicit(&w->done,true,memory_order_release);
    unsigned state=atomic_load(&idle_state);
    while(state && !atomic_compare_exchange_weak(&idle_state,&state,ESP_AGENT_CANDIDATE_EXPIRED)) {}
    vTaskSuspend(NULL);
    configASSERT(false);
}
static void join(void)
{
    if(!owner.task)return;
    while(!atomic_load_explicit(&owner.work->done,memory_order_acquire) ||
          eTaskGetState(owner.task)!=eSuspended)vTaskDelay(pdMS_TO_TICKS(1));
    vTaskDelete(owner.task);owner.task=NULL;
    /* Caller has joined live ASR/capture, or owns the idle work lock. The
     * worker cannot close an ASR socket that its consumer has claimed. */
    if(owner.work->warmed && !atomic_load(&owner.asr_result) && !owner.asr_claimed)
        esp_agent_asr_ws.close(esp_agent_asr_ws.ctx);
}
agent_err_t esp_agent_voice_candidate_asr_open(void *ctx,const atomic_bool *cancelled)
{
    candidate_work_t *w=owner.work;
    if(w && w->warmed) {
        uint64_t until=esp_agent_now()+5000;
        while(atomic_load_explicit(&owner.asr_result,memory_order_acquire)<0) {
            if(atomic_load(cancelled))return AGENT_ERR_CANCELLED;
            if(esp_agent_now()>=until)return AGENT_ERR_TIMEOUT;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if(atomic_load(cancelled))return AGENT_ERR_CANCELLED;
        if(!atomic_load(&owner.asr_result)) {
            if(owner.asr_claimed)return AGENT_ERR_BUSY;
            owner.asr_claimed=true;return AGENT_OK;
        }
    }
    return esp_agent_asr_ws.open(ctx,cancelled);
}
void esp_agent_voice_candidate_discard(void)
{
    if(owner.work) {
        configASSERT(owner.warm && !owner.ack.pending);
        atomic_store(&owner.work->gate.cancelled,true);join();close_session(owner.work);
        event("candidate_idle_end",agent_err_name(owner.work->result));
        cache_release(owner.work);owner.work=NULL;owner.warm=false;
    }
#if AGENT_RT_TEXT_REUSE
    retained_close();
#endif
    atomic_store(&idle_state,ESP_AGENT_CANDIDATE_COLD);
}
static agent_err_t bind(void *memory,size_t capacity,const atomic_bool *cancelled,
                        void (*notify)(void *,const char *,const char *),bool warm)
{
    configASSERT(!owner.work && !owner.task && !owner.ack.pending);
    unsigned generation=atomic_load(&cancel_generation);
    memset(&owner,0,sizeof(owner));owner.cancelled=cancelled;owner.event=notify;
    size_t pad=(0-(uintptr_t)memory)&15u;
    if(!memory || capacity<sizeof(candidate_work_t)+pad)return AGENT_ERR_MEMORY;
    owner.work=(candidate_work_t *)((char *)memory+pad);memset(owner.work,0,sizeof(*owner.work));
    /* Text-only candidates never consume audio-input IDs. Reuse that65B slot
     * for an immutable ASR preview, and initialize before publication instead
     * of clearing the reply after the worker has received a nomination. */
    candidate_work_t *w=owner.work;
    _Static_assert(sizeof(w->reply.input_id)==AGENT_CANDIDATE_SOURCE_BYTES,"Candidate source slot");
    agent_prefetch_init(&w->reply,&w->session,NULL,CACHE_BYTES,NULL,0);
    agent_speech_draft_init_pages(&w->reply.audio,w->cache,CACHE_BYTES);
    agent_candidate_gate_init(&w->gate,w->reply.input_id);atomic_init(&w->done,false);
    atomic_init(&owner.work->play_permitted,false);atomic_init(&owner.work->streaming,false);
    atomic_init(&owner.work->request_ready,false);
    atomic_init(&owner.asr_result,-1);
    atomic_init(&owner.work->claimed_at,warm?0:(unsigned)esp_agent_now());
    owner.warm=owner.work->warmed=warm;
    owner.work->cancel_generation=generation;
    atomic_store(&idle_state,warm?ESP_AGENT_CANDIDATE_WARMING:ESP_AGENT_CANDIDATE_COLD);
    owner.work->result=AGENT_ERR_NOT_FOUND;
    return AGENT_OK;
}
agent_err_t esp_agent_voice_candidate_warm(void *memory,size_t capacity,const atomic_bool *cancelled,
                                          void (*notify)(void *,const char *,const char *))
{
    if(owner.work || owner.task || owner.ack.pending)return AGENT_ERR_BUSY;
    agent_err_t error=bind(memory,capacity,cancelled,notify,true);
    if(!error) {event("candidate_warming",NULL);esp_agent_voice_candidate_ready();}
    return error?error:owner.task?AGENT_OK:AGENT_ERR_MEMORY;
}
void esp_agent_voice_candidate_begin(const atomic_bool *cancelled,void (*notify)(void *,const char *,const char *))
{
    void *memory=NULL;size_t capacity=0;
    agent_err_t error=esp_hi_voice_live_workspace(&memory,&capacity);
    if(!error && owner.warm && !atomic_load(&owner.work->done) &&
       owner.work->cancel_generation==atomic_load(&cancel_generation)) {
        configASSERT((uintptr_t)owner.work==(((uintptr_t)memory+15u)&~(uintptr_t)15u));
        owner.warm=false;atomic_store(&idle_state,ESP_AGENT_CANDIDATE_COLD);
        atomic_store_explicit(&owner.work->claimed_at,(unsigned)esp_agent_now(),memory_order_release);
        event("candidate_claim",NULL);event("candidate_begin",NULL);return;
    }
    if(owner.work)esp_agent_voice_candidate_discard();
    if(!error)error=bind(memory,capacity,cancelled,notify,false);
    if(error) {if(notify)notify(NULL,"candidate_miss",agent_err_name(error));return;}
    event("candidate_begin",NULL);
}
void esp_agent_voice_candidate_ready(void)
{
    if(!owner.work || owner.task)return;
    candidate_work_t *w=owner.work;
    if(!owner.warm && atomic_load(owner.cancelled)) {atomic_store(&w->gate.cancelled,true);return;}
    /* Reply handling shares ASR upload priority. The transport temporarily
     * lowers only speculative TLS setup, then restores this priority. */
    owner.task=xTaskCreateStatic(worker,"candidate",WORKER_STACK_BYTES,w,3,w->stack,&w->tcb);
    if(!owner.task) {
        w->result=AGENT_ERR_MEMORY;atomic_store(&w->done,true);
        if(owner.warm)atomic_store(&idle_state,ESP_AGENT_CANDIDATE_EXPIRED);
    }
}
void esp_agent_voice_candidate_asr_notice(const char *stage)
{
    /* Let the first TLS owner release handshake transients before the second
     * allocates them. This remains asynchronous to ADC/capture, and neither
     * final-input admission nor candidate playback deadlines change. */
#if AGENT_TLS_COOPERATE
    const char *boundary="asr_connected";
#else
    const char *boundary="asr_connect";
#endif
    if(stage && !strcmp(stage,boundary))esp_agent_voice_candidate_ready();
}
void esp_agent_voice_candidate_revision(unsigned revision)
{ if(owner.work)agent_candidate_revision(&owner.work->gate,revision); }
void esp_agent_voice_candidate_preview(const char *text)
{
    if(!owner.work)return;
#if AGENT_LOCAL_FIRST_PREFETCH
    bool nominated=agent_candidate_preview_local_first(&owner.work->gate,text);
#else
    bool nominated=agent_candidate_preview(&owner.work->gate,text);
#endif
    if(nominated)
        event("candidate_prepare",agent_candidate_text(&owner.work->gate));
}
#if AGENT_HANDOFF_PROBE
agent_err_t esp_agent_voice_candidate_probe(void *memory,size_t capacity,const atomic_bool *cancelled,
    void (*notify)(void *,const char *,const char *),const char *preview,bool inject_failure,bool hold)
{
    if(owner.work || owner.task || owner.ack.pending)return AGENT_ERR_BUSY;
    if(inject_failure && !hold)return AGENT_ERR_ARGUMENT;
    agent_err_t error=bind(memory,capacity,cancelled,notify,false);
    if(error)return error;
    owner.probe=true;owner.probe_failure=inject_failure;owner.probe_hold=hold;
    atomic_init(&owner.probe_prepared,false);atomic_init(&owner.probe_release,false);
    event("candidate_begin",NULL);
    esp_agent_voice_candidate_revision(1);
    esp_agent_voice_candidate_preview(preview);
    if(!atomic_load(&owner.work->gate.request))return AGENT_ERR_ARGUMENT;
    esp_agent_voice_candidate_ready();
    unsigned began=(unsigned)esp_agent_now();
    while(!atomic_load_explicit(&owner.probe_prepared,memory_order_acquire)) {
        if(atomic_load(cancelled))return AGENT_ERR_CANCELLED;
        if(atomic_load_explicit(&owner.work->done,memory_order_acquire))
            return owner.work->result?owner.work->result:AGENT_ERR_PROTOCOL;
        if((unsigned)esp_agent_now()-began>=8000)return AGENT_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return AGENT_OK;
}
#endif
const esp_agent_voice_ack_t *esp_agent_voice_candidate_ack(void) { return &owner.ack; }
agent_err_t esp_agent_voice_candidate_ack_join(agent_err_t error)
{
    if(!owner.ack.pending)return error;
    agent_err_t result=esp_hi_stream_finish(error);owner.ack.pending=false;
    owner.ack.spoken=!result;return result;
}
static agent_err_t persist(agent_engine_t *e,const char *input,agent_err_t result)
{
    /* All readers/task have joined. Reuse the same engine objects as normal
     * turns, and record actual full ASR, never the speculative canonical text. */
    agent_messages_t *m=&e->workspace->messages;strcpy(m->data,"[]");m->used=m->system_used=2;
    agent_err_t error=agent_messages_add(m,"user",input,NULL);
    if(!error && !result)error=agent_messages_add(m,"assistant",owner.ack.text,NULL);
    return agent_voice_history(e,input,result,error,AGENT_VOICE_RECORD_CANDIDATE|AGENT_VOICE_RECORD_PARTIAL);
}
static agent_err_t finish(agent_engine_t *e,const char *input,agent_err_t error,
                          void *pcm,bool *handled,bool allow_defer)
{
    *handled=false;candidate_work_t *w=owner.work;
    bool resuming=owner.deferred;owner.deferred=false;
    /* A local endpoint can land before required details or inside a repair.
     * Never supply the missing argument from history to a tool-capable fallback. */
    if(!resuming)e->clarify_only=!error && agent_speech_pending_argument(input);
    if(!w) {
        agent_err_t restored=esp_agent_workspace_restore();return error?error:restored;
    }
    uint8_t rgb[3];
    bool local=!resuming && !error && agent_speech_light_literal(input,rgb);
    bool agrees=resuming || (!local && agent_candidate_final(&w->gate,input,!error));
    /* One immutable nomination becomes authoritative only after full input.
     * A validated sentence may then stream while its remaining PCM arrives. */
    bool final_answer=agrees && direct_answer(atomic_load(&w->gate.request)&AGENT_CANDIDATE_KIND);
#if AGENT_HANDOFF_PROBE
    if(owner.probe && (resuming || (!allow_defer && final_answer)))
        atomic_store_explicit(&owner.probe_release,true,memory_order_release);
#endif
    unsigned began=resuming?w->final_at:(unsigned)esp_agent_now();
    bool cancelled=atomic_load(owner.cancelled),warmed_http=false,http_ready=false;
    if(!resuming && agrees && !cancelled) {
        event("candidate_final",NULL);w->pcm=pcm;w->final_at=began;
#if AGENT_LOCAL_FIRST_PREFETCH
        w->final_input=input;
#endif
        atomic_store_explicit(&w->play_permitted,true,memory_order_release);
    }
    if(!agrees || error || cancelled)atomic_store(&w->gate.cancelled,true);
    while(owner.task && !atomic_load_explicit(&w->done,memory_order_acquire)) {
        /* Only connect after final admission and actual receipt streaming.
         * Candidate owns WSS/its arena; this main worker alone owns HTTP.
         * No body or engine workspace is needed by the bounded TLS step. */
        if(!resuming && !warmed_http && agrees && !final_answer && !e->gateway &&
           !atomic_load(owner.cancelled) && atomic_load_explicit(&w->request_ready,memory_order_acquire)) {
            warmed_http=true;event("candidate_http_warm_begin",NULL);
            agent_err_t warm_error=esp_agent_http_warm(owner.cancelled);
            http_ready=!warm_error;
            event("candidate_http_warm_end",agent_err_name(warm_error));
#if AGENT_HANDOFF_PROBE
            if(owner.probe && (warm_error || !allow_defer))
                atomic_store_explicit(&owner.probe_release,true,memory_order_release);
#endif
        }
        /* Complete TLS's transient allocations before reserving request
         * scratch. A failed warm or an already completed receipt stays on the
         * joined path, without another speculative network operation. */
        if(allow_defer && http_ready && !error && !atomic_load(owner.cancelled) &&
           !atomic_load(&w->gate.cancelled) && !atomic_load_explicit(&w->done,memory_order_acquire) &&
           (unsigned)esp_agent_now()-began<FINAL_STREAM_WAIT_MS) {
            owner.deferred=true;event("candidate_request_overlap",NULL);
            return AGENT_OK;
        }
        unsigned wait=atomic_load_explicit(&w->streaming,memory_order_acquire)?FINAL_STREAM_WAIT_MS:
            final_answer?FINAL_ANSWER_WAIT_MS:PREVIEW_WAIT_MS;
        /* Unsigned elapsed time also works across the32-bit millisecond wrap. */
        if((unsigned)esp_agent_now()-began>=wait || atomic_load(owner.cancelled))atomic_store(&w->gate.cancelled,true);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    /* On single-core C3, wait until the task has actually suspended before
     * external deletion. IDF then destroys TCB/TLS synchronously, not in idle. */
    join();
    if(atomic_load(owner.cancelled))error=AGENT_ERR_CANCELLED;
    /* A producer may finish and park its silent cache before final capture.
     * That fast path must perform the same grounding as the live producer. */
    bool grounded=agent_candidate_reply(&w->gate,w->reply.text);
#if AGENT_LOCAL_FIRST_PREFETCH
    if((atomic_load(&w->gate.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK)
        grounded=agent_candidate_reply_final(&w->gate,input,w->reply.text);
#endif
    bool hit=!error && agrees && grounded && !atomic_load(&w->gate.cancelled) && atomic_load(&w->done) && !w->result;
    bool streaming=atomic_load(&w->streaming);
    char stats[480];snprintf(stats,sizeof(stats),
        "{\"hit\":%s,\"revoked\":%s,\"reused\":%s,\"result\":\"%s\",\"started_ms\":%u,\"opened_ms\":%u,\"ready_ms\":%u,\"request_ms\":%u,\"pcm_ms\":%u,\"complete_ms\":%u,\"samples\":%u,\"stack_free\":%u,\"sampled_heap_low\":%u,\"arena_bytes\":%u,\"claimed_ms\":%u,\"cache_peak\":%u"
#if AGENT_WS_CONNECT_TRACE
        ",\"created_ms\":%u,\"tls_begin_ms\":%u,\"tls_end_ms\":%u"
#endif
        "}",
        hit?"true":"false",w->gate.revoked?"true":"false",w->reused?"true":"false",agent_err_name(w->result),
        w->started_at,w->opened_at,w->ready_at,w->request_at,w->reply.first_pcm_at,w->complete_at,(unsigned)(w->session.pcm_bytes/2),
        w->stack_free,w->heap_low,(unsigned)sizeof(*w),atomic_load(&w->claimed_at),w->cache_peak
#if AGENT_WS_CONNECT_TRACE
        ,w->created_at,w->tls_began,w->tls_ended
#endif
        );event("candidate_stats",stats);
#if AGENT_TLS_COOPERATE
    unsigned tls_steps,tls_step_max;
    esp_agent_tls_cooperate_stats(&tls_steps,&tls_step_max);
    snprintf(stats,sizeof(stats),"{\"steps\":%u,\"max_step_ms\":%u}",tls_steps,tls_step_max);
    event("candidate_tls_schedule",stats);
#endif
    snprintf(stats,sizeof(stats),"{\"acked_ms\":%u}",owner.text_acked_at);
    event("candidate_text_clock",stats);
    snprintf(stats,sizeof(stats),"{\"polls\":%u,\"poll_begin\":%u,\"poll_end\":%u,\"pcm_begin\":%u,\"pcm_end\":%u,"
        "\"message_bytes\":%u,\"message_open\":%s,\"response_done\":%s,\"output_complete\":%s,\"request_ready\":%s}",
        owner.clock.polls,owner.clock.poll_begin,owner.clock.poll_end,owner.clock.pcm_begin,owner.clock.pcm_end,(unsigned)w->session.used,
        w->session.message?"true":"false",w->session.done?"true":"false",w->reply.complete?"true":"false",
        atomic_load(&w->request_ready)?"true":"false");
    event("candidate_receive_clock",stats);
#if AGENT_HANDOFF_PROBE
    esp_agent_speech_ws_probe(stats,sizeof(stats));
    event("candidate_transport_receive",stats);
    snprintf(stats,sizeof(stats),"{\"last\":\"%s\",\"at\":%u,\"events\":%u}",
        owner.last_event,owner.last_event_at,owner.protocol_events);
    event("candidate_protocol_last",stats);
    for(unsigned i=0;i<3;++i)for(unsigned side=0;side<2;++side)
        if(esp_agent_speech_ws_tcp_probe(i,side,stats,sizeof(stats)))event("candidate_tcp",stats);
#endif
#if AGENT_TLS_PHASE_TRACE
    if(w->opened_at && !w->reused) {
        /* The sole writer has joined; no next candidate can begin until
         * this coordinator returns. Avoid another per-worker clock copy. */
        const unsigned *p=esp_agent_tls_phase_clock();
        /* Fixed tuple follows TLS_PHASE_COUNT's public index order. */
        snprintf(stats,sizeof(stats),"[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",
            p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9]);
        event("candidate_tls",stats);
        bool overflow=false;unsigned count=esp_agent_tls_verify_count(&overflow);
        snprintf(stats,sizeof(stats),"{\"count\":%u,\"overflow\":%s}",count,overflow?"true":"false");
        event("candidate_tls_verify_summary",stats);
        for(unsigned i=0;i<count;++i) {
            const tls_verify_detail_t *v=esp_agent_tls_verify_detail(i);
            snprintf(stats,sizeof(stats),
                "{\"row\":%u,\"algorithm\":%u,\"type\":%u,\"bits\":%u,\"signature_bytes\":%u,"
                "\"wall_ms\":%u,\"scheduled_us\":%u,\"soft_calls\":%u,\"soft_ms\":%u,"
                "\"soft_scheduled_us\":%u,\"modulus_bits\":%u,\"exponent_bits\":%u,"
                "\"exponent_limbs\":%u,\"attributes_status\":%d,\"result\":%d}",
                i,v->algorithm,v->key_type,v->key_bits,v->signature_bytes,v->wall_ms,v->scheduled_us,
                v->soft_calls,v->soft_ms,v->soft_scheduled_us,v->modulus_bits,v->exponent_bits,
                v->exponent_limbs,v->attributes_status,v->result);
            event("candidate_tls_verify",stats);
        }
    }
#endif
    if(!hit && !streaming) {
        if(w->reply.text_length && w->reply.text_length<=ESP_AGENT_ACK_MAX)
            event("candidate_rejected",w->reply.text);
        event("candidate_miss",local?"final_local_light":!agrees?"final_input_changed":
            !grounded?"final_topic_changed":agent_err_name(w->result));
        close_session(w);cache_release(w);owner.work=NULL;
        agent_err_t restored=esp_agent_workspace_restore();
        if(!error)error=restored;
        if(local) {
            *handled=true; /* An attempted effect must never fall through. */
            if(!error)error=esp_agent_voice_fast_light(e,input,owner.event);
        }
        return error;
    }
    bool task=!direct_answer(w->reply.guess);
    if(streaming) {if(!error)error=w->result;}
    else error=start_stream(w,w->scratch,4096);
    if(!error && !streaming)error=drain(w);
    if(!error && task) {error=esp_hi_stream_cache_seal();owner.ack.pending=!error;}
    if(atomic_load(&w->streaming) && !owner.ack.pending)error=esp_hi_stream_finish(error);
    cache_release(w);
    bool kept=!error && !task && park(w);
    close_session(w);
    owner.work=NULL; /* No read of the borrowed prefix after this point. */
    agent_err_t restored=kept?esp_agent_workspace_restore_retaining_candidate():esp_agent_workspace_restore();
    if(restored==AGENT_ERR_MEMORY && kept) {
        /* Persistence wins over speculative connection reuse. Retry only
         * the local allocation, after releasing the retained TLS owner. */
        kept=false;restored=esp_agent_workspace_restore();
    }
    if(!error)error=restored;
    if(!task || error) {
        *handled=true;
        if(!error)event("fast_reply",owner.ack.text);
        if(e->workspace && !resuming)error=persist(e,input,error);
    }
    if(error && kept)esp_agent_voice_candidate_discard();
    return error;
}

agent_err_t esp_agent_voice_candidate_finish(agent_engine_t *e,const char *input,agent_err_t error,
                                            void *pcm,bool *handled)
{ return finish(e,input,error,pcm,handled,false); }
agent_err_t esp_agent_voice_candidate_stage(agent_engine_t *e,const char *input,agent_err_t error,
                                           void *pcm,bool *handled)
{ return finish(e,input,error,pcm,handled,true); }
bool esp_agent_voice_candidate_deferred(void) { return owner.deferred; }
