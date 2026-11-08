#include <cassert>
#include <cstring>
#include <iostream>
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_prefetch.cpp"

int64_t test_now=1000;
static void (*pending)(void*);
static void *argument;
static bool task_fail=false,cancel_in_http=false;
static int http_error=0;
static unsigned requests=0,erased=0,task_deletes=0,life=120;
int xTaskCreatePinnedToCore(void (*entry)(void*),const char*,unsigned stack,void *arg,unsigned priority,void*,int core){
    assert(stack==8192 && priority==4 && core==1 && !pending);
    if(task_fail)return 0;
    pending=entry;argument=arg;return pdPASS;
}
void vTaskDelete(void *p){assert(!p);++task_deletes;}
extern "C" int p4_aoq_allocate(p4_aoq_kind_t kind,p4_aoq_allocation_t *out,unsigned timeout){
    assert(timeout==5000);++requests;
    *out={};out->kind=kind;out->received_at_ms=test_now;out->expires_in_seconds=life;
    std::strcpy(out->token,"SYNTHETIC_NOT_A_CREDENTIAL");
    if(cancel_in_http)p4_aoq_prefetch_clear();
    return http_error;
}
extern "C" void p4_aoq_allocation_clear(p4_aoq_allocation_t *p){
    if(p){if(p->token[0])++erased;std::memset(p,0,sizeof(*p));}
}
static void complete(){assert(pending);auto call=pending;auto arg=argument;pending=nullptr;argument=nullptr;call(arg);}
static bool zero(const p4_aoq_allocation_t &a){const auto *p=reinterpret_cast<const unsigned char*>(&a);for(size_t i=0;i<sizeof(a);++i)if(p[i])return false;return true;}
int main(){
    p4_aoq_allocation_t out{};
    assert(p4_aoq_prefetch_start(P4_AOQ_ASR)==1 && !pending);
    assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,nullptr,12000));
    assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO));
    assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO) && !requests);
    assert(p4_aoq_prefetch_start(P4_AOQ_OMNI)==3);
    assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000));
    complete();assert(requests==1);
    assert(!p4_aoq_prefetch_take(P4_AOQ_OMNI,&out,12000) && zero(out));
    assert(p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000));
    assert(out.kind==P4_AOQ_AUDIO && out.token[0]);
    // Taken allocations cannot be reused, even if the later QUIC setup fails.
    assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000) && zero(out));
    assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO));p4_aoq_prefetch_clear();
    assert(p4_aoq_prefetch_start(P4_AOQ_AUDIO)==3);complete();
    assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000));
    // Clear while HTTP owns the job must reject its late result.
    cancel_in_http=true;assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO));complete();cancel_in_http=false;
    assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000));
    http_error=8;assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO));complete();http_error=0;
    assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000));
    task_fail=true;assert(p4_aoq_prefetch_start(P4_AOQ_AUDIO)==19);task_fail=false;
    for(int64_t age:{int64_t(-1),int64_t(60001),int64_t(108000)}){
        assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO));complete();test_now+=age;
        assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000) && zero(out));test_now-=age;
    }
    life=12;assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO));complete();
    assert(!p4_aoq_prefetch_take(P4_AOQ_AUDIO,&out,12000));life=120;
    assert(!p4_aoq_prefetch_start(P4_AOQ_AUDIO));complete();
    const unsigned before=erased;p4_aoq_prefetch_clear();assert(erased==before+1);
    assert(!p4_aoq_prefetch_start(P4_AOQ_OMNI));complete();
    assert(p4_aoq_prefetch_take(P4_AOQ_OMNI,&out,12000));
    p4_aoq_allocation_clear(&out);p4_aoq_prefetch_clear();
    assert(task_deletes==requests && erased>=requests);
    std::cout<<"AOQ prefetch ownership, cancellation, expiry and one-use checks passed\n";
}
