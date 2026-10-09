/* Real device adapter, deterministic transport only; no TLS or cloud claims. */
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include "../platform/espidf/speech_ws.c"

struct esp_transport_item_t {
    struct esp_transport_item_t *parent;
    unsigned id,writes,raw_writes,polls,tickets;
    int socket,write_ready;
    bool ws,alive,attached;
    size_t sdk_at,at,used,chunk;
    unsigned char wire[512];
    char path[128],last[2048];
    size_t last_size;
};
static unsigned allocations,next_id,priority=3,expected_handshake_priority=4;
static uint64_t clock_ms=100;
static bool online=true,config_fault,handshake_fault,ticket_fault,partial_write;
static unsigned fail_allocation;
static atomic_bool stop;
static esp_freertos_idle_cb_t idle_hook;
static bool hook_fault,finish_during_yield,cancel_during_connect;
static unsigned hook_registrations,yields;
static unsigned handshakes;
#if AGENT_HANDOFF_PROBE
static bool read_empty_once,read_error_once;
static int poll_fault;
static int probe_http=-1;
static unsigned tcp_snapshots;
int esp_agent_http_probe_socket(void) {return probe_http;}
void esp_agent_tcp_snapshot(int ws,int http,esp_agent_tcp_snapshot_t *p)
{assert(ws>=0 && http==probe_http);memset(p,0,sizeof(*p));p->at=(unsigned)clock_ms;++tcp_snapshots;}
void esp_agent_tcp_format(const esp_agent_tcp_snapshot_t *p,unsigned side,char *out,size_t capacity)
{assert(p->at && side<2);snprintf(out,capacity,"{\"at\":%u}",p->at);}
#endif

esp_err_t esp_register_freertos_idle_hook_for_cpu(esp_freertos_idle_cb_t hook,unsigned cpu)
{
    assert(cpu==0 && !idle_hook);++hook_registrations;
    if(hook_fault)return ESP_ERR_NO_MEM;
    idle_hook=hook;return ESP_OK;
}
void test_task_yield(void)
{
    assert(priority==0 && (atomic_load(&candidate_phase)==CANDIDATE_HOOK_ACTIVE));++yields;
    if(finish_during_yield)atomic_store(&candidate_phase,CANDIDATE_HOOK_IDLE);
}

uint64_t esp_agent_now(void) { return clock_ms++; }
bool esp_agent_online(void) { return online; }
agent_err_t esp_agent_qianwen_auth(char *out,size_t n)
{ assert(n>=20);strcpy(out,"Bearer test-fixture");return AGENT_OK; }
uint32_t esp_random(void) { return 12345; }
unsigned uxTaskPriorityGet(void *task) { (void)task;return priority; }
void vTaskPrioritySet(void *task,unsigned value) { (void)task;priority=value; }
void test_sdk_log(const char *tag,const char *format,...) { (void)tag;(void)format; }
esp_err_t esp_crt_bundle_attach(void *unused) { (void)unused;return ESP_OK; }

static esp_transport_handle_t allocate(bool ws)
{
    if(fail_allocation && !--fail_allocation)return NULL;
    esp_transport_handle_t t=calloc(1,sizeof(*t));assert(t);
    ++allocations;t->id=++next_id;t->ws=ws;t->alive=true;t->socket=-1;
    t->write_ready=1;t->chunk=3;return t;
}
esp_transport_handle_t esp_transport_ssl_init(void) { return allocate(false); }
esp_transport_handle_t esp_transport_ws_init(esp_transport_handle_t parent)
{ assert(parent && !parent->ws);esp_transport_handle_t t=allocate(true);if(t)t->parent=parent;return t; }
void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t t,esp_err_t (*fn)(void *))
{ assert(t->alive && fn==esp_crt_bundle_attach);t->attached=true; }
esp_err_t esp_transport_ws_set_config(esp_transport_handle_t t,const esp_transport_ws_config_t *config)
{
    assert(t->ws && !strcmp(config->auth,"Bearer test-fixture") && !config->propagate_control_frames);
    assert(strlen(config->ws_path)<sizeof(t->path));strcpy(t->path,config->ws_path);
    return config_fault?ESP_FAIL:ESP_OK;
}
#if AGENT_WS_CONNECT_TRACE
int __wrap_esp_transport_connect(esp_transport_handle_t,const char *,int,int);
#endif
int esp_transport_connect(esp_transport_handle_t t,const char *host,int port,int timeout)
{
    ++handshakes;
    assert(t->alive && !strcmp(host,"dashscope.aliyuncs.com"));
    assert(port==443 && timeout==4000 && priority==expected_handshake_priority);
    if(!priority) {
        assert(idle_hook && (atomic_load(&candidate_phase)==CANDIDATE_HOOK_ACTIVE));
        unsigned before=yields;assert(!idle_hook() && yields==before+1);
    } else if(idle_hook)assert(idle_hook());
    if(cancel_during_connect)atomic_store(&stop,true);
#if AGENT_TLS_COOPERATE
    agent_tls_cooperate_step(0);
    clock_ms+=17;
    agent_tls_cooperate_step(1);
#endif
#if AGENT_WS_CONNECT_TRACE
    if(t->ws) {
        /* Model the SDK's external nested call; the linker resolves the
         * clock module's __real reference to this deterministic endpoint. */
        int result=__wrap_esp_transport_connect(t->parent,host,port,timeout);
        if(result<0)return result;
        t->sdk_at=0;return 0;
    }
    assert(t->attached);
    if(handshake_fault)return -1;
    t->socket=socket(AF_INET,SOCK_STREAM,0);assert(t->socket>=0);
#else
    assert(t->ws && t->parent->attached);
    if(handshake_fault)return -1;
    t->parent->socket=socket(AF_INET,SOCK_STREAM,0);assert(t->parent->socket>=0);
#endif
    t->sdk_at=0;return 0;
}
int esp_transport_close(esp_transport_handle_t t)
{ assert(t->alive && t->ws);if(t->parent->socket>=0)close(t->parent->socket);t->parent->socket=-1;return 0; }
esp_err_t esp_transport_destroy(esp_transport_handle_t t)
{ assert(t && t->alive && allocations);assert(t->socket<0);t->alive=false;--allocations;free(t);return ESP_OK; }
esp_err_t esp_transport_ssl_session_ticket_operation(esp_transport_handle_t t,esp_transport_session_ticket_operation_t op)
{ assert(!t->ws && t->alive);(void)op;++t->tickets;return ticket_fault?ESP_FAIL:ESP_OK; }
int esp_transport_get_socket(esp_transport_handle_t t) { assert(t->alive);return t->socket; }
int esp_transport_ws_get_upgrade_request_status(esp_transport_handle_t t) { assert(t->ws);return 401; }
esp_tls_error_handle_t esp_transport_get_error_handle(esp_transport_handle_t t)
{ return (esp_tls_error_handle_t)t; }
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t error,int *code,int *flags)
{ assert(error);*code=*flags=0;return ESP_OK; }
int esp_transport_poll_write(esp_transport_handle_t t,int timeout)
{ assert(t->alive && !t->ws && timeout==0);++t->polls;return t->write_ready; }
int esp_transport_write(esp_transport_handle_t t,const char *out,int length,int timeout)
{
    assert(t->alive && !t->ws && length>0 && length<=(int)sizeof(t->last) && timeout==500);
    ++t->writes;memcpy(t->last,out,(size_t)length);t->last_size=(size_t)length;
    return partial_write?length-1:length;
}
int esp_transport_ws_send_raw(esp_transport_handle_t t,ws_transport_opcodes_t op,const char *out,int length,int timeout)
{ assert(t->alive && t->ws && (op&128) && timeout==500);(void)out;++t->raw_writes;return length; }
int esp_transport_poll_read(esp_transport_handle_t t,int timeout)
{
    assert(t->alive && timeout>=0);++t->polls;
#if AGENT_HANDOFF_PROBE
    if(poll_fault)return poll_fault;
#endif
    return t->ws?t->sdk_at<2:t->at<t->used;
}
int esp_transport_read(esp_transport_handle_t t,char *out,int capacity,int timeout)
{
    assert(t->alive && capacity>0 && timeout>=0);
#if AGENT_HANDOFF_PROBE
    if(read_empty_once) {read_empty_once=false;return 0;}
    if(read_error_once) {read_error_once=false;return -1;}
#endif
    if(t->ws) {
        size_t n=2-t->sdk_at;if(n>(size_t)capacity)n=(size_t)capacity;
        memcpy(out,"{}"+t->sdk_at,n);t->sdk_at+=n;return (int)n;
    }
    size_t n=t->used-t->at;if(n>t->chunk)n=t->chunk;if(n>(size_t)capacity)n=(size_t)capacity;
    memcpy(out,t->wire+t->at,n);t->at+=n;return (int)n;
}
int esp_transport_ws_get_read_payload_len(esp_transport_handle_t t) { assert(t->ws);return 2; }
ws_transport_opcodes_t esp_transport_ws_get_read_opcode(esp_transport_handle_t t)
{ assert(t->ws);return WS_TRANSPORT_OPCODES_TEXT; }
bool esp_transport_ws_get_fin_flag(esp_transport_handle_t t) { assert(t->ws);return true; }

static ws_owner_t *owner(const agent_ws_ops_t *ops) { return ops->ctx; }
static void opened(const agent_ws_ops_t *ops,const char *path)
{
    unsigned prior=priority;
    assert(!ops->open(ops->ctx,&stop) && priority==prior);
    if(idle_hook)assert(idle_hook() && !(atomic_load(&candidate_phase)==CANDIDATE_HOOK_ACTIVE));
    ws_owner_t *o=owner(ops);assert(!strcmp(o->connection.ws->path,path));
    char data[8];agent_ws_chunk_t part;
    assert(!ops->read(ops->ctx,data,sizeof(data),&part,0,&stop));
    assert(part.first && part.final && part.length==2 && !memcmp(data,"{}",2));
    assert(!o->connection.bootstrap);
}
static void frame(const agent_ws_ops_t *ops,unsigned code,const char *text)
{
    esp_transport_handle_t t=owner(ops)->connection.tls;size_t n=strlen(text);assert(n<=125);
    t->wire[0]=(unsigned char)(128|code);t->wire[1]=(unsigned char)n;
    memcpy(t->wire+2,text,n);t->at=0;t->used=n+2;
}
static void drain(const agent_ws_ops_t *ops,const char *text)
{
    char all[128];size_t used=0;unsigned firsts=0,finals=0;
    for(unsigned i=0;!finals;++i) {
        assert(i<100);agent_ws_chunk_t part;
        assert(!ops->read(ops->ctx,all+used,2,&part,0,&stop));
        used+=part.length;firsts+=part.first;finals+=part.final;
    }
    assert(firsts==1 && finals==1 && used==strlen(text) && !memcmp(all,text,used));
}
static void clean(void)
{
    esp_agent_realtime_ws.close(esp_agent_realtime_ws.ctx);
    esp_agent_asr_ws.close(esp_agent_asr_ws.ctx);
    esp_agent_speech_ws_expire(true);assert(!allocations);
}
static void warm_tts_checks(void)
{
    const agent_ws_ops_t *tts=&esp_agent_speech_ws,*asr=&esp_agent_asr_ws,*omni=&esp_agent_realtime_ws;
    expected_handshake_priority=4;
    assert(!esp_agent_speech_ws_warm(&stop));
    unsigned connected=handshakes,id=primary.connection.tls->id;
    assert(tts_warm_until && primary.connection.bootstrap && !primary.connection.ws->sdk_at);
    assert(!primary.connection.tls->writes && !primary.connection.ws->raw_writes);
    assert(!esp_agent_speech_ws_warm(&stop) && handshakes==connected);
    assert(omni->open(omni->ctx,&stop)==AGENT_ERR_BUSY);
    opened(tts,"/api-ws/v1/inference/");
    assert(!tts_warm_until && handshakes==connected && primary.connection.tls->id==id);
    assert(tts->open(tts->ctx,&stop)==AGENT_ERR_BUSY);
    esp_agent_speech_ws_warm_discard();esp_agent_speech_ws_expire(true);
    assert(primary.connection.connected && primary.connection.tls->id==id);
    clean();
    /* Unused/error paths close only the reserved warm slot, never an adopted
     * TTS stream or independent ASR. Expiry cannot leak across voice turns. */
    for(unsigned reason=0;reason<5;++reason) {
        opened(asr,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL);
        unsigned asr_id=isolated.connection.tls->id;
        assert(!esp_agent_speech_ws_warm(&stop));connected=handshakes;
        if(reason==0)esp_agent_speech_ws_warm_discard();
        if(reason==1) {clock_ms=tts_warm_until;esp_agent_speech_ws_expire(false);}
        if(reason==2)esp_agent_speech_ws_expire(true);
        if(reason==3) {
            atomic_store(&stop,true);
            assert(tts->open(tts->ctx,&stop)==AGENT_ERR_CANCELLED);
            atomic_store(&stop,false);
        }
        if(reason==4) {
            clock_ms=tts_warm_until;
            opened(tts,"/api-ws/v1/inference/");
            assert(handshakes>connected && !tts_warm_until);
            tts->close(tts->ctx);
        }
        assert(!tts_warm_until && !primary.connection.ws && isolated.connection.tls->id==asr_id);
        clean();
    }
    opened(omni,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
    id=primary.connection.tls->id;
    assert(esp_agent_speech_ws_warm(&stop)==AGENT_ERR_BUSY && !tts_warm_until);
    esp_agent_speech_ws_warm_discard();assert(primary.connection.tls->id==id);clean();
    for(unsigned failure=0;failure<5;++failure) {
        fail_allocation=failure<2?failure+1:0;
        handshake_fault=failure==2;config_fault=failure==3;cancel_during_connect=failure==4;
        assert(esp_agent_speech_ws_warm(&stop)!=AGENT_OK);
        assert(!tts_warm_until && !primary.connection.ws && !allocations && priority==3);
        fail_allocation=0;handshake_fault=config_fault=cancel_during_connect=false;
        atomic_store(&stop,false);clean();
    }
    puts("TTS warm: no task/text/PCM, one-time untouched adoption, stale/unused/cancel/failure cleanup and independent ASR ownership OK");
}
#if AGENT_HANDOFF_PROBE
static void receive_counters(void)
{
    const agent_ws_ops_t *p=&esp_agent_realtime_ws,*a=&esp_agent_asr_ws;
    opened(p,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
    assert(primary.receive.sdk_bytes==2 && primary.receive.ws_bytes==2);
    assert(primary.receive.sdk_poll[0]==1 && primary.receive.sdk_poll[1]==1);
    frame(p,1,"abc");read_empty_once=true;
    char bytes[8];agent_ws_chunk_t part;
    assert(!p->read(p->ctx,bytes,sizeof(bytes),&part,0,&stop) && !part.length);
    assert(primary.receive.empty_reads==1 && !primary.receive.tls_bytes);
    drain(p,"abc");
    assert(primary.receive.tls_bytes==5 && primary.receive.ws_bytes==5);
    assert(primary.receive.last_read_ms<=primary.receive.last_chunk_ms);
    assert(!p->read(p->ctx,bytes,sizeof(bytes),&part,0,&stop) && !part.length);
    assert(primary.receive.tls_poll[1]==1);
    ws_receive_probe_t before=primary.receive;
    opened(a,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL);
    frame(a,1,"independent");drain(a,"independent");
    assert(!memcmp(&before,&primary.receive,sizeof(before)));
    frame(p,1,"error");read_error_once=true;
    assert(p->read(p->ctx,bytes,sizeof(bytes),&part,0,&stop)==AGENT_ERR_NETWORK);
    assert(primary.receive.failed_reads==1);
    char report[480];esp_agent_speech_ws_probe(report,sizeof(report));
    assert(strstr(report,"\"tls_bytes\":5") && strstr(report,"\"ws_bytes\":5"));
    before=primary.receive;clean();
    assert(!memcmp(&before,&primary.receive,sizeof(before))); /* close preserves */
    opened(p,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
    assert(!primary.receive.tls_bytes && !primary.receive.failed_reads);
    poll_fault=-1;
    assert(p->read(p->ctx,bytes,sizeof(bytes),&part,0,&stop)==AGENT_ERR_NETWORK);
    assert(primary.receive.tls_poll[2]==1);poll_fault=0;clean();
    esp_agent_speech_ws_probe(bytes,1);assert(!bytes[0]);
    unsigned count=UINT32_MAX-1;probe_add(&count,2);assert(count==UINT32_MAX);
    puts("Diagnostic receive counters: SDK/TLS, partial/empty/error, isolated owner, close/reset and saturation OK");
}
static void stall_snapshots(void)
{
    clean();assert(!esp_agent_realtime_ws.open(&primary,&stop));
    primary.receive.last_chunk_ms=(unsigned)clock_ms;
    clock_ms+=401;probe_stall(&primary);assert(!tcp_snapshots);
    probe_http=17;probe_stall(&isolated);assert(!tcp_snapshots);
    probe_stall(&primary);assert(tcp_snapshots==1 && primary.receive.snapshots==1);
    probe_stall(&primary);assert(tcp_snapshots==1);
    clock_ms+=1100;probe_stall(&primary);assert(tcp_snapshots==2);
    clock_ms+=2500;probe_stall(&primary);assert(tcp_snapshots==3);
    clock_ms+=8000;probe_stall(&primary);assert(tcp_snapshots==3);
    char report[480];assert(esp_agent_speech_ws_tcp_probe(2,1,report,sizeof(report)) && report[0]);
    assert(!esp_agent_speech_ws_tcp_probe(3,0,report,sizeof(report)));
    clean();assert(primary.receive.snapshots==3);
    assert(!esp_agent_realtime_ws.open(&primary,&stop));assert(!primary.receive.snapshots);
    probe_http=-1;clean();
}
#endif
int main(void)
{
    warm_tts_checks();
#if AGENT_HANDOFF_PROBE
    receive_counters();
    stall_snapshots();
#endif
    const agent_ws_ops_t *a=&esp_agent_asr_ws,*b=&esp_agent_realtime_ws,*classic=&esp_agent_speech_ws;
    assert(a->ctx!=b->ctx && classic->ctx==b->ctx);
    opened(a,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL);
    opened(b,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
    assert(classic->open(classic->ctx,&stop)==AGENT_ERR_BUSY);
    assert(a->open(a->ctx,&stop)==AGENT_ERR_BUSY);
    char payload[]="hello";assert(!a->send(a->ctx,false,payload,5,&stop));
    assert(owner(a)->connection.tls->writes==1 && !owner(b)->connection.tls->writes);
    assert(!b->send(b->ctx,false,payload,5,&stop));
    frame(a,1,"ASR partial retained");frame(b,1,"candidate audio retained");
    /* Interleave incremental frame state; closing B must not clear A. */
    char prefix[2];agent_ws_chunk_t part;
    assert(!a->read(a->ctx,prefix,1,&part,0,&stop));
    assert(part.first && !part.final && part.length==1 && prefix[0]=='A');
    drain(b,"candidate audio retained");b->close(b->ctx);b->close(b->ctx);
    char rest[64];size_t used=0;
    do {assert(!a->read(a->ctx,rest+used,2,&part,0,&stop));assert(!part.first);used+=part.length;}while(!part.final);
    assert(used==strlen("SR partial retained") && !memcmp(rest,"SR partial retained",used));
    assert(!a->send(a->ctx,false,payload,5,&stop));
    unsigned aid=owner(a)->connection.tls->id,bid=owner(b)->cached->id;
    a->close(a->ctx);assert(owner(a)->cached->id==aid);
    opened(b,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
    assert(owner(b)->connection.tls->id==bid && owner(a)->cached->id==aid);
    frame(b,9,"ping");char pong[8];
    for(unsigned i=0;i<20 && !owner(b)->connection.ws->raw_writes;++i)
        assert(!b->read(b->ctx,pong,sizeof(pong),&part,0,&stop));
    assert(owner(b)->connection.ws->raw_writes==1);
    unsigned writes=owner(b)->connection.tls->writes;
    owner(b)->connection.tls->write_ready=0;
    assert(b->send(b->ctx,false,payload,5,&stop)==AGENT_ERR_BUSY && owner(b)->connection.tls->writes==writes);
    owner(b)->connection.tls->write_ready=1;partial_write=true;
    assert(b->send(b->ctx,false,payload,5,&stop)==AGENT_ERR_NETWORK);
    assert(owner(b)->connection.tls->writes==writes+1);partial_write=false;
    atomic_store(&stop,true);
    assert(b->read(b->ctx,pong,sizeof(pong),&part,0,&stop)==AGENT_ERR_CANCELLED);
    assert(b->send(b->ctx,false,payload,5,&stop)==AGENT_ERR_CANCELLED);atomic_store(&stop,false);
    clean();
    /* Failure of the second slot never disposes the first slot. */
    opened(a,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL);aid=owner(a)->connection.tls->id;
    for(unsigned fault=0;fault<5;++fault) {
        fail_allocation=fault<2?fault+1:0;config_fault=fault==2;handshake_fault=fault==3;online=fault!=4;
        agent_err_t expected=fault<3?AGENT_ERR_MEMORY:fault==3?AGENT_ERR_AUTH:AGENT_ERR_OFFLINE;
        assert(b->open(b->ctx,&stop)==expected);
        assert(owner(a)->connection.tls->id==aid && b->send(b->ctx,false,payload,0,&stop)==AGENT_ERR_CONFIG);
        assert(!a->send(a->ctx,false,payload,5,&stop));
        b->close(b->ctx);assert(allocations==2 && priority==3);
        fail_allocation=0;config_fault=handshake_fault=false;online=true;
    }
    opened(b,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
    ticket_fault=true;b->close(b->ctx);ticket_fault=false;
    assert(!owner(b)->cached && allocations==2);
    a->close(a->ctx);clock_ms=owner(a)->cache_until-1;expire_owner(owner(a),false);
    assert(owner(a)->cached);clock_ms=owner(a)->cache_until;expire_owner(owner(a),false);
    assert(!allocations);
    opened(classic,"/api-ws/v1/inference/");assert(!classic->send(classic->ctx,false,payload,5,&stop));
    assert(owner(classic)->connection.ws->raw_writes==1);clean();
    /* Speculative TLS shares idle priority, then restores the exact caller's
     * priority on success/failure/cancellation. ASR keeps its own policy. */
    const agent_ws_ops_t *candidate=&esp_agent_candidate_ws;
    assert(candidate->ctx==b->ctx);
    hook_fault=true;
    assert(candidate->open(candidate->ctx,&stop)==AGENT_ERR_MEMORY);
    assert(!idle_hook && !atomic_load(&candidate_phase) && !(atomic_load(&candidate_phase)==CANDIDATE_HOOK_ACTIVE));
    assert(!owner(candidate)->connection.ws);
    hook_fault=false;
    opened(a,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL);
    const unsigned candidate_priorities[]={0,2,3,5};
    for(unsigned i=0;i<sizeof(candidate_priorities)/sizeof(*candidate_priorities);++i) {
        unsigned p=candidate_priorities[i];
        priority=p;expected_handshake_priority=0;
        opened(candidate,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
#if AGENT_WS_CONNECT_TRACE
        unsigned cb,ce;esp_agent_ws_connect_clock(&cb,&ce);assert(cb && ce>=cb);
#endif
        assert(b->open(b->ctx,&stop)==AGENT_ERR_BUSY && priority==p);
        assert(!a->send(a->ctx,false,payload,5,&stop));
        unsigned id=owner(candidate)->connection.tls->id;
        candidate->close(candidate->ctx);
        assert(owner(candidate)->cached && owner(candidate)->cached->id==id);
        opened(candidate,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
        assert(owner(candidate)->connection.tls->id==id);
        candidate->close(candidate->ctx);
        handshake_fault=true;
        assert(candidate->open(candidate->ctx,&stop)==AGENT_ERR_AUTH && priority==p);
        assert(idle_hook() && !(atomic_load(&candidate_phase)==CANDIDATE_HOOK_ACTIVE));
        handshake_fault=false;
        assert(!owner(candidate)->connection.ws && owner(a)->connection.ws);
        atomic_store(&stop,true);
        assert(candidate->open(candidate->ctx,&stop)==AGENT_ERR_CANCELLED && priority==p);
        atomic_store(&stop,false);
        cancel_during_connect=true;
        assert(candidate->open(candidate->ctx,&stop)==AGENT_ERR_CANCELLED && priority==p);
        assert(idle_hook() && !(atomic_load(&candidate_phase)==CANDIDATE_HOOK_ACTIVE));
        cancel_during_connect=false;atomic_store(&stop,false);
    }
    assert(hook_registrations==2); /* One failed attempt, then one persistent registration. */
    priority=0;finish_during_yield=true;atomic_store(&candidate_phase,CANDIDATE_HOOK_ACTIVE);
    assert(!idle_hook() && !(atomic_load(&candidate_phase)==CANDIDATE_HOOK_ACTIVE));finish_during_yield=false;
    assert(idle_hook()); /* One final non-sleeping iteration, then ordinary idle. */
    priority=3;expected_handshake_priority=4;clean();
    /* Phase-boundary reclamation forgets only closed tickets. An active
     * independent owner must retain its socket and unfinished frame. */
    opened(a,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL);
    opened(b,"/api-ws/v1/realtime?model=" AGENT_RT_MODEL);
    aid=owner(a)->connection.tls->id;
    b->close(b->ctx);assert(owner(b)->cached && allocations==3);
    esp_agent_speech_ws_expire(true);
    assert(!owner(b)->cached && allocations==2 && owner(a)->connection.tls->id==aid);
    frame(a,1,"input survives ticket release");drain(a,"input survives ticket release");
    a->close(a->ctx);assert(owner(a)->cached);
    esp_agent_speech_ws_expire(true);assert(!allocations);
    esp_agent_speech_ws_expire(true);assert(!allocations);
    /* Idle ASR uses the existing isolated slot and idle-safe handshake. It
     * leaves even session.created unread for the later sole ASR consumer. */
    expected_handshake_priority=0;
    assert(!esp_agent_asr_ws_warm(&stop) && priority==3);
    assert(owner(a)->connection.bootstrap && !owner(a)->connection.ws->sdk_at);
    assert(!owner(b)->connection.ws && allocations==2);
    assert(!strcmp(owner(a)->connection.ws->path,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL));
    assert(esp_agent_asr_ws_warm(&stop)==AGENT_ERR_BUSY);
    char creation[2];
    assert(!a->read(a->ctx,creation,sizeof(creation),&part,0,&stop));
    assert(part.first && part.final && !memcmp(creation,"{}",2));
    a->close(a->ctx);esp_agent_speech_ws_expire(true);assert(!allocations);
    handshake_fault=true;
    assert(esp_agent_asr_ws_warm(&stop)==AGENT_ERR_AUTH && priority==3);
    assert(!owner(a)->connection.ws && !allocations);handshake_fault=false;
    atomic_store(&stop,true);
    assert(esp_agent_asr_ws_warm(&stop)==AGENT_ERR_CANCELLED && !allocations);
    atomic_store(&stop,false);cancel_during_connect=true;
    assert(esp_agent_asr_ws_warm(&stop)==AGENT_ERR_CANCELLED && !allocations && priority==3);
    cancel_during_connect=false;atomic_store(&stop,false);
    char metrics[512];esp_agent_speech_ws_measure(metrics,sizeof(metrics));
#if AGENT_WS_TIMING
    assert(strstr(metrics,"\"tx\":") && !strstr(metrics,"truncated"));
#else
    assert(!metrics[0]);
#endif
    esp_agent_speech_ws_measure_reset();
    puts("Real adapter: independent ownership, interleaved frames, faults, cancel, tickets, legacy exclusion OK");
    return 0;
}
