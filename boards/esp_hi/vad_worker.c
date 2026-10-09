#include "vad_worker.h"
#include "phase.h"
#include "vad_backend.h"
#include "confirmation.h"
#include "source_stream.h"
#include "confirmation_tail.h"
#include "speech_backend.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

/* Classic retains its measured stack. Fast reads producer metadata only. */
enum { STACK_BYTES=4096, FAST_STACK_BYTES=1536, WORKER_PRIORITY=4 };
static TaskHandle_t task;
static TaskHandle_t producer;
static unsigned producer_cpu_start;
static esp_hi_confirmation_memory_t memory;
static bool borrowed;
static agent_clip_t *clip_reader;
static agent_confirmation_t confirmation;
static confirmation_tail_t terminal_tail; /* worker-only; metadata remains borrowed */
static source_stream_t source_stream; /* audio producer owns this object */
static uint8_t *records; /* immutable after sample publication */
static void *arena_prefix;
static size_t arena_capacity;
static unsigned frozen_noise;
static bool prepared,started,fast_mode; /* Audio owner configures before creating the worker. */
static int16_t *raw_pcm;
static atomic_uint available,available_bytes,start_ms;
static atomic_bool cancelled,complete,finish_requested;
static atomic_uint asr_notice;
#if AGENT_ENDPOINT_TRACE
static endpoint_notice_trace_t notice_trace;
const endpoint_notice_trace_t *esp_hi_confirmation_notice_trace(void) {return &notice_trace;}
#endif
static atomic_int last_error;
static struct {
    atomic_uint frames,processed,backlog,confirmed_ms,confirmed_wall_ms;
    atomic_uint peak,sum5,max_us,stack,bytes,heap_min,elapsed,speech,quiet,end_silence,resume_frames,transcribed_ms,pending_hold_ms,asr_floor_ms,noise,level;
    atomic_uint heap_bytes,arena_bytes,cpu_us,producer_cpu_us,nn_cpu_us,nn_wall_us,nn_cpu_max_us;
    atomic_uint source_samples,source_frames,bound_ms,target_samples,source_stop_wall_ms;
    atomic_uint tail_possible,tail_started,tail_steps;
    atomic_uint tonal_frames,tonal_vetoes,tonal_clean;
    atomic_bool confirmed,deadline,fast,local_end;
} stats;

static unsigned milliseconds(void) { return (unsigned)(esp_timer_get_time()/1000); }
static unsigned task_cpu(TaskHandle_t handle)
{
#if configGENERATE_RUN_TIME_STATS
    _Static_assert(CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER,"Task timing needs microseconds");
    TaskStatus_t info; vTaskGetInfo(handle,&info,pdFALSE,eInvalid);
    return info.ulRunTimeCounter;
#else
    (void)handle; return 0;
#endif
}
static void maximum(atomic_uint *value,unsigned current)
{ if(current>atomic_load(value)) atomic_store(value,current); }
static void resources(void)
{
    unsigned heap=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if(heap<atomic_load(&stats.heap_min)) atomic_store(&stats.heap_min,heap);
    atomic_store(&stats.stack,uxTaskGetStackHighWaterMark(NULL));
    if(atomic_load(&start_ms)) atomic_store(&stats.producer_cpu_us,task_cpu(producer)-producer_cpu_start);
}
static agent_err_t apply_frames(size_t limit,bool *backend_open)
{
    while(confirmation.endpoint.state<AGENT_EP_DONE && (confirmation.endpoint.elapsed_ms+20u)*16u<=limit) {
        if(atomic_load(&cancelled)) { agent_confirmation_cancel(&confirmation);return AGENT_ERR_CANCELLED; }
        bool was_confirmed=confirmation.confirmed;
        source_frame_t frame;
        agent_err_t error=terminal_tail.owner?confirmation_tail_step(&terminal_tail,&confirmation,&frame):
            source_stream_confirm(&confirmation,records,(unsigned)limit,frozen_noise,&frame);
        if(!error && terminal_tail.owner)atomic_fetch_add(&stats.tail_steps,1);
        if(error)return error;
        if(was_confirmed) {
            unsigned threshold=frozen_noise*3/2;if(threshold<240)threshold=240;
            atomic_fetch_add(&stats.tonal_frames,1);
            if(frame.spectral && frame.level>threshold && frame.clean_level<=threshold)
                atomic_fetch_add(&stats.tonal_vetoes,1);
            atomic_store(&stats.tonal_clean,frame.clean_level);
        }
        atomic_store(&stats.elapsed,confirmation.endpoint.elapsed_ms);
        atomic_store(&stats.speech,confirmation.endpoint.speech_ms);
        atomic_store(&stats.quiet,confirmation.endpoint.quiet_ms);
        atomic_store(&stats.level,frame.level);
        if(confirmation.confirmed && *backend_open) {
            /* Shorten only an admitted utterance. The original no-speech and
             * bounded neural-tail proofs continue to use their 1000-ms rule. */
            error=agent_confirmation_set_silence(&confirmation,ESP_HI_CONFIRMED_SILENCE_MS);
            if(error)return error;
            atomic_store(&stats.end_silence,confirmation.endpoint.end_ms);
            atomic_store(&stats.confirmed,true);
            atomic_store(&stats.confirmed_ms,confirmation.confirmed_ms);
            atomic_store(&stats.confirmed_wall_ms,milliseconds()-atomic_load(&start_ms));
            esp_hi_vad_close();*backend_open=false;
        }
    }
    return AGENT_OK;
}
static agent_err_t run_fast(void)
{
#if AGENT_CAPTURE_PROBE
    unsigned shadow_frames=0;
#endif
    for(;;) {
        unsigned written=atomic_load_explicit(&available,memory_order_acquire);
        resources();
        if(atomic_load(&cancelled)) {
            agent_confirmation_cancel(&confirmation);return AGENT_ERR_CANCELLED;
        }
#if AGENT_CAPTURE_PROBE
        while(shadow_frames<400 && (shadow_frames+1)*320u<=written) {
#else
        while(confirmation.endpoint.state<AGENT_EP_DONE &&
              (confirmation.endpoint.elapsed_ms+20)*16u<=written) {
#endif
            if(atomic_load(&cancelled)) {
                agent_confirmation_cancel(&confirmation);return AGENT_ERR_CANCELLED;
            }
            source_frame_t frame;
            unsigned notice=atomic_load(&asr_notice);
#if AGENT_ENDPOINT_TRACE
#if AGENT_CAPTURE_PROBE
            endpoint_notice_record(&notice_trace,shadow_frames*20,notice);
            ++shadow_frames;
            /* The first local terminal remains frozen. Future notices are
             * source-clock observations for counterfactual replay only. */
            if(confirmation.endpoint.state>=AGENT_EP_DONE)continue;
#else
            endpoint_notice_record(&notice_trace,confirmation.endpoint.elapsed_ms,notice);
#endif
#endif
            agent_endpoint_observe(&confirmation.endpoint,notice);
            agent_err_t error=source_stream_endpoint(&confirmation.endpoint,records,written,frozen_noise,&frame);
            if(error)return error;
            atomic_store(&stats.elapsed,confirmation.endpoint.elapsed_ms);
            atomic_store(&stats.processed,confirmation.endpoint.elapsed_ms*16u);
            atomic_store(&stats.frames,confirmation.endpoint.elapsed_ms/20);
            atomic_store(&stats.speech,confirmation.endpoint.speech_ms);
            atomic_store(&stats.quiet,confirmation.endpoint.quiet_ms);
            atomic_store(&stats.resume_frames,confirmation.endpoint.resume_frames);
            atomic_store(&stats.pending_hold_ms,confirmation.endpoint.held_frames*20);
            atomic_store(&stats.transcribed_ms,confirmation.endpoint.transcribed_ms);
            atomic_store(&stats.asr_floor_ms,confirmation.endpoint.asr_floor_ms);
            atomic_store(&stats.level,frame.level);atomic_store(&stats.tonal_clean,frame.clean_level);
            bool confirmed=confirmation.endpoint.local_onset || confirmation.endpoint.transcribed_ms;
            if(confirmed!=atomic_load(&stats.confirmed)) {
                atomic_store(&stats.confirmed,confirmed);
                atomic_store(&stats.confirmed_ms,confirmed?confirmation.endpoint.elapsed_ms:0);
                atomic_store(&stats.confirmed_wall_ms,confirmed?milliseconds()-atomic_load(&start_ms):0);
            }
        }
#if AGENT_CAPTURE_PROBE
        if(shadow_frames==400 && confirmation.endpoint.state<AGENT_EP_DONE)
            confirmation.endpoint.state=AGENT_EP_LIMIT;
        if(shadow_frames==400) {
#else
        if(confirmation.endpoint.state>=AGENT_EP_DONE) {
#endif
            if(confirmation.endpoint.state!=AGENT_EP_DONE)return AGENT_ERR_TIMEOUT;
            atomic_store(&stats.local_end,true);return AGENT_OK;
        }
        if(written>=AGENT_MIC_RATE*AGENT_CLIP_MAX_MS/1000 ||
           milliseconds()-atomic_load(&start_ms)>=ESP_HI_FAST_CAPTURE_WALL_MS) {
            confirmation.endpoint.state=AGENT_EP_LIMIT;
            atomic_store(&stats.deadline,true);return AGENT_ERR_TIMEOUT;
        }
        /* Classification and filters run once in the producer. A slow network
         * cannot advance this sample clock or turn a limit into an endpoint. */
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(20));
    }
}
static void run(void *unused)
{
    (void)unused;
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    unsigned cpu_start=task_cpu(NULL);size_t processed=0;
    agent_err_t error=fast_mode?run_fast():AGENT_OK;bool backend_open=!fast_mode;
    while(!fast_mode && !error && confirmation.endpoint.state<AGENT_EP_DONE) {
        if(atomic_load(&cancelled)) { agent_confirmation_cancel(&confirmation);error=AGENT_ERR_CANCELLED;break; }
        if(!confirmation.confirmed && milliseconds()-atomic_load(&start_ms)>=ESP_HI_CONFIRM_WALL_MS) {
            atomic_store(&stats.deadline,true);error=AGENT_ERR_TIMEOUT;break;
        }
        size_t written=atomic_load_explicit(&available,memory_order_acquire);
        if(atomic_load(&finish_requested) && confirmation.confirmed) {
            error=apply_frames(written,&backend_open);
            if(!error)error=agent_confirmation_finish(&confirmation);
            break;
        }
        if(!confirmation.confirmed && !terminal_tail.owner && written>=64000) {
            if(!terminal_tail.records)error=confirmation_tail_open(&terminal_tail,records,(unsigned)written,frozen_noise);
            if(error)break;
            agent_err_t admitted=confirmation_tail_begin(&terminal_tail,&confirmation);
            if(!admitted) {
                atomic_store(&stats.tail_possible,terminal_tail.last_possible_ms);
                atomic_store(&stats.tail_started,milliseconds()-atomic_load(&start_ms));
                esp_hi_vad_close();backend_open=false;
            } else if(admitted!=AGENT_ERR_BUSY) { error=admitted;break; }
        }
        bool classic_only=confirmation.confirmed || terminal_tail.owner;
        size_t cursor=classic_only?confirmation.endpoint.elapsed_ms*16u:processed;
        if(written<cursor) { error=AGENT_ERR_CORRUPT;break; }
        maximum(&stats.backlog,(unsigned)(written-cursor));
        if(classic_only) {
            /* Published immutable metadata is the only remaining input after
             * actual confirmation. No raw reread or duplicated classic work. */
            if(written-cursor<320) { ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1));continue; }
            error=apply_frames(written,&backend_open);
            atomic_store(&stats.processed,confirmation.endpoint.elapsed_ms*16u);
        } else {
            if(written-processed<ESP_HI_VAD_SAMPLES) { ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1));continue; }
            size_t stored=atomic_load_explicit(&available_bytes,memory_order_acquire);
            error=agent_clip_read_pending(clip_reader,written,stored,processed,raw_pcm,ESP_HI_VAD_SAMPLES);
            if(error)break;
            unsigned probability,cpu_before=task_cpu(NULL);int64_t began=esp_timer_get_time();
            error=esp_hi_vad_process(raw_pcm,&probability);
            unsigned wall=(unsigned)(esp_timer_get_time()-began),cpu=task_cpu(NULL)-cpu_before;
            maximum(&stats.max_us,wall);maximum(&stats.nn_cpu_max_us,cpu);
            atomic_fetch_add(&stats.nn_wall_us,wall);atomic_fetch_add(&stats.nn_cpu_us,cpu);
            if(error)break;
            error=agent_confirmation_score(&confirmation,probability);if(error)break;
            maximum(&stats.peak,probability);maximum(&stats.sum5,confirmation.sum);
            atomic_store(&stats.frames,confirmation.frames);
            processed+=ESP_HI_VAD_SAMPLES;
            error=apply_frames(processed,&backend_open);atomic_store(&stats.processed,(unsigned)processed);
        }
        resources();vTaskDelay(1);
        atomic_store(&stats.cpu_us,task_cpu(NULL)-cpu_start);
    }
    if(backend_open)esp_hi_vad_close();
    resources();atomic_store(&last_error,error);atomic_store(&stats.cpu_us,task_cpu(NULL)-cpu_start);
    atomic_store_explicit(&complete,true,memory_order_release);
    vTaskSuspend(NULL);
}

void esp_hi_confirmation_memory(const esp_hi_confirmation_memory_t *ops)
{ if(ops && !prepared && !task && !borrowed) memory=*ops; }
static void release_memory(void)
{
    if(borrowed) { memory.release(memory.ctx);borrowed=false; }
    raw_pcm=NULL;clip_reader=NULL;
    records=NULL;arena_prefix=NULL;arena_capacity=0;
    memset(&source_stream,0,sizeof(source_stream));memset(&terminal_tail,0,sizeof(terminal_tail));
}
static bool spectral(void *ctx,int16_t *pcm)
{ (void)ctx;phase_stamp_t begin=phase_enter();bool speech=esp_hi_speech_vad(pcm);phase_leave(PH_SPECTRAL,begin);return speech; }
static agent_err_t configure_fast(void)
{
    agent_err_t error=agent_endpoint_init(&confirmation.endpoint,ESP_HI_FAST_SILENCE_MS,4000,AGENT_CLIP_MAX_MS);
    if(error)return error;
    /* Configure before the producer or classifier runs. Classic capture keeps
     * the initial mode3 detector; no second detector or runtime arena is added. */
    if(!esp_hi_speech_vad_fast())return AGENT_ERR_MEMORY;
    fast_mode=true;source_stream.fast_guard=true;
    source_stream.bound.end_grace_ms=AGENT_EP_PENDING_MS;
    atomic_store(&stats.fast,true);atomic_store(&stats.end_silence,confirmation.endpoint.end_ms);
    atomic_store(&stats.bytes,0);atomic_store(&stats.heap_bytes,0);atomic_store(&stats.arena_bytes,0);
    return AGENT_OK;
}
agent_err_t esp_hi_confirmation_open(agent_clip_t *source,unsigned noise,int16_t *raw,int16_t *filtered)
{
    if(prepared || task) return AGENT_ERR_BUSY;
    if(!source || !source->flash.read || !raw || !filtered || noise>32768) return AGENT_ERR_ARGUMENT;
    agent_err_t error=agent_confirmation_init(&confirmation,1000,4000,AGENT_CLIP_MAX_MS);
    if(error) return error;
    started=fast_mode=false;
#if AGENT_ENDPOINT_TRACE
    memset(&notice_trace,0,sizeof(notice_trace));
#endif
    /* USB may read telemetry while a new cycle opens; reset atomic fields
     * individually rather than writing through their object representation. */
#define RESET(name) atomic_store(&stats.name,0)
    RESET(frames); RESET(processed); RESET(backlog); RESET(confirmed_ms); RESET(confirmed_wall_ms);
    RESET(peak); RESET(sum5); RESET(max_us); RESET(stack); RESET(bytes); RESET(elapsed);
    RESET(speech); RESET(quiet); RESET(noise); RESET(level); RESET(confirmed); RESET(deadline);
    RESET(heap_bytes); RESET(arena_bytes); RESET(cpu_us); RESET(producer_cpu_us); RESET(nn_cpu_us); RESET(nn_wall_us); RESET(nn_cpu_max_us);
    RESET(tail_possible);RESET(tail_started);RESET(tail_steps);
    RESET(tonal_frames);RESET(tonal_vetoes);RESET(tonal_clean);
    RESET(fast);RESET(local_end);RESET(resume_frames);RESET(transcribed_ms);RESET(pending_hold_ms);RESET(asr_floor_ms);
#undef RESET
    atomic_store(&stats.end_silence,confirmation.endpoint.end_ms);
    memset(&terminal_tail,0,sizeof(terminal_tail));
    atomic_store(&stats.source_samples,0);atomic_store(&stats.source_frames,0);atomic_store(&stats.bound_ms,0);
    atomic_store(&stats.target_samples,0);atomic_store(&stats.source_stop_wall_ms,0);
    frozen_noise=noise;clip_reader=source;raw_pcm=raw;
    producer=xTaskGetCurrentTaskHandle(); producer_cpu_start=0;
    atomic_store(&stats.noise,noise); atomic_store(&stats.heap_min,UINT32_MAX);
    atomic_store(&available,0); atomic_store(&available_bytes,0); atomic_store(&start_ms,0);
    atomic_store(&cancelled,false); atomic_store(&complete,false); atomic_store(&last_error,AGENT_OK);
    atomic_store(&finish_requested,false);atomic_store(&asr_notice,0);
    void *workspace=NULL; size_t capacity=0;bool requested_fast=false;
    if(!memory.acquire || !memory.release) error=AGENT_ERR_CONFIG;
    else { error=memory.acquire(memory.ctx,&workspace,&capacity,&requested_fast); borrowed=!error; }
    if(!error && (!workspace || capacity<SOURCE_METADATA_BYTES))error=AGENT_ERR_MEMORY;
    if(!error) {
        size_t prefix=capacity-SOURCE_METADATA_BYTES;
        arena_prefix=workspace;arena_capacity=prefix;
        records=(uint8_t *)workspace+prefix;
        error=source_stream_init(&source_stream,records,SOURCE_METADATA_BYTES,filtered,noise,spectral,NULL);
        if(!error)error=requested_fast?configure_fast():esp_hi_vad_open_at(workspace,prefix);
        if(!error && !requested_fast && esp_hi_vad_heap_bytes()) { esp_hi_vad_close();error=AGENT_ERR_MEMORY; }
    }
    if(error) { release_memory();atomic_store(&last_error,error);atomic_store(&complete,true);return error; }
    if(!requested_fast) {
        atomic_store(&stats.bytes,(unsigned)esp_hi_vad_bytes());
        atomic_store(&stats.heap_bytes,(unsigned)esp_hi_vad_heap_bytes());
        atomic_store(&stats.arena_bytes,(unsigned)esp_hi_vad_arena_bytes());
    }
    prepared=true;
    return AGENT_OK;
}
agent_err_t esp_hi_confirmation_fast(bool fast)
{
    if(!prepared)return AGENT_ERR_CONFIG;
    if(xTaskGetCurrentTaskHandle()!=producer || started || atomic_load(&complete) || atomic_load(&cancelled))
        return AGENT_ERR_BUSY;
    if(fast_mode)return fast?AGENT_OK:AGENT_ERR_BUSY;
    if(fast) {
        esp_hi_vad_close();return configure_fast();
    }
    return AGENT_OK;
}
agent_err_t esp_hi_confirmation_start(void)
{
    if(!prepared)return AGENT_ERR_CONFIG;
    if(xTaskGetCurrentTaskHandle()!=producer || started)return AGENT_ERR_BUSY;
    if(atomic_load(&complete))return atomic_load(&last_error);
    unsigned stack=fast_mode?FAST_STACK_BYTES:STACK_BYTES;
    if(xTaskCreate(run,"agent_confirm",stack,NULL,WORKER_PRIORITY,&task)!=pdPASS) {
        task=NULL;
        if(!fast_mode)esp_hi_vad_close();
        atomic_store(&last_error,AGENT_ERR_MEMORY);atomic_store(&complete,true);
        return AGENT_ERR_MEMORY;
    }
    started=true;producer_cpu_start=task_cpu(NULL);
    atomic_store(&start_ms,milliseconds());xTaskNotifyGive(task);
    return AGENT_OK;
}
agent_err_t esp_hi_confirmation_fast_workspace(void **memory,size_t *capacity)
{
    if(!memory || !capacity || !prepared || !fast_mode || !borrowed || started ||
       xTaskGetCurrentTaskHandle()!=producer)return AGENT_ERR_BUSY;
    *memory=arena_prefix;*capacity=arena_capacity;return AGENT_OK;
}
agent_err_t esp_hi_confirmation_source(int16_t sample)
{
    if(!task)return AGENT_ERR_CONFIG;
    agent_err_t error=source_stream_feed(&source_stream,sample);
    if(!error && (source_stream.samples%320==0 || source_stream_done(&source_stream))) {
        atomic_store(&stats.source_samples,source_stream.samples);
        atomic_store(&stats.source_frames,source_stream.frames);
        atomic_store(&stats.target_samples,source_stream.bound.target_samples);
        if(source_stream.bound.target_samples)atomic_store(&stats.bound_ms,source_stream.bound.elapsed_ms);
    }
    return error;
}
bool esp_hi_confirmation_capture_done(void)
{
    return source_stream_done(&source_stream);
}
agent_err_t esp_hi_confirmation_source_frame(unsigned *end_ms,bool *speech)
{
    if(!end_ms || !speech || !task || !source_stream.frames || source_stream.samples%SOURCE_FRAME_SAMPLES)
        return AGENT_ERR_ARGUMENT;
    source_frame_t frame;
    agent_err_t error=source_stream_read(records,SOURCE_METADATA_BYTES,source_stream.samples,
        source_stream.frames-1,&frame);
    if(error)return error;
    unsigned threshold=fast_mode?frozen_noise*2:frozen_noise*3/2;if(threshold<240)threshold=240;
    *end_ms=source_stream.frames*20;
    *speech=frame.spectral && frame.level>threshold && frame.clean_level>threshold;
    return AGENT_OK;
}
void esp_hi_confirmation_capture_stopped(void)
{
    unsigned expected=0,value=milliseconds()-atomic_load(&start_ms);
    atomic_compare_exchange_strong(&stats.source_stop_wall_ms,&expected,value);
}
agent_err_t esp_hi_confirmation_publish(size_t written,size_t bytes)
{
    if(!task)return AGENT_ERR_CONFIG;
    if(written<atomic_load(&available) || bytes<atomic_load(&available_bytes) ||
       written>source_stream.samples || written/320>source_stream.frames || bytes>clip_reader->flash.size)return AGENT_ERR_CORRUPT;
    {
        /* Seeing a new sample count guarantees at least its earlier byte bound.
         * A newer byte bound is harmless; records are immutable after writing. */
        atomic_store_explicit(&available_bytes,(unsigned)bytes,memory_order_release);
        atomic_store_explicit(&available,(unsigned)written,memory_order_release);
        xTaskNotifyGive(task);
    }
    return AGENT_OK;
}
bool esp_hi_confirmation_done(void) { return atomic_load_explicit(&complete,memory_order_acquire); }
void esp_hi_confirmation_cancel(void)
{
    if(!prepared || atomic_load(&complete))return;
    atomic_store(&cancelled,true);
    if(task)xTaskNotifyGive(task);
    else {
        /* Cue/setup cancellation can precede task creation. Join still owns
         * the borrowed workspace, but must never wait for an absent worker. */
        agent_confirmation_cancel(&confirmation);
        if(!fast_mode)esp_hi_vad_close();
        atomic_store(&last_error,AGENT_ERR_CANCELLED);atomic_store(&complete,true);
    }
}
void esp_hi_confirmation_finish(void)
{ if(task && !fast_mode) { atomic_store(&finish_requested,true); xTaskNotifyGive(task); } }
void esp_hi_confirmation_hint(bool meaningful,bool pending,bool empty,bool settled,bool phrase)
{
    if(atomic_load(&stats.fast) && !atomic_load(&complete) && !atomic_load(&cancelled)) {
        /* Both publishers hold live_end_lock; no mixed old proposal/new hint.
         * A later draft retains the unconsumed empty-update generation. */
        atomic_store(&asr_notice,agent_endpoint_notice_with_phrase(atomic_load(&asr_notice),
            meaningful,pending,empty,settled,phrase));
    }
}
void esp_hi_confirmation_cloud_end(unsigned ms)
{ atomic_store(&asr_notice,agent_endpoint_proposal(atomic_load(&asr_notice),ms)); }
agent_err_t esp_hi_confirmation_join(agent_endpoint_t *endpoint)
{
    if(!prepared) return AGENT_OK;
    if(!task && !esp_hi_confirmation_done())esp_hi_confirmation_cancel();
    while(!esp_hi_confirmation_done()) vTaskDelay(1);
    if(endpoint) *endpoint=confirmation.endpoint;
    if(task) { vTaskDelete(task);task=NULL; }
    if(memory.observe && records && source_stream.frames)
        memory.observe(memory.ctx,records,source_stream.frames,frozen_noise,&confirmation.endpoint,fast_mode);
    prepared=started=false;
    release_memory();
    return atomic_load(&last_error);
}
void esp_hi_confirmation_stats(esp_hi_confirmation_stats_t *out)
{
    if(!out) return;
    *out=(esp_hi_confirmation_stats_t){
        .frames=atomic_load(&stats.frames),.processed_samples=atomic_load(&stats.processed),
        .backlog_samples=atomic_load(&stats.backlog),.confirmed_ms=atomic_load(&stats.confirmed_ms),
        .confirmed_wall_ms=atomic_load(&stats.confirmed_wall_ms),.peak=atomic_load(&stats.peak),
        .sum5=atomic_load(&stats.sum5),.max_us=atomic_load(&stats.max_us),.stack_bytes=atomic_load(&stats.stack),
        .model_bytes=atomic_load(&stats.bytes),.heap_min=atomic_load(&stats.heap_min),
        .elapsed_ms=atomic_load(&stats.elapsed),.speech_ms=atomic_load(&stats.speech),
        .quiet_ms=atomic_load(&stats.quiet),.end_silence_ms=atomic_load(&stats.end_silence),
        .resume_frames=atomic_load(&stats.resume_frames),
        .transcribed_ms=atomic_load(&stats.transcribed_ms),
        .pending_hold_ms=atomic_load(&stats.pending_hold_ms),
        .asr_floor_ms=atomic_load(&stats.asr_floor_ms),
        .noise=atomic_load(&stats.noise),.level=atomic_load(&stats.level),
        .confirmed=atomic_load(&stats.confirmed),.deadline=atomic_load(&stats.deadline),
        .fast=atomic_load(&stats.fast),.local_end=atomic_load(&stats.local_end),
        .done=esp_hi_confirmation_done(),.error=atomic_load(&last_error)};
    out->heap_bytes=atomic_load(&stats.heap_bytes); out->arena_bytes=atomic_load(&stats.arena_bytes);
    out->cpu_us=atomic_load(&stats.cpu_us); out->nn_cpu_us=atomic_load(&stats.nn_cpu_us);
    out->producer_cpu_us=atomic_load(&stats.producer_cpu_us);
    out->nn_wall_us=atomic_load(&stats.nn_wall_us); out->nn_cpu_max_us=atomic_load(&stats.nn_cpu_max_us);
    out->cpu_timing=configGENERATE_RUN_TIME_STATS!=0;
    out->source_samples=atomic_load(&stats.source_samples);out->source_frames=atomic_load(&stats.source_frames);
    out->bound_ms=atomic_load(&stats.bound_ms);out->target_samples=atomic_load(&stats.target_samples);
    out->source_stop_wall_ms=atomic_load(&stats.source_stop_wall_ms);
    out->tail_possible_ms=atomic_load(&stats.tail_possible);out->tail_started_ms=atomic_load(&stats.tail_started);
    out->tail_steps=atomic_load(&stats.tail_steps);
    out->tonal_frames=atomic_load(&stats.tonal_frames);out->tonal_vetoes=atomic_load(&stats.tonal_vetoes);
    out->tonal_clean=atomic_load(&stats.tonal_clean);
}
