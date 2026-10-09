#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"

/* One worker-owned slot. Closed fixed-origin handles keep TLS tickets between
 * jobs; provider changes, errors and expiry destroy the handle and its state. */
static esp_http_client_handle_t reusable;
static agent_http_target_t reusable_target;
static uint64_t reusable_expiry;
static bool preconnected;
#if AGENT_HANDOFF_PROBE
static bool probe_headers;
static atomic_int probe_socket=-1;
int esp_agent_http_probe_socket(void) {return atomic_load_explicit(&probe_socket,memory_order_acquire);}
void esp_agent_http_probe_headers(bool enabled) {probe_headers=enabled;}
#endif
void esp_agent_http_expire(void)
{
    if(reusable && reusable_expiry && esp_agent_now()>=reusable_expiry) {
        esp_http_client_cleanup(reusable);reusable=NULL;reusable_expiry=0;preconnected=false;
    }
}
void esp_agent_http_release(void)
{
    esp_agent_http_expire();
    if(reusable) {
        if(reusable_expiry)esp_http_client_close(reusable);
        else {esp_http_client_cleanup(reusable);reusable=NULL;}
    }
    preconnected=false;
}
void esp_agent_http_forget(void)
{
    reusable_expiry=0;esp_agent_http_release();
}

static bool cancelled(const agent_http_request_t *request)
{
    return request->cancelled && atomic_load(request->cancelled);
}

static void trace(const agent_http_request_t *request,const char *phase)
{ if(request->trace) request->trace(request->trace_ctx,phase); }

static agent_err_t connection_error(esp_http_client_handle_t client, esp_err_t error)
{
    int tls_code = 0, flags = 0;
    esp_err_t cause = esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &flags);
    if (cause == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) return AGENT_ERR_DNS;
    if (cause == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT || cause == ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT || tls_code == ESP_TLS_ERR_SSL_TIMEOUT)
        return AGENT_ERR_TIMEOUT;
    if (tls_code || flags) return AGENT_ERR_TLS;
    if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_HTTP_EAGAIN) return AGENT_ERR_TIMEOUT;
    return AGENT_ERR_NETWORK;
}

#if AGENT_TEXT_PREFETCH
/* This connect-only entry point is compiled beside the hash-pinned IDF
 * implementation. It preserves is_async, sends no HTTP bytes, and leaves
 * the normal open/write/read API and certificate checks in charge afterward. */
extern esp_err_t esp_agent_http_client_connect_step(esp_http_client_handle_t);
agent_err_t esp_agent_http_warm(const atomic_bool *stop)
{
    if(stop && atomic_load(stop))return AGENT_ERR_CANCELLED;
    if(!esp_agent_online())return AGENT_ERR_OFFLINE;
    if(time(NULL)<1735689600)return AGENT_ERR_CONFIG;
    esp_agent_http_expire();
    if(reusable && reusable_target!=AGENT_HTTP_DEEPSEEK)return AGENT_ERR_BUSY;
    if(preconnected)return AGENT_OK;
    char base[224],auth[224];const char *ca=NULL;
    agent_err_t result=esp_agent_http_auth(AGENT_HTTP_DEEPSEEK,base,sizeof(base),auth,sizeof(auth),&ca);
    for(volatile char *p=auth;p<auth+sizeof(auth);++p)*p=0;
    if(result)return result;
    if(strncmp(base,"https://",8) || strchr(base,'\r') || strchr(base,'\n'))return AGENT_ERR_TLS;
    if(!reusable) {
        esp_http_client_config_t config={.url=base,.timeout_ms=1000,.buffer_size=1024,.buffer_size_tx=1024,
            .disable_auto_redirect=true,.keep_alive_enable=true,
            .crt_bundle_attach=ca?NULL:esp_crt_bundle_attach,.cert_pem=ca,
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
            .save_client_session=true,
#endif
        };
        reusable=esp_http_client_init(&config);
        if(!reusable)return AGENT_ERR_MEMORY;
        reusable_target=AGENT_HTTP_DEEPSEEK;
    }
    esp_http_client_set_timeout_ms(reusable,1000);
    uint64_t until=esp_agent_now()+1000;
    for(;;) {
        if(stop && atomic_load(stop)) {result=AGENT_ERR_CANCELLED;break;}
        if(!esp_agent_online()) {result=AGENT_ERR_OFFLINE;break;}
        if(esp_agent_now()>=until) {result=AGENT_ERR_TIMEOUT;break;}
        esp_err_t error=esp_agent_http_client_connect_step(reusable);
        if(error==ESP_OK) {
            if(stop && atomic_load(stop)) {result=AGENT_ERR_CANCELLED;break;}
            preconnected=true;reusable_expiry=esp_agent_now()+8000;return AGENT_OK;
        }
        if(error!=ESP_ERR_HTTP_CONNECTING && error!=ESP_ERR_HTTP_EAGAIN) {
            result=connection_error(reusable,error);break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    esp_agent_http_forget();return result;
}
#endif

typedef struct { esp_http_client_handle_t client; const agent_http_request_t *request; uint64_t deadline; size_t sent; } body_writer_t;
static agent_err_t write_body(void *ctx,const char *data,size_t length)
{
    body_writer_t *w=ctx;
    while(length) {
        if(cancelled(w->request)) return AGENT_ERR_CANCELLED;
        if(esp_agent_now()>=w->deadline) return AGENT_ERR_TIMEOUT;
        int count=esp_http_client_write(w->client,data,(int)length);
        if(count<=0) return AGENT_ERR_NETWORK;
        data+=count; length-=(size_t)count; w->sent+=(size_t)count;
    }
    return AGENT_OK;
}

agent_err_t esp_agent_http(void *ctx, const agent_http_request_t *request, agent_http_feed_fn feed, void *feed_ctx)
{
    (void)ctx;
    if (!esp_agent_online()) return AGENT_ERR_OFFLINE;
    if (cancelled(request)) return AGENT_ERR_CANCELLED;
    if (time(NULL) < 1735689600) {
        static const char waiting[]="\r\n@wait clock 正在联网校准时间，请稍候。\r\n";
        esp_agent_write(waiting,sizeof(waiting)-1);
        uint64_t until=esp_agent_now()+15000;
        while(time(NULL)<1735689600) {
            if(cancelled(request)) return AGENT_ERR_CANCELLED;
            if(!esp_agent_online()) return AGENT_ERR_OFFLINE;
            if(esp_agent_now()>=until) {ESP_LOGW("agent_http","time synchronization timed out before TLS");return AGENT_ERR_TIMEOUT;}
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    if (request->length > AGENT_HTTP_REQUEST_MAX) return AGENT_ERR_LIMIT;
    char base[224], joined_url[512], auth[224]={0},bytes[1024];
    const bool resumable=request->target==AGENT_HTTP_DEEPSEEK || request->target==AGENT_HTTP_VOCALIGN;
    esp_http_client_handle_t client=NULL;
    uint64_t started=esp_agent_now(); const char *phase="config";
    body_writer_t writer={0}; size_t received=0; int status=0;
    const char *ca = NULL;
    agent_err_t result = esp_agent_http_auth(request->target, base, sizeof(base), auth, sizeof(auth), &ca);
    if (result) goto done;
    /* Public signed URLs are already complete and remain valid for this call.
     * Borrow them instead of adding a 2-KiB copy to the worker's nested stack. */
    const char *url=request->path;
    if(!url || strlen(url)>=2048) { result=AGENT_ERR_LIMIT;goto done; }
    if(base[0]) {
        if(url[0]!='/') {result=AGENT_ERR_PROTOCOL;goto done;}
        int n = snprintf(joined_url, sizeof(joined_url), "%s%s", base, url);
        if (n < 0 || (size_t)n >= sizeof(joined_url)) { result=AGENT_ERR_LIMIT; goto done; }
        url=joined_url;
    }
    if(strncmp(url,"https://",8) || strchr(url,'\r') || strchr(url,'\n')) { result=AGENT_ERR_TLS;goto done; }
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 15000, .buffer_size = 1024, .buffer_size_tx = 1024,
        .disable_auto_redirect = true, .keep_alive_enable = true,
        .crt_bundle_attach = ca ? NULL : esp_crt_bundle_attach, .cert_pem = ca,
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
        .save_client_session = resumable,
#endif
    };
    esp_agent_http_expire();
    if(reusable && (reusable_target!=request->target || request->target==AGENT_HTTP_PUBLIC))
        esp_agent_http_forget();
    if(reusable) {
        if(preconnected)trace(request,"preconnected");
        if(reusable_expiry)trace(request,"session_cached");
        client=reusable;reusable=NULL;reusable_expiry=0;preconnected=false;
    }
    if(client) {
        if(esp_http_client_set_url(client,url)!=ESP_OK) { result=AGENT_ERR_MEMORY;goto done; }
        esp_http_client_set_timeout_ms(client,15000);
    } else client = esp_http_client_init(&config);
    if (!client) { result=AGENT_ERR_MEMORY; goto done; }
    esp_http_client_set_method(client, !strcmp(request->method, "GET") ? HTTP_METHOD_GET : HTTP_METHOD_POST);
    /* Signed object GETs include Content-Type in their signature. Do not add
     * a JSON content type to a bodyless download. */
    const char *content_type=request->content_type;
    if(!content_type && strcmp(request->method,"GET")) content_type="application/json";
    if(!content_type) esp_http_client_delete_header(client,"Content-Type");
    if ((auth[0] && esp_http_client_set_header(client, "Authorization", auth) != ESP_OK) ||
        (content_type && esp_http_client_set_header(client, "Content-Type", content_type) != ESP_OK)) {
        result = AGENT_ERR_MEMORY; goto done;
    }
    phase="connect";
    trace(request,"connect_begin");
    esp_err_t error = esp_http_client_open(client, (int)request->length);
    if (error != ESP_OK) { result = cancelled(request)?AGENT_ERR_CANCELLED:connection_error(client, error); goto done; }
    trace(request,"connected");
    int socket=esp_http_client_get_socket(client),no_delay=1;
    if(socket>=0) setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay));
    phase="send"; writer=(body_writer_t){.client=client,.request=request,.deadline=esp_agent_now()+60000};
    result=agent_http_write_buffered(request,write_body,&writer,bytes,sizeof(bytes));
    if(result) goto done;
    trace(request,"sent");
    int64_t length=-ESP_ERR_HTTP_EAGAIN;
#if AGENT_HANDOFF_PROBE
    if(probe_headers && request->on_sent) {
        /* Diagnostic only: service HTTP without exposing any response bytes
         * to the engine while its arena is still held by the candidate. SDK
         * parser/body buffers remain owned by this same client throughout. */
        probe_headers=false;trace(request,"handoff_headers_begin");
        esp_http_client_set_timeout_ms(client,100);
        uint64_t until=esp_agent_now()+500;
        do {
            if(cancelled(request)) {result=AGENT_ERR_CANCELLED;goto done;}
            length=esp_http_client_fetch_headers(client);
        } while(length==-ESP_ERR_HTTP_EAGAIN && esp_agent_now()<until);
        if(length<0 && length!=-ESP_ERR_HTTP_EAGAIN) {result=AGENT_ERR_NETWORK;goto done;}
        trace(request,length>=0?"handoff_headers_ready":"handoff_headers_pending");
    }
#endif
    if(request->on_sent) {
#if AGENT_HANDOFF_PROBE
        atomic_store_explicit(&probe_socket,socket,memory_order_release);
#endif
        result=request->on_sent(request->sent_ctx);
#if AGENT_HANDOFF_PROBE
        atomic_store_explicit(&probe_socket,-1,memory_order_release);
#endif
        if(result)goto done;
    }
    phase="headers";
    /* Read timeouts are short so cancellation never waits for a long response. */
    esp_http_client_set_timeout_ms(client, 1000);
    uint64_t deadline = esp_agent_now() + 120000;
    do {
        if (cancelled(request)) { result = AGENT_ERR_CANCELLED; goto done; }
        if (esp_agent_now() >= deadline) { result = AGENT_ERR_TIMEOUT; goto done; }
        if(length>=0)break;
        length = esp_http_client_fetch_headers(client);
    } while (length == -ESP_ERR_HTTP_EAGAIN);
    if (length < 0) { result = AGENT_ERR_NETWORK; goto done; }
    trace(request,"headers");
    status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        result = status == 401 ? AGENT_ERR_AUTH : status == 403 ? AGENT_ERR_FORBIDDEN :
                 status == 429 ? AGENT_ERR_RATE : status >= 500 ? AGENT_ERR_SERVER : AGENT_ERR_PROTOCOL;
        goto done;
    }
    phase="receive";
    for (;;) {
        if (cancelled(request)) { result = AGENT_ERR_CANCELLED; break; }
        if (esp_agent_now() >= deadline) { result = AGENT_ERR_TIMEOUT; break; }
        int count = esp_http_client_read(client, bytes, sizeof(bytes));
        if (count > 0) {
            if(!received) trace(request,"first_byte");
            received+=(size_t)count;
            result = feed(feed_ctx, bytes, (size_t)count);
            if (result) break;
        } else if (count == -ESP_ERR_HTTP_EAGAIN) continue;
        else if (!count && esp_http_client_is_complete_data_received(client)) { result = AGENT_OK; break; }
        else { result = AGENT_ERR_PROTOCOL; break; }
    }
done:
    if(result) ESP_LOGW("agent_http","%s phase=%s ms=%u sent=%u/%u received=%u status=%d",
        agent_err_name(result),phase,(unsigned)(esp_agent_now()-started),(unsigned)writer.sent,
        (unsigned)request->length,(unsigned)received,status);
    if(client) {
        bool persistent=esp_http_client_is_persistent_connection(client);
        if(!result && request->target!=AGENT_HTTP_PUBLIC && esp_http_client_is_complete_data_received(client) &&
           (resumable || (request->reuse_connection && persistent))) {
            reusable=client;reusable_target=request->target;
            reusable_expiry=resumable?esp_agent_now()+120000:0;
            if(!request->reuse_connection || !persistent)esp_http_client_close(client);
        } else esp_http_client_cleanup(client);
    }
    for (volatile char *p = auth; p < auth + sizeof(auth); ++p) *p = 0;
    return result;
}
