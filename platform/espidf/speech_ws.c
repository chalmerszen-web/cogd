#include "runtime.h"
#include "qianwen.h"
#include "realtime.h"
#if AGENT_TLS_COOPERATE
#include "tls_cooperate.h"
#endif
#if AGENT_ISOLATED_ASR
#include "asr_realtime.h"
#endif
#include "ws_frame.h"
#include "ws_reader.h"
#if AGENT_WS_CONNECT_TRACE
#include "ws_connect_clock.h"
#endif
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_tls.h"
#if AGENT_TEXT_PREFETCH
#include "esp_freertos_hooks.h"
#endif
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#if AGENT_HANDOFF_PROBE
#include "tcp_probe.h"
#endif

enum { M_SEND,M_WRITE,M_WPOLL,M_RECV,M_RPOLL,M_READ,M_READ0,M_COUNT };
typedef struct { unsigned count,total_ms,max_ms; } ws_measurement_t;
typedef struct {
    esp_transport_handle_t tls,ws;
    bool connected;
    size_t remaining;
#if AGENT_WS_INCREMENTAL
    bool bootstrap;
    agent_ws_reader_t reader;
#endif
} ws_connection_t;
#if AGENT_HANDOFF_PROBE
typedef struct {
    unsigned sdk_poll[3],tls_poll[3]; /* ready, empty, error */
    unsigned sdk_bytes,tls_bytes,empty_reads,failed_reads;
    unsigned ws_bytes,ws_chunks,last_ready_ms,last_read_ms,last_chunk_ms;
    unsigned snapshots;
    esp_agent_tcp_snapshot_t tcp[3];
} ws_receive_probe_t;
#endif
typedef struct {
    ws_connection_t connection;
    /* Closed ticket only, fixed DashScope origin, never another slot's handle. */
    esp_transport_handle_t cached;
    uint64_t cache_until;
#if AGENT_WS_TIMING
    ws_measurement_t measurement[M_COUNT];
    unsigned send_busy;
#endif
#if AGENT_HANDOFF_PROBE
    ws_receive_probe_t receive;
#endif
} ws_owner_t;
/* A slot has one owning task. Classic TTS and Omni remain mutually exclusive.
 * Only isolated ASR has a second slot; enabling it does not open a connection. */
static ws_owner_t primary;
static uint64_t tts_warm_until;
#if AGENT_ISOLATED_ASR
static ws_owner_t isolated;
#endif
static ws_owner_t *const owners[]={&primary,
#if AGENT_ISOLATED_ASR
    &isolated,
#endif
};
#if AGENT_HANDOFF_PROBE
static void probe_add(unsigned *value,unsigned n)
{ *value=n>UINT32_MAX-*value?UINT32_MAX:*value+n; }
static void probe_poll(ws_owner_t *owner,bool sdk,int result)
{
    ws_receive_probe_t *p=&owner->receive;
    probe_add((sdk?p->sdk_poll:p->tls_poll)+(result>0?0:result<0?2:1),1);
    if(result>0)p->last_ready_ms=(unsigned)esp_agent_now();
}
static void probe_read(ws_owner_t *owner,bool sdk,int result)
{
    ws_receive_probe_t *p=&owner->receive;
    if(result>0) {
        probe_add(sdk?&p->sdk_bytes:&p->tls_bytes,(unsigned)result);
        p->last_read_ms=(unsigned)esp_agent_now();
    } else probe_add(result<0?&p->failed_reads:&p->empty_reads,1);
}
void esp_agent_speech_ws_probe(char *out,size_t capacity)
{
    if(!out || !capacity)return;
    const ws_receive_probe_t *p=&primary.receive;
    int n=snprintf(out,capacity,"{\"sdk_poll\":[%u,%u,%u],\"tls_poll\":[%u,%u,%u],"
        "\"sdk_bytes\":%u,\"tls_bytes\":%u,\"empty_reads\":%u,\"failed_reads\":%u,"
        "\"ws_bytes\":%u,\"ws_chunks\":%u,\"last_ready_ms\":%u,\"last_read_ms\":%u,\"last_chunk_ms\":%u}",
        p->sdk_poll[0],p->sdk_poll[1],p->sdk_poll[2],p->tls_poll[0],p->tls_poll[1],p->tls_poll[2],
        p->sdk_bytes,p->tls_bytes,p->empty_reads,p->failed_reads,p->ws_bytes,p->ws_chunks,
        p->last_ready_ms,p->last_read_ms,p->last_chunk_ms);
    if(n<0 || (size_t)n>=capacity)out[0]=0;
}
bool esp_agent_speech_ws_tcp_probe(unsigned index,unsigned side,char *out,size_t capacity)
{
    if(index>=primary.receive.snapshots || side>1)return false;
    esp_agent_tcp_format(&primary.receive.tcp[index],side,out,capacity);return true;
}
static void probe_stall(ws_owner_t *owner)
{
    static const unsigned waits[]={400,1500,4000};
    ws_receive_probe_t *p=&owner->receive;
    if(owner!=&primary || p->snapshots>=3 || !p->last_chunk_ms ||
       (unsigned)esp_agent_now()-p->last_chunk_ms<waits[p->snapshots])return;
    int http=esp_agent_http_probe_socket();if(http<0)return;
    esp_agent_tcp_snapshot(esp_transport_get_socket(owner->connection.tls),http,&p->tcp[p->snapshots++]);
}
#else
#define probe_poll(owner,sdk,result) ((void)(owner),(void)(sdk),(void)(result))
#define probe_read(owner,sdk,result) ((void)(owner),(void)(sdk),(void)(result))
#endif
#if AGENT_WS_TIMING
#define measurement_start() esp_agent_now()
static void measure_reset(ws_owner_t *owner)
{ memset(owner->measurement,0,sizeof(owner->measurement));owner->send_busy=0; }
/* Global reset/report require both owners joined. Per-slot open resets only
 * its own counters. Wall time includes waits and task preemption. */
void esp_agent_speech_ws_measure_reset(void)
{ for(size_t i=0;i<sizeof(owners)/sizeof(*owners);++i)measure_reset(owners[i]); }
static unsigned add_saturated(unsigned a,unsigned b)
{ return b>UINT32_MAX-a?UINT32_MAX:a+b; }
static void measure_value(ws_owner_t *owner,unsigned at,unsigned ms)
{
    ws_measurement_t *measurement=owner->measurement;
    if(measurement[at].count<UINT32_MAX)++measurement[at].count;
    unsigned prior=measurement[at].total_ms;
    measurement[at].total_ms=ms>UINT32_MAX-prior?UINT32_MAX:prior+ms;
    if(ms>measurement[at].max_ms)measurement[at].max_ms=ms;
}
static unsigned measure_add(ws_owner_t *owner,unsigned at,uint64_t began)
{
    uint64_t elapsed=esp_agent_now()-began;
    unsigned ms=elapsed>UINT32_MAX?UINT32_MAX:(unsigned)elapsed;
    measure_value(owner,at,ms);return ms;
}
void esp_agent_speech_ws_measure(char *out,size_t capacity)
{
    if(!out || !capacity)return;
    ws_measurement_t measurement[M_COUNT]={0};unsigned send_busy=0;
    for(size_t i=0;i<sizeof(owners)/sizeof(*owners);++i) {
        const ws_owner_t *owner=owners[i];send_busy=add_saturated(send_busy,owner->send_busy);
        for(unsigned j=0;j<M_COUNT;++j) {
            measurement[j].count=add_saturated(measurement[j].count,owner->measurement[j].count);
            measurement[j].total_ms=add_saturated(measurement[j].total_ms,owner->measurement[j].total_ms);
            if(owner->measurement[j].max_ms>measurement[j].max_ms)measurement[j].max_ms=owner->measurement[j].max_ms;
        }
    }
    /* Triples are [calls,total_ms,max_ms]. tx/rx include their polls;
     * wr/rd isolate the transport write/read. rd0 is the subset of rd called
     * by a nominal zero-timeout read, which can still block inside TLS/WS. */
#define TRIPLE(at) measurement[at].count,measurement[at].total_ms,measurement[at].max_ms
    int n=snprintf(out,capacity,"{\"tx\":[%u,%u,%u],\"wr\":[%u,%u,%u],\"busy\":%u,"
        "\"wp\":[%u,%u,%u],\"rx\":[%u,%u,%u],\"rp\":[%u,%u,%u],"
        "\"rd\":[%u,%u,%u],\"rd0\":[%u,%u,%u]}",
        TRIPLE(M_SEND),TRIPLE(M_WRITE),send_busy,TRIPLE(M_WPOLL),TRIPLE(M_RECV),
        TRIPLE(M_RPOLL),TRIPLE(M_READ),TRIPLE(M_READ0));
#undef TRIPLE
    if(n<0 || (size_t)n>=capacity) {
        static const char small[]="{\"truncated\":true}";
        if(capacity>=sizeof(small))memcpy(out,small,sizeof(small));else out[0]=0;
    }
}
#else
#define measurement_start() 0u
static inline unsigned measure_add(ws_owner_t *owner,unsigned at,uint64_t began)
{ (void)owner;(void)at;(void)began;return 0; }
#define measure_value(owner,at,ms) ((void)(owner),(void)(at),(void)(ms))
#define measure_reset(owner) ((void)(owner))
void esp_agent_speech_ws_measure_reset(void) {}
void esp_agent_speech_ws_measure(char *out,size_t capacity)
{ if(out && capacity)out[0]=0; }
#endif
static int measured_poll_write(ws_owner_t *owner)
{
    uint64_t began=measurement_start();int ready=esp_transport_poll_write(owner->connection.tls,0);
    measure_add(owner,M_WPOLL,began);return ready;
}
/* Closed TLS handle with a ticket only, pinned to the fixed DashScope origin.
 * No socket, live TLS session, WebSocket auth or model task survives close. */
static void expire_owner(ws_owner_t *owner,bool forget)
{
    if(owner->cached && (forget || esp_agent_now()>=owner->cache_until)) {
        esp_transport_destroy(owner->cached);owner->cached=NULL;
    }
}
void esp_agent_speech_ws_expire(bool forget)
{
    if(tts_warm_until && (forget || esp_agent_now()>=tts_warm_until))
        esp_agent_speech_ws_warm_discard();
    for(size_t i=0;i<sizeof(owners)/sizeof(*owners);++i)expire_owner(owners[i],forget);
}
static bool cancelled(const atomic_bool *flag) { return flag && atomic_load(flag); }
static void close_ws(void *ctx)
{
    ws_owner_t *owner=ctx;ws_connection_t *connection=&owner->connection;
    if(owner==&primary)tts_warm_until=0;
    bool keep=false;
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    if(connection->connected && connection->tls) {
        keep=esp_transport_ssl_session_ticket_operation(connection->tls,ESP_TRANSPORT_SESSION_TICKET_SAVE)==ESP_OK &&
            esp_transport_ssl_session_ticket_operation(connection->tls,ESP_TRANSPORT_SESSION_TICKET_USE)==ESP_OK;
    }
#endif
    if(connection->ws) {esp_transport_close(connection->ws);esp_transport_destroy(connection->ws);}
    if(keep) {owner->cached=connection->tls;owner->cache_until=esp_agent_now()+60000;}
    else if(connection->tls)esp_transport_destroy(connection->tls);
    memset(connection,0,sizeof(*connection));
}
static agent_err_t open_ws_path(void *ctx,const atomic_bool *flag,const char *path,UBaseType_t handshake_priority)
{
    ws_owner_t *owner=ctx;ws_connection_t *connection=&owner->connection;
    if(connection->ws)return AGENT_ERR_BUSY;
    measure_reset(owner);
#if AGENT_HANDOFF_PROBE
    memset(&owner->receive,0,sizeof(owner->receive));
#endif
    if(cancelled(flag))return AGENT_ERR_CANCELLED;
    if(!esp_agent_online())return AGENT_ERR_OFFLINE;
    if(time(NULL)<1735689600)return AGENT_ERR_CONFIG;
    char auth[224]={0};agent_err_t error=esp_agent_qianwen_auth(auth,sizeof(auth));
    if(error)return error;
    /* The WSS owner never touches HTTP handles: final-answer HTTP may be live
     * in the other worker. Callers release obsolete HTTP before ASR starts. */
    expire_owner(owner,false);
    connection->tls=owner->cached?owner->cached:esp_transport_ssl_init();owner->cached=NULL;
    if(connection->tls) {
        esp_transport_ssl_crt_bundle_attach(connection->tls,esp_crt_bundle_attach);
        connection->ws=esp_transport_ws_init(connection->tls);
    }
    if(!connection->ws)error=AGENT_ERR_MEMORY;
    if(!error) {
        esp_transport_ws_config_t config={.ws_path=path,.auth=auth,
            .user_agent="ESP-HI/0.11-fast",.propagate_control_frames=false};
        if(esp_transport_ws_set_config(connection->ws,&config)!=ESP_OK)error=AGENT_ERR_MEMORY;
    }
    for(volatile char *p=auth;p<auth+sizeof(auth);++p)*p=0;
    /* ASR/classic keep their admission rule. The optional candidate adapter
     * yields at TLS step boundaries; the default shares idle priority.
     * Restore the caller before parsing/generating/streaming a reply. */
    UBaseType_t priority=uxTaskPriorityGet(NULL);
    if(!handshake_priority || priority<handshake_priority)vTaskPrioritySet(NULL,handshake_priority);
#if AGENT_WS_CONNECT_TRACE
    bool watched=!handshake_priority;
#if AGENT_TLS_COOPERATE
    watched=watched || esp_agent_tls_cooperate_current();
#endif
    if(!error && watched)esp_agent_ws_connect_watch(connection->tls);
#endif
    int connected=error?-1:esp_transport_connect(connection->ws,"dashscope.aliyuncs.com",443,4000);
#if AGENT_WS_CONNECT_TRACE
    if(watched)esp_agent_ws_connect_watch(NULL);
#endif
    vTaskPrioritySet(NULL,priority);
    if(!error && connected<0) {
        int status=esp_transport_ws_get_upgrade_request_status(connection->ws);
        int code=0,flags=0;esp_err_t cause=esp_tls_get_and_clear_last_error(esp_transport_get_error_handle(connection->tls),&code,&flags);
        error=status==401?AGENT_ERR_AUTH:status==403?AGENT_ERR_FORBIDDEN:status==429?AGENT_ERR_RATE:
            status>=500?AGENT_ERR_SERVER:cause==ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME?AGENT_ERR_DNS:
            cause==ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT || cause==ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT?AGENT_ERR_TIMEOUT:
            code || flags?AGENT_ERR_TLS:AGENT_ERR_NETWORK;
    }
    if(cancelled(flag))error=AGENT_ERR_CANCELLED;
    if(!error) {
        connection->connected=true;
#if AGENT_WS_INCREMENTAL
        connection->bootstrap=true;
#endif
        int socket=esp_transport_get_socket(connection->tls),no_delay=1;
        /* The caller reports this failure once through the normal error event. */
        if(socket<0 || setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay))!=0)
            error=AGENT_ERR_NETWORK;
    }
    if(error)close_ws(ctx);
    return error;
}
void esp_agent_speech_ws_warm_discard(void)
{ if(tts_warm_until)close_ws(&primary); }
agent_err_t esp_agent_speech_ws_warm(const atomic_bool *flag)
{
    if(cancelled(flag))return AGENT_ERR_CANCELLED;
    if(tts_warm_until && esp_agent_now()<tts_warm_until)return AGENT_OK;
    esp_agent_speech_ws_warm_discard();
    agent_err_t error=open_ws_path(&primary,flag,"/api-ws/v1/inference/",4);
    if(!error)tts_warm_until=esp_agent_now()+10000;
    return error;
}
static agent_err_t open_ws(void *ctx,const atomic_bool *flag)
{
    if(ctx==&primary && tts_warm_until) {
        if(!cancelled(flag) && esp_agent_now()<tts_warm_until) {
            /* Transfer an untouched transport. The new task supplies its own
             * task_id and performs the ordinary run-task/started handshake. */
            tts_warm_until=0;return AGENT_OK;
        }
        esp_agent_speech_ws_warm_discard();
    }
    return open_ws_path(ctx,flag,"/api-ws/v1/inference/",4);
}
static agent_err_t open_realtime_ws(void *ctx,const atomic_bool *flag)
{ return open_ws_path(ctx,flag,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL,4); }
#if AGENT_TEXT_PREFETCH
enum { CANDIDATE_HOOK_IDLE=1, CANDIDATE_HOOK_ACTIVE=2 };
static atomic_uchar candidate_phase; /*0: not registered. No callback owns a task pointer. */
static bool candidate_idle_yield(void)
{
    /* This is only a scheduler hint, with no published payload or borrowed
     * object lifetime; relaxed atomic access is sufficient. */
    if(atomic_load_explicit(&candidate_phase,memory_order_relaxed)!=CANDIDATE_HOOK_ACTIVE)return true;
    /* Remain Ready, never block or access a task/arena. The SDK still runs
     * its other idle callbacks (including WDT); do not sleep away the rest
     * of a quantum when the same-priority handshake can continue. */
    taskYIELD();
    return false; /* If the phase ended while yielding, next idle iteration sleeps normally. */
}
static agent_err_t open_speculative_ws(void *ctx,const atomic_bool *flag,const char *path)
{
#if AGENT_TLS_COOPERATE
    if(!esp_agent_tls_cooperate_begin())return AGENT_ERR_BUSY;
    agent_err_t cooperative_error=open_ws_path(ctx,flag,path,4);
    esp_agent_tls_cooperate_end();
    return cooperative_error;
#endif
    if(!atomic_load_explicit(&candidate_phase,memory_order_relaxed) &&
       esp_register_freertos_idle_hook_for_cpu(candidate_idle_yield,0)!=ESP_OK)return AGENT_ERR_MEMORY;
    atomic_store_explicit(&candidate_phase,CANDIDATE_HOOK_ACTIVE,memory_order_relaxed);
    agent_err_t error=open_ws_path(ctx,flag,path,0);
    atomic_store_explicit(&candidate_phase,CANDIDATE_HOOK_IDLE,memory_order_relaxed);
    return error;
}
static agent_err_t open_candidate_ws(void *ctx,const atomic_bool *flag)
{ return open_speculative_ws(ctx,flag,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL); }
agent_err_t esp_agent_asr_ws_warm(const atomic_bool *flag)
{ return open_speculative_ws(&isolated,flag,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL); }
#endif
#if AGENT_ISOLATED_ASR
static agent_err_t open_asr_ws(void *ctx,const atomic_bool *flag)
{ return open_ws_path(ctx,flag,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL,4); }
#endif
static agent_err_t send_ws_inner(void *ctx,bool binary,char *data,size_t length,const atomic_bool *flag)
{
    ws_owner_t *owner=ctx;ws_connection_t *connection=&owner->connection;
    if(cancelled(flag))return AGENT_ERR_CANCELLED;
    if(!connection->ws)return AGENT_ERR_CONFIG;
    if(length>2048)return AGENT_ERR_LIMIT;
    /* Return unsent backpressure to the duplex owner so it can drain ASR
     * events. A blocking write-only wait can deadlock two full TCP windows. */
    int ready=measured_poll_write(owner);
    if(ready<0)return AGENT_ERR_NETWORK;
    if(!ready)return AGENT_ERR_BUSY;
    uint64_t began=measurement_start();
    int n=esp_transport_ws_send_raw(connection->ws,(binary?WS_TRANSPORT_OPCODES_BINARY:WS_TRANSPORT_OPCODES_TEXT)|WS_TRANSPORT_OPCODES_FIN,
        data,(int)length,500);
    measure_add(owner,M_WRITE,began);
    return cancelled(flag)?AGENT_ERR_CANCELLED:n==(int)length?AGENT_OK:AGENT_ERR_NETWORK;
}
static agent_err_t send_realtime_ws_inner(void *ctx,bool binary,char *data,size_t length,const atomic_bool *flag)
{
    ws_owner_t *owner=ctx;ws_connection_t *connection=&owner->connection;
    if(cancelled(flag))return AGENT_ERR_CANCELLED;
    if(!connection->ws)return AGENT_ERR_CONFIG;
    if(length>AGENT_WS_FRAME_PAYLOAD_MAX)return AGENT_ERR_LIMIT;
    if(!data && length)return AGENT_ERR_ARGUMENT;
    int ready=measured_poll_write(owner);
    if(ready<0)return AGENT_ERR_NETWORK;
    if(!ready)return AGENT_ERR_BUSY; /* No frame bytes have been sent. */
    /* The realtime network worker has its own larger stack. Keep the classic
     * ASR/TTS sender unchanged so its smaller task never inherits this buffer.
     * One TLS record avoids the tiny header record of send_raw's two writes. */
    uint8_t frame[AGENT_WS_FRAME_MAX],mask[4];uint32_t random=esp_random();
    for(unsigned i=0;i<4;++i)mask[i]=(uint8_t)(random>>(8*i));
    size_t size=agent_ws_frame_pack(frame,sizeof(frame),binary,data,length,mask);
    if(!size)return AGENT_ERR_ARGUMENT;
    uint64_t began=measurement_start();
    int n=esp_transport_write(connection->tls,(const char *)frame,(int)size,500);
    measure_add(owner,M_WRITE,began);
    /* Any partial/ambiguous write closes the session upstream, never retries. */
    return cancelled(flag)?AGENT_ERR_CANCELLED:n==(int)size?AGENT_OK:AGENT_ERR_NETWORK;
}
#if AGENT_WS_INCREMENTAL
static int read_tls_bytes(void *ctx,void *data,size_t capacity,unsigned timeout)
{
    ws_owner_t *owner=ctx;ws_connection_t *connection=&owner->connection;
    uint64_t began=measurement_start();
    int ready=esp_transport_poll_read(connection->tls,(int)timeout);
    probe_poll(owner,false,ready);
    measure_add(owner,M_RPOLL,began);
    if(ready<=0)return ready;
    /* select() can see only part of an encrypted record. Nonblocking recv
     * lets mbedTLS retain that record and return WANT_READ instead of waiting
     * for its tail. Restore flags before any write; this socket has one owner. */
    int socket=esp_transport_get_socket(connection->tls);
    int flags=socket<0?-1:fcntl(socket,F_GETFL,0);
    if(flags<0 || fcntl(socket,F_SETFL,flags|O_NONBLOCK)<0)return -1;
    began=measurement_start();
    int n=esp_transport_read(connection->tls,data,(int)capacity,0);
    probe_read(owner,false,n);
    int restored=fcntl(socket,F_SETFL,flags);
    unsigned ms=measure_add(owner,M_READ,began);
    if(!timeout)measure_value(owner,M_READ0,ms);
    return restored<0?-1:n;
}
#endif
static agent_err_t read_sdk(ws_owner_t *owner,char *data,size_t capacity,agent_ws_chunk_t *part,unsigned timeout)
{
    ws_connection_t *connection=&owner->connection;
    /* IDF may retain the first WS bytes after HTTP's CRLFCRLF. Drain them
     * through its reader until a complete frame and an observed empty poll.
     * Never skip or inspect private SDK buffers. The upper JSON state and
     * fragment flag continue unchanged when raw TLS reading takes ownership. */
    if(!connection->remaining) {
        uint64_t began=measurement_start();int ready=esp_transport_poll_read(connection->ws,(int)timeout);
        probe_poll(owner,true,ready);
        measure_add(owner,M_RPOLL,began);if(ready<=0)return ready<0?AGENT_ERR_NETWORK:AGENT_OK;
    }
    uint64_t began=measurement_start();int n=esp_transport_read(connection->ws,data,(int)capacity,100);
    probe_read(owner,true,n);
    unsigned ms=measure_add(owner,M_READ,began);if(!timeout)measure_value(owner,M_READ0,ms);
    if(n<=0)return n<0?AGENT_ERR_NETWORK:AGENT_OK;
    part->first=!connection->remaining;
    if(part->first) {
        int length=esp_transport_ws_get_read_payload_len(connection->ws);
        if(length<0 || length>24000*2*90)return AGENT_ERR_LIMIT;
        connection->remaining=(size_t)length;
    }
    if((size_t)n>connection->remaining)return AGENT_ERR_PROTOCOL;
    part->opcode=(unsigned)esp_transport_ws_get_read_opcode(connection->ws);
    part->length=(size_t)n;connection->remaining-=(size_t)n;
    part->final=!connection->remaining && esp_transport_ws_get_fin_flag(connection->ws);
#if AGENT_WS_INCREMENTAL
    if(!connection->remaining) {
        connection->reader.fragmented=!esp_transport_ws_get_fin_flag(connection->ws);
        int ready=esp_transport_poll_read(connection->ws,0);probe_poll(owner,true,ready);
        if(!ready)connection->bootstrap=false;
    }
#endif
    return AGENT_OK;
}
static agent_err_t read_ws_inner(void *ctx,char *data,size_t capacity,agent_ws_chunk_t *part,unsigned timeout,const atomic_bool *flag)
{
    ws_owner_t *owner=ctx;ws_connection_t *connection=&owner->connection;memset(part,0,sizeof(*part));
    if(cancelled(flag))return AGENT_ERR_CANCELLED;
    if(!connection->ws)return AGENT_ERR_CONFIG;
#if AGENT_WS_INCREMENTAL
    if(connection->bootstrap) {
        agent_err_t error=read_sdk(owner,data,capacity,part,timeout);
        return cancelled(flag)?AGENT_ERR_CANCELLED:error;
    }
    agent_err_t error=agent_ws_reader_next(&connection->reader,read_tls_bytes,owner,data,capacity,part,timeout);
    if(cancelled(flag))return AGENT_ERR_CANCELLED;
    if(error)return error;
    if(part->opcode>=8 && part->first) {
        if(part->opcode==8)return AGENT_ERR_NETWORK;
        if(part->opcode==9 && esp_transport_ws_send_raw(connection->ws,
            WS_TRANSPORT_OPCODES_PONG|WS_TRANSPORT_OPCODES_FIN,data,(int)part->length,500)!=(int)part->length)
            return AGENT_ERR_NETWORK;
        memset(part,0,sizeof(*part));
    }
    return AGENT_OK;
#else
    agent_err_t error=read_sdk(owner,data,capacity,part,timeout);
    return cancelled(flag)?AGENT_ERR_CANCELLED:error;
#endif
}
static agent_err_t send_result(ws_owner_t *owner,agent_err_t error,uint64_t began)
{
    measure_add(owner,M_SEND,began);
#if AGENT_WS_TIMING
    if(error==AGENT_ERR_BUSY && owner->send_busy<UINT32_MAX)++owner->send_busy;
#endif
    return error;
}
static agent_err_t send_ws(void *ctx,bool binary,char *data,size_t length,const atomic_bool *flag)
{
    uint64_t began=measurement_start();
    return send_result(ctx,send_ws_inner(ctx,binary,data,length,flag),began);
}
static agent_err_t send_realtime_ws(void *ctx,bool binary,char *data,size_t length,const atomic_bool *flag)
{
    uint64_t began=measurement_start();
    return send_result(ctx,send_realtime_ws_inner(ctx,binary,data,length,flag),began);
}
static agent_err_t read_ws(void *ctx,char *data,size_t capacity,agent_ws_chunk_t *part,unsigned timeout,const atomic_bool *flag)
{
    uint64_t began=measurement_start();
    agent_err_t error=read_ws_inner(ctx,data,capacity,part,timeout,flag);
#if AGENT_HANDOFF_PROBE
    if(!error && part->length) {
        ws_receive_probe_t *p=&((ws_owner_t *)ctx)->receive;
        probe_add(&p->ws_bytes,(unsigned)part->length);probe_add(&p->ws_chunks,1);
        p->last_chunk_ms=(unsigned)esp_agent_now();
    } else if(!error)probe_stall(ctx);
#endif
    measure_add(ctx,M_RECV,began);return error;
}
const agent_ws_ops_t esp_agent_speech_ws={open_ws,send_ws,read_ws,close_ws,&primary};
const agent_ws_ops_t esp_agent_realtime_ws={open_realtime_ws,send_realtime_ws,read_ws,close_ws,&primary};
#if AGENT_TEXT_PREFETCH
const agent_ws_ops_t esp_agent_candidate_ws={open_candidate_ws,send_realtime_ws,read_ws,close_ws,&primary};
#endif
#if AGENT_ISOLATED_ASR
const agent_ws_ops_t esp_agent_asr_ws={open_asr_ws,send_realtime_ws,read_ws,close_ws,&isolated};
#endif
