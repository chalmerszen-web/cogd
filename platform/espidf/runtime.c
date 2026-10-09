#include "runtime.h"
#include "voice_heap.h"
#if AGENT_ENABLE_AUDIO
#include "phase.h"
#endif
#include "busy_trace.h"
#ifdef AGENT_KWS_C11
#include "speech_backend.h"
#include "kws_usb.h"
#ifdef AGENT_KWS_GRU_RESOURCE_ONLY
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
#ifdef AGENT_KWS_GRU_Q6_RESOURCE_ONLY
#define AGENT_RUNTIME_VERSION "0.11.505-gru64-q6-resource"
#else
#define AGENT_RUNTIME_VERSION "0.11.500-gru64-resource"
#endif
#elif defined(AGENT_KWS_GRU_Q6_RESOURCE_ONLY)
#define AGENT_RUNTIME_VERSION "0.11.482-gru32-q6-resource"
#else
#define AGENT_RUNTIME_VERSION "0.11.476-gru32-resource"
#endif
#elif defined(AGENT_KWS_VERIFIED_RESOURCE_ONLY)
#define AGENT_RUNTIME_VERSION "0.11.289-compact-resource"
#elif defined(AGENT_KWS_VERIFIED)
#ifdef AGENT_KEYWORD_PCM
#define AGENT_RUNTIME_VERSION "0.11.471-wake-input"
#elif AGENT_CAPTURE_PROBE
#define AGENT_RUNTIME_VERSION "0.11.283-verified-shadow"
#elif defined(AGENT_VOICE_HEAP_DIAGNOSTICS)
#if AGENT_VOICE_SCRATCH_LOANS
#define AGENT_RUNTIME_VERSION "0.11.511-heap-export"
#else
#define AGENT_RUNTIME_VERSION "0.11.284-verified-lifetime"
#endif
#elif AGENT_CONTEXTUAL_CACHED_ACK
#if AGENT_VOICE_SCRATCH_LOANS
#define AGENT_RUNTIME_VERSION "0.11.491-scratch-scope"
#else
#define AGENT_RUNTIME_VERSION "0.11.469-contextual-cache"
#endif
#elif AGENT_VOICE_HTTP_FORGET
#define AGENT_RUNTIME_VERSION "0.11.444-http-drop"
#elif AGENT_VOICE_HTTP_RELEASE && AGENT_REQUEST_SCRATCH_COMPACT && AGENT_TLS_TX_COMPACT
#define AGENT_RUNTIME_VERSION "0.11.309-tx-buffer"
#elif AGENT_VOICE_HTTP_RELEASE && AGENT_REQUEST_SCRATCH_COMPACT
#define AGENT_RUNTIME_VERSION "0.11.301-early-memory"
#elif AGENT_VOICE_HTTP_RELEASE
#define AGENT_RUNTIME_VERSION "0.11.292-topic-cooperate"
#else
#define AGENT_RUNTIME_VERSION "0.11.282-verified-experimental"
#endif
#elif defined(AGENT_KWS_WIDE_PROBE)
#define AGENT_RUNTIME_VERSION "0.11.262-wide-usb-probe"
#elif defined(AGENT_KWS_TRAINED)
#ifdef AGENT_KEYWORD_PCM
#define AGENT_RUNTIME_VERSION "0.11.218-kws-input"
#elif defined(AGENT_KWS_STABLE_WAKE)
#define AGENT_RUNTIME_VERSION "0.11.216-wake128"
#elif AGENT_TEXT_PREFETCH
#if AGENT_HANDOFF_PROBE
#define AGENT_RUNTIME_VERSION "0.11.203-protocol-diag"
#elif defined(AGENT_VOICE_HEAP_DIAGNOSTICS)
#if AGENT_VOICE_HTTP_RELEASE
#define AGENT_RUNTIME_VERSION "0.11.253-http-heap-protocol-diag"
#elif AGENT_TLS_COOPERATE
#define AGENT_RUNTIME_VERSION "0.11.252-heap-protocol-diag"
#else
#define AGENT_RUNTIME_VERSION "0.11.164-heap-protocol-diag"
#endif
#elif AGENT_ENDPOINT_TRACE
#if AGENT_CAPTURE_PROBE
#define AGENT_RUNTIME_VERSION "0.11.181-protocol-diag"
#else
#define AGENT_RUNTIME_VERSION "0.11.164-endpoint-protocol-diag"
#endif
#elif AGENT_VOICE_HTTP_RELEASE
#define AGENT_RUNTIME_VERSION "0.11.253-final-http"
#elif AGENT_TLS_COOPERATE
#define AGENT_RUNTIME_VERSION "0.11.251-asr-first"
#elif AGENT_TLS_PHASE_TRACE
#if AGENT_TLS_PUBLIC_PERF
#define AGENT_RUNTIME_VERSION "0.11.248-protocol-diag"
#else
#define AGENT_RUNTIME_VERSION "0.11.169-protocol-diag"
#endif
#elif AGENT_TLS_PUBLIC_PERF
#define AGENT_RUNTIME_VERSION "0.11.231-scratch"
#else
#define AGENT_RUNTIME_VERSION "0.11.164"
#endif
#elif AGENT_WS_OWNER_PROBE
#define AGENT_RUNTIME_VERSION "0.11.97-protocol-diag"
#elif AGENT_WS_TIMING
#define AGENT_RUNTIME_VERSION "0.11.83-protocol-diag"
#elif defined(AGENT_RT_DIAGNOSTICS) && AGENT_RT_DIAGNOSTICS
#define AGENT_RUNTIME_VERSION "0.11.73-protocol-diag"
#elif defined(AGENT_VOICE_HEAP_DIAGNOSTICS)
#define AGENT_RUNTIME_VERSION "0.11.77-heap-protocol-diag"
#elif AGENT_ENDPOINT_TRACE
#define AGENT_RUNTIME_VERSION "0.11.86-protocol-diag"
#elif AGENT_ISOLATED_ASR
#define AGENT_RUNTIME_VERSION "0.11.97-ws-owners"
#elif AGENT_WS_INCREMENTAL
#define AGENT_RUNTIME_VERSION "0.11.90-upload-block"
#else
#define AGENT_RUNTIME_VERSION "0.11.77-sdk-reader"
#endif
#else
#define AGENT_RUNTIME_VERSION "0.6.3-kws-probe"
#endif
#else
#define AGENT_RUNTIME_VERSION "0.6.3-context"
#endif
#ifdef AGENT_MIC_EXACT_CLOCK
#include "adc_clock.h"
#undef AGENT_RUNTIME_VERSION
#define AGENT_RUNTIME_VERSION "0.11.515-adc-clock"
#endif
#if AGENT_USER_TEST
#undef AGENT_RUNTIME_VERSION
#define AGENT_RUNTIME_VERSION "0.12.0-rc2"
#endif
#if AGENT_CLOCK_BOOT
#undef AGENT_RUNTIME_VERSION
#define AGENT_RUNTIME_VERSION "0.12.4-clock"
#endif
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
#include "voice_stream.h"
#include "voice_fast.h"
#if AGENT_TEXT_PREFETCH
#include "voice_candidate.h"
#include "engine_memory.h"
#include "source_stream.h"
#endif
#include "capture_radio.h"
#include "realtime.h"
#include "upload.h"
#include "crc.h"
#if AGENT_ISOLATED_ASR
#include "asr_realtime.h"
#include "intent.h"
extern const agent_ws_ops_t esp_agent_asr_ws;
#define AGENT_CLASSIC_ASR_MODEL AGENT_ASR_RT_MODEL
#else
#define AGENT_CLASSIC_ASR_MODEL AGENT_QWEN_ASR_MODEL
#endif
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
#if AGENT_ENABLE_AUDIO
typedef struct { char key[193],voice[97]; } speech_settings_t;
static speech_settings_t speech_settings,qianwen_settings;
static atomic_bool voice_enabled,voice_rearm,speech_qianwen,voice_handoff;
static atomic_bool voice_fast=AGENT_USER_TEST; /* Packaged test route; legacy builds start in classic mode. */
static bool voice_first_text;
extern const agent_ws_ops_t esp_agent_speech_ws;
static bool voice_capture_start(void);
static void voice_event(void *,const char *,const char *);
static atomic_uint voice_stage,voice_turns,voice_failures,voice_started,voice_elapsed;
static atomic_uint voice_usb_dropped;
static atomic_uint_fast64_t fast_retry_at;
/* Capture-time connection overlaps recording without disabling the listener.
 * Idle priming remains opt-in only for controlled timing comparisons. */
static atomic_bool fast_prime_idle=false;
static atomic_int voice_error;
static const char *const voice_stages[]={"off","listening","vad_end","upload","asr_submit","asr_wait","asr_text","llm","tts","playback","done","error",
    "capture_begin","asr_connect","asr_connected","asr_started","asr_audio","asr_partial","asr_final","asr_finish","asr_done",
    "llm_first_text","llm_done","tts_connect","tts_connected","tts_started","tts_finish","tts_done","speaker_start","llm_body_sent","tts_text"};
static bool voice_configured(void)
{ const speech_settings_t *s=atomic_load(&speech_qianwen)?&qianwen_settings:&speech_settings;return s->key[0] && s->voice[0]; }
#endif
static agent_core_t *kernel;
static nvs_handle_t nvs;
static bool storage_ready;
static SemaphoreHandle_t output_lock;
static atomic_bool wifi_up, wifi_connecting, wifi_paused;
static atomic_uint wifi_failures;
static atomic_uint_fast64_t wifi_retry_at;
static QueueHandle_t jobs;
static TaskHandle_t network_handle;
typedef struct {
    const char *text; bool stream, control, upload,voice,say,live,fast,candidate;
#if AGENT_HANDOFF_PROBE
    bool probe,probe_join,probe_hold,probe_reserve;
#endif
} turn_job_t;
/* work_lock owns either a queued input or a temporary provisioning copy. */
static union { char input[AGENT_ARGS_MAX+33]; settings_t settings; } work_buffer;
_Static_assert(sizeof(settings_t)<=sizeof(work_buffer.input),"Settings must fit the input workspace");
#if AGENT_TEXT_PREFETCH
static agent_engine_t engine;
static void *capture_workspace;
enum { CAPTURE_WORKSPACE_BYTES=ESP_AGENT_CANDIDATE_PREFIX_BYTES+SOURCE_METADATA_BYTES+AGENT_QWEN_SCRATCH };
enum { CAPTURE_ALLOCATION_BYTES=CAPTURE_WORKSPACE_BYTES>sizeof(agent_engine_workspace_t)?
       CAPTURE_WORKSPACE_BYTES:sizeof(agent_engine_workspace_t) };
_Static_assert(CAPTURE_WORKSPACE_BYTES%8==0,"ASR tail must remain aligned");
#else
static agent_engine_storage_t engine_workspace;
static agent_engine_t engine={AGENT_ENGINE_STORAGE_INIT(engine_workspace)};
#endif
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

static agent_err_t workspace_restore(bool retain_candidate)
{
#if AGENT_TEXT_PREFETCH
    if(!retain_candidate)esp_agent_voice_candidate_discard();
    /* All capture/network borrowers have joined. Closed ticket allocations
     * can pin holes while the larger workspace is restored, leaving too
     * little contiguous room for the next TLS record despite ample free RAM.
     * Release them only at this phase boundary, not at every idle check. */
    if(capture_workspace) {
        esp_agent_speech_ws_expire(true);
        /* No borrower survives candidate join/cache seal. The logical capture
         * layout stays unchanged, while its allocation also fits state. Keep
         * the block instead of relying on a larger contiguous reallocation. */
        agent_err_t error=esp_agent_engine_memory_adopt(&engine,capture_workspace,CAPTURE_ALLOCATION_BYTES);
        if(!error)capture_workspace=NULL;
        return error;
    }
    return esp_agent_engine_memory_restore(&engine,false);
#else
    (void)retain_candidate;
#endif
    return AGENT_OK;
}
agent_err_t esp_agent_workspace_restore(void)
{
#if AGENT_TEXT_PREFETCH
    bool restoring=capture_workspace || !engine.workspace;
    if(restoring)esp_agent_voice_heap_mark(VOICE_HEAP_WORKSPACE_RESTORE_BEGIN);
#endif
    agent_err_t error=workspace_restore(false);
#if AGENT_TEXT_PREFETCH
    if(restoring)esp_agent_voice_heap_mark(VOICE_HEAP_WORKSPACE_RESTORE_END);
#endif
    return error;
}
#if AGENT_ENABLE_AUDIO
/* Legacy TEN/live-ASR borrowers need one large arena. Request it explicitly;
 * candidate-to-answer restoration never needs this contiguous allocation. */
static agent_err_t workspace_contiguous(void)
{
    agent_err_t error=esp_agent_workspace_restore();
#if AGENT_TEXT_PREFETCH
    if(!error)error=esp_agent_engine_memory_restore(&engine,true);
#endif
    return error;
}
#endif
#if AGENT_TEXT_PREFETCH
agent_err_t esp_agent_workspace_restore_retaining_candidate(void)
{ return workspace_restore(true); }
#endif
#if AGENT_TEXT_PREFETCH
static bool voice_candidate_selected(void)
{
    return atomic_load(&voice_enabled) && atomic_load(&speech_qianwen) && voice_configured() &&
        atomic_load(&voice_fast) && !atomic_load(&route_gateway) && esp_agent_voice_prefetch_enabled();
}
#if AGENT_REQUEST_SCRATCH_COMPACT
static bool capture_idle_selected(void)
{
    return voice_candidate_selected() && !atomic_load(&fast_prime_idle) &&
        !esp_agent_voice_candidate_idle_state();
}
static bool capture_idle_held(void)
{ return capture_workspace && capture_idle_selected(); }
/* The network owner has joined response, candidate and progress borrowers,
 * refreshed context, and still holds work_lock. Keep only the known capture
 * allocation while listening; context/text/sync restore before using it. */
static void capture_workspace_idle(void)
{
    if(capture_workspace)return;
    agent_err_t error=esp_agent_engine_memory_take_capture(&engine,CAPTURE_ALLOCATION_BYTES,&capture_workspace);
    if(!error) {
        memset(capture_workspace,0,CAPTURE_ALLOCATION_BYTES);
        voice_event(NULL,"capture_idle_retained",NULL);
    } else if(error!=AGENT_ERR_NOT_FOUND)voice_event(NULL,"capture_idle_error",agent_err_name(error));
}
#endif
static agent_err_t capture_workspace_prepare(void)
{
    if(capture_workspace)return AGENT_OK;
    esp_agent_voice_fast_discard();esp_agent_http_forget();
#if AGENT_REQUEST_SCRATCH_COMPACT
    agent_err_t recycled=esp_agent_engine_memory_take_capture(&engine,CAPTURE_ALLOCATION_BYTES,&capture_workspace);
    if(!recycled) {
        /* Same heap allocation, now with its previous engine borrowers joined.
         * Match calloc before admitting the next ASR/candidate owner. */
        memset(capture_workspace,0,CAPTURE_ALLOCATION_BYTES);
        voice_event(NULL,"capture_workspace_reused",NULL);return AGENT_OK;
    }
    if(recycled!=AGENT_ERR_NOT_FOUND)return recycled;
#endif
    agent_err_t error=esp_agent_engine_memory_release(&engine);
    if(error)return error;
    capture_workspace=calloc(1,CAPTURE_ALLOCATION_BYTES);
    return capture_workspace?AGENT_OK:AGENT_ERR_MEMORY;
}
#endif

bool esp_agent_online(void) { return atomic_load(&wifi_up); }

#if AGENT_ENABLE_AUDIO
agent_err_t esp_agent_qianwen_auth(char *out,size_t cap)
{
    if(!atomic_load(&speech_qianwen) || !qianwen_settings.key[0])return AGENT_ERR_CONFIG;
    int n=snprintf(out,cap,"Bearer %s",qianwen_settings.key);
    return n<0 || (size_t)n>=cap?AGENT_ERR_LIMIT:AGENT_OK;
}
#endif

agent_err_t esp_agent_http_auth(agent_http_target_t target, char *base, size_t base_size,
                               char *auth, size_t auth_size, const char **ca)
{
    if(target==AGENT_HTTP_PUBLIC) { if(!base_size || !auth_size) return AGENT_ERR_LIMIT; base[0]=auth[0]=0;*ca=NULL;return AGENT_OK; }
    const char *url = target == AGENT_HTTP_DEEPSEEK ? "https://api.deepseek.com" : settings.gateway_url;
    const char *key = target == AGENT_HTTP_DEEPSEEK ? settings.key : settings.gateway_token;
#if AGENT_ENABLE_AUDIO
    if(target==AGENT_HTTP_VOCALIGN) { url="https://platform.vocaligntech.com";key=speech_settings.key; }
#else
    if(target==AGENT_HTTP_VOCALIGN) return AGENT_ERR_CONFIG;
#endif
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

enum { USB_TX_BYTES = 2048 };
void esp_agent_write(const char *data, size_t size)
{
    if (!output_lock || xSemaphoreTake(output_lock, pdMS_TO_TICKS(2000)) != pdTRUE) return;
    while (size) {
        /* The driver enqueues each call atomically into this fixed TX ring. */
        size_t count = size > USB_TX_BYTES ? USB_TX_BYTES : size;
        int n = usb_serial_jtag_write_bytes(data, count, pdMS_TO_TICKS(100));
        if (n <= 0) break;
        data += n; size -= (size_t)n;
    }
    xSemaphoreGive(output_lock);
}

static void say(const char *text) { esp_agent_write(text, strlen(text)); }
#if AGENT_ENABLE_AUDIO
/* Voice has no dependency on an open serial terminal. The IDF driver enqueues
 * each write all-or-nothing; zero timeout cannot stall speech on a full TX ring.
 * Command replies retain esp_agent_write's bounded reliable path. */
static void voice_write(const char *text,size_t size)
{
    if(!size)return;
    int sent=0;
    if(output_lock && xSemaphoreTake(output_lock,0)==pdTRUE) {
        sent=usb_serial_jtag_write_bytes(text,size,0);
        xSemaphoreGive(output_lock);
    }
    if(sent<=0)atomic_fetch_add(&voice_usb_dropped,(unsigned)size);
}
#endif
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
    agent_json_printf(&w, "{\"firmware\":\"esp-hi-agent\",\"version\":\"" AGENT_RUNTIME_VERSION "\",\"phase\":\"LCD\","
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
    int8_t tx_power;
    if(esp_wifi_get_max_tx_power(&tx_power)==ESP_OK)
        agent_json_printf(&w,",\"wifi_tx_quarter_dbm\":%d",(int)tx_power);
    wifi_ap_record_t ap;
    if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)
        agent_json_printf(&w,",\"wifi_rssi_dbm\":%d",(int)ap.rssi);
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
    esp_agent_http_forget();
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
#if AGENT_ENABLE_AUDIO
    if(engine.voice_mode && !engine.answer_write && event->type==AGENT_EVENT_TEXT && event->length && !voice_first_text) {
        voice_first_text=true;voice_event(NULL,"llm_first_text",NULL);
    }
    if(engine.voice_mode && event->type==AGENT_EVENT_TOOL_END)voice_event(NULL,"tool_done",NULL);
    if(engine.voice_mode) {
        if(event->type==AGENT_EVENT_TEXT)voice_write(event->data,event->length);
        if(event->type==AGENT_EVENT_TOOL_END && event->data) {
            char header[128];int n=snprintf(header,sizeof(header),"\r\n@tool %s ",engine.workspace->reply.calls[event->index].name);
            if(n>0 && (size_t)n<sizeof(header))voice_write(header,(size_t)n);
            voice_write(event->data,event->length);voice_write("\r\n",2);
        }
        return AGENT_OK;
    }
#endif
    if (event->type == AGENT_EVENT_TEXT) esp_agent_write(event->data, event->length);
    if (event->type == AGENT_EVENT_TOOL_END && event->data) {
        say("\r\n@tool "); say(engine.workspace->reply.calls[event->index].name); say(" ");
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
static void pause_ms(unsigned ms);
static bool tool_cancelled(void *ctx) { (void)ctx;return atomic_load(&kernel->cancelled); }
static agent_err_t tool_search(void *ctx,const char *query,uint64_t before,unsigned limit,char *out,size_t cap)
{ (void)ctx; return agent_context_search(&context,query,before,limit,out,cap); }
static agent_err_t tool_summary_get(void *ctx,char *out,size_t cap)
{ (void)ctx; return agent_context_summary_get(&context,out,cap); }
static agent_err_t tool_summary_set(void *ctx,const char *text,uint64_t through)
{ (void)ctx; return agent_context_summary_set(&context,text,through); }
static const agent_tool_ops_t tool_ops = { .status = tool_status, .context_stats = tool_context,
    .light_get = agent_devices_light_get, .light_set = agent_devices_light_set, .now_ms = tool_now,
    .ctx=&esp_hi_devices,.control=&esp_hi_control,.wait_ms=pause_ms,.cancelled=tool_cancelled,
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
#if AGENT_ENDPOINT_TRACE
static atomic_bool endpoint_trace_enabled;
/* Disabled by default. This diagnostic reads the existing immutable records
 * after acquisition stops. Its one150-ms USB budget never extends sampling.
 * Enabled runs measure classification, not end-to-end response latency. */
static bool endpoint_trace_write(const char *data,size_t length,uint64_t deadline)
{
    if(esp_agent_now()>=deadline || !output_lock ||
       xSemaphoreTake(output_lock,pdMS_TO_TICKS(5))!=pdTRUE)return false;
    int sent=0;
    if(esp_agent_now()<deadline)
        sent=usb_serial_jtag_write_bytes(data,length,pdMS_TO_TICKS(5));
    xSemaphoreGive(output_lock);
    return sent==(int)length;
}
#if AGENT_CAPTURE_PROBE
static void noise_trace_write(unsigned id,uint64_t deadline)
{
    const noise_trace_t *trace=esp_hi_wake_noise_trace();
    unsigned count=noise_trace_count(trace);
    char line[288];
    int n=snprintf(line,sizeof(line),"\r\n@noise {\"id\":%u,\"kind\":\"begin\",\"count\":%u,\"total\":%u}\r\n",id,count,trace->total);
    bool ok=n>0 && (size_t)n<sizeof(line) && endpoint_trace_write(line,(size_t)n,deadline);
    uint32_t crc=UINT32_MAX;static const char hex[]="0123456789abcdef";
    for(unsigned row=0;ok && row<count;row+=12) {
        unsigned take=count-row;if(take>12)take=12;
        uint8_t bytes[96];
        for(unsigned i=0;i<take;++i) {
            const noise_trace_row_t *r=noise_trace_row(trace,row+i);
            for(unsigned j=0;j<4;++j)bytes[i*8+j]=(uint8_t)(r->time_ms>>(j*8));
            bytes[i*8+4]=(uint8_t)r->level;bytes[i*8+5]=(uint8_t)(r->level>>8);
            bytes[i*8+6]=(uint8_t)r->noise;bytes[i*8+7]=(uint8_t)(r->noise>>8);
        }
        crc=agent_crc32_update(crc,bytes,take*8);
        n=snprintf(line,sizeof(line),"\r\n@noise {\"id\":%u,\"kind\":\"rows\",\"offset\":%u,\"hex\":\"",id,row);
        if(n<0 || (size_t)n+take*16+4>=sizeof(line)){ok=false;break;}
        size_t used=(size_t)n;
        for(unsigned i=0;i<take*8;++i){line[used++]=hex[bytes[i]>>4];line[used++]=hex[bytes[i]&15];}
        memcpy(line+used,"\"}\r\n",4);used+=4;ok=endpoint_trace_write(line,used,deadline);
    }
    if(ok) {
        n=snprintf(line,sizeof(line),"\r\n@noise {\"id\":%u,\"kind\":\"end\",\"crc32\":\"%08lx\"}\r\n",id,(unsigned long)~crc);
        if(n>0 && (size_t)n<sizeof(line))(void)endpoint_trace_write(line,(size_t)n,deadline);
    }
}
#endif
static void audio_scratch_observe(void *ctx,const uint8_t *records,unsigned frames,unsigned noise,
                                  const agent_endpoint_t *endpoint,bool fast)
{
    (void)ctx;
    if(!fast || !atomic_load(&endpoint_trace_enabled))return;
    static unsigned serial;
    const unsigned id=++serial;
    const endpoint_notice_trace_t *notices=esp_hi_confirmation_notice_trace();
    uint64_t at=esp_agent_now(),deadline=at+150;
    char line[352];
    int n=snprintf(line,sizeof(line),"\r\n@endpoint {\"id\":%u,\"kind\":\"begin\",\"time_ms\":%llu,\"frames\":%u,\"noise\":%u,\"end_ms\":%u,\"elapsed_ms\":%u,\"state\":%u,\"speech_ms\":%u,\"quiet_ms\":%u,\"resume_frames\":%u,\"transcribed_ms\":%u,\"shadow\":%s}\r\n",
        id,(unsigned long long)at,frames,noise,endpoint->end_ms,endpoint->elapsed_ms,
        (unsigned)endpoint->state,endpoint->speech_ms,endpoint->quiet_ms,endpoint->resume_frames,endpoint->transcribed_ms,
        AGENT_CAPTURE_PROBE?"true":"false");
    bool ok=frames<=500 && n>0 && (size_t)n<sizeof(line) && endpoint_trace_write(line,(size_t)n,deadline);
    static const char hex[]="0123456789abcdef";
    for(unsigned row=0;ok && row<frames;row+=16) {
        unsigned count=frames-row;if(count>16)count=16;
        n=snprintf(line,sizeof(line),"\r\n@endpoint {\"id\":%u,\"kind\":\"frames\",\"offset\":%u,\"hex\":\"",id,row);
        if(n<0 || (size_t)n+count*12+6>=sizeof(line)) {ok=false;break;}
        size_t used=(size_t)n;
        for(unsigned i=0;i<count*6;++i) {
            unsigned byte=records[row*6+i];line[used++]=hex[byte>>4];line[used++]=hex[byte&15];
        }
        memcpy(line+used,"\"}\r\n",4);used+=4;
        ok=endpoint_trace_write(line,used,deadline);
    }
    uint32_t notice_crc=UINT32_MAX;
    for(unsigned row=0;ok && row<notices->count;row+=8) {
        unsigned count=notices->count-row;if(count>8)count=8;
        uint8_t bytes[64];
        for(unsigned i=0;i<count;++i) {
            const endpoint_notice_row_t *r=&notices->rows[row+i];
            for(unsigned j=0;j<4;++j) {
                bytes[i*8+j]=(uint8_t)(r->before_ms>>(j*8));
                bytes[i*8+4+j]=(uint8_t)(r->notice>>(j*8));
            }
        }
        notice_crc=agent_crc32_update(notice_crc,bytes,count*8);
        n=snprintf(line,sizeof(line),"\r\n@endpoint {\"id\":%u,\"kind\":\"notices\",\"offset\":%u,\"hex\":\"",id,row);
        if(n<0 || (size_t)n+count*16+6>=sizeof(line)) {ok=false;break;}
        size_t used=(size_t)n;
        for(unsigned i=0;i<count*8;++i) {
            line[used++]=hex[bytes[i]>>4];line[used++]=hex[bytes[i]&15];
        }
        memcpy(line+used,"\"}\r\n",4);used+=4;
        ok=endpoint_trace_write(line,used,deadline);
    }
    /* A missing trailer is an incomplete diagnostic, never fabricated frames.
     * Do not spend another USB budget on failure or after the original bound. */
    if(ok) {
        n=snprintf(line,sizeof(line),"\r\n@endpoint {\"id\":%u,\"kind\":\"end\",\"crc32\":\"%08lx\",\"elapsed_ms\":%llu,\"notice_count\":%u,\"notice_overflow\":%s,\"notice_crc32\":\"%08lx\",\"held_ms\":%u,\"dense_ms\":%u,\"asr_notice\":%u,\"asr_floor_ms\":%u,\"local_onset\":%s}\r\n",
            id,(unsigned long)agent_crc32(records,frames*6),(unsigned long long)(esp_agent_now()-at),
            notices->count,notices->overflow?"true":"false",(unsigned long)~notice_crc,
            endpoint->held_frames*20,endpoint->dense_at_ms,endpoint->asr_notice,endpoint->asr_floor_ms,
            endpoint->local_onset?"true":"false");
        if(n>0 && (size_t)n<sizeof(line))ok=endpoint_trace_write(line,(size_t)n,deadline);
#if AGENT_CAPTURE_PROBE
        if(ok)noise_trace_write(id,deadline);
#endif
    }
}
#endif
static agent_err_t audio_scratch_acquire(void *ctx,void **memory,size_t *capacity,bool *fast)
{
    (void)ctx;
    agent_err_t error=agent_begin_turn(kernel); if(error) return error;
    if(xSemaphoreTake(work_lock,0)!=pdTRUE) { agent_end_turn(kernel); return AGENT_ERR_BUSY; }
    atomic_store(&voice_handoff,false);
    *fast=false;
#if AGENT_TEXT_PREFETCH
    if(voice_candidate_selected()) {
        /* This decision precedes cue/capture and remains frozen by the turn
         * reservation. The compact layout never constructs the TEN backend. */
        /* The completed HTTP turn may retain a closed ticket/configuration.
         * Release it before resizing this arena so it cannot pin a hole
         * across both capture TLS connections and the later engine restore.
         * Ordinary non-candidate voice turns keep their HTTP reuse policy. */
        error=capture_workspace_prepare();
        if(!error) {
            *memory=capture_workspace;*capacity=CAPTURE_WORKSPACE_BYTES-AGENT_QWEN_SCRATCH;*fast=true;
            return AGENT_OK;
        }
    } else
#endif
    error=workspace_contiguous();
    if(error) {
        (void)esp_agent_workspace_restore();xSemaphoreGive(work_lock);agent_end_turn(kernel);return error;
    }
    *memory=agent_engine_scratch(&engine,capacity);
    /* A disjoint 8KiB tail belongs to live ASR while TEN owns the prefix. */
    *capacity=(*capacity-AGENT_QWEN_SCRATCH)&~(size_t)7;
    _Static_assert(AGENT_ENGINE_SCRATCH_SIZE-AGENT_QWEN_SCRATCH>=43000,"VAD arena plus source metadata must fit");
    return AGENT_OK;
}
static void audio_scratch_release(void *ctx)
{
    (void)ctx;
    /* A queued live worker retains the reservation and may own the idle TEN
     * prefix for a silent draft. It initializes messages/reply only after
     * draining that draft. Non-handoff captures still clear on release. */
    if(!atomic_load(&voice_handoff)) {
        agent_err_t error=esp_agent_workspace_restore();
        if(error)report(error);
        else {
            size_t capacity;void *memory=agent_engine_scratch(&engine,&capacity);
            memset(memory,0,(capacity-AGENT_QWEN_SCRATCH)&~(size_t)7);
        }
    }
    xSemaphoreGive(work_lock);
    if(!atomic_load(&voice_handoff))agent_end_turn(kernel);
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
    snprintf(output,sizeof(output),"{\"pcm\":2,\"active\":%s,\"reserved\":%s,\"generation\":%u,\"produced\":%u,\"consumed\":%u,\"slots\":%u,\"limit\":%u,\"discarded\":%u,\"reason\":\"%s\",\"writing\":%s,\"io_error\":%s,\"cleanup_error\":%u}\r\n",
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
    char text[384];
    int n=snprintf(text,sizeof(text),"{\"pcm\":2,\"generation\":%u,\"sequence\":%u,\"time_ms\":%u,\"inference_us\":%u,\"copy_us\":%u,\"flags\":%u,\"sample_end\":%llu,\"score_q8\":%d,\"scores_q8\":[%d,%d],\"heads_q8\":[%d,%d,%d],\"armed\":%s,\"crc\":%u,\"hex\":\"",
        stream->generation,(unsigned)f->sequence,(unsigned)f->time_ms,(unsigned)f->inference_us,(unsigned)f->copy_us,
        (unsigned)f->flags,(unsigned long long)f->sample_end,(int)f->score_q8,
        (int)f->scores_q8[0],(int)f->scores_q8[1],
        (int)f->heads_q8[0],(int)f->heads_q8[1],(int)f->heads_q8[2],
        f->flags&KEYWORD_PCM_ARMED?"true":"false",(unsigned)f->checksum);
    bool okay=n>0 && (size_t)n<sizeof(text) && pcm_write(text,(size_t)n,deadline);
    static const char digits[]="0123456789abcdef";
    for(size_t offset=0;okay && offset<sizeof(f->data);offset+=128) {
        for(unsigned i=0;i<128;i++) { unsigned value=f->data[offset+i];text[2*i]=digits[value>>4];text[2*i+1]=digits[value&15]; }
        okay=pcm_write(text,256,deadline);
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
    void *memory=NULL;size_t capacity=0;bool fast=false;
    agent_err_t error=audio_scratch_acquire(NULL,&memory,&capacity,&fast);
    if(!error) {
        if(fast)error=AGENT_ERR_BUSY;
        agent_audio_state_t audio={0};if(!error)error=esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx,&audio);
        if(audio.listening || audio.playing || audio.recording || audio.mic_requested || audio.mic_enabled) error=AGENT_ERR_BUSY;
        if(!error && !keyword_pcm_open(stream,memory,capacity,limit)) error=AGENT_ERR_MEMORY;
        if(error) audio_scratch_release(NULL);
        else { pcm_reserved=true;pcm_discard=pcm_io_error=false;pcm_cleanup_error=AGENT_OK;pcm_deadline=esp_agent_now()+(uint64_t)limit*32+2000; }
    }
    report(error);
}
static bool pcm_command_allowed(const char *line)
{
#ifdef AGENT_MIC_EXACT_CLOCK
    if(!strcmp(line,"agent mic clock")) return true;
#endif
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
        error=esp_agent_workspace_restore();
        if(!error)error=agent_upload_begin(&music_upload,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,length,crc,esp_agent_now());
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

#if AGENT_WS_OWNER_PROBE
/* Compile-only opt-in. Existing network task and reserved engine scratch;
 * no microphone, response request, tool execution, playback or extra stack. */
extern const agent_ws_ops_t esp_agent_realtime_ws;
static uint64_t ws_probe_now(void *ctx) { (void)ctx;return esp_agent_now(); }
static void ws_probe_report(unsigned cycle,const char *stage,uint64_t began,agent_err_t error)
{
    char out[320];int n=snprintf(out,sizeof(out),
        "{\"ws_probe\":1,\"cycle\":%u,\"stage\":\"%s\",\"ms\":%u,\"error\":\"%s\","
        "\"free\":%u,\"min\":%u,\"largest\":%u,\"stack\":%u}\r\n",
        cycle,stage,(unsigned)(esp_agent_now()-began),agent_err_name(error),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if(n>0 && (size_t)n<sizeof(out))say(out);
}
static agent_err_t voice_connections_probe(void)
{
    if(atomic_load(&voice_enabled) || !atomic_load(&speech_qianwen))return AGENT_ERR_CONFIG;
    agent_err_t prepared=workspace_contiguous();if(prepared)return prepared;
    typedef struct {
        agent_asr_realtime_t asr;
        agent_asr_segments_t segments;
        agent_realtime_t reply;
        agent_speech_t input,output;
        char text[AGENT_INPUT_MAX+1],asr_scratch[8192],reply_scratch[8192];
    } probe_t;
    _Static_assert(sizeof(probe_t)<=AGENT_ENGINE_SCRATCH_SIZE,"Probe cannot allocate a new large buffer");
    size_t capacity;probe_t *p=agent_engine_scratch(&engine,&capacity);
    (void)capacity;esp_agent_http_release();esp_agent_speech_ws_expire(true);
    uint64_t began=esp_agent_now();ws_probe_report(0,"before",began,AGENT_OK);
    if(heap_caps_monitor_local_minimum_free_size_start()!=ESP_OK)return AGENT_ERR_BUSY;
    agent_err_t error=AGENT_OK;
    for(unsigned cycle=1;cycle<=3 && !error;++cycle) {
        memset(p,0,sizeof(*p));
        p->input=(agent_speech_t){.cancelled=&kernel->cancelled,.scratch=p->asr_scratch,
            .capacity=sizeof(p->asr_scratch),.now_ms=ws_probe_now};
        p->output=p->input;p->output.scratch=p->reply_scratch;p->output.capacity=sizeof(p->reply_scratch);
        agent_asr_realtime_init(&p->asr,&p->input,&esp_agent_asr_ws,p->text,sizeof(p->text));
        agent_realtime_init(&p->reply,&p->output,&esp_agent_realtime_ws,NULL,NULL,NULL);
        error=agent_asr_realtime_use_vad(&p->asr,&p->segments);
        if(!error)error=agent_asr_realtime_begin(&p->asr);
        ws_probe_report(cycle,"asr",began,error);
        if(!error)error=agent_realtime_begin(&p->reply,"你是小言。等待用户输入。","[]",false);
        ws_probe_report(cycle,"both",began,error);
        for(unsigned i=0;i<3 && !error;++i) {
            error=agent_asr_realtime_poll(&p->asr,20);
            if(!error)error=agent_realtime_poll(&p->reply,20);
        }
        /* Alternate close order; poll the survivor before its own close. */
        if(cycle&1) {
            agent_asr_realtime_cancel(&p->asr);
            if(!error)error=agent_realtime_poll(&p->reply,20);
            ws_probe_report(cycle,"asr_closed",began,error);
            agent_realtime_cancel(&p->reply);
        } else {
            agent_realtime_cancel(&p->reply);
            if(!error)error=agent_asr_realtime_poll(&p->asr,20);
            ws_probe_report(cycle,"reply_closed",began,error);
            agent_asr_realtime_cancel(&p->asr);
        }
        ws_probe_report(cycle,"closed",began,error);
        if(!error && esp_agent_now()-began>60000)error=AGENT_ERR_TIMEOUT;
    }
    esp_agent_speech_ws_expire(true);ws_probe_report(0,"expired",began,error);
    esp_err_t ended=heap_caps_monitor_local_minimum_free_size_stop();
    if(!error && ended!=ESP_OK)error=AGENT_ERR_CONFIG;
    return error;
}
#endif
__attribute__((noinline)) static agent_err_t context_command(const char *text)
{
#if AGENT_WS_OWNER_PROBE
    if(!strcmp(text,"voice connections-probe"))return voice_connections_probe();
#endif
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
            engine.workspace->reply.call_count,(unsigned)engine.workspace->reply.args_used,agent_err_name(engine.workspace->reply.error),
            agent_err_name(engine.workspace->sse.error),(unsigned)engine.workspace->sse.used,engine.workspace->reply.finished);
        say(diagnostic);
        for(unsigned i=0;i<engine.workspace->reply.call_count;++i) {
            if(!strcmp(engine.workspace->reply.calls[i].name,"device_audio_play_song")) {
                say(agent_call_arguments(&engine.workspace->reply,i)); say("\r\n");
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
    if(!strncmp(text,"archive ",8)) {
        cJSON *args=agent_json_parse(text+8,strlen(text+8));
        uint64_t generation,through;
        bool valid=cJSON_IsObject(args) &&
            agent_json_uint(cJSON_GetObjectItemCaseSensitive(args,"generation"),AGENT_SEQ_MAX,&generation) &&
            agent_json_uint(cJSON_GetObjectItemCaseSensitive(args,"through_record"),AGENT_SEQ_MAX,&through);
        cJSON_Delete(args);
        return valid?agent_context_archive(&context,generation,through):AGENT_ERR_ARGUMENT;
    }
    if(!strncmp(text,"budget ",7)) {
        size_t bytes=!strcmp(text+7,"64")?64u*1024u:!strcmp(text+7,"128")?128u*1024u:!strcmp(text+7,"200")?200u*1024u:0;
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

#if AGENT_ENABLE_AUDIO
static void pause_ms(unsigned ms);
static void voice_event_at(const char *stage,const char *text,uint64_t at)
{
    for(unsigned i=0;i<sizeof(voice_stages)/sizeof(*voice_stages);++i) if(!strcmp(stage,voice_stages[i])) atomic_store(&voice_stage,i);
    char output[2304]; agent_json_writer_t w;agent_json_writer_init(&w,output,sizeof(output));
    agent_json_raw(&w,"\r\n@voice {\"stage\":");agent_json_quote(&w,stage);
    /* Some audio timestamps are reported after the drain. Keep the heap's
     * sampling time explicit instead of attributing it to an earlier DMA. */
    agent_json_printf(&w,",\"time_ms\":%llu,\"heap_time_ms\":%llu,\"free_heap\":%u,\"min_heap\":%u,\"largest_block\":%u",
        (unsigned long long)at,(unsigned long long)esp_agent_now(),
        (unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if(text) {agent_json_raw(&w,",\"text\":");agent_json_quote(&w,text);}
    agent_json_raw(&w,"}\r\n");if(!w.error) voice_write(output,w.used);
}
static void voice_event(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(!strcmp(stage,"asr_started") && atomic_load(&voice_handoff))esp_hi_voice_live_ready();
#if AGENT_TEXT_PREFETCH
    /* The coordinator admits connection setup at the transport boundary;
     * the cooperative path lets ASR finish TLS before starting its peer. */
    esp_agent_voice_candidate_asr_notice(stage);
#endif
    voice_event_at(stage,text,esp_agent_now());
}
#ifdef AGENT_VOICE_HEAP_DIAGNOSTICS
/* A closing session is exported by USB/main, whose stack is smaller than the
 * network task's. Keep its bounded metadata out of the general speech event
 * writer and candidate bookkeeping. The row's original clock stays in text. */
static void __attribute__((noinline)) voice_heap_event(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    char output[512];agent_json_writer_t w;agent_json_writer_init(&w,output,sizeof(output));
    agent_json_raw(&w,"\r\n@voice {\"stage\":");agent_json_quote(&w,stage);
    unsigned now=(unsigned)esp_agent_now();
    agent_json_printf(&w,",\"time_ms\":%u,\"heap_time_ms\":%u,\"free_heap\":%u,\"min_heap\":%u,\"text\":",
        now,now,(unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size());
    agent_json_quote(&w,text);agent_json_raw(&w,"}\r\n");
    if(!w.error)esp_agent_write(output,w.used);
    else report(w.error);
}
#endif
static unsigned voice_request_id;
static void voice_request_trace(void *ctx,const char *phase)
{
    const agent_engine_t *e=ctx;
    if(!strcmp(phase,"select_begin")) ++voice_request_id;
    /* Fixed internal phase identifiers; this diagnostic does not touch the
     * shared speech/context scratch and uses a small bounded stack buffer. */
    char line[384];
    uint64_t at=esp_agent_now();
    int n=snprintf(line,sizeof(line),"\r\n@voice {\"stage\":\"llm_%s_%s\","
        "\"time_ms\":%llu,\"heap_time_ms\":%llu,\"free_heap\":%u,\"min_heap\":%u,"
        "\"largest_block\":%u,\"request\":%u,\"request_bytes\":%u,\"history_bytes\":%u}\r\n",
        e->answer_final?"final":"plan",phase,(unsigned long long)at,(unsigned long long)at,
        (unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),voice_request_id,
        (unsigned)e->context->request_bytes,(unsigned)e->context->prompt_bytes);
    if(n>0 && (size_t)n<sizeof(line)) voice_write(line,(size_t)n);
}
static void voice_asr_sentence(void *ctx,uint32_t id,unsigned end_ms,bool final,bool nonempty,bool timed)
{
    (void)ctx;
    if(atomic_load(&voice_handoff))esp_hi_voice_live_sentence(id,end_ms,final,nonempty,timed);
#if AGENT_TEXT_PREFETCH
    esp_agent_voice_candidate_revision(id);
#endif
}
static bool voice_capture_start(void)
{
    if(!atomic_load(&voice_enabled) || !atomic_load(&speech_qianwen) || !voice_configured())return false;
    turn_job_t job={.text=work_buffer.input,.voice=true,.live=true,
        .fast=atomic_load(&voice_fast) && !atomic_load(&route_gateway)};
#if AGENT_TEXT_PREFETCH
    /* Bind to the allocation already selected by the audio owner, not a
     * second read of mode flags between admission and capture start. */
    job.candidate=capture_workspace!=NULL;
    if(job.candidate)job.fast=false;
#endif
    if(esp_hi_voice_live_fast(job.fast || AGENT_ISOLATED_ASR,!job.fast))return false;
    /* The audio owner already holds the turn and work mutex. It gives the
     * mutex back after joining VAD but transfers the turn to this worker. */
    atomic_store(&voice_handoff,true);
    if(xQueueSend(jobs,&job,0)==pdTRUE)return true;
    atomic_store(&voice_handoff,false);return false;
}
#if AGENT_ISOLATED_ASR
static void voice_isolated_event(void *ctx,const char *stage,const char *text)
{
    bool settled=!strcmp(stage,"asr_segment_final");
    bool preview=settled || !strcmp(stage,"asr_partial") || !strcmp(stage,"asr_segment_pending");
    if(preview) {
        if(!atomic_load(&kernel->cancelled))esp_hi_voice_live_hint(text,settled);
#if AGENT_TEXT_PREFETCH
        esp_agent_voice_candidate_preview(text);
#endif
    }
    voice_event(ctx,stage,text);
}
#endif
/* Keep capture-only sessions out of the persistent network-task frame. */
__attribute__((noinline)) static agent_err_t voice_live_asr(bool candidate)
{
    (void)candidate;
    esp_agent_http_release();
    atomic_store(&voice_started,(unsigned)esp_agent_now());voice_event(NULL,"capture_begin",NULL);
    size_t bytes;char *scratch=agent_engine_scratch(&engine,&bytes);
#if AGENT_TEXT_PREFETCH
    if(candidate) {scratch=capture_workspace;bytes=CAPTURE_WORKSPACE_BYTES;}
#endif
    scratch+=(bytes-AGENT_QWEN_SCRATCH)&~(size_t)7;
    agent_speech_t speech={.cancelled=&kernel->cancelled,.scratch=scratch,.capacity=AGENT_QWEN_SCRATCH,
        .now_ms=tool_now,.wait_ms=pause_ms,.event=voice_event,.random_u32=esp_random,
        .asr_sentence=voice_asr_sentence};
    agent_speech_live_t input;agent_err_t error=esp_hi_voice_live_input(&input);
#if AGENT_TEXT_PREFETCH
    if(candidate)esp_agent_voice_candidate_begin(&kernel->cancelled,voice_event);
#endif
    /* The classic/isolated ASR route owns capture too. Retry a retained
     * restoration before starting a new temporary radio budget. */
    esp_agent_capture_radio_t radio={0};
    if(!error)error=esp_agent_capture_radio_end();
    if(!error)error=esp_agent_capture_radio_begin(&radio);
    if(!error) {
        char text[96];snprintf(text,sizeof(text),"previous=%d,applied=%d,rssi=%d",
            radio.previous,radio.applied,radio.rssi);
        voice_event(NULL,"capture_radio",text);
    }
#if AGENT_ISOLATED_ASR
    _Static_assert(AGENT_ASR_RT_SCRATCH==AGENT_QWEN_SCRATCH,"Keep the capture scratch partition");
    speech.event=voice_isolated_event;
    agent_asr_realtime_t session;
    agent_asr_segments_t segments;
    agent_ws_ops_t transport=esp_agent_asr_ws;
#if AGENT_TEXT_PREFETCH
    if(candidate)transport.open=esp_agent_voice_candidate_asr_open;
#endif
    agent_asr_realtime_init(&session,&speech,&transport,work_buffer.input,AGENT_INPUT_MAX+1);
    if(!error)error=agent_asr_realtime_use_vad(&session,&segments);
    if(!error)error=AGENT_VOICE_SCRATCH_LOANS?agent_asr_realtime_live_shared(&session,&input):
        agent_asr_realtime_live(&session,&input);
#else
    agent_qwen_t session;agent_qwen_init(&session,&speech,&esp_agent_speech_ws);
    if(!error)error=agent_qwen_live(&speech,&input,work_buffer.input,AGENT_INPUT_MAX+1);
#endif
    error=esp_hi_voice_live_join(error);
    agent_err_t restored=esp_agent_capture_radio_end();
    voice_event(NULL,"capture_radio_restored",agent_err_name(restored));
    return error?error:restored;
}
static agent_err_t voice_pcm_open(void *ctx,unsigned rate)
{
    (void)ctx;voice_event(NULL,"playback",NULL);
    return esp_hi_stream_open(rate,engine.workspace->messages.data,12288);
}
static agent_err_t voice_pcm_write(void *ctx,const int16_t *data,size_t count)
{ (void)ctx;return esp_hi_stream_write(data,count,&kernel->cancelled); }
typedef struct {
    esp_agent_voice_progress_t cached;
    const esp_agent_voice_ack_t *ack;
    bool dynamic_active,notice_active,notice_attempted,candidate;
} voice_progress_t;
static void voice_progress_event(void *ctx,const char *stage,const char *text)
{
    (void)ctx;char name[64];
    int n=snprintf(name,sizeof(name),"progress_%s",stage);
    if(n>0 && (size_t)n<sizeof(name))voice_event(NULL,name,text);
}
static void voice_stream_prepare(bool progress)
{
    esp_agent_voice_stream_prepare(&kernel->cancelled,qianwen_settings.voice,
        work_buffer.input,sizeof(work_buffer.input),engine.buffer+AGENT_ENGINE_ANSWER_SCRATCH,
        AGENT_ENGINE_BUFFER_SIZE-AGENT_ENGINE_ANSWER_SCRATCH,progress?voice_progress_event:voice_event);
}
static agent_err_t voice_progress_begin(void *ctx)
{
    voice_progress_t *progress=ctx;
    if(!progress->ack || !progress->ack->text[0])return AGENT_ERR_CONFIG;
    /* Engine has persisted/copied ASR and completely sent planning's body.
     * The original input now becomes the text queue, and only SSE's prefix
     * remains live in buffer. No second audio/WSS worker is introduced. */
    voice_stream_prepare(true);progress->dynamic_active=true;
    voice_event(NULL,"progress_start",progress->ack->text);
    agent_err_t error=esp_agent_voice_stream_begin(NULL);
    if(!error)error=esp_agent_voice_stream_write(NULL,progress->ack->text,strlen(progress->ack->text));
    if(!error)error=esp_agent_voice_stream_seal();
    return error;
}
static agent_err_t voice_progress_end(void *ctx,agent_err_t result)
{
    voice_progress_t *progress=ctx;
    if(!progress->dynamic_active)return result;
    agent_err_t error=esp_agent_voice_stream_end(NULL,result);
    progress->dynamic_active=false;
    uint64_t started=esp_hi_stream_started_at();
    if(started)voice_event_at("progress_speaker_start",NULL,started);
    if(!error && started) {
        engine.progress_text=progress->ack->text;engine.progress_language=progress->ack->language;
    }
    voice_event(NULL,"progress_end",error?agent_err_name(error):NULL);
    return error;
}
static agent_err_t voice_native_join(voice_progress_t *progress,agent_err_t result)
{
    if(!progress->ack || !progress->ack->pending)return result;
    agent_err_t error;
#if AGENT_TEXT_PREFETCH
    if(progress->candidate)error=esp_agent_voice_candidate_ack_join(result);
    else
#endif
    error=esp_agent_voice_fast_ack_join(result);
    uint64_t started=esp_hi_stream_started_at();
    if(started)voice_event_at("progress_speaker_start",NULL,started);
    if(!error && engine.voice_mode) {
        engine.heard_ack=progress->ack->text;engine.heard_ack_language=progress->ack->language;
        engine.progress_text=progress->ack->text;engine.progress_language=progress->ack->language;
    }
    voice_event(NULL,"progress_end",error?agent_err_name(error):NULL);
    return error;
}
static agent_err_t voice_progress_join(void *ctx)
{
    voice_progress_t *progress=ctx;
    agent_err_t native=voice_native_join(progress,AGENT_OK);
    if(native)return native;
    if(progress->dynamic_active)return voice_progress_end(ctx,AGENT_OK);
    bool active=progress->cached.active;
    agent_err_t error=esp_agent_voice_progress_join(&progress->cached);
    if(active) {
        uint64_t started=esp_hi_progress_started_at();
        if(started)voice_event_at("progress_speaker_start",NULL,started);
#if AGENT_CONTEXTUAL_CACHED_ACK
        if(!error)(void)esp_agent_voice_progress_heard(&progress->cached,started,&engine);
#endif
        voice_event(NULL,"progress_end",error?agent_err_name(error):NULL);
    }
    if(progress->notice_active) {
        agent_err_t ended=esp_hi_progress_join();progress->notice_active=false;
        uint64_t at=esp_hi_progress_started_at();
        if(at)voice_event_at("stage_speaker_start",NULL,at);
        voice_event(NULL,"stage_end",ended?agent_err_name(ended):NULL);
        if(!error)error=ended;
    }
    return error;
}
#if AGENT_CONTEXTUAL_CACHED_ACK
static agent_err_t voice_cached_progress_begin(void *ctx)
{
    const voice_progress_t *progress=ctx;
    /* The settled fallback already owns the board job. This only registers
     * its lifetime with the engine; never starts a second playback. */
    return progress->cached.active && progress->cached.queued?AGENT_OK:AGENT_ERR_CONFIG;
}
static agent_err_t voice_cached_progress_end(void *ctx,agent_err_t result)
{ (void)result;return voice_progress_join(ctx); }
#endif
static agent_err_t voice_before_tools(void *ctx)
{
    voice_progress_t *progress=ctx;
    agent_err_t error=voice_progress_join(ctx);
    if(error || progress->notice_attempted)return error;
    if(!engine.workspace->reply.call_count)return AGENT_OK;
    /* Announce only an admitted homogeneous context batch. Hardware/audio
     * tools may need the speaker; no timer-driven or speculative notices. */
    const char *name=engine.workspace->reply.calls[0].name;
    bool memory=!strcmp(name,"agent_context_summary_set");
    if(!memory && strcmp(name,"agent_context_search"))return AGENT_OK;
    for(unsigned i=1;i<engine.workspace->reply.call_count;++i)
        if(strcmp(engine.workspace->reply.calls[i].name,name))return AGENT_OK;
#if AGENT_CONTEXTUAL_CACHED_ACK
    if(progress->cached.completed && engine.progress_text &&
       progress->cached.kind==(memory?ESP_AGENT_PROGRESS_MEMORY:ESP_AGENT_PROGRESS_SEARCH)) {
        progress->notice_attempted=true;return AGENT_OK;
    }
#endif
    progress->notice_attempted=progress->notice_active=true;
    bool yue=progress->ack && !strcmp(progress->ack->language,"yue");
    voice_event(NULL,"stage_start",memory?(yue?"我记低先。":"我来保存。"):
        (yue?"我而家查紧之前嘅记录。":"我在查之前的记录。"));
    return memory?esp_hi_progress_memory_begin(yue,&kernel->cancelled):
        esp_hi_progress_search_begin(yue,&kernel->cancelled);
}
static agent_err_t voice_answer_begin(void *ctx)
{
    agent_err_t error=voice_progress_join(ctx);
    if(!error)voice_stream_prepare(false);
    return error?error:esp_agent_voice_stream_begin(NULL);
}
static agent_err_t voice_answer_prepare(void *ctx)
{
    (void)ctx;
    voice_event(NULL,"tts_warm_begin",NULL);
    agent_err_t error=esp_agent_speech_ws_warm(&kernel->cancelled);
    voice_event(NULL,error?"tts_warm_miss":"tts_warm_ready",error?agent_err_name(error):NULL);
    /* No synthesis request or text was sent. A failed speculative connection
     * can leave ordinary TTS startup available, without replaying a task. */
    return atomic_load(&kernel->cancelled)?AGENT_ERR_CANCELLED:AGENT_OK;
}
#if AGENT_TEXT_PREFETCH
static agent_err_t voice_candidate_resume(void *ctx,agent_err_t error)
{
    (void)ctx;bool handled=false;
    void *captured=capture_workspace;
    agent_err_t result=esp_agent_voice_candidate_finish(&engine,work_buffer.input,error,NULL,&handled);
    voice_event(NULL,"workspace_adopt",engine.workspace && engine.scratch==captured?"capture_arena":"failed");
    return result;
}
#endif
static agent_err_t voice_run(const turn_job_t *job,agent_err_t error)
{
    voice_progress_t progress={0};
    agent_engine_deferred_t *deferred=NULL;
    if(!job->live)atomic_store(&voice_started,(unsigned)esp_agent_now());
#if AGENT_TEXT_PREFETCH
    if(job->candidate) {
        bool handled=false;progress.candidate=true;
#if AGENT_HANDOFF_PROBE
        if(job->probe_join)
            error=esp_agent_voice_candidate_finish(&engine,work_buffer.input,error,
                (char *)capture_workspace+CAPTURE_WORKSPACE_BYTES-AGENT_QWEN_SCRATCH,&handled);
        else
#endif
        error=esp_agent_voice_candidate_stage(&engine,work_buffer.input,error,
            (char *)capture_workspace+CAPTURE_WORKSPACE_BYTES-AGENT_QWEN_SCRATCH,&handled);
        progress.ack=esp_agent_voice_candidate_ack();
        if(!error && esp_agent_voice_candidate_deferred()) {
#if AGENT_HANDOFF_PROBE
            if(job->probe_hold) {
                /* Approximate the measured warm-to-sent interval without
                 * sending a request. Only reserve allocates the real buffer;
                 * it stays owned by engine memory through joined adoption. */
                uint64_t until=esp_agent_now()+650;
                voice_event(NULL,"handoff_probe_wait_begin",job->probe_reserve?"reserve":"delay");
                if(job->probe_reserve) {
                    error=esp_agent_engine_memory_prepare(&engine);
                    voice_event(NULL,"handoff_probe_reserved",agent_err_name(error));
                }
                while(!error && esp_agent_now()<until) {
                    if(atomic_load(&kernel->cancelled))error=AGENT_ERR_CANCELLED;
                    else vTaskDelay(pdMS_TO_TICKS(2));
                }
                voice_event(NULL,"handoff_probe_wait_end",agent_err_name(error));
                error=voice_candidate_resume(NULL,error);
            } else
#endif
            {
                deferred=calloc(1,sizeof(*deferred));
                agent_err_t allocated=AGENT_ERR_MEMORY;
                if(deferred) {
#if AGENT_REQUEST_SCRATCH_COMPACT
                    allocated=esp_agent_engine_memory_prepare_input(&engine,work_buffer.input);
#else
                    allocated=esp_agent_engine_memory_prepare(&engine);
#endif
                }
                if(!allocated) {
                    deferred->resume=voice_candidate_resume;
#if AGENT_REQUEST_SCRATCH_COMPACT
                    char prepared[32];snprintf(prepared,sizeof(prepared),"{\"bytes\":%u}",(unsigned)context.capacity);
                    voice_event(NULL,"request_scratch",prepared);
#endif
                }
                else {
                    /* No request was sent. Release the live producer before the
                     * ordinary path; keep the original full input/capacities. */
                    free(deferred);deferred=NULL;
                    error=voice_candidate_resume(NULL,AGENT_OK);
                }
            }
        }
        if(handled || error) {
            uint64_t at=esp_hi_stream_started_at();
            if(at>=atomic_load(&voice_started))voice_event_at("speaker_start",NULL,at);
            goto finished;
        }
    }
#endif
    if(!engine.workspace && !deferred) {if(!error)error=AGENT_ERR_MEMORY;goto finished;}
    if(job->fast) {
        bool delegate=false;
        /* Even failed capture owns a possibly open session. Close/join it
         * before classic fallback or releasing this turn's borrowed memory. */
        error=esp_agent_voice_fast_finish(error,&delegate);
        uint64_t cue_begin=0,cue_end=0;esp_hi_stream_cue_times(&cue_begin,&cue_end);
        unsigned began_at=atomic_load(&voice_started);
        if(cue_begin>=began_at && cue_begin)voice_event_at("endpoint_cue_start",NULL,cue_begin);
        if(cue_end>=began_at && cue_end)voice_event_at("endpoint_cue_end",NULL,cue_end);
        if(error || !delegate) {
            unsigned started=(unsigned)esp_hi_stream_started_at(),began=atomic_load(&voice_started);
            if(started && (unsigned)(started-began)<=(unsigned)esp_agent_now()-began)
                voice_event_at(esp_agent_voice_fast_ack_native()?"progress_speaker_start":"speaker_start",NULL,started);
            goto finished;
        }
        progress.ack=esp_agent_voice_fast_ack();
        engine.clarify_only=esp_agent_voice_fast_clarify();
        if(progress.ack->spoken && esp_hi_stream_started_at())
            voice_event_at("progress_speaker_start",NULL,esp_hi_stream_started_at());
    }
    if(!error && (job->fast || progress.candidate)) {
        /* Both settled routes can reach DeepSeek without a generated receipt.
         * Deferred/native owners remain joined by their existing path. */
        error=esp_agent_voice_progress_fallback(&progress.cached,progress.ack,
            deferred!=NULL,engine.clarify_only,work_buffer.input,&kernel->cancelled);
        if(error)goto finished;
        if(progress.cached.active)
#if AGENT_CONTEXTUAL_CACHED_ACK
            voice_event(NULL,"progress_start",esp_agent_voice_progress_text(&progress.cached));
#else
            voice_event(NULL,"progress_start","fallback_cached_acknowledgement");
#endif
    }
    agent_speech_t speech={.transport=engine.transport,.cancelled=&kernel->cancelled,
        .scratch=engine.buffer,.capacity=AGENT_ENGINE_BUFFER_SIZE,.now_ms=tool_now,.wait_ms=pause_ms,
        .event=voice_event,.nonce=esp_random(),.random_u32=esp_random};
    bool realtime=atomic_load(&speech_qianwen);
    bool overlap=realtime && !job->say && !engine.gateway;
    agent_qwen_t session;agent_qwen_init(&session,&speech,&esp_agent_speech_ws);
    const char *reply=job->text;
    if(!error && !voice_configured())error=AGENT_ERR_CONFIG;
    if(!error && !job->say) {
        if(!job->live) {
            agent_speech_input_t input;error=esp_hi_voice_input(&input);
            if(!error) {char value[64];snprintf(value,sizeof(value),"%lu samples at 16000 Hz",(unsigned long)input.samples);voice_event(NULL,"vad_end",value);}
#if AGENT_ISOLATED_ASR
            if(!error && realtime) {
                agent_asr_realtime_t asr;
                agent_asr_realtime_init(&asr,&speech,&esp_agent_asr_ws,work_buffer.input,AGENT_INPUT_MAX+1);
                error=agent_asr_realtime_transcribe(&asr,&input);
            } else if(!error)error=agent_vocalign_asr.transcribe(&speech,&input,work_buffer.input,AGENT_INPUT_MAX+1);
#else
            if(!error)error=(realtime?&agent_qianwen_asr:&agent_vocalign_asr)->transcribe(&speech,&input,work_buffer.input,AGENT_INPUT_MAX+1);
#endif
        }
        if(!error) {
            voice_event(NULL,"llm",NULL);engine.voice_mode=true;voice_first_text=false;
            if(overlap) {
                engine.answer_begin=voice_answer_begin;
                engine.answer_prepare=voice_answer_prepare;
                engine.answer_write=esp_agent_voice_stream_write;
                engine.answer_end=esp_agent_voice_answer_end;
                engine.answer_ctx=&progress;
                if(progress.ack && progress.ack->spoken) {
                    engine.heard_ack=progress.ack->text;engine.heard_ack_language=progress.ack->language;
                } else if(!deferred && progress.ack && progress.ack->text[0] && !progress.ack->pending) {
                    engine.progress_begin=voice_progress_begin;
                    engine.progress_end=voice_progress_end;engine.progress_ctx=&progress;
                }
#if AGENT_CONTEXTUAL_CACHED_ACK
                else if(progress.cached.active) {
                    engine.progress_begin=voice_cached_progress_begin;
                    engine.progress_end=voice_cached_progress_end;engine.progress_ctx=&progress;
                }
#endif
            }
            engine.before_tools=voice_before_tools;engine.before_tools_ctx=&progress;
            voice_request_id=0;engine.request_trace=voice_request_trace;engine.request_trace_ctx=&engine;
            error=deferred?agent_engine_turn_deferred(&engine,work_buffer.input,true,deferred):
                agent_engine_turn(&engine,work_buffer.input,true);
            engine.voice_mode=false;
            engine.request_trace=NULL;engine.request_trace_ctx=NULL;
            engine.answer_begin=NULL;engine.answer_prepare=NULL;engine.answer_write=NULL;engine.answer_end=NULL;
            engine.answer_ctx=NULL;engine.before_tools=NULL;engine.before_tools_ctx=NULL;
            engine.progress_begin=NULL;engine.progress_end=NULL;engine.progress_ctx=NULL;
            engine.progress_text=engine.progress_language=NULL;
            engine.heard_ack=engine.heard_ack_language=NULL;
            if(!overlap)voice_event(NULL,"llm_done",NULL);
            else if(esp_hi_stream_started_at())voice_event_at("speaker_start",NULL,esp_hi_stream_started_at());
            if(engine.workspace)reply=engine.workspace->reply.text;
        }
    }
    uint64_t until=esp_agent_now()+90000;
    while(!error) {
        agent_audio_state_t audio={0};error=esp_hi_audio_ops.inspect(NULL,&audio);
        if(error || !audio.playing) break;
        if(atomic_load(&kernel->cancelled)) {error=AGENT_ERR_CANCELLED;break;}
        if(esp_agent_now()>until) {error=AGENT_ERR_TIMEOUT;break;}vTaskDelay(pdMS_TO_TICKS(20));
    }
    if(!error && !overlap) {
        const agent_pcm_sink_t sink={.open=voice_pcm_open,.write=voice_pcm_write};
        if(realtime) {
            esp_agent_http_release();voice_event(NULL,"tts",NULL);
            agent_qwen_init(&session,&speech,&esp_agent_speech_ws);
            error=agent_qianwen_tts.speak(&speech,qianwen_settings.voice,reply,&sink);
        } else error=agent_vocalign_tts.speak(&speech,speech_settings.voice,reply,&sink);
        error=esp_hi_stream_finish(error);
        if(esp_hi_stream_started_at())voice_event_at("speaker_start",NULL,esp_hi_stream_started_at());
    }
finished:
    {
#if AGENT_TEXT_PREFETCH
        if(esp_agent_voice_candidate_deferred())error=voice_candidate_resume(NULL,error);
#endif
        free(deferred);deferred=NULL;
        /* Includes errors before engine startup and turns without any tools.
         * Do not rearm microphone/wake while a progress job owns the speaker. */
        /* Cancel a detached native acknowledgement on every failed exit,
         * including failure before the first request or before_tools hook. */
        if(job->fast && !progress.ack)progress.ack=esp_agent_voice_fast_ack();
        error=voice_native_join(&progress,error);
        agent_err_t joined=voice_progress_join(&progress);
        if(!error)error=joined;
        esp_agent_speech_ws_warm_discard();
    }
    esp_hi_voice_release();
    engine.clarify_only=false;
    atomic_store(&voice_error,error);atomic_store(&voice_elapsed,(unsigned)esp_agent_now()-atomic_load(&voice_started));
    atomic_fetch_add(error?&voice_failures:&voice_turns,1);
    voice_event(NULL,error?"error":"done",error?agent_err_name(error):NULL);
    return error;
}
#if AGENT_HANDOFF_PROBE
static agent_err_t voice_handoff_probe(const char *mode)
{
    unsigned scenario=!strcmp(mode,"pass")?1:!strcmp(mode,"revision")?2:
        !strcmp(mode,"cancel")?3:!strcmp(mode,"failure")?4:!strcmp(mode,"warm")?5:
        !strcmp(mode,"headers")?6:!strcmp(mode,"delay")?7:!strcmp(mode,"reserve")?8:
        !strcmp(mode,"flow")?9:0;
    if(!scenario)return AGENT_ERR_ARGUMENT;
    if(atomic_load(&voice_enabled) || !atomic_load(&speech_qianwen) || engine.gateway)
        return AGENT_ERR_BUSY;
    atomic_store(&voice_started,(unsigned)esp_agent_now());
    voice_event(NULL,"handoff_probe_begin",mode);
    agent_err_t error=capture_workspace_prepare();
    if(!error)error=esp_agent_voice_candidate_probe(capture_workspace,ESP_AGENT_CANDIDATE_PREFIX_BYTES,
        &kernel->cancelled,voice_event,"现在灯是什么颜色",scenario==4,scenario!=9);
    strcpy(work_buffer.input,scenario==2?"不用问灯了，请介绍你自己。":"现在灯是什么颜色？请简单回答。");
    turn_job_t job={.text=work_buffer.input,.voice=true,.live=true,.candidate=capture_workspace!=NULL,
        .probe_join=scenario==5,.probe_hold=scenario==7 || scenario==8,.probe_reserve=scenario==8};
    voice_event(NULL,"handoff_probe_final",work_buffer.input);
    esp_agent_http_probe_headers(scenario==6);
    error=voice_run(&job,error);
    esp_agent_http_probe_headers(false);
    voice_event(NULL,"handoff_probe_end",agent_err_name(error));
    return error;
}
#endif
static void voice_status(void)
{
    bool rt=atomic_load(&speech_qianwen);
    bool fast=atomic_load(&voice_fast),frontend=fast && rt && !atomic_load(&route_gateway);
    unsigned candidate_idle=0;
#if AGENT_TEXT_PREFETCH
    /* The fast option uses isolated ASR while its text candidate is enabled. */
    if(esp_agent_voice_prefetch_enabled())frontend=false;
    candidate_idle=esp_agent_voice_candidate_idle_state();
#endif
    char out[864];snprintf(out,sizeof(out),"{\"voice_enabled\":%s,\"configured\":%s,\"stage\":\"%s\",\"turns\":%u,\"failures\":%u,\"elapsed_ms\":%u,\"underruns\":%u,\"error\":\"%s\",\"provider\":\"%s\",\"asr_streaming\":%s,\"tts_streaming_text\":%s,\"pcm_streaming\":true,\"llm_streaming\":true,\"llm_to_tts_overlap\":%s,\"tts_stack\":%u,\"usb_dropped_bytes\":%u,\"asr_model\":\"%s\",\"tts_model\":\"%s\",\"mode\":\"%s\",\"frontend_model\":\"%s\",\"fast_ready\":%s,\"preconnect\":\"%s\",\"candidate_idle\":%u}\r\n",
        atomic_load(&voice_enabled)?"true":"false",voice_configured()?"true":"false",voice_stages[atomic_load(&voice_stage)],
        atomic_load(&voice_turns),atomic_load(&voice_failures),atomic_load(&voice_elapsed),esp_hi_stream_underruns(),agent_err_name(atomic_load(&voice_error)),
        rt?"qianwen":"vocalign",rt?"true":"false",rt && !frontend?"true":"false",rt && !frontend && !atomic_load(&route_gateway)?"true":"false",esp_agent_voice_stream_stack(),atomic_load(&voice_usb_dropped),frontend?AGENT_RT_MODEL:rt?AGENT_CLASSIC_ASR_MODEL:"qwen-asr",frontend?AGENT_RT_MODEL:rt?AGENT_QWEN_TTS_MODEL:"gpt-4o-mini-tts",fast?"fast":"classic",frontend?AGENT_RT_MODEL:"",esp_agent_voice_fast_ready()?"true":"false",atomic_load(&fast_prime_idle)?"idle":"capture",candidate_idle);
    say(out);
}
static agent_err_t voice_mode_set(const char *mode)
{
    if(strcmp(mode,"fast") && strcmp(mode,"classic"))return AGENT_ERR_ARGUMENT;
    agent_err_t error=agent_begin_turn(kernel);if(error)return error;
    if(xSemaphoreTake(work_lock,0)!=pdTRUE) {agent_end_turn(kernel);return AGENT_ERR_BUSY;}
    agent_audio_state_t audio={0};error=esp_hi_audio_ops.inspect(NULL,&audio);
    if(!error && (audio.playing || audio.recording))error=AGENT_ERR_BUSY;
    if(!error)error=esp_agent_workspace_restore();
    if(!error) {
        esp_agent_voice_fast_discard();
        atomic_store(&voice_fast,!strcmp(mode,"fast"));fast_retry_at=0;
    }
    xSemaphoreGive(work_lock);agent_end_turn(kernel);return error;
}
static agent_err_t voice_configure(const char *json)
{
    if(!storage_ready) return AGENT_ERR_STORAGE;
    if(atomic_load(&kernel->busy) || xSemaphoreTake(work_lock,0)!=pdTRUE) return AGENT_ERR_BUSY;
    cJSON *root=agent_json_parse(json,strlen(json));
    const char *key=agent_json_string(root,"api_key"),*voice=agent_json_string(root,"voice");
    const char *provider=agent_json_string(root,"provider");
    bool rt=provider && !strcmp(provider,"qianwen");
    agent_err_t error=AGENT_OK;
    if((provider && !rt && strcmp(provider,"vocalign")) || !key || !voice || !*key || !*voice || strlen(key)>=sizeof(speech_settings.key) || strlen(voice)>=sizeof(speech_settings.voice) ||
       strspn(key,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=strlen(key) ||
       strspn(voice,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=strlen(voice)) error=AGENT_ERR_ARGUMENT;
    else if(!(error=esp_agent_workspace_restore())) {
        esp_agent_voice_fast_discard();
        unsigned char candidate[sizeof(speech_settings)];memset(candidate,0,sizeof(candidate));
        memcpy(candidate,key,strlen(key));memcpy(candidate+sizeof(speech_settings.key),voice,strlen(voice));
        if(nvs_set_blob(nvs,rt?"speech_qwen":"speech_v1",candidate,sizeof(candidate))!=ESP_OK ||
           nvs_set_u8(nvs,"speech_rt",rt)!=ESP_OK || nvs_commit(nvs)!=ESP_OK)error=AGENT_ERR_STORAGE;
        else {
            memcpy(rt?&qianwen_settings:&speech_settings,candidate,sizeof(candidate));
            atomic_store(&speech_qianwen,rt);esp_agent_http_forget();
            esp_agent_speech_ws_expire(true);
            esp_hi_voice_live_configure(rt,voice_capture_start);
        }
        for(volatile unsigned char *p=candidate;p<candidate+sizeof(candidate);++p) *p=0;
    }
    cJSON_Delete(root);xSemaphoreGive(work_lock);return error;
}
static agent_err_t voice_preconnect_set(const char *mode)
{
    if(strcmp(mode,"capture") && strcmp(mode,"idle"))return AGENT_ERR_ARGUMENT;
    if(atomic_load(&voice_enabled))return AGENT_ERR_BUSY;
    agent_err_t error=agent_begin_turn(kernel);if(error)return error;
    if(xSemaphoreTake(work_lock,0)!=pdTRUE) {agent_end_turn(kernel);return AGENT_ERR_BUSY;}
    error=esp_agent_workspace_restore();
    if(!error) {
        esp_agent_voice_fast_discard();
        atomic_store(&fast_prime_idle,!strcmp(mode,"idle"));fast_retry_at=0;
    }
    xSemaphoreGive(work_lock);agent_end_turn(kernel);return error;
}
static agent_err_t voice_option_set(const char *mode,void (*set)(bool))
{
    if(strcmp(mode,"on") && strcmp(mode,"off"))return AGENT_ERR_ARGUMENT;
    if(atomic_load(&voice_enabled))return AGENT_ERR_BUSY;
    agent_err_t error=agent_begin_turn(kernel);if(error)return error;
    if(xSemaphoreTake(work_lock,0)!=pdTRUE) {agent_end_turn(kernel);return AGENT_ERR_BUSY;}
    error=esp_agent_workspace_restore();
    if(!error) {set(!strcmp(mode,"on"));fast_retry_at=0;}
    xSemaphoreGive(work_lock);agent_end_turn(kernel);return error;
}
static void voice_submit(const char *text,bool say_only)
{
    if(say_only && (!*text || strlen(text)>AGENT_SPEECH_TEXT_MAX || !agent_utf8_valid(text,strlen(text)))) {report(AGENT_ERR_LIMIT);return;}
    agent_err_t error=agent_begin_turn(kernel);if(error) {if(say_only) report(error);return;}
    if(atomic_load(&sync_active)) atomic_store(&sync_cancel,true);
    if(say_only) strcpy(work_buffer.input,text);
    turn_job_t job={.text=work_buffer.input,.voice=true,.say=say_only};
    if(xQueueSend(jobs,&job,0)!=pdTRUE) {agent_end_turn(kernel);report(AGENT_ERR_BUSY);}
}
__attribute__((noinline)) static void voice_fast_idle(void)
{
    if(atomic_load(&kernel->busy))return;
    bool wanted=atomic_load(&voice_enabled) && atomic_load(&voice_fast) &&
        atomic_load(&speech_qianwen) && !atomic_load(&route_gateway) && voice_configured() && esp_agent_online();
#if AGENT_TEXT_PREFETCH
    if(voice_candidate_selected()) {
        bool warm=wanted && atomic_load(&fast_prime_idle);
        unsigned state=esp_agent_voice_candidate_idle_state();
        if(state==ESP_AGENT_CANDIDATE_PARKED && wanted && esp_agent_voice_candidate_retained()) {
            if(!warm)return; /* Capture mode keeps only the bounded transport. */
        } else if(state==ESP_AGENT_CANDIDATE_PARKED) {
            (void)esp_agent_workspace_restore();return;
        }
        if(!warm || esp_agent_voice_candidate_idle_state()==ESP_AGENT_CANDIDATE_EXPIRED) {
#if AGENT_REQUEST_SCRATCH_COMPACT
            if(!capture_idle_held())
#endif
                (void)esp_agent_workspace_restore();
            return;
        }
        if(state && state!=ESP_AGENT_CANDIDATE_PARKED)return;
        if(state==ESP_AGENT_CANDIDATE_PARKED || !fast_retry_at) {
            /* One bounded asynchronous attempt per idle period; never hold a
             * turn or pause the wake listener for its TLS handshake. */
            fast_retry_at=UINT64_MAX;
            agent_err_t error=capture_workspace_prepare();
            if(!error)error=esp_agent_voice_candidate_warm(capture_workspace,
                ESP_AGENT_CANDIDATE_PREFIX_BYTES,&kernel->cancelled,voice_event);
            if(error) {(void)esp_agent_workspace_restore();voice_event(NULL,"candidate_warm_error",agent_err_name(error));}
        }
        return;
    }
    if(esp_agent_voice_candidate_idle_state() && esp_agent_workspace_restore())return;
#endif
    if(!wanted) {esp_agent_voice_fast_discard();fast_retry_at=0;return;}
    if(atomic_load(&kernel->busy))return;
    if(esp_agent_voice_fast_ready()) {
        if(esp_agent_voice_fast_poll_idle())fast_retry_at=esp_agent_now()+5000;
        return;
    }
    if(!atomic_load(&fast_prime_idle) || esp_agent_now()<fast_retry_at)return;
    agent_err_t error=agent_begin_turn(kernel);if(error)return;
    /* Reserve the idle turn while TLS allocates; listen becomes ready only
     * after the warm connection exists. No microphone audio is sent here. */
    error=workspace_contiguous();
    if(!error)error=esp_hi_wake_network(true);
    if(!error)error=esp_agent_voice_fast_warm(&engine,work_buffer.input,AGENT_INPUT_MAX+1,voice_event);
    esp_hi_wake_network(false);agent_end_turn(kernel);
    fast_retry_at=esp_agent_now()+(error?10000:0);
}
#endif

#if AGENT_ENABLE_AUDIO
/* Upload-only buffer; ordinary text/voice turns never retain this frame. */
__attribute__((noinline)) static agent_err_t upload_song_job(void)
{
    char output[1024];
    agent_err_t error=agent_tool_invoke(&tool_ops,"device_audio_play_song",engine.buffer,output,sizeof(output));
    memset(engine.buffer,0,AGENT_ARGS_MAX+1);
    if(!error) { say(output); say("\r\n"); }
    return error;
}
#endif

static void network_task(void *arg)
{
    (void)arg;
    turn_job_t job;
    for (;;) {
        if(xQueueReceive(jobs,&job,pdMS_TO_TICKS(250))!=pdTRUE) {
            /* Do not block behind capture: it may enqueue a live ASR job. */
            if(xSemaphoreTake(work_lock,0)!=pdTRUE)continue;
            if(!atomic_load(&kernel->busy)
#if AGENT_TEXT_PREFETCH
               && !esp_agent_voice_candidate_idle_state()
#endif
#if AGENT_TEXT_PREFETCH && AGENT_REQUEST_SCRATCH_COMPACT
               && !capture_idle_held()
#endif
               && esp_agent_workspace_restore()) {
                xSemaphoreGive(work_lock);continue;
            }
            esp_agent_http_expire();
#if AGENT_ENABLE_AUDIO
#if AGENT_TEXT_PREFETCH
            /* An idle candidate may still own/close primary from its worker.
             * Expiry of closed tickets resumes after that owner is joined. */
            if(!esp_agent_voice_candidate_idle_state())
#endif
            esp_agent_speech_ws_expire(false);
            voice_fast_idle();
#endif
            if(atomic_load(&kernel->busy) || !esp_agent_online() || !context.wal.ready || context.mode==AGENT_CONTEXT_LOCAL ||
               !settings.gateway_url[0] || !settings.gateway_token[0] || esp_agent_now()<sync_at) {
                xSemaphoreGive(work_lock); continue;
            }
            atomic_store(&sync_cancel,false); atomic_store(&sync_active,true);
            if(atomic_load(&kernel->busy)) atomic_store(&sync_cancel,true);
            bool more=false;
            agent_err_t error=esp_agent_workspace_restore();
#if AGENT_ENABLE_AUDIO
            esp_agent_voice_fast_discard();
            if(!error)error=esp_hi_wake_network(true);
#endif
            if(!error) error=agent_context_sync(&context,engine.transport,&sync_cancel,&more);
            esp_agent_http_release();
#if AGENT_ENABLE_AUDIO
            esp_hi_wake_network(false);
#endif
            atomic_store(&sync_more,more); atomic_store(&sync_error,error);
            sync_at=esp_agent_now()+(error?5000:more?250:30000);
            refresh_context(); atomic_store(&sync_active,false); xSemaphoreGive(work_lock);
            continue;
        }
        agent_err_t error=AGENT_OK;
#if AGENT_ENABLE_AUDIO
        if(job.voice)esp_agent_voice_heap_begin();
        if(job.live) {
            if(!job.fast)esp_agent_voice_fast_discard();
            /* Endpoint confirmation must not queue behind TLS work on C3. */
            vTaskPrioritySet(NULL,3);
            if(job.fast) {
                atomic_store(&voice_started,(unsigned)esp_agent_now());voice_event(NULL,"capture_begin",NULL);
                error=esp_agent_voice_fast_capture(&engine,work_buffer.input,AGENT_INPUT_MAX+1,voice_event);
            } else error=voice_live_asr(job.candidate);
            vTaskPrioritySet(NULL,4);
        }
#endif
        xSemaphoreTake(work_lock,portMAX_DELAY);
        if(!job.candidate) {
            agent_err_t restored=esp_agent_workspace_restore();if(!error)error=restored;
        }
        engine.gateway=atomic_load(&route_gateway);
#if AGENT_ENABLE_AUDIO
        if(!job.fast)esp_agent_voice_fast_discard();
        if(!error)error=esp_hi_wake_network(true);
        if(job.voice) {
#if AGENT_HANDOFF_PROBE
            if(job.probe && !error)error=voice_handoff_probe(job.text+20);
            else
#endif
            error=voice_run(&job,error);
        }
        else
#endif
        if(!error) {
#if AGENT_ENABLE_AUDIO
            if(job.upload) error=upload_song_job();
            else
#endif
            error=job.control?context_command(job.text):agent_engine_turn(&engine,job.text,job.stream);
        }
#if AGENT_ENABLE_AUDIO
        if(job.voice) esp_hi_voice_release();
        if(job.live)atomic_store(&voice_handoff,false);
#if AGENT_TEXT_PREFETCH && AGENT_REQUEST_SCRATCH_COMPACT
        bool compact_idle=job.voice && capture_idle_selected();
        if(compact_idle) {
            /* Release completed response storage before the audio task can
             * allocate the next wake model. Metadata refresh needs scratch. */
            esp_agent_voice_heap_mark(VOICE_HEAP_HTTP_RELEASE_BEGIN);
            esp_agent_http_release();
            esp_agent_voice_heap_mark(VOICE_HEAP_HTTP_RELEASE_END);
            refresh_context();
            capture_workspace_idle();
        }
#endif
        esp_agent_voice_heap_mark(VOICE_HEAP_REARM_BEGIN);
        esp_hi_wake_network(false);
        esp_agent_voice_heap_mark(VOICE_HEAP_REARM_END);
        if(job.voice && atomic_load(&voice_enabled)) atomic_store(&voice_rearm,true);
        fast_retry_at=0;
#endif
#if AGENT_TEXT_PREFETCH && AGENT_REQUEST_SCRATCH_COMPACT
        if(!compact_idle)
#endif
        {
            esp_agent_voice_heap_mark(VOICE_HEAP_HTTP_RELEASE_BEGIN);
            esp_agent_http_release();
            esp_agent_voice_heap_mark(VOICE_HEAP_HTTP_RELEASE_END);
            refresh_context();
        }
        esp_agent_voice_heap_mark(VOICE_HEAP_CLEANUP_END);
#if AGENT_ENABLE_AUDIO
        if(job.voice)esp_agent_voice_heap_end(voice_event);
#endif
        xSemaphoreGive(work_lock);
        agent_end_turn(kernel);
#if AGENT_ENABLE_AUDIO
        if(job.voice) {
            char message[96];int n=snprintf(message,sizeof(message),"\r\n@%s%s%s\r\n",
                error?"error":"done",error?" ":"",error?agent_err_name(error):"");
            if(n>0 && (size_t)n<sizeof(message))voice_write(message,(size_t)n);
        } else
#endif
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
#if AGENT_HANDOFF_PROBE
    if(control && !strncmp(text,"voice handoff-probe ",20)) {job.probe=true;job.voice=true;}
#endif
    if (xQueueSend(jobs, &job, 0) != pdTRUE) { agent_end_turn(kernel); report(busy_result(BUSY_SUBMIT_QUEUE,AGENT_ERR_BUSY)); }
}

#ifdef AGENT_KWS_C11
static bool kws_reserved;
static uint64_t kws_deadline;
static void kws_release(void)
{
    kws_usb_end(); kws_reserved=false;
    esp_hi_wake_network(false);
    xSemaphoreGive(work_lock); agent_end_turn(kernel);
}
static bool kws_command(char *line)
{
    if(!strcmp(line,"agent kws profile")) {
        char output[1024];
        if(esp_hi_kws_profile(output,sizeof(output))) { say(output); say("\r\n"); }
        else report(AGENT_ERR_LIMIT);
        return true;
    }
    if(!strcmp(line,"agent kws reset-profile")) {
        agent_audio_state_t audio={0};
        agent_err_t error=esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx,&audio);
        if(audio.listening || esp_hi_speech_chunk()) error=AGENT_ERR_BUSY;
        if(!error) esp_hi_kws_profile_reset();
        report(error); return true;
    }
    if(!strcmp(line,"agent kws begin")) {
        agent_audio_state_t audio={0};
        agent_err_t error=esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx,&audio);
        if(kws_reserved || audio.listening || audio.recording || audio.playing ||
           atomic_load(&sync_active) || atomic_load(&esp_hi_control.active)) error=AGENT_ERR_BUSY;
        if(!error) error=agent_begin_turn(kernel);
        if(error) { report(error); return true; }
        if(xSemaphoreTake(work_lock,0)!=pdTRUE) { agent_end_turn(kernel); report(AGENT_ERR_BUSY); return true; }
        error=esp_hi_wake_network(true);
        if(!error && !kws_usb_begin()) error=AGENT_ERR_MEMORY;
        if(error) { esp_hi_wake_network(false); xSemaphoreGive(work_lock); agent_end_turn(kernel); report(error); return true; }
        kws_reserved=true; kws_deadline=esp_agent_now()+30000;
        report(AGENT_OK); return true;
    }
    if(!strcmp(line,"agent kws end")) {
        if(kws_reserved) kws_release();
        report(AGENT_OK); return true;
    }
    if(!strcmp(line,"agent kws pcen-check")) {
        const char *result=kws_reserved?kws_usb_pcen_check():NULL;
        if(!result) report(AGENT_ERR_ARGUMENT);
        else { kws_deadline=esp_agent_now()+30000; say(result); say("\r\n"); }
        return true;
    }
    if(!strncmp(line,"agent kws frame ",16)) {
        unsigned sequence,crc; int consumed=0;
        if(!kws_reserved || sscanf(line+16,"%u %x %n",&sequence,&crc,&consumed)!=2 || !consumed) {
            report(AGENT_ERR_ARGUMENT); return true;
        }
        const char *result=kws_usb_frame(sequence,crc,line+16+consumed);
        if(!result) report(AGENT_ERR_ARGUMENT);
        else { kws_deadline=esp_agent_now()+30000; say(result); say("\r\n"); }
        return true;
    }
    if(!kws_reserved) return false;
    if(!strcmp(line,"agent cancel")) { kws_release(); return false; }
    static const char *const allowed[]={"agent status","agent wake status","agent audio status","agent context stats"};
    for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);++i) if(!strcmp(line,allowed[i])) return false;
    report(AGENT_ERR_BUSY); return true;
}
#endif
static void command(char *line)
{
#if AGENT_ENABLE_AUDIO
    if(!strncmp(line,"agent voice ",12)) {
        const char *arg=line+12;
        if(!strcmp(arg,"status")) voice_status();
#ifdef AGENT_VOICE_HEAP_DIAGNOSTICS
        else if(!strcmp(arg,"heap begin") || !strcmp(arg,"heap end")) {
            if(atomic_load(&voice_enabled) || atomic_load(&kernel->busy) ||
               xSemaphoreTake(work_lock,0)!=pdTRUE)report(AGENT_ERR_BUSY);
            else {
                bool ok=!strcmp(arg,"heap begin")?esp_agent_voice_heap_session_begin():
                    esp_agent_voice_heap_session_end(voice_heap_event);
                xSemaphoreGive(work_lock);report(ok?AGENT_OK:AGENT_ERR_CONFIG);
            }
        }
#endif
#if AGENT_HANDOFF_PROBE
        else if(!strncmp(arg,"handoff-probe ",14)) {
            if(atomic_load(&voice_enabled))report(AGENT_ERR_BUSY);
            else submit(line+6,false,true);
        }
#endif
#if AGENT_WS_OWNER_PROBE
        else if(!strcmp(arg,"connections-probe")) {
            if(atomic_load(&voice_enabled))report(AGENT_ERR_BUSY);
            else submit("voice connections-probe",false,true);
        }
#endif
        else if(!strncmp(arg,"mode ",5)) report(voice_mode_set(arg+5));
        else if(!strncmp(arg,"preconnect ",11)) report(voice_preconnect_set(arg+11));
        else if(!strcmp(arg,"prefetch status")) say(esp_agent_voice_prefetch_enabled()?"{\"prefetch\":true}\r\n":"{\"prefetch\":false}\r\n");
        else if(!strncmp(arg,"prefetch ",9)) report(voice_option_set(arg+9,esp_agent_voice_prefetch_set));
        else if(!strcmp(arg,"reuse status")) say(esp_agent_voice_reuse_enabled()?"{\"reuse\":true}\r\n":"{\"reuse\":false}\r\n");
        else if(!strncmp(arg,"reuse ",6)) report(voice_option_set(arg+6,esp_agent_voice_reuse_set));
#ifdef AGENT_BACKGROUND_VERIFY
#if AGENT_ENDPOINT_TRACE
#if AGENT_CAPTURE_PROBE
        else if(!strcmp(arg,"noise-clock")) {
            char clock[64];
            snprintf(clock,sizeof(clock),"{\"noise_clock_ms\":%llu}\r\n",(unsigned long long)esp_agent_now());say(clock);
        }
#endif
        else if(!strncmp(arg,"endpoint-trace ",15)) {
            const char *mode=arg+15;
            agent_err_t error=strcmp(mode,"on") && strcmp(mode,"off")?AGENT_ERR_ARGUMENT:
                atomic_load(&voice_enabled) || atomic_load(&kernel->busy)?AGENT_ERR_BUSY:AGENT_OK;
            if(!error)atomic_store(&endpoint_trace_enabled,!strcmp(mode,"on"));
            report(error);
        }
#endif
#endif
        else if(!strncmp(arg,"capture-output ",15)) {
            const char *mode=arg+15;
            agent_err_t error=strcmp(mode,"hold") && strcmp(mode,"off")?AGENT_ERR_ARGUMENT:
                atomic_load(&voice_enabled) || atomic_load(&kernel->busy)?AGENT_ERR_BUSY:
                esp_hi_voice_capture_output(!strcmp(mode,"hold"));
            report(error);
        }
        else if(!strncmp(arg,"configure ",10)) report(voice_configure(arg+10));
        else if(!strncmp(arg,"say ",4)) voice_submit(arg+4,true);
        else if(!strcmp(arg,"run")) voice_submit("",false);
        else if(!strcmp(arg,"on") || !strcmp(arg,"off")) {
            bool enable=!strcmp(arg,"on");agent_err_t error=AGENT_OK;
            if(enable && !voice_configured()) error=AGENT_ERR_CONFIG;
            else if(enable && atomic_load(&kernel->busy)) error=AGENT_ERR_BUSY;
            else {
                if(!enable) {
                    esp_agent_voice_heap_mark(VOICE_HEAP_VOICE_OFF_BEGIN);
                    atomic_store(&voice_enabled,false);atomic_store(&voice_rearm,false);agent_cancel(kernel);
#if AGENT_TEXT_PREFETCH
                    esp_agent_voice_candidate_cancel();
#endif
                }
                error=esp_hi_voice_enable(enable);
                if(!error && (nvs_set_u8(nvs,"voice_on",enable)!=ESP_OK || nvs_commit(nvs)!=ESP_OK)) error=AGENT_ERR_STORAGE;
                if(!error) {atomic_store(&voice_enabled,enable);atomic_store(&voice_rearm,false);atomic_store(&voice_stage,enable?1:0);if(enable)fast_retry_at=0;}
                if(!enable)esp_agent_voice_heap_mark(VOICE_HEAP_VOICE_OFF_END);
            }
            report(error);
        } else report(AGENT_ERR_ARGUMENT);
        return;
    }
#endif
#ifdef AGENT_KWS_C11
    if(kws_command(line)) return;
#endif
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
    } else if(!strncmp(line,"agent wifi power ",17)) {
        /* Volatile diagnostic limit in SDK quarter-dBm units. No NVS write;
         * changing RF during acquisition would invalidate the comparison. */
        unsigned power;char extra;agent_err_t error=AGENT_OK;
        if(sscanf(line+17,"%u %c",&power,&extra)!=1 || power<8 || power>84)error=AGENT_ERR_ARGUMENT;
        else if(atomic_load(&kernel->busy) || atomic_load(&sync_active) || atomic_load(&esp_hi_control.active))error=AGENT_ERR_BUSY;
#if AGENT_ENABLE_AUDIO
        else if(atomic_load(&voice_enabled))error=AGENT_ERR_BUSY;
        if(!error) {
            agent_audio_state_t audio={0};error=esp_hi_audio_ops.inspect(esp_hi_audio_ops.ctx,&audio);
            if(!error && (audio.listening || audio.recording || audio.playing || audio.mic_enabled))error=AGENT_ERR_BUSY;
        }
#endif
        if(!error && esp_wifi_set_max_tx_power((int8_t)power)!=ESP_OK)error=AGENT_ERR_CONFIG;
        report(error);
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
#if AGENT_TEXT_PREFETCH
        esp_agent_voice_candidate_cancel();
#endif
        if(atomic_load(&voice_enabled)) {
            /* Clear a just-captured pending clip as well as active audio. The
             * main loop rearms only after cancellation has released ownership. */
            esp_hi_voice_enable(false);atomic_store(&voice_rearm,true);
        } else esp_hi_devices.audio.stop(&esp_hi_devices);
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
#if AGENT_ENDPOINT_TRACE
        say("Endpoint diagnostic: agent status | agent voice on/off/status/endpoint-trace on|off | agent audio clip read SAMPLE COUNT\r\n");
#elif AGENT_TEXT_PREFETCH
        say("agent: README.md\r\n"
            "status|hardware|reboot|cancel|chat [--no-stream]\r\n"
            "wifi set/status/pause/resume|light get/set|route\r\n"
            "display status/off/set|gpio get/set\r\n"
            "context stats/init/compact/mode/remember/forget/memory/sync/budget/search/summary\r\n"
            "control capabilities/validate/run/status/cancel\r\n"
            "audio status/play/stop/volume/capture/replay/song/clip read\r\n"
            "mic on/off/status|wake on/off/status/threshold/gain\r\n"
            "voice on/off/status/mode/say/run/configure/prefetch/reuse/capture-output\r\n");
#else
        say("agent hardware | agent display status/off/set {\"mode\":\"clock\",\"utc_offset_minutes\":480}\r\n"
            "agent status | wifi set JSON | wifi status/pause/resume | chat [--no-stream] TEXT | cancel | light get/set R G B | context stats/init/compact/mode LOCAL|CLOUD|HYBRID/remember KEY VALUE/forget KEY/memory/sync | route DIRECT|GATEWAY | reboot\r\n"
            "full context: tools/archive_context.py; USB archive verified LOCAL prefix\r\n"
#if AGENT_ENABLE_AUDIO
            "agent audio status/play JSON/stop/volume 0..100/capture MS/replay | song JSON/get/begin BYTES CRC32/chunk OFFSET HEX/commit/abort | agent mic on/off/status\r\n"
            "agent wake on/off/status/threshold 500..950/gain 1..4 (wake off) | agent audio clip read SAMPLE COUNT (<=256, wake off)\r\n"
            "agent voice on/off/status | mode fast|classic (volatile) | capture-output hold|off (voice off, idle) | say TEXT | run (clip) | configure {provider:qianwen|vocalign,api_key,voice}\r\n"
            "agent voice prefetch|reuse on|off (test, voice off, volatile)\r\n"
#endif
            "agent control capabilities/validate JSON/run JSON/status/cancel | agent context budget 64|128|200/search JSON/summary/summary set JSON\r\n"
            "agent gpio get {\"pin\":10} | set {\"pin\":10,\"mode\":\"output\",\"value\":1} | input/pwm(hz,duty)\r\n");
#endif
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
#ifdef AGENT_MIC_EXACT_CLOCK
    else if(!strcmp(line,"agent mic clock")) {
        char output[256]; agent_err_t error=esp_agent_adc_clock_status(output,sizeof(output));
        if(!error) { say(output); say("\r\n"); } else report(error);
    }
#endif
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
static const agent_llm_ops_t llm_ops={.build=agent_deepseek_request,.parse=agent_llm_parse,.compose=agent_deepseek_compose,.compose_final=agent_deepseek_compose_final};
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
    /* USB control must remain schedulable while TLS and VAD share the CPU.
     * Audio DMA keeps its existing higher priority; this loop blocks on USB. */
    vTaskPrioritySet(NULL,5);
    output_lock = xSemaphoreCreateMutex();
    context_cache_lock=xSemaphoreCreateMutex();
    work_lock=xSemaphoreCreateMutex();
    usb_serial_jtag_driver_config_t usb = { .tx_buffer_size = USB_TX_BYTES, .rx_buffer_size = 1024 };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    engine.context=&context;
    agent_err_t workspace_error=esp_agent_workspace_restore();
    if(workspace_error) {report(workspace_error);return;}
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
        _Static_assert(AGENT_ENGINE_BUFFER_SIZE >= sizeof(settings), "Boot settings must fit scratch");
        if (nvs_get_blob(nvs, "settings_v1", engine.buffer, &size) == ESP_OK && size == sizeof(settings))
            memcpy(&settings, engine.buffer, sizeof(settings));
        memset(engine.buffer, 0, sizeof(settings));
#if AGENT_ENABLE_AUDIO
        size=sizeof(speech_settings);
        if(nvs_get_blob(nvs,"speech_v1",engine.buffer,&size)==ESP_OK && size==sizeof(speech_settings))
            memcpy(&speech_settings,engine.buffer,sizeof(speech_settings));
        memset(engine.buffer,0,sizeof(speech_settings));
        size=sizeof(qianwen_settings);
        if(nvs_get_blob(nvs,"speech_qwen",engine.buffer,&size)==ESP_OK && size==sizeof(qianwen_settings))
            memcpy(&qianwen_settings,engine.buffer,sizeof(qianwen_settings));
        memset(engine.buffer,0,sizeof(qianwen_settings));
        uint8_t realtime=0;nvs_get_u8(nvs,"speech_rt",&realtime);atomic_store(&speech_qianwen,realtime==1);
        uint8_t voice_on=0;nvs_get_u8(nvs,"voice_on",&voice_on);
        atomic_store(&voice_enabled,voice_on && voice_configured());
#endif
    }
    context.device=settings.device_id; context.user=settings.user_id; context.session=settings.session_id;
    context.scratch=engine.buffer; context.capacity=AGENT_ENGINE_BUFFER_SIZE;
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
    static const esp_hi_confirmation_memory_t audio_memory={audio_scratch_acquire,audio_scratch_release,NULL,
#if AGENT_ENDPOINT_TRACE
        audio_scratch_observe
#else
        NULL
#endif
    };
    esp_hi_confirmation_memory(&audio_memory);
#endif
    report(esp_hi_audio_init());
#endif
    agent_err_t control_error=esp_hi_control_init();
    if(control_error) { report(control_error); return; }
#if AGENT_CLOCK_BOOT
    const agent_display_config_t boot_clock={.mode=AGENT_DISPLAY_CLOCK,
        .utc_offset=480,.foreground=65535,.background=0};
    report(esp_hi_display_ops.set(esp_hi_display_ops.ctx,&boot_clock));
#endif
    jobs = xQueueCreate(1, sizeof(turn_job_t));
    /* 16-KiB stack retained 3596 bytes in measured long-context TLS/tool runs.
     * Recover 1 KiB; reverify the remaining margin with real requests. */
    if (!jobs || xTaskCreate(network_task, "agent_net", 9216, NULL, 4, &network_handle) != pdPASS) {
        report(AGENT_ERR_MEMORY); return;
    }
    say("ESP-HI ready: agent help\r\n");
#if AGENT_ENABLE_AUDIO
    esp_hi_voice_live_configure(atomic_load(&speech_qianwen),voice_capture_start);
    if(atomic_load(&voice_enabled)) {report(esp_hi_voice_enable(true));atomic_store(&voice_stage,1);}
#endif
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
        if(atomic_load(&voice_enabled) && atomic_load(&voice_rearm) && !atomic_load(&kernel->busy)) {
            agent_err_t error=esp_hi_voice_enable(true);
            if(error!=AGENT_ERR_BUSY) {
                atomic_store(&voice_rearm,false);
                if(error) voice_event(NULL,"error",agent_err_name(error));
                else atomic_store(&voice_stage,1);
            }
        }
        if(atomic_load(&voice_enabled) && esp_hi_voice_pending() && !atomic_load(&kernel->busy)) voice_submit("",false);
#endif
#ifdef AGENT_KEYWORD_PCM
        pcm_poll();
#endif
#ifdef AGENT_KWS_C11
        if(kws_reserved && esp_agent_now()>=kws_deadline) { kws_release(); report(AGENT_ERR_TIMEOUT); }
#endif
    }
}
