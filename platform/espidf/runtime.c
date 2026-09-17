#include "runtime.h"
#if AGENT_ENABLE_AUDIO
#include "phase.h"
#endif
#include "busy_trace.h"
#ifdef AGENT_KEYWORD_TRACE
#include "trace.h"
#endif
#ifdef AGENT_HEAP_WATCH
#include "watch.h"
#endif
#ifdef AGENT_BACKGROUND_VERIFY
#include "vad_worker.h"
#endif
#include "json.h"
#include "llm.h"
#include "sse.h"
#include "tools.h"
#if AGENT_ENABLE_AUDIO
#include "audio_board.h"
#include "upload.h"
#include "crc.h"
#endif
#include "board.h"
#include "control_board.h"
#include "display_board.h"
#include "hardware.h"
#include "engine.h"
#include "store.h"
#include "sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

typedef struct {
    char ssid[33], password[65], key[193];
    char gateway_url[193], gateway_token[193], gateway_ca[2049];
    char user_id[65], session_id[65], device_id[33];
} settings_t;

static settings_t settings;
static agent_core_t *kernel;
static nvs_handle_t nvs;
static bool storage_ready;
static SemaphoreHandle_t output_lock;
static atomic_bool wifi_up, wifi_connecting, wifi_paused;
static atomic_uint wifi_failures;
static atomic_uint_fast64_t wifi_retry_at;
static QueueHandle_t jobs;
static TaskHandle_t network_handle;
typedef struct { const char *text; bool stream, control, upload; } turn_job_t;
/* work_lock owns either a queued input or a temporary provisioning copy. */
static union { char input[AGENT_ARGS_MAX+33]; settings_t settings; } work_buffer;
_Static_assert(sizeof(settings_t)<=sizeof(work_buffer.input),"Settings must fit the input workspace");
static agent_engine_t engine;
static agent_context_t context;
static char context_cache[768];
static SemaphoreHandle_t context_cache_lock;
static atomic_bool route_gateway;
static atomic_bool sync_active,sync_cancel,sync_more;
static atomic_int sync_error=AGENT_ERR_CONFIG;
static SemaphoreHandle_t work_lock;
static uint64_t sync_at;
static uint64_t wifi_deadline;
#if AGENT_ENABLE_AUDIO
static agent_upload_t music_upload;
#endif

bool esp_agent_online(void) { return atomic_load(&wifi_up); }

agent_err_t esp_agent_http_auth(agent_http_target_t target, char *base, size_t base_size,
                               char *auth, size_t auth_size, const char **ca)
{
    const char *url = target == AGENT_HTTP_DEEPSEEK ? "https://api.deepseek.com" : settings.gateway_url;
    const char *key = target == AGENT_HTTP_DEEPSEEK ? settings.key : settings.gateway_token;
    if (!url[0] || !key[0]) return AGENT_ERR_CONFIG;
    if (strlen(url) >= base_size) return AGENT_ERR_LIMIT;
    strcpy(base, url);
    size_t length = strlen(base);
    if (length && base[length - 1] == '/') base[length - 1] = 0;
    int n = snprintf(auth, auth_size, "Bearer %s", key);
    if (n < 0 || (size_t)n >= auth_size) return AGENT_ERR_LIMIT;
    *ca = target == AGENT_HTTP_GATEWAY && settings.gateway_ca[0] ? settings.gateway_ca : NULL;
    return AGENT_OK;
}

uint64_t esp_agent_now(void) { return (uint64_t)esp_timer_get_time() / 1000; }

void esp_agent_write(const char *data, size_t size)
{
    if (!output_lock || xSemaphoreTake(output_lock, pdMS_TO_TICKS(2000)) != pdTRUE) return;
    while (size) {
        int n = usb_serial_jtag_write_bytes(data, size, pdMS_TO_TICKS(100));
        if (n <= 0) break;
        data += n; size -= (size_t)n;
    }
    xSemaphoreGive(output_lock);
}

static void say(const char *text) { esp_agent_write(text, strlen(text)); }
static void report(agent_err_t error)
{
    char message[96];
    snprintf(message, sizeof(message), "\r\n@%s %s\r\n", error ? "error" : "ok", agent_err_name(error));
    say(message);
}

void esp_agent_status(char *output, size_t capacity)
{
    agent_json_writer_t w;
    agent_json_writer_init(&w, output, capacity);
    agent_json_printf(&w, "{\"firmware\":\"esp-hi-agent\",\"version\":\"0.6.2-repair\",\"phase\":\"LCD\","
        "\"busy\":%s,\"wifi\":%s,\"nvs\":%s,\"key_configured\":%s,\"time_valid\":%s,"
        "\"free_heap\":%u,\"min_heap\":%u,\"largest_block\":%u,\"stack_watermark\":%u,\"worker_stack\":%u,\"route\":\"%s\","
        "\"sync_active\":%s,\"sync_more\":%s,\"sync_error\":\"%s\",\"device_id\":",
        atomic_load(&kernel->busy) ? "true" : "false", atomic_load(&wifi_up) ? "true" : "false",
        storage_ready ? "true" : "false", settings.key[0] ? "true" : "false", time(NULL) >= 1735689600 ? "true" : "false",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL),
        network_handle ? (unsigned)uxTaskGetStackHighWaterMark(network_handle) : 0,
        atomic_load(&route_gateway) ? "GATEWAY" : "DIRECT",
        atomic_load(&sync_active)?"true":"false",atomic_load(&sync_more)?"true":"false",agent_err_name(atomic_load(&sync_error)));
    agent_json_quote(&w, settings.device_id);
    esp_netif_ip_info_t ip={0};
    esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if(netif && esp_netif_get_ip_info(netif,&ip)==ESP_OK)
        agent_json_printf(&w,",\"ip\":\"" IPSTR "\",\"gateway\":\"" IPSTR "\"",IP2STR(&ip.ip),IP2STR(&ip.gw));
    wifi_ps_type_t power_save;
    if(esp_wifi_get_ps(&power_save)==ESP_OK)
        agent_json_printf(&w,",\"wifi_power_save\":\"%s\"",power_save==WIFI_PS_NONE?"none":power_save==WIFI_PS_MIN_MODEM?"min":"max");
    unsigned flash_ms[4]; esp_agent_store_metrics(flash_ms);
    agent_json_printf(&w,",\"control_stack\":%u,\"plan_active\":%s,\"flash_max_ms\":[%u,%u,%u,%u]}",esp_hi_control_stack(),
        atomic_load(&esp_hi_control.active)?"true":"false",flash_ms[0],flash_ms[1],flash_ms[2],flash_ms[3]);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&wifi_up, true);
        atomic_store(&wifi_connecting, false);
        atomic_store(&wifi_failures, 0);
        esp_netif_sntp_start();
        say("\r\n@wifi connected\r\n");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&wifi_up, false);
        atomic_store(&wifi_connecting, false);
        unsigned failures = atomic_fetch_add(&wifi_failures, 1);
        if (failures > 5) failures = 5;
        atomic_store(&wifi_retry_at, esp_agent_now() + (1000u << failures) + esp_random() % 1000);
    }
}

static agent_err_t wifi_apply(void)
{
    if (!settings.ssid[0]) return AGENT_OK;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, settings.ssid, strlen(settings.ssid));
    memcpy(config.sta.password, settings.password, strlen(settings.password));
    config.sta.threshold.authmode = settings.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
#ifdef AGENT_WIFI_AUDIO_DIAGNOSTICS
    config.sta.listen_interval = 10; /* Used only by the MAX_MODEM comparison. */
#endif
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) return AGENT_ERR_CONFIG;
    atomic_store(&wifi_retry_at, esp_agent_now() + 250);
    return AGENT_OK;
}

static agent_err_t provision(const char *line)
{
    if (atomic_load(&kernel->busy)) return AGENT_ERR_BUSY;
    if (!storage_ready) return AGENT_ERR_STORAGE;
    if(xSemaphoreTake(work_lock,0)!=pdTRUE) return AGENT_ERR_BUSY;
    cJSON *root = agent_json_parse(line, strlen(line));
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); xSemaphoreGive(work_lock); return AGENT_ERR_JSON; }
    settings_t *candidate = &work_buffer.settings;
    *candidate = settings;
    struct field { const char *wire; char *dst; size_t capacity; } fields[] = {
        {"ssid", candidate->ssid, sizeof(candidate->ssid)},
        {"password", candidate->password, sizeof(candidate->password)},
        {"deepseek_api_key", candidate->key, sizeof(candidate->key)},
        {"gateway_url", candidate->gateway_url, sizeof(candidate->gateway_url)},
        {"gateway_token", candidate->gateway_token, sizeof(candidate->gateway_token)},
        {"gateway_ca", candidate->gateway_ca, sizeof(candidate->gateway_ca)},
        {"user_id", candidate->user_id, sizeof(candidate->user_id)},
        {"session_id", candidate->session_id, sizeof(candidate->session_id)}
    };
    agent_err_t error = AGENT_OK;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, fields[i].wire);
        if (!item) continue;
        if (!cJSON_IsString(item) || strlen(item->valuestring) >= fields[i].capacity) {
            error = AGENT_ERR_ARGUMENT; goto done;
        }
        strcpy(fields[i].dst, item->valuestring);
    }
    if (candidate->gateway_url[0] && strncmp(candidate->gateway_url, "https://", 8)) {
        error = AGENT_ERR_TLS; goto done;
    }
    if(!candidate->user_id[0] || !candidate->session_id[0]) { error=AGENT_ERR_ARGUMENT; goto done; }
    if(context.wal.ready && context.lamport && (strcmp(candidate->user_id,settings.user_id) || strcmp(candidate->session_id,settings.session_id))) {
        error=AGENT_ERR_FORBIDDEN; goto done;
    }
    const char *nonce = agent_json_string(root, "nonce");
    if (nonce && (strlen(nonce) != 16 || strspn(nonce, "0123456789abcdef") != 16)) {
        error = AGENT_ERR_ARGUMENT; goto done;
    }
    if (nvs_set_blob(nvs, "settings_v1", candidate, sizeof(*candidate)) != ESP_OK || nvs_commit(nvs) != ESP_OK) {
        error = AGENT_ERR_STORAGE; goto done;
    }
    settings = *candidate;
    uint64_t epoch;
    if (agent_json_uint(cJSON_GetObjectItemCaseSensitive(root, "epoch"), 4102444800ULL, &epoch) && epoch >= 1735689600) {
        struct timeval now = { .tv_sec = (time_t)epoch };
        settimeofday(&now, NULL);
    }
    error = wifi_apply();
    sync_at=0;
    if (!error && nonce) {
        char reply[64];
        snprintf(reply, sizeof(reply), "\r\n@provision_ok %s\r\n", nonce);
        say(reply);
    }
done:
    cJSON_Delete(root);
    /* Volatile writes keep this scratch copy from retaining credentials. */
    for (volatile unsigned char *p = (volatile unsigned char *)candidate;
         p < (volatile unsigned char *)candidate + sizeof(*candidate); ++p) *p = 0;
    xSemaphoreGive(work_lock);
    return error;
}

static agent_err_t output_event(void *ctx, const agent_event_t *event)
{
    (void)ctx;
    if (atomic_load(&kernel->cancelled)) return AGENT_ERR_CANCELLED;
    if (event->type == AGENT_EVENT_TEXT) esp_agent_write(event->data, event->length);
    if (event->type == AGENT_EVENT_TOOL_END && event->data) {
        say("\r\n@tool "); say(engine.reply.calls[event->index].name); say(" ");
        esp_agent_write(event->data,event->length); say("\r\n");
    }
    return AGENT_OK;
}

static agent_err_t tool_status(void *ctx, char *output, size_t capacity)
{
    (void)ctx; esp_agent_status(output, capacity); return AGENT_OK;
}

static agent_err_t tool_context(void *ctx, char *output, size_t capacity)
{
    (void)ctx;
    return agent_context_stats(&context,output,capacity);
}

static uint64_t tool_now(void *ctx) { (void)ctx; return esp_agent_now(); }
static agent_err_t tool_search(void *ctx,const char *query,uint64_t before,unsigned limit,char *out,size_t cap)
{ (void)ctx; return agent_context_search(&context,query,before,limit,out,cap); }
static agent_err_t tool_summary_get(void *ctx,char *out,size_t cap)
{ (void)ctx; return agent_context_summary_get(&context,out,cap); }
static agent_err_t tool_summary_set(void *ctx,const char *text,uint64_t through)
{ (void)ctx; return agent_context_summary_set(&context,text,through); }
static const agent_tool_ops_t tool_ops = { .status = tool_status, .context_stats = tool_context,
    .light_get = agent_devices_light_get, .light_set = agent_devices_light_set, .now_ms = tool_now,
    .ctx=&esp_hi_devices,.control=&esp_hi_control,
    .context_search=tool_search,.summary_get=tool_summary_get,.summary_set=tool_summary_set,
    .hardware_prompt=esp_hi_hardware_prompt,.hardware=esp_hi_hardware_status,.display=&esp_hi_display_ops,
#if AGENT_ENABLE_AUDIO
    .audio = &esp_hi_devices.audio
#endif
};

static void refresh_context(void)
{
    char text[768]; agent_context_stats(&context,text,sizeof(text));
    xSemaphoreTake(context_cache_lock,portMAX_DELAY); strcpy(context_cache,text); xSemaphoreGive(context_cache_lock);
}

#if AGENT_ENABLE_AUDIO
typedef struct { uint32_t crc; size_t bytes; } song_output_t;
static agent_err_t song_output(void *ctx,const char *data,size_t length)
{
    song_output_t *o=ctx; o->crc=agent_crc32_update(o->crc,data,length); o->bytes+=length;
    esp_agent_write(data,length); return AGENT_OK;
}
#ifdef AGENT_BACKGROUND_VERIFY
static agent_err_t audio_scratch_acquire(void *ctx,void **memory,size_t *capacity)
{
    (void)ctx;
    agent_err_t error=agent_begin_turn(kernel); if(error) return error;
    if(xSemaphoreTake(work_lock,0)!=pdTRUE) { agent_end_turn(kernel); return AGENT_ERR_BUSY; }
    *memory=agent_engine_scratch(&engine,capacity); return AGENT_OK;
}
static void audio_scratch_release(void *ctx)
{
    (void)ctx;
    size_t capacity; void *memory=agent_engine_scratch(&engine,&capacity);
    memset(memory,0,capacity);
    xSemaphoreGive(work_lock); agent_end_turn(kernel);
}
#endif
#ifdef AGENT_KEYWORD_PCM
static bool pcm_reserved,pcm_discard,pcm_io_error;
static uint64_t pcm_deadline;
static agent_err_t pcm_cleanup_error;

static void pcm_status(void)
{
    keyword_pcm_info_t info;keyword_pcm_info(esp_hi_keyword_pcm(),&info);
    static const char *const reasons[]={"none","complete","full","cancelled","timeout","io"};
    char output[320];
    snprintf(output,sizeof(output),"{\"pcm\":1,\"active\":%s,\"reserved\":%s,\"generation\":%u,\"produced\":%u,\"consumed\":%u,\"slots\":%u,\"limit\":%u,\"discarded\":%u,\"reason\":\"%s\",\"writing\":%s,\"io_error\":%s,\"cleanup_error\":%u}\r\n",
        info.active?"true":"false",pcm_reserved?"true":"false",info.generation,info.produced,info.consumed,
        info.slots,info.limit,info.discarded,reasons[info.reason],info.writing?"true":"false",pcm_io_error?"true":"false",(unsigned)pcm_cleanup_error);
    say(output);
}
static void pcm_poll(void)
{
    if(!pcm_reserved) return;
    keyword_pcm_t *stream=esp_hi_keyword_pcm();
    if(esp_agent_now()>=pcm_deadline) { keyword_pcm_halt(stream,KEYWORD_PCM_TIMEOUT);pcm_discard=true; }
    if(keyword_pcm_reason(stream)==KEYWORD_PCM_NONE) return;
    pcm_cleanup_error=esp_hi_audio_ops.listen(esp_hi_audio_ops.ctx,false);
    if(pcm_cleanup_error) return;
    agent_audio_state_t audio={0};pcm_cleanup_error=esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx,&audio);
    if(pcm_cleanup_error || audio.listening || audio.playing || audio.recording) return;
    if(keyword_pcm_close(stream,pcm_discard)) {
        audio_scratch_release(NULL);pcm_reserved=false;pcm_deadline=0;pcm_discard=false;
    }
}
static bool pcm_write(const char *data,size_t count,uint64_t deadline)
{
    while(count) {
        if(esp_agent_now()>=deadline) return false;
        int n=usb_serial_jtag_write_bytes(data,count,pdMS_TO_TICKS(10));
        if(n<0) return false;
        data+=n;count-=(size_t)n;
    }
    return true;
}
static agent_err_t pcm_next(void)
{
    keyword_pcm_t *stream=esp_hi_keyword_pcm();
    const keyword_pcm_frame_t *f=keyword_pcm_peek(stream);
    if(!f) { pcm_status();return AGENT_OK; }
    if(xSemaphoreTake(output_lock,pdMS_TO_TICKS(20))!=pdTRUE) return AGENT_ERR_BUSY;
    uint64_t deadline=esp_agent_now()+100;
    char text[256];
    int n=snprintf(text,sizeof(text),"{\"pcm\":1,\"generation\":%u,\"sequence\":%u,\"time_ms\":%u,\"inference_us\":%u,\"copy_us\":%u,\"flags\":%u,\"positive\":%u,\"negative\":%u,\"crc\":%u,\"hex\":\"",
        stream->generation,(unsigned)f->sequence,(unsigned)f->time_ms,(unsigned)f->inference_us,(unsigned)f->copy_us,
        (unsigned)f->flags,(unsigned)f->positive,(unsigned)f->negative,(unsigned)f->checksum);
    bool okay=n>0 && (size_t)n<sizeof(text) && pcm_write(text,(size_t)n,deadline);
    static const char digits[]="0123456789abcdef";
    for(size_t offset=0;okay && offset<sizeof(f->data);offset+=128) {
        for(unsigned i=0;i<128;i++) { unsigned value=f->data[offset+i];text[2*i]=digits[value>>4];text[2*i+1]=digits[value&15]; }
        okay=pcm_write(text,sizeof(text),deadline);
    }
    if(okay) okay=pcm_write("\"}\r\n",4,deadline);
    xSemaphoreGive(output_lock);
    if(!okay) {
        pcm_io_error=true;pcm_discard=true;keyword_pcm_halt(stream,KEYWORD_PCM_IO);return AGENT_ERR_TIMEOUT;
    }
    return keyword_pcm_pop(stream,f->sequence)?AGENT_OK:AGENT_ERR_PROTOCOL;
}
static void pcm_command(const char *text)
{
    keyword_pcm_t *stream=esp_hi_keyword_pcm();
    if(!strcmp(text,"status")) { pcm_status();return; }
    if(!strcmp(text,"next")) { agent_err_t e=pcm_next();if(e) report(e);return; }
    if(!strcmp(text,"off")) {
        keyword_pcm_halt(stream,KEYWORD_PCM_CANCELLED);pcm_discard=true;pcm_poll();report(AGENT_OK);return;
    }
    unsigned limit;char extra;
    if(strncmp(text,"arm ",4) || sscanf(text+4,"%u %c",&limit,&extra)!=1 || !limit || limit>KEYWORD_PCM_MAX_FRAMES) { report(AGENT_ERR_ARGUMENT);return; }
    if(pcm_reserved || atomic_load(&esp_hi_control.active)) { report(AGENT_ERR_BUSY);return; }
    void *memory=NULL;size_t capacity=0;
    agent_err_t error=audio_scratch_acquire(NULL,&memory,&capacity);
    if(!error) {
        agent_audio_state_t audio={0};error=esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx,&audio);
        if(audio.listening || audio.playing || audio.recording || audio.mic_requested || audio.mic_enabled) error=AGENT_ERR_BUSY;
        if(!error && !keyword_pcm_open(stream,memory,capacity,limit)) error=AGENT_ERR_MEMORY;
        if(error) audio_scratch_release(NULL);
        else { pcm_reserved=true;pcm_discard=pcm_io_error=false;pcm_cleanup_error=AGENT_OK;pcm_deadline=esp_agent_now()+10000; }
    }
    report(error);
}
static bool pcm_command_allowed(const char *line)
{
    static const char *const safe[]={"agent status","agent wifi status","agent wake status","agent audio status",
        "agent mic status","agent context stats","agent control status","agent light get","agent cancel","agent wake on","agent wake off","agent audio stop"};
    for(unsigned i=0;i<sizeof(safe)/sizeof(*safe);i++) if(!strcmp(line,safe[i])) return true;
    return false;
}
#endif
/* Main task owns work_lock across an upload; no worker can touch the shared scratch. */
static void upload_release(void)
{
    agent_upload_abort(&music_upload); xSemaphoreGive(work_lock); agent_end_turn(kernel);
}
static void upload_command(const char *text)
{
    if(!strncmp(text,"begin ",6)) {
        unsigned length,crc; char extra;
        if(sscanf(text+6,"%u %x %c",&length,&crc,&extra)!=2 || !length || length>AGENT_ARGS_MAX) { report(AGENT_ERR_ARGUMENT); return; }
        agent_err_t error=agent_begin_turn(kernel); if(error) { report(error); return; }
        if(atomic_load(&sync_active)) atomic_store(&sync_cancel,true);
        if(xSemaphoreTake(work_lock,0)!=pdTRUE) { agent_end_turn(kernel); report(AGENT_ERR_BUSY); return; }
        error=agent_upload_begin(&music_upload,engine.buffer,sizeof(engine.buffer),length,crc,esp_agent_now());
        if(error) { xSemaphoreGive(work_lock); agent_end_turn(kernel); }
        report(error); return;
    }
    if(!music_upload.active) { report(AGENT_ERR_ARGUMENT); return; }
    agent_err_t error=AGENT_ERR_ARGUMENT;
    if(!strncmp(text,"chunk ",6)) {
        unsigned offset; int used=0;
        if(sscanf(text+6,"%u %n",&offset,&used)==1 && used>0)
            error=agent_upload_hex(&music_upload,offset,text+6+used,esp_agent_now());
        if(error) upload_release();
        report(error); return;
    }
    if(!strcmp(text,"abort")) { upload_release(); report(AGENT_OK); return; }
    if(!strcmp(text,"commit")) {
        error=agent_upload_finish(&music_upload,esp_agent_now());
        if(error) { upload_release(); report(error); return; }
        turn_job_t job={.control=true,.upload=true};
        if(xQueueSend(jobs,&job,0)!=pdTRUE) { upload_release(); report(AGENT_ERR_BUSY); return; }
        /* The admitted worker now owns buffer bytes; do not scrub until parsing finishes. */
        memset(&music_upload,0,sizeof(music_upload));
        xSemaphoreGive(work_lock); return;
    }
    upload_release(); report(error);
}
#endif

static agent_err_t context_command(const char *text)
{
    if(!strncmp(text,"display set ",12)) {
        char output[768];
        agent_err_t error=agent_tool_invoke(&tool_ops,"device_display_set",text+12,output,sizeof(output));
        if(!error) {say(output);say("\r\n");}
        return error;
    }
    if(!strncmp(text,"gpio get ",9) || !strncmp(text,"gpio set ",9)) {
        char output[160];
        agent_err_t error=agent_tool_invoke(&tool_ops,text[5]=='g'?"device_gpio_get":"device_gpio_set",text+9,output,sizeof(output));
        if(!error) { say(output); say("\r\n"); }
        return error;
    }
    if(!strncmp(text,"control run ",12) || !strncmp(text,"control validate ",17)) {
        bool run=!strncmp(text,"control run ",12); char output[2048];
        agent_err_t error=agent_tool_invoke(&tool_ops,run?"device_control_run":"device_control_validate",text+(run?12:17),output,sizeof(output));
        if(!error) { say(output); say("\r\n"); }
        return error;
    }
    if(!strncmp(text,"search ",7) || !strcmp(text,"summary") || !strncmp(text,"summary set ",12)) {
        const char *name="agent_context_summary_get",*args="{}";
        if(!strncmp(text,"search ",7)) { name="agent_context_search"; args=text+7; }
        else if(!strncmp(text,"summary set ",12)) { name="agent_context_summary_set"; args=text+12; }
        char output[2048];
        agent_err_t error=agent_tool_invoke(&tool_ops,name,args,output,sizeof(output));
        if(!error) { say(output); say("\r\n"); }
        return error;
    }
#if AGENT_ENABLE_AUDIO
    if(!strcmp(text,"audio song draft")) {
        char diagnostic[192];
        snprintf(diagnostic,sizeof(diagnostic),"@draft calls=%u args=%u reply=%s sse=%s frame=%u finished=%u\r\n",
            engine.reply.call_count,(unsigned)engine.reply.args_used,agent_err_name(engine.reply.error),
            agent_err_name(engine.sse.error),(unsigned)engine.sse.used,engine.reply.finished);
        say(diagnostic);
        for(unsigned i=0;i<engine.reply.call_count;++i) {
            if(!strcmp(engine.reply.calls[i].name,"device_audio_play_song")) {
                say(agent_call_arguments(&engine.reply,i)); say("\r\n");
                /* The frame arena is reused by context persistence after a turn. */
                return AGENT_OK;
            }
        }
        return AGENT_ERR_NOT_FOUND;
    }
    if(!strcmp(text,"audio song get")) {
        song_output_t out={.crc=UINT32_MAX};
        agent_err_t error=esp_hi_devices.audio.read_song(&esp_hi_devices,song_output,&out);
        if(!error) { char trailer[80]; snprintf(trailer,sizeof(trailer),"\r\n@song %u %08x\r\n",(unsigned)out.bytes,(unsigned)~out.crc); say(trailer); }
        return error;
    }
    if(!strncmp(text,"audio song ",11)) {
        char output[1024];
        agent_err_t error=agent_tool_invoke(&tool_ops,"device_audio_play_song",text+11,output,sizeof(output));
        if(!error) { say(output); say("\r\n"); }
        return error;
    }
    /* Keep score JSON parsing serialized with the existing network/context worker. */
    if (!strncmp(text, "audio play ", 11)) {
        char output[1024];
        agent_err_t error = agent_tool_invoke(&tool_ops, "device_audio_play_score", text + 11, output, sizeof(output));
        if (!error) { say(output); say("\r\n"); }
        return error;
    }
#endif
    if(!strcmp(text,"sync")) {
        bool more=false;
        agent_err_t error=agent_context_sync(&context,engine.transport,&kernel->cancelled,&more);
        atomic_store(&sync_more,more); atomic_store(&sync_error,error); return error;
    }
    if(!strcmp(text,"memory")) {
        for(unsigned i=0;i<AGENT_MEMORY_MAX;++i) {
            const agent_memory_t *m=&context.memory[i]; if(!m->key[0]) continue;
            char text[1024]; agent_json_writer_t w; agent_json_writer_init(&w,text,sizeof(text));
            agent_json_raw(&w,"{\"key\":"); agent_json_quote(&w,m->key);
            agent_json_raw(&w,",\"value\":"); agent_json_quote(&w,m->value);
            agent_json_printf(&w,",\"deleted\":%s,\"lamport\":%llu}",m->deleted?"true":"false",(unsigned long long)m->lamport);
            say(text); say("\r\n");
        }
        return AGENT_OK;
    }
    if(!strcmp(text,"power-test")) {
        say("@power_ready\r\n");
        for(unsigned i=0;i<600;++i) {
            if(atomic_load(&kernel->cancelled)) return AGENT_ERR_CANCELLED;
            char content[128]; snprintf(content,sizeof(content),"{\"key\":\"power_test\",\"value\":\"%u\"}",i);
            agent_err_t error=agent_context_emit(&context,"memory","user",content,NULL,0);
            if(error) return error;
            snprintf(content,sizeof(content),"@power_committed %llu\r\n",(unsigned long long)context.last_local); say(content);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        return AGENT_OK;
    }
    if(!strcmp(text,"init")) return esp_agent_store_open(&context,nvs,true);
    if(!strcmp(text,"compact")) return agent_context_compact(&context);
    if(!strncmp(text,"budget ",7)) {
        size_t bytes=!strcmp(text+7,"64")?64u*1024u:!strcmp(text+7,"128")?128u*1024u:0;
        if(!bytes) return AGENT_ERR_ARGUMENT;
        size_t previous=context.history_budget; context.history_budget=bytes;
        agent_err_t error=agent_context_checkpoint(&context,context.cursor,context.acked,context.mode);
        if(error) context.history_budget=previous;
        return error;
    }
    if(!strncmp(text,"mode ",5)) {
        agent_context_mode_t mode;
        if(!strcmp(text+5,"LOCAL")) mode=AGENT_CONTEXT_LOCAL;
        else if(!strcmp(text+5,"CLOUD")) mode=AGENT_CONTEXT_CLOUD;
        else if(!strcmp(text+5,"HYBRID")) mode=AGENT_CONTEXT_HYBRID;
        else return AGENT_ERR_ARGUMENT;
        return agent_context_checkpoint(&context,context.cursor,context.acked,mode);
    }
    if(!strncmp(text,"remember ",9) || !strncmp(text,"forget ",7)) {
        bool deleted=text[0]=='f'; const char *key=text+(deleted?7:9),*value=strchr(key,' ');
        char name[65],content[1200]; size_t n=value?(size_t)(value-key):strlen(key);
        if(!n || n>=sizeof(name) || (!deleted && !value)) return AGENT_ERR_ARGUMENT;
        memcpy(name,key,n); name[n]=0;
        agent_json_writer_t w; agent_json_writer_init(&w,content,sizeof(content));
        agent_json_raw(&w,"{\"key\":"); agent_json_quote(&w,name);
        if(!deleted) { agent_json_raw(&w,",\"value\":"); agent_json_quote(&w,value+1); }
        agent_json_raw(&w,"}");
        return w.error?w.error:agent_context_emit(&context,deleted?"tombstone":"memory","user",content,NULL,0);
    }
    return AGENT_ERR_ARGUMENT;
}

static void network_task(void *arg)
{
    (void)arg;
    turn_job_t job;
    for (;;) {
        if(xQueueReceive(jobs,&job,pdMS_TO_TICKS(250))!=pdTRUE) {
            xSemaphoreTake(work_lock,portMAX_DELAY);
            if(atomic_load(&kernel->busy) || !esp_agent_online() || !context.wal.ready || context.mode==AGENT_CONTEXT_LOCAL ||
               !settings.gateway_url[0] || !settings.gateway_token[0] || esp_agent_now()<sync_at) {
                xSemaphoreGive(work_lock); continue;
            }
            atomic_store(&sync_cancel,false); atomic_store(&sync_active,true);
            if(atomic_load(&kernel->busy)) atomic_store(&sync_cancel,true);
            bool more=false;
            agent_err_t error=AGENT_OK;
#if AGENT_ENABLE_AUDIO
            error=esp_hi_wake_network(true);
#endif
            if(!error) error=agent_context_sync(&context,engine.transport,&sync_cancel,&more);
#if AGENT_ENABLE_AUDIO
            esp_hi_wake_network(false);
#endif
            atomic_store(&sync_more,more); atomic_store(&sync_error,error);
            sync_at=esp_agent_now()+(error?5000:more?250:30000);
            refresh_context(); atomic_store(&sync_active,false); xSemaphoreGive(work_lock);
            continue;
        }
        xSemaphoreTake(work_lock,portMAX_DELAY);
        engine.gateway=atomic_load(&route_gateway);
        agent_err_t error=AGENT_OK;
#if AGENT_ENABLE_AUDIO
        error=esp_hi_wake_network(true);
#endif
        if(!error) {
#if AGENT_ENABLE_AUDIO
        if(job.upload) {
            char output[1024];
            error=agent_tool_invoke(&tool_ops,"device_audio_play_song",engine.buffer,output,sizeof(output));
            memset(engine.buffer,0,AGENT_ARGS_MAX+1);
            if(!error) { say(output); say("\r\n"); }
        } else
#endif
        error=job.control?context_command(job.text):agent_engine_turn(&engine,job.text,job.stream);
        }
#if AGENT_ENABLE_AUDIO
        esp_hi_wake_network(false);
#endif
        refresh_context();
        xSemaphoreGive(work_lock);
        agent_end_turn(kernel);
        if(error || job.control) report(error); else say("\r\n@done\r\n");
    }
}

static void submit(const char *text, bool stream, bool control)
{
    if (!text[0] || strlen(text) > (control?sizeof(work_buffer.input)-1:AGENT_INPUT_MAX) || !agent_utf8_valid(text, strlen(text))) {
        report(AGENT_ERR_LIMIT); return;
    }
    agent_err_t error = busy_result(BUSY_SUBMIT,agent_begin_turn(kernel));
    if (error) { report(error); return; }
    if(atomic_load(&sync_active)) atomic_store(&sync_cancel,true);
    strcpy(work_buffer.input,text);
    turn_job_t job = { .text=work_buffer.input,.stream = stream, .control=control };
    if (xQueueSend(jobs, &job, 0) != pdTRUE) { agent_end_turn(kernel); report(busy_result(BUSY_SUBMIT_QUEUE,AGENT_ERR_BUSY)); }
}

static void command(char *line)
{
#ifdef AGENT_KEYWORD_PCM
    if(!strncmp(line,"agent keyword pcm ",18)) { pcm_command(line+18);return; }
    if(pcm_reserved) {
        if(!pcm_command_allowed(line) || (!strcmp(line,"agent wake on") && keyword_pcm_reason(esp_hi_keyword_pcm())!=KEYWORD_PCM_NONE)) { report(AGENT_ERR_BUSY);return; }
        if(!strcmp(line,"agent wake off") || !strcmp(line,"agent audio stop") || !strcmp(line,"agent cancel"))
            keyword_pcm_halt(esp_hi_keyword_pcm(),KEYWORD_PCM_CANCELLED);
        if(!strcmp(line,"agent cancel")) pcm_discard=true;
    }
#endif
#ifdef AGENT_BUSY_TRACE
    if(!strcmp(line,"agent debug busy")) {
        uint32_t values[BUSY_SITES]; char output[256];
        busy_trace_snapshot(values);
        agent_json_writer_t w; agent_json_writer_init(&w,output,sizeof(output));
        agent_json_raw(&w,"{\"v\":1,\"counts\":[");
        for(unsigned i=0;i<BUSY_SITES;i++) agent_json_printf(&w,"%s%u",i?",":"",values[i]);
        agent_json_raw(&w,"]}");
        if(w.error) report(w.error); else { say(output); say("\r\n"); }
        return;
    }
#endif
#if AGENT_ENABLE_AUDIO
    if(!strcmp(line,"agent producer profile")) {
        char output[1024];agent_err_t e=phase_status(output,sizeof(output));
        if(!e) {say(output);say("\r\n");} else report(e);
        return;
    }
#endif
    if (!strcmp(line, "agent status") || !strcmp(line, "agent wifi status")) {
        char output[768]; esp_agent_status(output, sizeof(output)); say(output); say("\r\n");
    } else if(!strcmp(line,"agent hardware") || !strcmp(line,"agent display status")) {
        char output[1536];
        agent_err_t error=!strcmp(line,"agent hardware")?esp_hi_hardware_status(NULL,output,sizeof(output)):
            esp_hi_display_ops.status(NULL,output,sizeof(output));
        if(!error) {say(output);say("\r\n");} else report(error);
    } else if(!strcmp(line,"agent display off")) {
        esp_hi_display_cancel();report(AGENT_OK);
    } else if(!strncmp(line,"agent display set ",18)) {
        submit(line+6,false,true);
#ifdef AGENT_KEYWORD_TRACE
    } else if (!strcmp(line, "agent keyword trace arm") || !strcmp(line, "agent keyword trace off")) {
        agent_audio_state_t audio = {0};
        agent_err_t error = esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx, &audio);
        if (audio.listening || audio.playing || audio.recording || atomic_load(&kernel->busy)) error = AGENT_ERR_BUSY;
        if (!error && !(!strcmp(line, "agent keyword trace arm") ? keyword_trace_arm() : keyword_trace_cancel())) error = AGENT_ERR_BUSY;
        report(error);
    } else if (!strncmp(line, "agent keyword trace row ", 24)) {
        unsigned row; char extra, output[768];
        keyword_trace_info_t info; int16_t values[KG_CHANNELS];
        if (sscanf(line+24, "%u %c", &row, &extra) != 1 || row >= KG_FRAMES) report(AGENT_ERR_ARGUMENT);
        else if (!keyword_trace_read(row, &info, values)) say("{\"ready\":false}\r\n");
        else {
            agent_json_writer_t w; agent_json_writer_init(&w, output, sizeof(output));
            agent_json_printf(&w,"{\"ready\":true,\"generation\":%u,\"time_ms\":%u,\"checksum\":%u,\"accepted\":%s,\"row\":%u,\"scores\":[",
                info.generation,info.time_ms,info.checksum,info.accepted?"true":"false",row);
            for (unsigned i=0;i<KG_TEMPLATE_COUNT;i++) agent_json_printf(&w,"%s%u",i?",":"",info.scores[i]);
            agent_json_printf(&w,"],\"start\":{\"channel\":%d,\"samples\":%d,\"us\":%u,\"called\":%s},\"values\":[",
                info.start.channel,info.start.samples,info.start.us,info.start.called?"true":"false");
            for (unsigned i=0;i<KG_CHANNELS;i++) agent_json_printf(&w,"%s%d",i?",":"",values[i]);
            agent_json_printf(&w,"]}"); say(output); say("\r\n");
        }
#endif
#ifdef AGENT_HEAP_WATCH
    } else if (!strcmp(line, "agent heap watch on") || !strcmp(line, "agent heap watch off")) {
        agent_heap_watch_reset(!strcmp(line, "agent heap watch on")); report(AGENT_OK);
    } else if (!strcmp(line, "agent heap watch status")) {
        char output[1536]; agent_heap_watch_status(output, sizeof(output)); say(output); say("\r\n");
    } else if (!strcmp(line, "agent heap watch check")) {
        agent_audio_state_t audio = {0};
        agent_err_t error = esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx, &audio);
        if (atomic_load(&kernel->busy) || atomic_load(&sync_active) || audio.listening || audio.playing) error = AGENT_ERR_BUSY;
        if (!error && !agent_heap_watch_check()) error = AGENT_ERR_MEMORY;
        report(error);
#endif
    } else if (!strncmp(line, "@provision ", 11)) {
        agent_err_t error = provision(line + 11); if (error) report(error);
    } else if (!strncmp(line, "agent wifi set ", 15)) {
        report(provision(line + 15));
    } else if(!strcmp(line,"agent wifi pause")) {
        atomic_store(&wifi_paused,true); esp_wifi_disconnect(); report(AGENT_OK);
    } else if(!strcmp(line,"agent wifi resume")) {
        atomic_store(&wifi_paused,false); atomic_store(&wifi_retry_at,0); report(AGENT_OK);
#ifdef AGENT_WIFI_AUDIO_DIAGNOSTICS
    } else if(!strcmp(line,"agent wifi modem")) {
        wifi_ps_type_t mode;
        if(esp_wifi_get_ps(&mode)!=ESP_OK) report(AGENT_ERR_CONFIG);
        else { char value[64]; snprintf(value,sizeof(value),"{\"mode\":%u,\"listen_interval\":10}\r\n",(unsigned)mode); say(value); }
    } else if(!strncmp(line,"agent wifi modem ",16)) {
        unsigned mode; char extra; agent_audio_state_t audio={0};
        agent_err_t error=esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx,&audio);
        if(sscanf(line+16,"%u %c",&mode,&extra)!=1 || mode>2) error=AGENT_ERR_ARGUMENT;
        else if(atomic_load(&kernel->busy) || atomic_load(&sync_active) ||
                atomic_load(&esp_hi_control.active) || audio.listening || audio.playing) error=AGENT_ERR_BUSY;
        else if(!error && esp_wifi_set_ps((wifi_ps_type_t)mode)!=ESP_OK) error=AGENT_ERR_CONFIG;
        report(error);
#endif
    } else if (!strcmp(line, "agent cancel")) {
        agent_control_cancel(&esp_hi_control);
        esp_hi_display_cancel();
#if AGENT_ENABLE_AUDIO
        esp_hi_devices.audio.stop(&esp_hi_devices);
#endif
        agent_cancel(kernel); atomic_store(&sync_cancel,true); report(AGENT_OK);
#if AGENT_ENABLE_AUDIO
        if(music_upload.active) upload_release();
#endif
    } else if (!strncmp(line, "agent time set ", 15)) {
        char *end;
        unsigned long long epoch = strtoull(line + 15, &end, 10);
        if (*end || epoch < 1735689600 || epoch > 4102444800ULL) report(AGENT_ERR_ARGUMENT);
        else { struct timeval now = { .tv_sec = (time_t)epoch }; settimeofday(&now, NULL); report(AGENT_OK); }
    } else if (!strcmp(line, "agent reboot")) {
        if(atomic_load(&kernel->busy) || atomic_load(&sync_active) || atomic_load(&esp_hi_control.active)) { report(AGENT_ERR_BUSY); return; }
        say("@reboot\r\n"); vTaskDelay(pdMS_TO_TICKS(100)); esp_restart();
    } else if (!strcmp(line, "agent help")) {
        say("agent hardware | agent display status/off/set {\"mode\":\"clock\",\"utc_offset_minutes\":480}\r\n");
        say("agent status | wifi set JSON | wifi status/pause/resume | chat [--no-stream] TEXT | cancel | light get/set R G B | context stats/init/compact/mode LOCAL|CLOUD|HYBRID/remember KEY VALUE/forget KEY/memory/sync | route DIRECT|GATEWAY | reboot\r\n");
#if AGENT_ENABLE_AUDIO
        say("agent audio status/play JSON/stop/volume 0..100/capture MS/replay | song JSON/get/begin BYTES CRC32/chunk OFFSET HEX/commit/abort | agent mic on/off/status\r\n");
        say("agent wake on/off/status/threshold 500..950/gain 1..4 (gain while off) | agent audio clip read SAMPLE COUNT (max 256, wake off)\r\n");
#endif
        say("agent control capabilities/validate JSON/run JSON/status/cancel | agent context budget 64|128/search JSON/summary/summary set JSON\r\n");
        say("agent gpio get {\"pin\":10} | agent gpio set {\"pin\":10,\"mode\":\"output\",\"value\":1} | input or pwm(hz,duty)\r\n");
    } else if(!strcmp(line,"agent context stats")) {
        xSemaphoreTake(context_cache_lock,portMAX_DELAY); say(context_cache); say("\r\n"); xSemaphoreGive(context_cache_lock);
    } else if(!strncmp(line,"agent context ",14)) submit(line+14,false,true);
    else if(!strcmp(line,"agent control status") || !strcmp(line,"agent control capabilities")) {
        char output[2048];
        agent_err_t error=!strcmp(line,"agent control status")?agent_control_status(&esp_hi_control,output,sizeof(output)):
            agent_control_capabilities(&esp_hi_control,output,sizeof(output));
        if(!error) { say(output); say("\r\n"); } else report(error);
    } else if(!strcmp(line,"agent control cancel")) { agent_control_cancel(&esp_hi_control); report(AGENT_OK); }
    else if(!strncmp(line,"agent control run ",18) || !strncmp(line,"agent control validate ",23)) submit(line+6,false,true);
    else if(!strncmp(line,"agent gpio get ",15) || !strncmp(line,"agent gpio set ",15)) submit(line+6,false,true);
    else if(!strncmp(line,"agent route ",12)) {
        if(atomic_load(&kernel->busy)) report(AGENT_ERR_BUSY);
        else if(!strcmp(line+12,"DIRECT") || !strcmp(line+12,"GATEWAY")) {
            atomic_store(&route_gateway,!strcmp(line+12,"GATEWAY")); report(AGENT_OK);
        } else report(AGENT_ERR_ARGUMENT);
    } else if (!strcmp(line, "agent light get")) {
        uint8_t rgb[3]; char output[96];
        agent_err_t error = agent_devices_light_get(&esp_hi_devices, rgb);
        if (!error) { snprintf(output, sizeof(output), "{\"r\":%u,\"g\":%u,\"b\":%u}\r\n", rgb[0], rgb[1], rgb[2]); say(output); }
        else report(error);
    } else if (!strncmp(line, "agent light set ", 16)) {
        unsigned r, g, b; char extra;
        if (sscanf(line + 16, "%u %u %u %c", &r, &g, &b, &extra) != 3 || r > 255 || g > 255 || b > 255) report(AGENT_ERR_ARGUMENT);
        else { uint8_t rgb[] = {(uint8_t)r, (uint8_t)g, (uint8_t)b}; report(agent_devices_light_set(&esp_hi_devices, rgb)); }
    }
#if AGENT_ENABLE_AUDIO
    else if(!strcmp(line,"agent wake status")) {
#ifdef AGENT_BACKGROUND_VERIFY
        char output[1536]; agent_err_t error=esp_hi_wake_status(output,sizeof(output));
#elif defined(AGENT_VOICE_VERIFY) || defined(AGENT_KEYWORD_VERIFY)
        char output[1024]; agent_err_t error=esp_hi_wake_status(output,sizeof(output));
#else
        char output[768]; agent_err_t error=esp_hi_wake_status(output,sizeof(output));
#endif
        if(!error) { say(output); say("\r\n"); } else report(error);
    } else if(!strcmp(line,"agent wake on") || !strcmp(line,"agent wake off"))
        report(esp_hi_devices.audio.listen(&esp_hi_devices,!strcmp(line,"agent wake on")));
    else if(!strncmp(line,"agent wake threshold ",21)) {
        unsigned value; char extra;
        report(sscanf(line+21,"%u %c",&value,&extra)==1?esp_hi_wake_threshold(value):AGENT_ERR_ARGUMENT);
    } else if(!strncmp(line,"agent wake gain ",16)) {
        report(line[16]>='1' && line[16]<='4' && !line[17]?
            esp_hi_wake_gain((unsigned)(line[16]-'0')):AGENT_ERR_ARGUMENT);
    }
    else if(!strncmp(line,"agent audio clip read ",22)) {
        unsigned offset,count; char extra; char output[1100];
        agent_err_t error=sscanf(line+22,"%u %u %c",&offset,&count,&extra)==2?
            esp_hi_clip_export(offset,count,output,sizeof(output)):AGENT_ERR_ARGUMENT;
        if(!error) { say(output); say("\r\n"); } else report(error);
    }
    else if (!strcmp(line, "agent audio status") || !strcmp(line, "agent mic status")) {
        char output[1024];
        agent_err_t error = esp_hi_devices.audio.status(&esp_hi_devices, output, sizeof(output));
        if (!error) { say(output); say("\r\n"); } else report(error);
    } else if (!strcmp(line, "agent audio stop")) report(esp_hi_devices.audio.stop(&esp_hi_devices));
    else if (!strcmp(line, "agent mic on") || !strcmp(line, "agent mic off"))
        report(esp_hi_devices.audio.microphone(&esp_hi_devices, !strcmp(line, "agent mic on")));
    else if (!strncmp(line, "agent audio volume ", 19)) {
        unsigned volume; char extra;
        if (sscanf(line + 19, "%u %c", &volume, &extra) != 1 || volume > 100) report(AGENT_ERR_ARGUMENT);
        else report(esp_hi_devices.audio.volume(&esp_hi_devices, volume));
    } else if (!strncmp(line, "agent audio play ", 17)) submit(line + 6, false, true);
    else if(!strncmp(line,"agent audio song ",17)) {
        const char *text=line+17;
        if(!strncmp(text,"begin ",6) || !strncmp(text,"chunk ",6) || !strcmp(text,"commit") || !strcmp(text,"abort")) upload_command(text);
        else submit(line+6,false,true);
    }
    else if(!strncmp(line,"agent audio capture ",20)) {
        unsigned ms; char extra;
        if(sscanf(line+20,"%u %c",&ms,&extra)!=1 || ms<100 || ms>AGENT_CAPTURE_MS) report(AGENT_ERR_ARGUMENT);
        else report(esp_hi_devices.audio.capture(&esp_hi_devices,ms));
    } else if(!strcmp(line,"agent audio replay")) report(esp_hi_devices.audio.replay(&esp_hi_devices));
#endif
    else if (!strncmp(line, "agent chat --no-stream ", 23)) submit(line + 23, false,false);
    else if (!strncmp(line, "agent chat ", 11)) submit(line + 11, true,false);
    else if (!strncmp(line, "agent ", 6)) report(AGENT_ERR_ARGUMENT);
    else submit(line, true,false);
}

static void pause_ms(unsigned ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static agent_err_t dispatch(void *ctx,const agent_event_t *event) { return agent_dispatch(ctx,event); }
static agent_err_t platform_random(void *ctx,void *dst,size_t n) { (void)ctx; esp_fill_random(dst,n); return AGENT_OK; }
static agent_err_t platform_get(void *ctx,const char *key,void *dst,size_t *n)
{ (void)ctx; return nvs_get_blob(nvs,key,dst,n)==ESP_OK ? AGENT_OK : AGENT_ERR_NOT_FOUND; }
static agent_err_t platform_set(void *ctx,const char *key,const void *src,size_t n)
{ (void)ctx; return nvs_set_blob(nvs,key,src,n)==ESP_OK && nvs_commit(nvs)==ESP_OK ? AGENT_OK : AGENT_ERR_STORAGE; }
static const agent_platform_ops_t platform_ops={tool_now,platform_random,platform_get,platform_set,NULL};
static const agent_transport_ops_t transport_ops={esp_agent_http,NULL};
static const agent_llm_ops_t llm_ops={.build=agent_deepseek_request,.parse=agent_llm_parse,.compose=agent_deepseek_compose};
static agent_err_t usb_read(void *ctx,char *data,size_t capacity,size_t *length)
{
    (void)ctx; int n=usb_serial_jtag_read_bytes(data,capacity,pdMS_TO_TICKS(25));
    *length=n>0?(size_t)n:0; return n<0 ? AGENT_ERR_PROTOCOL : AGENT_OK;
}
static void usb_write(void *ctx,const char *data,size_t length) { (void)ctx; esp_agent_write(data,length); }
static const agent_input_ops_t input_ops={usb_read,NULL};
static const agent_output_ops_t output_ops={usb_write,NULL};
static uint32_t platform_u32(void)
{
    const agent_platform_ops_t *ops=agent_find(kernel,"platform.espidf")->ops;
    uint32_t value=0; ops->random_bytes(ops->ctx,&value,sizeof(value)); return value;
}

void esp_agent_run(agent_core_t *core)
{
    kernel = core;
    output_lock = xSemaphoreCreateMutex();
    context_cache_lock=xSemaphoreCreateMutex();
    work_lock=xSemaphoreCreateMutex();
    usb_serial_jtag_driver_config_t usb = { .tx_buffer_size = 2048, .rx_buffer_size = 1024 };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(settings.device_id, sizeof(settings.device_id), "esp-hi-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    strcpy(settings.user_id, "local-user"); strcpy(settings.session_id, "default");
    if (nvs_flash_init() == ESP_OK && nvs_open("agent_m0", NVS_READWRITE, &nvs) == ESP_OK) {
        storage_ready = true;
        size_t size = sizeof(settings);
        /* Before context, network and audio start, the engine buffer is idle.
         * Reuse it for the boot-only copy instead of reserving another 2.8 KiB. */
        _Static_assert(sizeof(engine.buffer) >= sizeof(settings), "Boot settings must fit scratch");
        if (nvs_get_blob(nvs, "settings_v1", engine.buffer, &size) == ESP_OK && size == sizeof(settings))
            memcpy(&settings, engine.buffer, sizeof(settings));
        memset(engine.buffer, 0, sizeof(settings));
    }
    context.device=settings.device_id; context.user=settings.user_id; context.session=settings.session_id;
    context.scratch=engine.buffer; context.capacity=sizeof(engine.buffer);
    context.cancelled=&kernel->cancelled;
    context.now_ms=tool_now;
    report(storage_ready?esp_agent_store_open(&context,nvs,false):AGENT_ERR_STORAGE);
    refresh_context();
    static const agent_plugin_t plugins[]={
        AGENT_PLUGIN(AGENT_PLATFORM,"platform.espidf",&platform_ops,NULL),
        AGENT_PLUGIN(AGENT_TRANSPORT,"transport.https",&transport_ops,NULL),
        AGENT_PLUGIN(AGENT_LLM,"llm.deepseek",&llm_ops,NULL),
        AGENT_PLUGIN(AGENT_CONTEXT_STORE,"context.wal",&agent_context_ops,&context),
        AGENT_PLUGIN(AGENT_TOOL,"tool.device",&tool_ops,NULL),
#if AGENT_ENABLE_AUDIO
        AGENT_PLUGIN(AGENT_TOOL,"tool.audio",&esp_hi_devices.audio,&esp_hi_devices),
#endif
        AGENT_PLUGIN(AGENT_TOOL,"tool.control",&esp_hi_control,&esp_hi_control),
        AGENT_PLUGIN(AGENT_OUTPUT,"output.lcd",&esp_hi_display_ops,NULL),
        AGENT_PLUGIN(AGENT_INPUT,"input.usb",&input_ops,NULL),
        {.abi_major=AGENT_ABI_MAJOR,.struct_size=sizeof(agent_plugin_t),.kind=AGENT_OUTPUT,.name="output.usb",.ops=&output_ops,.handle_event=output_event}
    };
    for(size_t i=0;i<sizeof(plugins)/sizeof(plugins[0]);++i) {
        agent_err_t error=agent_register(core,&plugins[i]); if(error) { report(error); return; }
    }
    agent_err_t start_error=agent_start(core); if(start_error) { report(start_error); return; }
    engine.core=core; engine.transport=agent_find(core,"transport.https")->ops;
    engine.llm=agent_find(core,"llm.deepseek")->ops; engine.tools=agent_find(core,"tool.device")->ops;
    engine.context=agent_find(core,"context.wal")->ctx; engine.emit=dispatch; engine.emit_ctx=core;
    engine.context_ops=agent_find(core,"context.wal")->ops;
    engine.pause_ms=pause_ms; engine.random_u32=platform_u32;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.start = false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    ESP_ERROR_CHECK(esp_wifi_start());
#if AGENT_ENABLE_AUDIO
    /* Repeated associated ADC comparisons measure 6–9 dB less noise without
     * modem sleep. Keep this audio build's tested policy across reboots. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#endif
    report(wifi_apply());
    report(esp_hi_light_init());
#if AGENT_ENABLE_AUDIO
#ifdef AGENT_BACKGROUND_VERIFY
    static const esp_hi_confirmation_memory_t audio_memory={audio_scratch_acquire,audio_scratch_release,NULL};
    esp_hi_confirmation_memory(&audio_memory);
#endif
    report(esp_hi_audio_init());
#endif
    agent_err_t control_error=esp_hi_control_init();
    if(control_error) { report(control_error); return; }
    jobs = xQueueCreate(1, sizeof(turn_job_t));
    /* 16-KiB stack retained 3596 bytes in measured long-context TLS/tool runs.
     * Recover 1 KiB; reverify the remaining margin with real requests. */
    if (!jobs || xTaskCreate(network_task, "agent_net", 15360, NULL, 4, &network_handle) != pdPASS) {
        report(AGENT_ERR_MEMORY); return;
    }
    say("ESP-Hi Agent ready. Type agent help.\r\n");
    static char line[AGENT_ARGS_MAX + 65];
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        char bytes[128];
        size_t count=0;
        const agent_input_ops_t *input=agent_find(core,"input.usb")->ops;
        input->read(input->ctx,bytes,sizeof(bytes),&count);
        for (size_t i = 0; i < count; ++i) {
            unsigned char ch = (unsigned char)bytes[i];
            if (ch == '\r' || ch == '\n') {
                if (overflow) report(AGENT_ERR_LIMIT);
                else if (used) { line[used] = 0; command(line); }
                for (volatile char *p = line; p < line + used; ++p) *p = 0;
                used = 0; overflow = false;
            } else if (ch == 8 || ch == 127) { if (used) --used; }
            else if (!ch) overflow = true;
            else if (used < sizeof(line) - 1) line[used++] = (char)ch;
            else overflow = true;
        }
        if (settings.ssid[0] && !atomic_load(&wifi_paused) && !atomic_load(&wifi_up) && !atomic_load(&wifi_connecting) &&
            esp_agent_now() >= atomic_load(&wifi_retry_at)) {
            atomic_store(&wifi_connecting, true);
            wifi_deadline=esp_agent_now()+20000;
            if (esp_wifi_connect() != ESP_OK) {
                atomic_store(&wifi_connecting, false);
                atomic_store(&wifi_retry_at, esp_agent_now() + 5000);
            }
        }
        if(atomic_load(&wifi_connecting) && esp_agent_now()>wifi_deadline) esp_wifi_disconnect();
#if AGENT_ENABLE_AUDIO
        if(music_upload.active && esp_agent_now()>=music_upload.deadline) { upload_release(); report(AGENT_ERR_TIMEOUT); }
#endif
#ifdef AGENT_KEYWORD_PCM
        pcm_poll();
#endif
    }
}
