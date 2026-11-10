#include "aoq_conversation.h"
#include "aoq_session.h"
#include "aoq_objects.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <deque>
#include <memory>
#include <new>
#include <vector>

namespace {
constexpr int Argument=1,Busy=3,Limit=5,Protocol=7,Memory=19;
constexpr size_t QueueLimit=1024*1024,EarlyLimit=128*1024;
constexpr size_t AudioHighWater=QueueLimit-128*1024;
const cJSON* field(const cJSON* v,const char* key){return cJSON_GetObjectItemCaseSensitive(v,key);}
const char* str(const cJSON* v,const char* key){const auto* p=field(v,key);return cJSON_IsString(p)&&p->valuestring?p->valuestring:"";}
struct Json {cJSON* p;~Json(){cJSON_Delete(p);}};
struct Buffer {
    uint8_t* data=nullptr;size_t size=0;
    Buffer()=default;
    Buffer(const void* p,size_t n):size(n){data=static_cast<uint8_t*>(heap_caps_malloc(n+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(data){std::memcpy(data,p,n);data[n]=0;}}
    ~Buffer(){clear();}
    Buffer(Buffer&& b)noexcept:data(b.data),size(b.size){b.data=nullptr;b.size=0;}
    Buffer& operator=(Buffer&& b)noexcept{if(this!=&b){clear();data=b.data;size=b.size;b.data=nullptr;b.size=0;}return *this;}
    Buffer(const Buffer&)=delete;Buffer& operator=(const Buffer&)=delete;
    void clear(){if(data){volatile uint8_t* p=data;for(size_t i=0;i<size;++i)p[i]=0;heap_caps_free(data);}data=nullptr;size=0;}
};
struct Message {Buffer body;bool pcm=false;std::array<char,129> response{};};
}

struct p4_aoq_conversation {
    aoq::Session session;
    p4_aoq_conversation_options_t options{};
    std::deque<Message> queue;
    size_t queued=0,early_bytes=0;
    std::deque<Buffer> early;
    Buffer done;
    char response[129]{};
    uint64_t stream=0,group=0,subgroup=0,next_object=0;
    int64_t media_report_at=0;
    struct Retired { uint64_t stream=0;std::array<char,129> response{};bool discarded=false; };
    std::array<Retired,256> retired{};size_t retired_count=0;
    bool interrupted=false,strict_media_owner=false;
    bool active=false,has_stream=false,terminal=false,ready=false,abandoned=false;
    const char* stage="model_events";
    int error=0;
    bool audio_ready(size_t bytes)const{
        if(abandoned||interrupted)return true;
        // Reserve room for Data completion/control events while PCM waits
        // for playback. A false result leaves the object in Session's bounded
        // receive window and grants no new media credit.
        // Early media shares the same headroom with a prior reply still
        // queued for playback. Otherwise response.created could overflow
        // the queue when it flushes that early media into the next response.
        if(queued>AudioHighWater||early_bytes>AudioHighWater-queued||
           bytes>AudioHighWater-queued-early_bytes)return false;
        return active?queue.size()<1900:(early_bytes<=EarlyLimit&&bytes<=EarlyLimit-early_bytes);
    }
    int fail(int e,const char* why="adapter_error"){if(!error){error=e;stage=why;}return error;}
    int enqueue(const void* p,size_t n,bool pcm=false){
        if(!p||!n||n>QueueLimit-queued||queue.size()>=2048)return fail(Limit,"adapter_queue_limit");
        Message message;message.body=Buffer(p,n);if(!message.body.data)return fail(Memory);
        message.pcm=pcm;if(pcm)std::strcpy(message.response.data(),response);
        queue.push_back(std::move(message));queued+=n;return 0;
    }
    int flush_early(){
        if(!active)return 0;
        while(!early.empty()){auto& b=early.front();int e=enqueue(b.data,b.size,true);if(e)return e;early.pop_front();}
        early_bytes=0;return 0;
    }
    int retire_stream(bool discarded){
        for(size_t i=0;i<retired_count;++i)if(retired[i].stream==stream){
            if(std::strcmp(retired[i].response.data(),response))return fail(Protocol,"retired_stream_owner");
            retired[i].discarded|=discarded;return 0;
        }
        if(retired_count==retired.size())return fail(Limit);
        auto& r=retired[retired_count++];r.stream=stream;r.discarded=discarded;
        std::strcpy(r.response.data(),response);return 0;
    }
    void drop_pcm(const char* id){
        for(auto it=queue.begin();it!=queue.end();){
            if(it->pcm&&!std::strcmp(it->response.data(),id)){queued-=it->body.size;it=queue.erase(it);}
            else ++it;
        }
    }
    int interrupt(const char* id,bool& cancel_needed){
        cancel_needed=false;
        if(error)return error;
        if(abandoned||!id||!*id||std::strlen(id)>=sizeof(response))return Busy;
        if(active&&!std::strcmp(response,id)){
            if(!has_stream||interrupted)return Busy;
            int e=retire_stream(true);if(e)return e;
            drop_pcm(id);early.clear();early_bytes=0;
            interrupted=true;strict_media_owner=true;
            if(done.data){
                e=enqueue(done.data,done.size);if(e)return e;
                done.clear();active=has_stream=terminal=interrupted=false;response[0]=0;
            }else cancel_needed=true;
            return 0;
        }
        if(active||has_stream||done.data)return Busy;
        for(size_t i=0;i<retired_count;++i)if(!std::strcmp(retired[i].response.data(),id)){
            // Playout can lag a response which has fully completed upstream.
            retired[i].discarded=true;strict_media_owner=true;drop_pcm(id);return 0;
        }
        return Busy;
    }
    int finish(){
        if(!done.data)return 0;
        if(has_stream&&!terminal)return 0;
        int e=flush_early();if(e)return e;
        e=enqueue(done.data,done.size);if(e)return e;
        done.clear();
        if(has_stream){int retired_error=retire_stream(false);if(retired_error)return retired_error;}
        active=has_stream=terminal=false;response[0]=0;return 0;
    }
    int data(const aoq::Object& object){
        if(abandoned)return 0;
        Json value{cJSON_ParseWithLength(reinterpret_cast<const char*>(object.payload.data),object.payload.size)};
        if(!cJSON_IsObject(value.p))return fail(Protocol);
        const char* type=str(value.p,"type");
        if(!std::strcmp(type,"session.updated"))ready=true;
        if(!std::strcmp(type,"response.created")){
            const char* id=str(field(value.p,"response"),"id");
            if(!*id||std::strlen(id)>=sizeof(response))return fail(Protocol);
            if(active){
                if(!std::strcmp(response,id))return 0;
                // Never guess the owner of media across overlapping responses.
                return fail(Protocol,"overlapping_responses");
            }
            std::strcpy(response,id);active=true;
            int e=enqueue(object.payload.data,object.payload.size);if(e)return e;
            return flush_early();
        }
        if(!std::strcmp(type,"response.done")){
            const auto* r=field(value.p,"response");
            if(!active||std::strcmp(response,str(r,"id")))return fail(Protocol,"completion_identity");
            if(interrupted&&(!std::strcmp(str(r,"status"),"completed")||!std::strcmp(str(r,"status"),"cancelled"))){
                // Only this exact locally retired response may finish without
                // its terminator. Its known stream remains discard-only.
                int e=enqueue(object.payload.data,object.payload.size);if(e)return e;
                done.clear();active=has_stream=terminal=interrupted=false;response[0]=0;
                return 0;
            }
            if(std::strcmp(str(r,"status"),"completed")){
                // A failed/cancelled response can have no PCM or terminator.
                // Report its actual status immediately. This allocation is
                // retired before any new answer; never relabel late media.
                abandoned=true;stage="response_not_completed";
                return enqueue(object.payload.data,object.payload.size);
            }
            bool audio=false;
            const auto* output=field(r,"output");
            for(const auto* item=output?output->child:nullptr;item;item=item->next){
                const auto* content=field(item,"content");
                for(const auto* part=content?content->child:nullptr;part;part=part->next)
                    if(!std::strcmp(str(part,"type"),"audio")||!std::strcmp(str(part,"type"),"output_audio"))audio=true;
            }
            if(done.data)return fail(Protocol);
            done=Buffer(object.payload.data,object.payload.size);if(!done.data)return fail(Memory);
            // A zero-audio tool result can finish without a media track. An
            // audio response waits for the actual Segment terminator, even if
            // the Data track ran ahead of its first PCM packet.
            if(audio&&!has_stream)return 0;
            return finish();
        }
        return enqueue(object.payload.data,object.payload.size);
    }
    int audio(const aoq::Object& object){
        if(abandoned)return 0;
        if(!ready)return fail(Protocol);
        for(size_t i=0;i<retired_count;++i)if(retired[i].stream==object.stream)
            return retired[i].discarded?0:fail(Protocol);
        // After cancellation, do not infer that an unidentified early stream
        // belongs to a future response. A reconnect is safer than relabelling.
        if(strict_media_owner&&!active)return fail(Protocol,"unowned_media_after_interrupt");
        if(!has_stream){has_stream=true;stream=object.stream;group=object.group;subgroup=object.subgroup;next_object=object.id;}
        if(terminal||stream!=object.stream||group!=object.group||subgroup!=object.subgroup||object.id!=next_object)return fail(Protocol,"audio_segment_identity");
        aoq_wire::AudioProperties props;
        if(aoq_wire::decode_audio_properties(object.properties.data,object.properties.size,props)!=aoq_wire::ParseStatus::Ok||
           !aoq_wire::pcm_payload_size_matches(props,1,object.payload.size))return fail(Protocol);
        ++next_object;
        if(active)return enqueue(object.payload.data,object.payload.size,true);
        if(object.payload.size>EarlyLimit-early_bytes)return fail(Limit,"early_audio_limit");
        Buffer b(object.payload.data,object.payload.size);if(!b.data)return fail(Memory);
        early_bytes+=b.size;early.push_back(std::move(b));return 0;
    }
    int end(const aoq::End& e){
        if(abandoned)return 0;
        if(e.alias!=202)return 0;
        for(size_t i=0;i<retired_count;++i)if(retired[i].stream==e.stream)return 0; // duplicate FIN following EOG
        if(!has_stream||e.stream!=stream||e.group!=group||e.subgroup!=subgroup)return fail(Protocol);
        if(e.end_group||e.end_track){terminal=true;return finish();}
        return 0;
    }
};

extern "C" int p4_aoq_conversation_open(p4_aoq_conversation_t** out,const p4_aoq_conversation_options_t* options){
    if(!out||!options||!options->cancelled)return Argument;
    *out=nullptr;
    void* storage=heap_caps_malloc(sizeof(p4_aoq_conversation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!storage)return Memory;
    auto* s=new(storage)p4_aoq_conversation;s->options=*options;
    aoq::Options limits;limits.conversation=true;limits.lifetime_ms=180000;
    limits.opus_downlink=!options->video;
    limits.use_prefetched_allocation=true;
    limits.cancelled=options->cancelled;limits.cancel_userdata=options->ctx;
    if(options->video){limits.video_width=320;limits.video_height=256;}
    aoq::Callbacks callbacks;callbacks.userdata=s;
    callbacks.data=[](void* p,const aoq::Object& o){return static_cast<p4_aoq_conversation*>(p)->data(o);};
    callbacks.audio=[](void* p,const aoq::Object& o){return static_cast<p4_aoq_conversation*>(p)->audio(o);};
    callbacks.end=[](void* p,const aoq::End& e){return static_cast<p4_aoq_conversation*>(p)->end(e);};
    callbacks.audio_ready=[](void* p,size_t n){return static_cast<p4_aoq_conversation*>(p)->audio_ready(n);};
    callbacks.audio_discarded=[](void* p,uint64_t stream){
        auto* owner=static_cast<p4_aoq_conversation*>(p);
        for(size_t i=0;i<owner->retired_count;++i)
            if(owner->retired[i].stream==stream)return owner->retired[i].discarded;
        return false;
    };
    callbacks.audio_urgent=[](void* p){
        auto* owner=static_cast<p4_aoq_conversation*>(p);
        return owner->options.audio_urgent&&owner->options.audio_urgent(owner->options.ctx);
    };
    int e=s->session.open(options->video?P4_AOQ_OMNI:P4_AOQ_AUDIO,limits,callbacks);
    const auto timing=s->session.diagnostics();
    std::printf("AOQ_CONNECT prefetched=%u allocation_ms=%lld elapsed_ms=%lld error=%d\n",
        unsigned(timing.allocation_prefetched),static_cast<long long>(timing.allocation_ms),
        static_cast<long long>(timing.elapsed_ms),e);
    if(e){p4_aoq_conversation_close(s);return e;}
    *out=s;return 0;
}
extern "C" int p4_aoq_conversation_interrupt(p4_aoq_conversation_t* s,const char* response,bool* cancel_needed){
    if(!s||!cancel_needed)return Argument;
    return s->interrupt(response,*cancel_needed);
}
extern "C" int p4_aoq_conversation_send(p4_aoq_conversation_t* s,const char* text,size_t n){
    if(!s)return Argument;
    return s->error?s->error:s->session.send_json(text,n);
}
extern "C" int p4_aoq_conversation_pcm(p4_aoq_conversation_t* s,const int16_t* pcm,size_t n,uint64_t stamp){
    if(!s)return Argument;
    if(!s->ready)return Busy;
    return s->error?s->error:s->session.send_pcm16(pcm,n,stamp);
}
extern "C" int p4_aoq_conversation_jpeg(p4_aoq_conversation_t* s,const uint8_t* p,size_t n,uint64_t stamp){
    if(!s||!s->options.video)return Argument;
    if(!s->ready)return Busy;
    return s->error?s->error:s->session.send_jpeg(p,n,stamp);
}
extern "C" int p4_aoq_conversation_pump(p4_aoq_conversation_t* s){
    if(!s)return Argument;
    if(s->error)return s->error;
    const int e=s->session.poll(0);
    const auto d=s->session.diagnostics();
    const auto now=esp_timer_get_time()/1000;
    if(s->options.diagnostic && d.audio_objects && (s->active || s->has_stream || s->done.data || e) &&
       (e || now-s->media_report_at>=1000)){
        s->media_report_at=now;
        char detail[384];
        std::snprintf(detail,sizeof(detail),"audio=%llu decoded=%llu objects=%u queued=%u contiguous=%u reordered=%u bp=%u decode_us=%llu rx_packets=%u rx_bytes=%llu rtt_ms=%u done=%u terminal=%u udp_peak=%u udp_waits=%u poll_gap_ms=%u",
            static_cast<unsigned long long>(d.audio_rx_bytes),
            static_cast<unsigned long long>(d.audio_decoded_bytes),unsigned(d.audio_objects),unsigned(s->queued),
            unsigned(d.contiguous_bytes),unsigned(d.out_of_order_span),unsigned(d.audio_backpressure),
            static_cast<unsigned long long>(d.audio_decode_total_us),unsigned(d.rx_packets),
            static_cast<unsigned long long>(d.rx_bytes),unsigned(d.rtt_ms),bool(s->done.data),s->terminal,
            unsigned(d.udp_queue_peak),unsigned(d.udp_queue_waits),unsigned(d.poll_gap_max_ms));
        s->options.diagnostic(s->options.ctx,detail);
    }
    if(e){
        std::printf("AOQ_ADAPTER_ERROR done_pending=%u terminal=%u queued=%u\n",bool(s->done.data),s->terminal,static_cast<unsigned>(s->queued));
        return s->fail(e,s->session.diagnostics().stage);
    }
    return 0;
}
extern "C" int p4_aoq_conversation_read(p4_aoq_conversation_t* s,void* out,size_t capacity,p4_aoq_conversation_message_t* message){
    if(!s||!out||!message)return Argument;
    *message={};
    if(s->error)return s->error;
    if(s->queue.empty())return Busy;
    auto& m=s->queue.front();if(m.body.size+1>capacity)return Limit;
    std::memcpy(out,m.body.data,m.body.size);static_cast<uint8_t*>(out)[m.body.size]=0;
    message->pcm=m.pcm;message->bytes=m.body.size;std::strcpy(message->response,m.response.data());
    s->queued-=m.body.size;s->queue.pop_front();return 0;
}
extern "C" bool p4_aoq_conversation_pending(const p4_aoq_conversation_t* s){return s&&(s->active||s->done.data||s->has_stream);}
extern "C" void p4_aoq_conversation_input_stats(const p4_aoq_conversation_t* s,char* out,size_t cap){
    if(!out||!cap)return;
    if(!s){out[0]=0;return;}
    const auto d=s->session.diagnostics();
    const auto now=esp_timer_get_time()/1000;
    std::snprintf(out,cap,"sent_datagrams=%u declared_lost_datagrams=%u rtt_ms=%u rx_packets=%u rx_bytes=%llu audio_bytes=%llu udp_age_ms=%lld audio_age_ms=%lld poll_gap_ms=%u contiguous=%u reordered_span=%u backpressure=%u command_datagrams=%u codec=%s decoded_bytes=%llu decode_max_us=%u",
        unsigned(d.audio_datagrams_sent),unsigned(d.declared_lost_datagrams),unsigned(d.rtt_ms),unsigned(d.rx_packets),
        static_cast<unsigned long long>(d.rx_bytes),static_cast<unsigned long long>(d.audio_rx_bytes),
        static_cast<long long>(d.last_udp_ms?now-d.last_udp_ms:-1),static_cast<long long>(d.last_audio_ms?now-d.last_audio_ms:-1),
        unsigned(d.poll_gap_max_ms),unsigned(d.contiguous_bytes),unsigned(d.out_of_order_span),unsigned(d.audio_backpressure),unsigned(d.command_datagrams_received),
        d.opus48k_selected?"opus":"pcm",static_cast<unsigned long long>(d.audio_decoded_bytes),unsigned(d.audio_decode_max_us));
}
extern "C" const char* p4_aoq_conversation_stage(const p4_aoq_conversation_t* s){
    if(!s)return "not_open";
    return s->error?s->stage:s->session.diagnostics().stage;
}
extern "C" void p4_aoq_conversation_close(p4_aoq_conversation_t* s){if(s){s->session.close();s->~p4_aoq_conversation();heap_caps_free(s);}}
