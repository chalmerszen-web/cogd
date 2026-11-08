#include "aoq_prefetch.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <memory>
#include <mutex>
#include <new>

namespace {
struct Erase {
    void operator()(p4_aoq_allocation_t *p)const {
        if(p){p4_aoq_allocation_clear(p);heap_caps_free(p);}
    }
};
using Allocation=std::unique_ptr<p4_aoq_allocation_t,Erase>;
struct Job {unsigned epoch;p4_aoq_kind_t kind;};
std::mutex mutex;
Allocation ready;
unsigned epoch=0,worker_epoch=0;
p4_aoq_kind_t worker_kind=P4_AOQ_AUDIO;
bool inflight=false;

void run(void *arg)
{
    std::unique_ptr<Job> job(static_cast<Job*>(arg));
    Allocation value(static_cast<p4_aoq_allocation_t*>(
        heap_caps_calloc(1,sizeof(p4_aoq_allocation_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
    int error=value?p4_aoq_allocate(job->kind,value.get(),5000):19;
    {
        // HTTP never owns this lock. Cancellation/take are bounded by one
        // pointer transfer, not by TLS, network I/O or secret erasure.
        std::lock_guard<std::mutex> guard(mutex);
        inflight=false;
        if(!error && job->epoch==epoch)ready=std::move(value);
    }
    value.reset();job.reset();
    vTaskDelete(nullptr);
}
}

extern "C" int p4_aoq_prefetch_start(p4_aoq_kind_t kind)
{
    if(kind!=P4_AOQ_AUDIO && kind!=P4_AOQ_OMNI)return 1;
    Allocation stale;
    std::unique_ptr<Job> job(new(std::nothrow) Job{});
    if(!job)return 19;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if(inflight)return worker_epoch==epoch && worker_kind==kind?0:3;
        if(ready && ready->kind==kind)return 0;
        stale=std::move(ready);
        job->epoch=++epoch;job->kind=kind;
        worker_epoch=epoch;worker_kind=kind;inflight=true;
    }
    stale.reset();
    // The worker owns Job only after successful task creation. At most one
    // request remains in flight, including after clear and a rapid new wake.
    Job *submitted=job.release();
    if(xTaskCreatePinnedToCore(run,"aoq_prefetch",8192,submitted,4,nullptr,1)!=pdPASS){
        delete submitted;
        std::lock_guard<std::mutex> guard(mutex);inflight=false;
        return 19;
    }
    return 0;
}

extern "C" bool p4_aoq_prefetch_take(p4_aoq_kind_t kind,p4_aoq_allocation_t *out,unsigned setup_budget_ms)
{
    if(!out)return false;
    p4_aoq_allocation_clear(out);
    Allocation value;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if(!ready || ready->kind!=kind)return false;
        value=std::move(ready); // one consumer, including an unsuccessful setup
    }
    const int64_t now=esp_timer_get_time()/1000;
    const int64_t age=now-value->received_at_ms;
    const int64_t life=static_cast<int64_t>(value->expires_in_seconds)*1000;
    if(age<0 || age>60000 || life-age<=setup_budget_ms)return false;
    *out=*value;return true;
}

extern "C" void p4_aoq_prefetch_clear(void)
{
    Allocation discarded;
    {
        std::lock_guard<std::mutex> guard(mutex);
        ++epoch;discarded=std::move(ready);
        // Do not clear inflight: its bounded request still owns the worker.
        // Its old epoch prevents publication after this conversation ends.
    }
}
