#include "voice_stream.h"
#include "voice_heap.h"
#include "runtime.h"
#include "audio_board.h"
#include "qianwen.h"
#include "text_pipe.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const agent_ws_ops_t esp_agent_speech_ws;
enum { START_PENDING, START_READY, START_DEFERRED, START_WAIT_MS=15000 };
static struct {
    atomic_bool *cancelled;
    const char *voice;
    char *text,*scratch;
    size_t text_capacity,scratch_capacity;
    void (*event)(void *,const char *,const char *);
    agent_text_pipe_t pipe;
    bool first_text;
    atomic_uint stack,start;
    atomic_int error;
    atomic_bool active,done;
} stream;
_Static_assert(AGENT_TEXT_PIPE_CAPACITY==AGENT_SPEECH_TEXT_MAX,"Speech text budgets must agree");

static uint64_t clock_ms(void *ctx) { (void)ctx;return esp_agent_now(); }
static void event(const char *stage)
{ if(stream.event)stream.event(NULL,stage,NULL); }
static agent_err_t open_pcm(void *ctx,unsigned rate)
{
    (void)ctx;
    if(rate!=24000)return AGENT_ERR_PROTOCOL;
    event("playback");return AGENT_OK;
}
static agent_err_t write_pcm(void *ctx,const int16_t *pcm,size_t n)
{ (void)ctx;return esp_hi_stream_write(pcm,n,stream.cancelled); }

static void speak_task(void *ctx)
{
    (void)ctx;
    agent_speech_t speech={.cancelled=stream.cancelled,.scratch=stream.scratch,
        .capacity=AGENT_QWEN_SCRATCH,.now_ms=clock_ms,.event=stream.event,
        .random_u32=esp_random};
    agent_qwen_t session;agent_qwen_init(&session,&speech,&esp_agent_speech_ws);
    const agent_pcm_sink_t sink={.open=open_pcm,.write=write_pcm};
    agent_err_t error=AGENT_OK;
#if AGENT_VOICE_SCRATCH_LOANS
    error=agent_qwen_use_rx_pcm(&session);
#endif
    /* A requested song/plan may already own the speaker. Preserve its existing
     * bounded join and cancellation instead of starting a second DMA writer. */
    uint64_t until=esp_agent_now()+90000;
    for(;;) {
        if(error)break;
        agent_audio_state_t audio={0};error=esp_hi_audio_ops.inspect(NULL,&audio);
        if(error || !audio.playing)break;
        /* Preserve existing songs/plans: their owner may last up to90s. Do
         * not hold an unrelated HTTP response behind that bounded join. */
        atomic_store_explicit(&stream.start,START_DEFERRED,memory_order_release);
        if(atomic_load(stream.cancelled)) {error=AGENT_ERR_CANCELLED;break;}
        if(esp_agent_now()>=until) {error=AGENT_ERR_TIMEOUT;break;}
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    bool opened=false;
    if(!error && atomic_load(stream.cancelled))error=AGENT_ERR_CANCELLED;
    if(!error) {
        /* Reserve the original ring before the network handshake so the audio
         * owner can settle PDM at digital zero while the first PCM is pending. */
        error=esp_hi_stream_open(24000,stream.scratch+AGENT_QWEN_SCRATCH,
            (stream.scratch_capacity-AGENT_QWEN_SCRATCH)&~(size_t)1);
        opened=!error;
    }
    if(!error) {event("tts");error=agent_qianwen_tts.begin(&speech,stream.voice,&sink);}
    if(!error)atomic_store_explicit(&stream.start,START_READY,memory_order_release);
    size_t read=0;
    bool first=true;
    while(!error) {
        if(atomic_load(stream.cancelled)) {error=AGENT_ERR_CANCELLED;break;}
        const char *text;size_t n;bool finished;
        error=agent_text_pipe_next(&stream.pipe,read,&text,&n,&finished);
        if(error)break;
        if(n) {
            if(first) {event("tts_text");first=false;}
            error=agent_qianwen_tts.feed_text(&speech,text,n);read+=n;
        } else if(finished) {
            error=agent_qianwen_tts.finish(&speech);break;
        } else error=agent_qwen_poll(&session,20);
    }
    if(error) {
        atomic_store(&stream.error,error);
        agent_qianwen_tts.cancel(&speech);
        atomic_store(stream.cancelled,true);
    }
    if(opened)error=esp_hi_stream_finish(error);
    atomic_store(&stream.error,error);
    atomic_store(&stream.stack,uxTaskGetStackHighWaterMark(NULL));
    atomic_store(&stream.done,true);
    vTaskDelete(NULL);
}

void esp_agent_voice_stream_prepare(atomic_bool *cancelled,const char *voice,
    char *text,size_t text_capacity,char *scratch,size_t scratch_capacity,
    void (*notify)(void *,const char *,const char *))
{
    stream.cancelled=cancelled;stream.voice=voice;stream.text=text;
    stream.text_capacity=text_capacity;stream.scratch=scratch;
    stream.scratch_capacity=scratch_capacity;stream.event=notify;
    stream.first_text=false;atomic_store(&stream.error,AGENT_OK);
    atomic_store(&stream.start,START_PENDING);
    atomic_store(&stream.done,false);
    atomic_store(&stream.active,false);
}
agent_err_t esp_agent_voice_stream_begin(void *ctx)
{
    (void)ctx;
    if(atomic_load(&stream.active))return AGENT_OK;
    if(!stream.cancelled || !stream.text || stream.text_capacity<AGENT_SPEECH_TEXT_MAX ||
       !stream.scratch || stream.scratch_capacity<AGENT_QWEN_SCRATCH+4096)return AGENT_ERR_CONFIG;
    if(atomic_load(stream.cancelled))return AGENT_ERR_CANCELLED;
    agent_err_t error=agent_text_pipe_begin(&stream.pipe,stream.text,stream.text_capacity,stream.cancelled);
    if(error)return error;
    event("llm_body_sent");
    atomic_store(&stream.active,true);
    if(xTaskCreate(speak_task,"agent_tts",7168,NULL,3,NULL)!=pdPASS) {
        atomic_store(&stream.active,false);return AGENT_ERR_MEMORY;
    }
    /* on_sent runs before HTTP response parsing. Cloud generation is already
     * in flight; avoid overlapping its TLS read/JSON allocations with the TTS
     * handshake. The ordinary text/PCM stream resumes as soon as TTS is ready.
     * A pre-existing speaker owner keeps the former asynchronous behaviour.
     * end() must still join every partial start, including timeout/cancel. */
    uint64_t until=esp_agent_now()+START_WAIT_MS;
    while(atomic_load_explicit(&stream.start,memory_order_acquire)==START_PENDING) {
        if(atomic_load(&stream.done))return atomic_load(&stream.error);
        if(atomic_load(stream.cancelled)) {
            agent_err_t failed=atomic_load(&stream.error);
            return failed?failed:AGENT_ERR_CANCELLED;
        }
        if(esp_agent_now()>=until) {
            atomic_store(stream.cancelled,true);return AGENT_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return AGENT_OK;
}
agent_err_t esp_agent_voice_stream_write(void *ctx,const char *text,size_t length)
{
    (void)ctx;
    if(!atomic_load(&stream.active))return AGENT_ERR_PROTOCOL;
    if(atomic_load(&stream.done))return atomic_load(&stream.error)?atomic_load(&stream.error):AGENT_ERR_PROTOCOL;
    if(atomic_load(stream.cancelled))return AGENT_ERR_CANCELLED;
    /* Stamp receipt before publishing: the consumer can run immediately and
     * submit text to TTS before this producer reaches optional USB mirroring. */
    if(length && !stream.first_text) {stream.first_text=true;event("llm_first_text");}
    return agent_text_pipe_write(&stream.pipe,text,length);
}
agent_err_t esp_agent_voice_stream_seal(void)
{
    if(!atomic_load(&stream.active))return AGENT_ERR_PROTOCOL;
    agent_text_pipe_finish(&stream.pipe);
    return AGENT_OK;
}
agent_err_t esp_agent_voice_stream_end(void *ctx,agent_err_t error)
{
    (void)ctx;
    if(!atomic_load(&stream.active))return error;
    if(error)atomic_store(stream.cancelled,true);
    event("llm_done");agent_text_pipe_finish(&stream.pipe);
    while(!atomic_load(&stream.done))vTaskDelay(pdMS_TO_TICKS(5));
    agent_err_t spoken=atomic_load(&stream.error);
    atomic_store(&stream.active,false);
    return (!error || error==AGENT_ERR_CANCELLED) && spoken?spoken:error;
}
unsigned esp_agent_voice_stream_stack(void) { return atomic_load(&stream.stack); }
agent_err_t esp_agent_voice_answer_end(void *ctx,agent_err_t error)
{
    esp_agent_voice_heap_mark(VOICE_HEAP_ANSWER_END);
#if AGENT_VOICE_HTTP_RELEASE
    /* The engine has returned from its final HTTP request. The sole HTTP
     * owner closes only that completed connection, retaining session tickets;
     * the independent TTS worker still owns its WSS and borrowed PCM ring. */
    if(atomic_load(&stream.active)) {
#if AGENT_VOICE_HTTP_FORGET
        /* HTTP has returned: no reader or producer can still borrow its
         * buffers. Free its handle too; the independent TTS WSS stays live.
         * The next DeepSeek turn intentionally has no retained TLS ticket. */
        esp_agent_http_forget();
#else
        esp_agent_http_release();
#endif
    }
#endif
    return esp_agent_voice_stream_end(ctx,error);
}
