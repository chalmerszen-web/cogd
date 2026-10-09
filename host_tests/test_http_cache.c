#include "runtime.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Exercise the production adapter's ownership and credential boundaries.
 * This stub cannot prove a real server accepted TLS session resumption. */
struct test_http {
    unsigned id;bool open,ticket,save,read;
    char url[512],auth[32],content_type[32];size_t sent;int method;
};
static unsigned next_id,created,destroyed,closed,opens,resumed,live,peak;
static uint64_t ticks;
static agent_http_target_t expected_target;
static atomic_bool cancel_flag;
static bool online=true,persistent=true,complete=true,fail_open,fail_url,fail_write,cancel_write;
static agent_err_t feed_error;
static int status=200;
static unsigned warm_steps,warm_pending,warm_cancel_step,prepared_traces;
static bool warm_fail;
static unsigned header_calls,header_pending,callbacks,feeds,headers_at_callback,sent_bytes;
static bool header_fail,header_cancel,require_callback;
static agent_err_t callback_error;
static esp_err_t tls_cause;
static const char *origins[]={"https://deepseek.test","https://gateway.test","https://vocalign.test",""};
static const char *auths[]={"Bearer TEST-D","Bearer TEST-G","Bearer TEST-V",""};
uint64_t esp_agent_now(void) {return ticks;}
bool esp_agent_online(void) {return online;}
void esp_agent_write(const char *p,size_t n) {(void)p;(void)n;assert(false);}
void vTaskDelay(unsigned ms) {ticks+=ms;}
void test_http_log(const char *tag,const char *fmt,...) {(void)tag;(void)fmt;}
time_t time(time_t *out) {time_t t=1790000000;if(out)*out=t;return t;}
esp_err_t esp_crt_bundle_attach(void *p) {(void)p;return ESP_OK;}
agent_err_t esp_agent_http_auth(agent_http_target_t target,char *base,size_t n,char *auth,size_t m,const char **ca)
{
    assert(target==expected_target);assert(strlen(origins[target])<n && strlen(auths[target])<m);
    strcpy(base,origins[target]);strcpy(auth,auths[target]);*ca=target==AGENT_HTTP_GATEWAY?"TEST CA":NULL;
    return AGENT_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    assert(c->disable_auto_redirect && c->keep_alive_enable);
    assert(c->save_client_session==(expected_target==AGENT_HTTP_DEEPSEEK || expected_target==AGENT_HTTP_VOCALIGN));
    assert((expected_target==AGENT_HTTP_GATEWAY)==!!c->cert_pem);
    assert((expected_target!=AGENT_HTTP_GATEWAY)==!!c->crt_bundle_attach);
    struct test_http *h=calloc(1,sizeof(*h));assert(h);h->id=++next_id;h->save=c->save_client_session;
    strcpy(h->url,c->url);++created;++live;if(live>peak)peak=live;assert(peak==1);return h;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h)
{assert(h && live==1);--live;++destroyed;free(h);return ESP_OK;}
esp_err_t esp_http_client_close(esp_http_client_handle_t h)
{assert(h);h->open=false;++closed;return ESP_OK;}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t h,const char *url)
{
    if(fail_url)return 5;
    /* An existing client must never change to another provider, even closed. */
    assert(!strncmp(h->url,origins[expected_target],strlen(origins[expected_target])));
    strcpy(h->url,url);return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms) {assert(h && ms>0);return ESP_OK;}
esp_err_t esp_http_client_set_method(esp_http_client_handle_t h,int method) {h->method=method;return ESP_OK;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value)
{strcpy(!strcmp(key,"Authorization")?h->auth:h->content_type,value);return ESP_OK;}
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t h,const char *key)
{assert(!strcmp(key,"Content-Type"));h->content_type[0]=0;return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int length)
{
    assert(h && length>=0 && !strcmp(h->auth,auths[expected_target]));++opens;
    assert(!strcmp(h->content_type,h->method==HTTP_METHOD_GET?"":"application/json"));
    if(fail_open)return 5;
    if(!h->open && h->ticket)++resumed;
    h->open=true;h->ticket=h->save;h->sent=0;sent_bytes=0;h->read=false;return ESP_OK;
}
esp_err_t esp_agent_http_client_connect_step(esp_http_client_handle_t h)
{
    assert(h && expected_target==AGENT_HTTP_DEEPSEEK);
    ++warm_steps;
    if(warm_cancel_step==warm_steps)atomic_store(&cancel_flag,true);
    if(warm_fail)return 5;
    if(warm_pending) {--warm_pending;return ESP_ERR_HTTP_CONNECTING;}
    if(!h->open && h->ticket)++resumed;
    h->open=true;h->ticket=h->save;h->read=false;return ESP_OK;
}
int esp_http_client_get_socket(esp_http_client_handle_t h) {assert(h);return 17;}
int esp_http_client_write(esp_http_client_handle_t h,const char *data,int n)
{
    assert(h->open && data && n>0);if(fail_write)return -1;
    int part=n>3?3:n;h->sent+=(size_t)part;sent_bytes+=(unsigned)part;
    if(cancel_write)atomic_store(&cancel_flag,true);
    return part;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h)
{
    assert(h->open);++header_calls;
    if(header_fail)return -1;
    if(header_cancel)atomic_store(&cancel_flag,true);
    if(header_pending) {--header_pending;ticks+=100;return -ESP_ERR_HTTP_EAGAIN;}
    return 2;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h) {assert(h);return status;}
int esp_http_client_read(esp_http_client_handle_t h,char *data,int capacity)
{assert(capacity>=2);if(h->read)return 0;memcpy(data,"OK",2);h->read=true;return 2;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) {return h->read && complete;}
bool esp_http_client_is_persistent_connection(esp_http_client_handle_t h) {return h->open && persistent;}
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t h,int *code,int *flags)
{assert(h);*code=*flags=0;return tls_cause;}
static agent_err_t feed(void *ctx,const char *data,size_t n)
{
    (void)ctx;assert(n==2 && !memcmp(data,"OK",2));
    if(require_callback)assert(callbacks==1);
    ++feeds;return feed_error;
}
static void trace(void *ctx,const char *phase)
{ (void)ctx;if(!strcmp(phase,"preconnected"))++prepared_traces; }
static agent_err_t request(agent_http_target_t target,bool reuse,bool get,const char *path)
{
    expected_target=target;
    agent_http_request_t r={.target=target,.method=get?"GET":"POST",.path=path?path:
        target==AGENT_HTTP_PUBLIC?"https://public.test/object":"/test",
        .body="testing",.length=get?0:7,.reuse_connection=reuse,.cancelled=&cancel_flag,.trace=trace};
    return esp_agent_http(NULL,&r,feed,NULL);
}
static void reset(void)
{
    esp_agent_http_forget();assert(!live);created=destroyed=closed=opens=resumed=peak=0;ticks=10;
    online=persistent=complete=true;fail_open=fail_url=fail_write=cancel_write=false;
    feed_error=AGENT_OK;status=200;tls_cause=ESP_OK;atomic_store(&cancel_flag,false);
    warm_steps=warm_pending=warm_cancel_step=prepared_traces=0;warm_fail=false;
    header_calls=header_pending=callbacks=feeds=headers_at_callback=sent_bytes=0;
    header_fail=header_cancel=require_callback=false;callback_error=AGENT_OK;
#if AGENT_HANDOFF_PROBE
    esp_agent_http_probe_headers(false);
#endif
}
#if AGENT_HANDOFF_PROBE
static agent_err_t handed_off(void *ctx)
{
    (void)ctx;assert(sent_bytes==7 && !callbacks && !feeds);++callbacks;
    assert(esp_agent_http_probe_socket()==17);
    headers_at_callback=header_calls;return callback_error;
}
static void header_probe_checks(void)
{
    for(unsigned scenario=0;scenario<7;++scenario) {
        reset();expected_target=AGENT_HTTP_DEEPSEEK;require_callback=true;
        esp_agent_http_probe_headers(scenario!=0);
        if(scenario==1)header_pending=2;
        if(scenario==2)header_pending=7; /* loop budget, then continue same parser */
        if(scenario==3)header_fail=true;
        if(scenario==4) {header_pending=7;header_cancel=true;}
        if(scenario==5)callback_error=AGENT_ERR_PROTOCOL;
        if(scenario==6)status=401;
        agent_http_request_t r={.target=AGENT_HTTP_DEEPSEEK,.method="POST",.path="/test",
            .body="testing",.length=7,.cancelled=&cancel_flag,.on_sent=handed_off};
        const agent_err_t expected[]={AGENT_OK,AGENT_OK,AGENT_OK,AGENT_ERR_NETWORK,
                                     AGENT_ERR_CANCELLED,AGENT_ERR_PROTOCOL,AGENT_ERR_AUTH};
        assert(esp_agent_http(NULL,&r,feed,NULL)==expected[scenario]);
        assert(esp_agent_http_probe_socket()==-1);
        assert(sent_bytes==7 && callbacks==(scenario==3 || scenario==4?0u:1u));
        assert(feeds==(scenario<3?1u:0u));
        if(scenario<3) {
            assert(headers_at_callback==(scenario==0?0u:scenario==1?3u:5u));
            assert(header_calls==(scenario==0?1u:scenario==1?3u:8u));
            assert(ticks==(scenario==0?10u:scenario==1?210u:710u));
            /* One-shot flag must not alter a second request in the same turn. */
            header_calls=callbacks=feeds=headers_at_callback=0;
            assert(!esp_agent_http(NULL,&r,feed,NULL));
            assert(!headers_at_callback && header_calls==1 && callbacks==1 && feeds==1);
        }
    }
    reset();puts("Diagnostic HTTP header probe: bounded pending, preserved body, handoff order, faults, cancel and one-shot reset OK");
}
#endif
static void warm_checks(void)
{
    reset();expected_target=AGENT_HTTP_DEEPSEEK;warm_pending=3;
    assert(!esp_agent_http_warm(&cancel_flag));
    assert(warm_steps==4 && created==1 && !opens && live==1);
    assert(!esp_agent_http_warm(&cancel_flag) && warm_steps==4);
    assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));
    assert(created==1 && opens==1 && prepared_traces==1 && !resumed);
    esp_agent_http_release();
    assert(!esp_agent_http_warm(&cancel_flag));
    assert(resumed==1 && opens==1); /* Warming itself never calls HTTP open. */
    assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));
    assert(created==1 && opens==2 && prepared_traces==2);
    reset();expected_target=AGENT_HTTP_DEEPSEEK;
    assert(!esp_agent_http_warm(&cancel_flag));ticks+=8000;esp_agent_http_expire();
    assert(!live && destroyed==1 && !opens);
    assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));assert(!prepared_traces);
    reset();expected_target=AGENT_HTTP_DEEPSEEK;
    assert(!esp_agent_http_warm(&cancel_flag));esp_agent_http_release();
    assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));assert(!prepared_traces && resumed==1);
    reset();assert(!request(AGENT_HTTP_VOCALIGN,true,false,NULL));
    assert(esp_agent_http_warm(&cancel_flag)==AGENT_ERR_BUSY && live==1 && !warm_steps);
    reset();expected_target=AGENT_HTTP_DEEPSEEK;
    assert(!esp_agent_http_warm(&cancel_flag));
    assert(!request(AGENT_HTTP_GATEWAY,true,false,NULL));assert(destroyed==1 && created==2 && !prepared_traces);
    for(unsigned failure=0;failure<5;++failure) {
        reset();expected_target=AGENT_HTTP_DEEPSEEK;
        if(failure==0)atomic_store(&cancel_flag,true);
        if(failure==1) {warm_pending=10;warm_cancel_step=2;}
        if(failure==2)warm_cancel_step=1; /* Cancellation on successful TLS step. */
        if(failure==3)warm_pending=1000;
        if(failure==4) {warm_fail=true;tls_cause=ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;}
        const agent_err_t want[]={AGENT_ERR_CANCELLED,AGENT_ERR_CANCELLED,AGENT_ERR_CANCELLED,
                                AGENT_ERR_TIMEOUT,AGENT_ERR_DNS};
        assert(esp_agent_http_warm(&cancel_flag)==want[failure]);
        assert(!live && !opens && created==destroyed);
        assert(ticks<=1010);
        atomic_store(&cancel_flag,false);warm_pending=warm_cancel_step=0;warm_fail=false;tls_cause=ESP_OK;
        assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));assert(opens==1 && !prepared_traces);
    }
    reset();expected_target=AGENT_HTTP_DEEPSEEK;online=false;
    assert(esp_agent_http_warm(&cancel_flag)==AGENT_ERR_OFFLINE && !created);
    reset();
}
int main(void)
{
#if AGENT_HANDOFF_PROBE
    header_probe_checks();
#endif
    warm_checks();
    for(unsigned target=0;target<4;++target) {
        reset();bool session=target==AGENT_HTTP_DEEPSEEK || target==AGENT_HTTP_VOCALIGN;
        assert(!request(target,true,false,NULL));assert(created==1);
        assert(!request(target,true,true,NULL));assert(created==(target==AGENT_HTTP_PUBLIC?2u:1u));
        esp_agent_http_release();assert(live==session);
        assert(!request(target,false,false,NULL));assert(created==(session?1u:target==AGENT_HTTP_PUBLIC?3u:2u));
        assert(resumed==session);esp_agent_http_release();
        if(session) {ticks+=119999;esp_agent_http_expire();assert(live==1);++ticks;esp_agent_http_expire();assert(!live);}
    }
    reset();assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));esp_agent_http_release();
    assert(!request(AGENT_HTTP_VOCALIGN,true,false,NULL));assert(destroyed==1 && created==2);
    assert(!request(AGENT_HTTP_PUBLIC,true,true,NULL));assert(!live && destroyed==3 && !resumed);
    reset();assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));esp_agent_http_forget();
    assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));assert(created==2 && !resumed);
    for(unsigned fault=0;fault<10;++fault) {
        reset();assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));esp_agent_http_release();
        if(fault==0) {fail_open=true;tls_cause=ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;}
        if(fault==1)fail_write=true;
        if(fault==2)cancel_write=true;
        if(fault==3)feed_error=AGENT_ERR_PROTOCOL;
        if(fault==4)complete=false;
        if(fault==5)status=401;
        if(fault==6)status=429;
        if(fault==7)status=503;
        if(fault==8)fail_url=true;
        if(fault==9)persistent=false;
        const agent_err_t expected[]={AGENT_ERR_DNS,AGENT_ERR_NETWORK,AGENT_ERR_CANCELLED,AGENT_ERR_PROTOCOL,
            AGENT_ERR_PROTOCOL,AGENT_ERR_AUTH,AGENT_ERR_RATE,AGENT_ERR_SERVER,AGENT_ERR_MEMORY,AGENT_OK};
        assert(request(AGENT_HTTP_DEEPSEEK,true,false,NULL)==expected[fault]);
        assert(live==(fault==9) && created==1 && opens==(fault==8?1u:2u));
        if(fault==9) {assert(!request(AGENT_HTTP_DEEPSEEK,false,false,NULL));assert(resumed==2);}
    }
    reset();assert(request(AGENT_HTTP_DEEPSEEK,false,false,".evil/path")==AGENT_ERR_PROTOCOL && !created);
    assert(request(AGENT_HTTP_PUBLIC,false,true,"http://public.test")==AGENT_ERR_TLS && !created);
    reset();assert(!request(AGENT_HTTP_DEEPSEEK,true,false,NULL));esp_agent_http_release();
    online=false;assert(request(AGENT_HTTP_DEEPSEEK,true,false,NULL)==AGENT_ERR_OFFLINE && opens==1);
    online=true;atomic_store(&cancel_flag,true);
    assert(request(AGENT_HTTP_DEEPSEEK,true,false,NULL)==AGENT_ERR_CANCELLED && opens==1);
    esp_agent_http_forget();assert(!live && created==destroyed);
    puts("production HTTP ownership: provider isolation, expiry, reuse, completion, cancel, credentials and failure eviction PASS");
}
