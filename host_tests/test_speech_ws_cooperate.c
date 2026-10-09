/* Existing real adapter stubs, separate optional-policy test entrypoint. */
#define main legacy_adapter_main
#include "test_speech_ws.c"
#undef main

static char candidate_task,other_task;
static void *current_task=&candidate_task;
static unsigned delays;
void *xTaskGetCurrentTaskHandle(void) {return current_task;}
void vTaskDelay(unsigned ticks)
{
    assert(ticks==1 && esp_agent_tls_cooperate_current());
    ++delays;clock_ms+=ticks;
}

int main(void)
{
    const agent_ws_ops_t *candidate=&esp_agent_candidate_ws;
    expected_handshake_priority=4;
    opened(&esp_agent_asr_ws,"/api-ws/v1/realtime?model=" AGENT_ASR_RT_MODEL);
    assert(!delays && !esp_agent_tls_cooperate_current());
    unsigned asr_id=isolated.connection.tls->id;
    for(unsigned p=0;p<=5;++p) {
        priority=p;expected_handshake_priority=p>4?p:4;
        unsigned before=delays;
        assert(!candidate->open(candidate->ctx,&stop));
        assert(priority==p && delays==before+1 && !esp_agent_tls_cooperate_current());
        assert(isolated.connection.tls->id==asr_id && !idle_hook && !hook_registrations);
        candidate->close(candidate->ctx);esp_agent_speech_ws_expire(true);
        for(unsigned fault=0;fault<5;++fault) {
            fail_allocation=fault<2?fault+1:0;
            config_fault=fault==2;handshake_fault=fault==3;cancel_during_connect=fault==4;
            assert(candidate->open(candidate->ctx,&stop)!=AGENT_OK);
            assert(priority==p && !esp_agent_tls_cooperate_current() && !primary.connection.ws);
            assert(isolated.connection.tls->id==asr_id);
            fail_allocation=0;config_fault=handshake_fault=cancel_during_connect=false;
            atomic_store(&stop,false);esp_agent_speech_ws_expire(true);
        }
    }
    priority=3;expected_handshake_priority=4;
    assert(esp_agent_tls_cooperate_begin());
    current_task=&other_task;
    assert(candidate->open(candidate->ctx,&stop)==AGENT_ERR_BUSY && priority==3);
    assert(!esp_agent_tls_cooperate_current());
    current_task=&candidate_task;assert(esp_agent_tls_cooperate_current());
    esp_agent_tls_cooperate_end();
    atomic_store(&stop,true);
    assert(candidate->open(candidate->ctx,&stop)==AGENT_ERR_CANCELLED);
    assert(!esp_agent_tls_cooperate_current() && priority==3);
    atomic_store(&stop,false);clean();
    assert(!esp_agent_asr_ws_warm(&stop) && delays && !esp_agent_tls_cooperate_current());
    clean();assert(!allocations);
    puts("Cooperative real adapter: caller priority restored, failures/cancel, scope exclusion and independent ASR OK");
}
