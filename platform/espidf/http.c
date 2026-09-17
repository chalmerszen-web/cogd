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

static bool cancelled(const agent_http_request_t *request)
{
    return request->cancelled && atomic_load(request->cancelled);
}

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
    char base[224], url[768], auth[224]={0};
    esp_http_client_handle_t client=NULL;
    uint64_t started=esp_agent_now(); const char *phase="config";
    body_writer_t writer={0}; size_t received=0; int status=0;
    const char *ca = NULL;
    agent_err_t result = esp_agent_http_auth(request->target, base, sizeof(base), auth, sizeof(auth), &ca);
    if (result) goto done;
    int n = snprintf(url, sizeof(url), "%s%s", base, request->path);
    if (n < 0 || (size_t)n >= sizeof(url)) { result=AGENT_ERR_LIMIT; goto done; }
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 15000, .buffer_size = 1024, .buffer_size_tx = 1024,
        .disable_auto_redirect = true, .keep_alive_enable = false,
        .crt_bundle_attach = ca ? NULL : esp_crt_bundle_attach, .cert_pem = ca,
    };
    client = esp_http_client_init(&config);
    if (!client) { result=AGENT_ERR_MEMORY; goto done; }
    esp_http_client_set_method(client, !strcmp(request->method, "GET") ? HTTP_METHOD_GET : HTTP_METHOD_POST);
    if (esp_http_client_set_header(client, "Authorization", auth) != ESP_OK ||
        esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK) {
        result = AGENT_ERR_MEMORY; goto done;
    }
    phase="connect";
    esp_err_t error = esp_http_client_open(client, (int)request->length);
    if (error != ESP_OK) { result = cancelled(request)?AGENT_ERR_CANCELLED:connection_error(client, error); goto done; }
    phase="send"; writer=(body_writer_t){.client=client,.request=request,.deadline=esp_agent_now()+60000};
    result=agent_http_write_body(request,write_body,&writer);
    if(result) goto done;
    phase="headers";
    /* Read timeouts are short so cancellation never waits for a long response. */
    esp_http_client_set_timeout_ms(client, 1000);
    uint64_t deadline = esp_agent_now() + 60000;
    int64_t length;
    do {
        if (cancelled(request)) { result = AGENT_ERR_CANCELLED; goto done; }
        if (esp_agent_now() >= deadline) { result = AGENT_ERR_TIMEOUT; goto done; }
        length = esp_http_client_fetch_headers(client);
    } while (length == -ESP_ERR_HTTP_EAGAIN);
    if (length < 0) { result = AGENT_ERR_NETWORK; goto done; }
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
        char bytes[1024];
        int count = esp_http_client_read(client, bytes, sizeof(bytes));
        if (count > 0) {
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
    if(client) esp_http_client_cleanup(client);
    for (volatile char *p = auth; p < auth + sizeof(auth); ++p) *p = 0;
    return result;
}
