#include "aoq_tts_trial.h"
#include "aoq_session.h"
#include "aoq_objects.h"
#include "aoq_heap.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
constexpr int Argument=1,Busy=3,Cancelled=4,Limit=5,JsonError=6,Protocol=7,Server=14,Timeout=15,Memory=19,NotFound=23;
constexpr size_t PcmMax=512*1024;
std::atomic_flag owner=ATOMIC_FLAG_INIT;
std::atomic<uint32_t> cancel_epoch{0};
uint64_t generation=0;
int64_t now_ms(){return esp_timer_get_time()/1000;}
void wipe(void* data,size_t bytes){auto* p=static_cast<volatile uint8_t*>(data);while(bytes--)*p++=0;}
struct Owner {
    bool held=!owner.test_and_set(std::memory_order_acquire);
    ~Owner(){if(held)owner.clear(std::memory_order_release);}
};
struct PcmDelete {void operator()(uint8_t* p)const{if(p){wipe(p,PcmMax);heap_caps_free(p);}}};
using Pcm=std::unique_ptr<uint8_t,PcmDelete>;
Pcm retained;
size_t retained_bytes=0;
uint64_t retained_generation=0;
void discard(){retained.reset();retained_bytes=0;retained_generation=0;}
uint32_t crc_update(uint32_t crc,const uint8_t* p,size_t size){
    while(size--){crc^=*p++;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int32_t(crc&1)));}
    return crc;
}
const cJSON* unique(const cJSON* object,const char* name){
    if(!cJSON_IsObject(object))return nullptr;
    const cJSON* found=nullptr;
    for(auto* p=object->child;p;p=p->next)if(p->string&&!std::strcmp(p->string,name)){if(found)return nullptr;found=p;}
    return found;
}
const char* string(const cJSON* object,const char* name){const auto* p=unique(object,name);return cJSON_IsString(p)&&p->valuestring?p->valuestring:"";}
struct Json {cJSON* p=nullptr;~Json(){cJSON_Delete(p);}};
struct Tuple {uint64_t group=0,subgroup=0,stream=0;};
bool same(const Tuple& a,const Tuple& b){return a.group==b.group&&a.subgroup==b.subgroup&&a.stream==b.stream;}

struct Trial {
    aoq::Session session;
    Pcm pcm;
    uint64_t id=0;
    uint32_t cancel_snapshot=0,crc=0xffffffffu;
    char task[37]{};
    const char* phrase="原生语音测试成功。";
    const char* stage="starting";
    int error=0;
    int64_t started=now_ms(),deadline=started+30000;
    bool run_queued=false,text_queued=false,finish_queued=false,task_started=false,task_finished=false,task_failed=false;
    bool selected_seen=false,has_tuple=false,has_conflict=false,end_group=false,end_track=false;
    bool model_receive_drained=false;
    int64_t task_finished_at=0,last_pcm_at=0;
    unsigned drain_guard_ms=1000;
    Tuple first,conflict;
    size_t bytes=0;
    uint64_t next_sample=0,cursor_expected=0,cursor_received=0,first_timestamp=0,received_timestamp=0,received_object=0;
    uint32_t received_duration=0,cursor_discontinuities=0;
    uint64_t next_audio_object=0;
    uint32_t pcm_objects=0,groups=0,streams=0,ignored_events=0,unmatched_events=0,busy_retries=0,terminal_events=0;

    int fail(int code,const char* why){if(!error){error=code;stage=why;}return error;}
    bool cancelled()const{return cancel_epoch.load(std::memory_order_relaxed)!=cancel_snapshot;}
    static bool stop(void* p){return static_cast<Trial*>(p)->cancelled();}
    static int data(void* p,const aoq::Object& object){return static_cast<Trial*>(p)->receive_data(object);}
    static int audio(void* p,const aoq::Object& object){return static_cast<Trial*>(p)->receive_audio(object);}
    static int end(void* p,const aoq::End& event){return static_cast<Trial*>(p)->receive_end(event);}
    int receive_data(const aoq::Object& object){
        Json json{cJSON_ParseWithLength(reinterpret_cast<const char*>(object.payload.data),object.payload.size)};
        if(!json.p)return fail(JsonError,"task_json_invalid");
        const auto* header=unique(json.p,"header");const char* event=string(header,"event");
        if(std::strcmp(event,"task-started")&&std::strcmp(event,"task-finished")&&
           std::strcmp(event,"task-failed")&&std::strcmp(event,"result-generated")){++ignored_events;return 0;}
        const char* identity=string(header,"task_id");
        if(!identity[0])return fail(Protocol,"task_identity_missing");
        if(std::strcmp(identity,task)){++unmatched_events;return 0;}
        if(!std::strcmp(event,"task-failed")){task_failed=true;return fail(Server,"provider_task_failed");}
        if(!run_queued)return fail(Protocol,"task_event_before_request");
        if(!std::strcmp(event,"task-started")){task_started=true;return 0;}
        if(!std::strcmp(event,"task-finished")){
            if(!task_started||!finish_queued)return fail(Protocol,"task_finished_before_input_end");
            task_finished=true;
            task_finished_at=now_ms();
        }
        return 0;
    }
    int receive_audio(const aoq::Object& object){
        if(!task_started||!text_queued)return fail(Protocol,"audio_before_text");
        if(!session.diagnostics().pcm24k_selected)return fail(Protocol,"audio_without_selection");
        selected_seen=true;
        aoq_wire::AudioProperties properties;
        if(!object.payload.size||!object.payload.data||
           aoq_wire::decode_audio_properties(object.properties.data,object.properties.size,properties)!=aoq_wire::ParseStatus::Ok||
           !aoq_wire::pcm_payload_size_matches(properties,1,object.payload.size))return fail(Protocol,"invalid_pcm_object");
        const Tuple current{object.group,object.subgroup,object.stream};
        if(!has_tuple){first=current;has_tuple=true;groups=streams=1;next_sample=properties.sample_cursor;
            next_audio_object=object.id;first_timestamp=properties.timestamp_us;}
        else if(!same(first,current)){
            has_conflict=true;conflict=current;groups+=current.group!=first.group;streams+=current.stream!=first.stream;
            return fail(Protocol,"unsupported_audio_layout");
        }
        cursor_expected=next_sample;cursor_received=properties.sample_cursor;received_duration=properties.duration_samples;
        received_timestamp=properties.timestamp_us;received_object=object.id;
        // LOC 50 is AlgoInternalSampleCursor, not a PCM buffer offset.
        // The real TTS service emits zero while object IDs and timestamps
        // advance. Keep it as telemetry; reliable stream object order, format,
        // payload duration and the exact tuple terminal establish completeness.
        if(object.id!=next_audio_object)return fail(Protocol,"pcm_object_gap_or_reorder");
        if(properties.sample_cursor!=next_sample)++cursor_discontinuities;
        if(object.payload.size>PcmMax-bytes)return fail(Limit,"pcm_capacity");
        if(properties.duration_samples>UINT64_MAX-properties.sample_cursor||object.id==UINT64_MAX)return fail(Protocol,"pcm_counter_overflow");
        std::memcpy(pcm.get()+bytes,object.payload.data,object.payload.size);
        crc=crc_update(crc,object.payload.data,object.payload.size);bytes+=object.payload.size;
        next_sample=properties.sample_cursor+properties.duration_samples;next_audio_object=object.id+1;++pcm_objects;
        last_pcm_at=now_ms();model_receive_drained=false;
        return 0;
    }
    int receive_end(const aoq::End& event){
        if(event.alias!=202||!text_queued||(!event.end_group&&!event.end_track))return 0;
        ++terminal_events;
        // This trial accepts exactly one PCM tuple. An empty/other stream's
        // terminal cannot authorize later PCM or truncate the active stream.
        const Tuple terminal{event.group,event.subgroup,event.stream};
        if(!has_tuple||!same(first,terminal))return 0;
        if(event.end_track)end_track=true;
        if(event.end_group)end_group=true;
        return 0;
    }
    bool complete()const{
        const auto d=session.diagnostics();
        return !error&&!d.error&&!task_failed&&d.setup_accepted&&d.pcm24k_selected&&selected_seen&&
               run_queued&&text_queued&&finish_queued&&d.sent_objects>=3&&task_started&&task_finished&&
               bytes&&has_tuple&&!has_conflict&&(end_group||end_track||model_receive_drained);
    }
    void drain_after_model_end(){
        const auto d=session.diagnostics();
        // Official Inference completion is task-finished; the shared Audio
        // track may stay open for another task. Do not manufacture an EOS.
        // This fresh-connection, single-task trial drains known fragments and
        // uses a bounded adaptive quiet window for cross-track arrival skew.
        // This is an application drain policy, not proof of a transport fence
        // or of arbitrary-delay/loss completeness. Explicit EOS remains separate.
        drain_guard_ms=d.rtt_ms?static_cast<unsigned>(std::clamp<uint64_t>(uint64_t(d.rtt_ms)*4+100,250,2000)):1000u;
        if(task_finished&&bytes&&d.receive_drained&&!d.queue_bytes&&
           now_ms()-std::max(task_finished_at,last_pcm_at)>=drain_guard_ms)model_receive_drained=true;
    }
    int queue_one(){
        if(finish_queued||(!task_started&&run_queued))return 0;
        char request[640];int size=0;
        if(!run_queued){
            stage="queue_run_task";
            size=std::snprintf(request,sizeof(request),
                "{\"header\":{\"action\":\"run-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
                "\"payload\":{\"task_group\":\"audio\",\"task\":\"tts\",\"function\":\"SpeechSynthesizer\","
                "\"model\":\"qwen-audio-3.0-tts-flash\",\"parameters\":{\"text_type\":\"PlainText\","
                "\"voice\":\"longanlingxi\",\"format\":\"pcm\",\"sample_rate\":24000},\"input\":{}}}",task);
        }else if(!text_queued){
            stage="queue_text";
            size=std::snprintf(request,sizeof(request),
                "{\"header\":{\"action\":\"continue-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
                "\"payload\":{\"input\":{\"text\":\"%s\"}}}",task,phrase);
        }else{
            stage="queue_finish_task";
            size=std::snprintf(request,sizeof(request),
                "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},\"payload\":{\"input\":{}}}",task);
        }
        if(size<0||static_cast<size_t>(size)>=sizeof(request))return fail(Limit,"request_capacity");
        const int e=session.send_json(request,static_cast<size_t>(size));
        wipe(request,sizeof(request));
        if(e==Busy){++busy_retries;return 0;}
        if(e)return fail(e,"request_queue_failed");
        if(!run_queued)run_queued=true;else if(!text_queued)text_queued=true;else finish_queued=true;
        stage=finish_queued?"receiving_pcm_and_task_end":"waiting_task_started";
        return 0;
    }
    int run(){
        uint8_t random[16];esp_fill_random(random,sizeof(random));random[6]=(random[6]&15)|0x40;random[8]=(random[8]&63)|0x80;
        std::snprintf(task,sizeof(task),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7],
            random[8],random[9],random[10],random[11],random[12],random[13],random[14],random[15]);
        wipe(random,sizeof(random));
        pcm.reset(static_cast<uint8_t*>(heap_caps_malloc(PcmMax,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
        if(!pcm)return fail(Memory,"pcm_memory");
        aoq::Options options;options.deadline_ms=deadline;options.cancelled=stop;options.cancel_userdata=this;
        options.trace_metadata=true;
        aoq::Callbacks callbacks{this,data,audio,end};stage="opening_session";
        const int opened=session.open(P4_AOQ_TTS,options,callbacks);
        if(opened)return fail(opened,"session_open_failed");
        while(!error){
            if(cancelled())return fail(Cancelled,"cancelled");
            if(now_ms()>=deadline){
                return fail(Timeout,!task_started?"missing_task_started":!task_finished?"missing_task_finished":
                    !bytes?"missing_pcm":!(end_group||end_track||model_receive_drained)?"missing_receive_drain":"trial_deadline");
            }
            if(queue_one())return error;
            const int e=session.poll(10);
            if(e)return fail(e,"session_poll_failed");
            if(cancelled())return fail(Cancelled,"cancelled");
            drain_after_model_end();
            if(complete()){stage="receive_pass_not_played";return 0;}
        }
        return error;
    }
};
const char* boolean(bool b){return b?"true":"false";}
int report(const Trial& t,const aoq::Diagnostics& d,bool pass,char* output,size_t cap){
    char first[144]="null",conflict[144]="null",crc[9]{};
    if(t.has_tuple)std::snprintf(first,sizeof(first),"{\"group\":%llu,\"subgroup\":%llu,\"stream\":%llu}",
        static_cast<unsigned long long>(t.first.group),static_cast<unsigned long long>(t.first.subgroup),static_cast<unsigned long long>(t.first.stream));
    if(t.has_conflict)std::snprintf(conflict,sizeof(conflict),"{\"group\":%llu,\"subgroup\":%llu,\"stream\":%llu}",
        static_cast<unsigned long long>(t.conflict.group),static_cast<unsigned long long>(t.conflict.subgroup),static_cast<unsigned long long>(t.conflict.stream));
    if(t.bytes)std::snprintf(crc,sizeof(crc),"%08x",static_cast<unsigned>(t.crc^0xffffffffu));
    const int n=std::snprintf(output,cap,
        "{\"aoq_tts_trial\":true,\"pass\":%s,\"generation\":%llu,\"stage\":\"%s\",\"error\":%d,"
        "\"session_error\":%d,\"session_stage\":\"%s\",\"setup_accepted\":%s,\"pcm24k_selected\":%s,\"selected_seen\":%s,"
        "\"tls_verified\":%s,\"quic_error\":%d,\"tx_packets\":%u,\"rx_packets\":%u,"
        "\"last_control_type\":%llu,\"last_control_length\":%u,\"control_frames\":%u,\"media_setups\":%u,"
        "\"goaway_received\":%s,\"goaway_reason_length\":%u,\"goaway_reason\":\"%s\",\"last_notify_tag\":\"%s\","
        "\"run_queued\":%s,\"text_queued\":%s,\"finish_queued\":%s,\"requests_sent\":%u,"
        "\"task_started\":%s,\"task_finished\":%s,\"task_failed\":%s,\"pcm_bytes\":%u,\"pcm_objects\":%u,"
        "\"pcm_crc32\":\"%s\",\"sample_rate\":24000,\"channels\":1,\"bits_per_sample\":16,"
        "\"cursor_expected\":%llu,\"cursor_received\":%llu,\"duration_received\":%u,\"object_received\":%llu,"
        "\"timestamp_first\":%llu,\"timestamp_received\":%llu,\"internal_cursor_discontinuities\":%u,"
        "\"audio_end_group\":%s,\"audio_end_track\":%s,\"terminal_events\":%u,\"pcm_groups\":%u,\"pcm_streams\":%u,"
        "\"model_receive_drained\":%s,\"drain_guard_ms\":%u,\"rtt_ms\":%u,"
        "\"first_tuple\":%s,\"conflict_tuple\":%s,\"busy_retries\":%u,\"unmatched_events\":%u,\"ignored_events\":%u,"
        "\"elapsed_ms\":%lld,\"pcm_retained\":%s,\"playback_tested\":false,\"capture_used\":false}",
        boolean(pass),static_cast<unsigned long long>(t.id),t.stage,t.error,d.error,d.stage,
        boolean(d.setup_accepted),boolean(d.pcm24k_selected),boolean(t.selected_seen),
        boolean(d.tls_verified),d.quic_error,static_cast<unsigned>(d.tx_packets),static_cast<unsigned>(d.rx_packets),
        static_cast<unsigned long long>(d.last_control_type),static_cast<unsigned>(d.last_control_length),
        static_cast<unsigned>(d.control_frames),static_cast<unsigned>(d.media_setups),
        boolean(d.goaway_received),static_cast<unsigned>(d.goaway_reason_length),d.goaway_reason,d.last_notify_tag,
        boolean(t.run_queued),boolean(t.text_queued),boolean(t.finish_queued),static_cast<unsigned>(d.sent_objects),
        boolean(t.task_started),boolean(t.task_finished),boolean(t.task_failed),static_cast<unsigned>(t.bytes),static_cast<unsigned>(t.pcm_objects),
        crc,static_cast<unsigned long long>(t.cursor_expected),static_cast<unsigned long long>(t.cursor_received),
        static_cast<unsigned>(t.received_duration),static_cast<unsigned long long>(t.received_object),
        static_cast<unsigned long long>(t.first_timestamp),static_cast<unsigned long long>(t.received_timestamp),static_cast<unsigned>(t.cursor_discontinuities),
        boolean(t.end_group),boolean(t.end_track),static_cast<unsigned>(t.terminal_events),static_cast<unsigned>(t.groups),
        static_cast<unsigned>(t.streams),boolean(t.model_receive_drained),t.drain_guard_ms,static_cast<unsigned>(d.rtt_ms),
        first,conflict,static_cast<unsigned>(t.busy_retries),
        static_cast<unsigned>(t.unmatched_events),static_cast<unsigned>(t.ignored_events),static_cast<long long>(now_ms()-t.started),boolean(pass));
    return n<0||static_cast<size_t>(n)>=cap?Limit:0;
}
} // namespace

extern "C" int p4_aoq_tts_trial_command(const char* command,char* output,size_t capacity){
    if(output&&capacity)output[0]=0;
    if(!command||!output||capacity<2048||(std::strcmp(command,"tts probe")&&std::strcmp(command,"tts question")))return Argument;
    const auto snapshot=cancel_epoch.load(std::memory_order_relaxed);
    Owner guard;if(!guard.held){std::snprintf(output,capacity,"{\"aoq_tts_trial\":true,\"pass\":false,\"error\":3,\"stage\":\"busy\"}");return Busy;}
    discard(); // Any newly accepted trial invalidates all previous PCM first.
    if(!generation)generation=uint64_t(esp_random())<<32;
    if(++generation==0)++generation;
    auto state=aoq::object_in_psram<Trial>();if(!state)return Memory;
    auto& trial=*state;trial.id=generation;trial.cancel_snapshot=snapshot;
    if(!std::strcmp(command,"tts question"))trial.phrase="请直接回答，一加一等于多少？";
    int error=trial.run();const auto diagnostics=trial.session.diagnostics();
    bool pass=!error&&trial.complete()&&!trial.cancelled();
    if(!pass&&!error)error=trial.fail(trial.cancelled()?Cancelled:Protocol,"final_state_failed");
    trial.session.close();
    if(pass){retained=std::move(trial.pcm);retained_bytes=trial.bytes;retained_generation=trial.id;}
    else trial.pcm.reset();
    const int format_error=report(trial,diagnostics,pass,output,capacity);
    if(format_error){
        discard();std::snprintf(output,capacity,"{\"aoq_tts_trial\":true,\"pass\":false,\"error\":5,\"stage\":\"report_capacity\"}");return format_error;
    }
    return error;
}
extern "C" void p4_aoq_tts_trial_cancel(void){cancel_epoch.fetch_add(1,std::memory_order_relaxed);}
extern "C" int p4_aoq_tts_trial_copy_pcm(uint64_t expected,void* destination,size_t capacity,size_t* written){
    if(written)*written=0;
    if(!expected||!destination||!written)return Argument;
    Owner guard;if(!guard.held)return Busy;
    if(!retained||!retained_bytes||expected!=retained_generation)return NotFound;
    if(capacity<retained_bytes)return Limit;
    std::memcpy(destination,retained.get(),retained_bytes);*written=retained_bytes;return 0;
}
