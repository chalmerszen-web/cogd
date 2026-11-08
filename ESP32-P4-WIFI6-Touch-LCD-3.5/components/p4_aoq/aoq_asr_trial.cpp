#include "aoq_asr_trial.h"
#include "aoq_session.h"
#include "aoq_tts_trial.h"
#include "aoq_pcm_fixture.h"
#include "aoq_heap.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
constexpr int Argument=1,Busy=3,Cancelled=4,Limit=5,JsonError=6,Protocol=7,Server=14,Timeout=15,Corrupt=18,Memory=19;
constexpr size_t SourceCapacity=512*1024,SourceMax=8*24000*2,InputSamplesMax=8*16000,FrameBytes=1280;
constexpr size_t TextMax=256,SentenceMax=8;
constexpr const char* Expected="原生语音测试成功";
std::atomic_flag owner=ATOMIC_FLAG_INIT;
std::atomic<uint32_t> cancel_epoch{0};
uint64_t run_generation=0;
int64_t now_ms(){return esp_timer_get_time()/1000;}
void wipe(void* data,size_t n){auto* p=static_cast<volatile uint8_t*>(data);while(n--)*p++=0;}
struct Owner {bool held=!owner.test_and_set(std::memory_order_acquire);~Owner(){if(held)owner.clear(std::memory_order_release);}};
struct Buffer {
    uint8_t* data=nullptr;size_t capacity=0;
    bool allocate(size_t n){data=static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));capacity=data?n:0;return data;}
    void clear(){if(data){wipe(data,capacity);heap_caps_free(data);}data=nullptr;capacity=0;}
    ~Buffer(){clear();}
};
uint32_t crc32(const uint8_t* p,size_t size){
    uint32_t crc=0xffffffffu;
    while(size--){crc^=*p++;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int32_t(crc&1)));}
    return crc^0xffffffffu;
}
bool parse_generation(const char* command,uint64_t& value){
    constexpr const char prefix[]="asr probe ";value=0;
    if(!command||std::strncmp(command,prefix,sizeof(prefix)-1))return false;
    const char* p=command+sizeof(prefix)-1;size_t digits=0;
    while(*p){if(++digits>20||*p<'0'||*p>'9'||value>(UINT64_MAX-uint64_t(*p-'0'))/10)return false;value=value*10+uint64_t(*p++-'0');}
    return digits&&value;
}
const cJSON* unique(const cJSON* object,const char* name){
    if(!cJSON_IsObject(object))return nullptr;
    const cJSON* found=nullptr;
    for(auto* p=object->child;p;p=p->next)if(p->string&&!std::strcmp(p->string,name)){if(found)return nullptr;found=p;}
    return found;
}
const char* string(const cJSON* object,const char* name){const auto* p=unique(object,name);return cJSON_IsString(p)&&p->valuestring?p->valuestring:"";}
struct Json {cJSON* p=nullptr;~Json(){cJSON_Delete(p);}};
bool ignored_codepoint(uint32_t c){
    if(c==' '||c=='\t'||c=='\n'||c=='\r'||c==0xa0||c==0x1680||
       (c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000)return true;
    if(c<128&&std::strchr(".,!?;:'\"()[]{}-",static_cast<int>(c)))return true;
    switch(c){case 0x3001:case 0x3002:case 0xff0c:case 0xff01:case 0xff1f:case 0xff1b:case 0xff1a:
    case 0x2018:case 0x2019:case 0x201c:case 0x201d:case 0xff08:case 0xff09:case 0x3010:case 0x3011:
    case 0x300a:case 0x300b:case 0x3008:case 0x3009:case 0x2026:case 0x2014:return true;default:return false;}
}
// Strict UTF-8: reject overlong forms, surrogates, NUL, invalid continuations,
// >U+10FFFF and controls other than the removable whitespace above. No case,
// homophone, compatibility-form or word substitution normalization is allowed.
bool normalize(const char* text,size_t n,char* out,size_t cap){
    size_t at=0,written=0,points=0;
    while(at<n){
        if(++points>128)return false;
        const size_t start=at;const auto first=static_cast<uint8_t>(text[at++]);uint32_t c=0,min=0;unsigned extra=0;
        if(first<0x80)c=first;
        else if(first>=0xc2&&first<=0xdf){c=first&31;extra=1;min=0x80;}
        else if(first>=0xe0&&first<=0xef){c=first&15;extra=2;min=0x800;}
        else if(first>=0xf0&&first<=0xf4){c=first&7;extra=3;min=0x10000;}
        else return false;
        if(extra>n-at)return false;
        for(unsigned i=0;i<extra;++i){const auto b=static_cast<uint8_t>(text[at++]);if((b&0xc0)!=0x80)return false;c=(c<<6)|(b&63);}
        if(c<min||!c||c>0x10ffff||(c>=0xd800&&c<=0xdfff)||(c<32&&c!='\t'&&c!='\n'&&c!='\r')||c==127)return false;
        if(!ignored_codepoint(c)){const auto bytes=at-start;if(bytes>=cap-written)return false;std::memcpy(out+written,text+start,bytes);written+=bytes;}
    }
    if(written>=cap)return false;
    out[written]=0;return true;
}
bool quoted(const char* text,char* out,size_t cap){
    size_t at=0;if(cap<3)return false;out[at++]='"';
    for(const auto* p=reinterpret_cast<const uint8_t*>(text);*p;++p){
        if(at+3>=cap)return false;
        if(*p=='"'||*p=='\\'){out[at++]='\\';out[at++]=static_cast<char>(*p);}
        else if(*p=='\n'||*p=='\r'||*p=='\t'){out[at++]='\\';out[at++]=*p=='\n'?'n':*p=='\r'?'r':'t';}
        else if(*p<32)return false;
        else out[at++]=static_cast<char>(*p);
    }
    out[at++]='"';out[at]=0;return true;
}

struct Trial {
    aoq::Session session;Buffer input;
    uint64_t source_generation=0,id=0;uint32_t cancel_snapshot=0;
    int64_t started=now_ms(),deadline=started+30000;
    const char* stage="starting";int error=0;
    char task[37]{},transcript[TextMax+1]{},normalized[TextMax+1]{};
    char packet[2048]{},encoded[1712]{};
    struct Sentence{uint32_t id=0;char text[TextMax+1]{};} sentences[SentenceMax];
    size_t sentence_count=0,total_text=0,source_bytes=0,input_bytes=0,queued_bytes=0;
    uint32_t source_crc=0,input_crc=0,frames=0,busy_retries=0,heartbeats=0,interims=0,duplicates=0,unmatched=0,ignored=0;
    bool run_queued=false,finish_queued=false,task_started=false,task_finished=false,task_failed=false,expected_match=false;
    int fail(int code,const char* why){if(!error){error=code;stage=why;}return error;}
    bool cancelled()const{return cancel_epoch.load(std::memory_order_relaxed)!=cancel_snapshot;}
    static bool stop(void* p){return static_cast<Trial*>(p)->cancelled();}
    static int data(void* p,const aoq::Object& o){return static_cast<Trial*>(p)->receive(o);}
    static int audio(void* p,const aoq::Object&){return static_cast<Trial*>(p)->fail(Protocol,"unexpected_audio_downlink");}
    void assemble(){
        size_t at=0;
        for(size_t i=0;i<sentence_count;++i){const size_t n=std::strlen(sentences[i].text);std::memcpy(transcript+at,sentences[i].text,n);at+=n;}
        transcript[at]=0;expected_match=normalize(transcript,at,normalized,sizeof(normalized))&&!std::strcmp(normalized,Expected);
    }
    int receive(const aoq::Object& object){
        // cJSON stores strings as C strings: never accept escaped NUL truncation.
        for(size_t i=0;i+6<=object.payload.size;++i)if(!std::memcmp(object.payload.data+i,"\\u0000",6))return fail(Protocol,"escaped_nul_in_event");
        Json json{cJSON_ParseWithLength(reinterpret_cast<const char*>(object.payload.data),object.payload.size)};
        if(!json.p)return fail(JsonError,"task_json_invalid");
        const auto* header=unique(json.p,"header");const char* event=string(header,"event");
        if(std::strcmp(event,"task-started")&&std::strcmp(event,"task-finished")&&std::strcmp(event,"task-failed")&&std::strcmp(event,"result-generated")){++ignored;return 0;}
        const char* identity=string(header,"task_id");if(!identity[0])return fail(Protocol,"task_identity_missing");
        if(std::strcmp(identity,task)){++unmatched;return 0;}
        if(!std::strcmp(event,"task-failed")){task_failed=true;return fail(Server,"provider_task_failed");}
        if(!run_queued)return fail(Protocol,"task_event_before_request");
        if(!std::strcmp(event,"task-started")){task_started=true;return 0;}
        if(!std::strcmp(event,"task-finished")){
            if(!task_started||!finish_queued)return fail(Protocol,"task_finished_before_input_end");
            task_finished=true;return 0;
        }
        if(!task_started)return fail(Protocol,"result_before_task_started");
        const auto* sentence=unique(unique(unique(json.p,"payload"),"output"),"sentence");
        if(!cJSON_IsObject(sentence))return fail(Protocol,"sentence_missing");
        const auto* heartbeat=unique(sentence,"heartbeat");
        if(!heartbeat&&cJSON_GetObjectItemCaseSensitive(sentence,"heartbeat"))return fail(Protocol,"duplicate_heartbeat");
        if(heartbeat&&!cJSON_IsBool(heartbeat))return fail(Protocol,"heartbeat_invalid");
        if(cJSON_IsTrue(heartbeat)){++heartbeats;return 0;}
        const auto* final=unique(sentence,"sentence_end");if(!cJSON_IsBool(final))return fail(Protocol,"sentence_finality_missing");
        if(!cJSON_IsTrue(final)){++interims;return 0;}
        const auto* number=unique(sentence,"sentence_id");
        if(!cJSON_IsNumber(number)||!std::isfinite(number->valuedouble)||number->valuedouble<1||number->valuedouble>UINT32_MAX||
           std::floor(number->valuedouble)!=number->valuedouble)return fail(Protocol,"sentence_id_invalid");
        const auto sid=static_cast<uint32_t>(number->valuedouble);const char* text=string(sentence,"text");const size_t n=std::strlen(text);
        char check[TextMax+1];if(!n)return 0;
        if(n>TextMax||!normalize(text,n,check,sizeof(check)))return fail(Protocol,"sentence_utf8_or_size_invalid");
        for(size_t i=0;i<sentence_count;++i)if(sentences[i].id==sid){
            if(std::strcmp(sentences[i].text,text))return fail(Protocol,"conflicting_final_sentence");
            ++duplicates;return 0;
        }
        if(sentence_count==SentenceMax||n>TextMax-total_text)return fail(Limit,"final_text_capacity");
        size_t at=sentence_count;while(at&&sentences[at-1].id>sid){sentences[at]=sentences[at-1];--at;}
        sentences[at].id=sid;std::memcpy(sentences[at].text,text,n+1);++sentence_count;total_text+=n;assemble();return 0;
    }
    size_t sent_bytes(const aoq::Diagnostics& d)const{
        const auto sent=d.sent_objects>0?std::min<uint32_t>(frames,d.sent_objects-1):0;
        return std::min(input_bytes,static_cast<size_t>(sent)*FrameBytes);
    }
    bool transport_complete(const aoq::Diagnostics& d)const{return d.setup_accepted&&!d.error&&run_queued&&finish_queued&&queued_bytes==input_bytes&&d.sent_objects>=frames+2;}
    int queue_one(){
        if(finish_queued||(!task_started&&run_queued))return 0;
        int size=0;size_t frame=0;
        if(!run_queued){
            stage="queue_run_task";
            size=std::snprintf(packet,sizeof(packet),"{\"header\":{\"action\":\"run-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
                "\"payload\":{\"task_group\":\"audio\",\"task\":\"asr\",\"function\":\"recognition\","
                "\"model\":\"qwen-audio-3.1-asr-flash-streaming\",\"parameters\":{\"format\":\"pcm\",\"sample_rate\":16000},\"input\":{}}}",task);
        }else if(queued_bytes<input_bytes){
            stage="sending_data_pcm";frame=std::min(FrameBytes,input_bytes-queued_bytes);size_t encoded_size=0;
            if(mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded),sizeof(encoded),&encoded_size,input.data+queued_bytes,frame))return fail(Limit,"audio_base64_capacity");
            size=std::snprintf(packet,sizeof(packet),"{\"type\":\"input_audio_buffer.append\",\"event_id\":\"%s-audio-%u\",\"audio\":\"%s\"}",task,static_cast<unsigned>(frames),encoded);
            wipe(encoded,sizeof(encoded));
        }else{
            const auto d=session.diagnostics();if(d.queue_bytes||d.sent_objects<frames+1)return 0;
            stage="queue_finish_task";
            size=std::snprintf(packet,sizeof(packet),"{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},\"payload\":{\"input\":{}}}",task);
        }
        if(size<0||static_cast<size_t>(size)>=sizeof(packet))return fail(Limit,"request_capacity");
        const int e=session.send_json(packet,static_cast<size_t>(size));wipe(packet,sizeof(packet));
        if(e==Busy){++busy_retries;return 0;}
        if(e)return fail(e,"request_queue_failed");
        if(!run_queued)run_queued=true;
        else if(frame){queued_bytes+=frame;if(++frames>200)return fail(Limit,"frame_limit");}
        else finish_queued=true;
        return 0;
    }
    int run(){
        Buffer source;if(!source.allocate(SourceCapacity)||!input.allocate(InputSamplesMax*2))return fail(Memory,"pcm_memory");
        const int copied=p4_aoq_tts_trial_copy_pcm(source_generation,source.data,source.capacity,&source_bytes);
        if(copied)return fail(copied,"source_pcm_unavailable");
        if(!source_bytes||source_bytes>SourceMax||(source_bytes&1))return fail(Limit,"source_pcm_duration_or_size");
        source_crc=crc32(source.data,source_bytes);size_t samples=0;
        const int converted=p4_aoq_pcm24_to16(reinterpret_cast<const int16_t*>(source.data),source_bytes/2,
            reinterpret_cast<int16_t*>(input.data),InputSamplesMax,&samples);
        if(converted)return fail(converted,"pcm_conversion_failed");
        if(!samples||samples>InputSamplesMax)return fail(Protocol,"pcm_conversion_size");
        input_bytes=samples*2;input_crc=crc32(input.data,input_bytes);source.clear();
        if(cancelled())return fail(Cancelled,"cancelled");
        if(now_ms()>=deadline)return fail(Timeout,"preparation_deadline");
        uint8_t random[16];esp_fill_random(random,sizeof(random));random[6]=(random[6]&15)|0x40;random[8]=(random[8]&63)|0x80;
        std::snprintf(task,sizeof(task),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7],random[8],random[9],random[10],random[11],random[12],random[13],random[14],random[15]);wipe(random,sizeof(random));
        aoq::Options options;options.deadline_ms=deadline;options.cancelled=stop;options.cancel_userdata=this;
        aoq::Callbacks callbacks{this,data,audio,nullptr};stage="opening_session";
        const int opened=session.open(P4_AOQ_ASR,options,callbacks);if(opened)return fail(opened,"session_open_failed");
        while(!error){
            if(cancelled())return fail(Cancelled,"cancelled");
            if(now_ms()>=deadline)return fail(Timeout,!task_started?"missing_task_started":!task_finished?"missing_task_finished":"missing_final_transcript");
            if(queue_one())return error;
            const int e=session.poll(10);if(e)return fail(e,"session_poll_failed");
            if(cancelled())return fail(Cancelled,"cancelled");
            const auto d=session.diagnostics();
            if(task_finished&&transport_complete(d)&&sentence_count){
                if(!expected_match)return fail(Corrupt,"expected_text_mismatch");
                stage="fixed_asr_pass";return 0;
            }
        }
        return error;
    }
};
const char* boolean(bool b){return b?"true":"false";}
int report(const Trial& t,const aoq::Diagnostics& d,bool pass,char* output,size_t cap){
    char text[TextMax*2+3];if(!quoted(t.transcript,text,sizeof(text)))return Protocol;
    const int n=std::snprintf(output,cap,
        "{\"aoq_asr_trial\":true,\"pass\":%s,\"generation\":%llu,\"source_generation\":%llu,\"stage\":\"%s\",\"error\":%d,"
        "\"session_error\":%d,\"session_stage\":\"%s\",\"setup_accepted\":%s,\"transport_complete\":%s,"
        "\"task_started\":%s,\"task_finished\":%s,\"task_failed\":%s,\"source_bytes_24k\":%u,\"source_crc32\":\"%08x\","
        "\"input_bytes_16k\":%u,\"input_crc32\":\"%08x\",\"audio_queued_bytes\":%u,\"audio_sent_bytes\":%u,"
        "\"frames_queued\":%u,\"finish_queued\":%s,\"final_sentences\":%u,\"final_transcript\":%s,\"expected_text_match\":%s,"
        "\"heartbeat_events\":%u,\"interim_events\":%u,\"duplicate_finals\":%u,\"unmatched_events\":%u,\"ignored_events\":%u,"
        "\"busy_retries\":%u,\"elapsed_ms\":%lld,\"audio_media_sent\":false,\"capture_used\":false,\"playback_used\":false,\"input_retained\":false}",
        boolean(pass),static_cast<unsigned long long>(t.id),static_cast<unsigned long long>(t.source_generation),t.stage,t.error,d.error,d.stage,
        boolean(d.setup_accepted),boolean(t.transport_complete(d)),boolean(t.task_started),boolean(t.task_finished),boolean(t.task_failed),
        static_cast<unsigned>(t.source_bytes),static_cast<unsigned>(t.source_crc),static_cast<unsigned>(t.input_bytes),static_cast<unsigned>(t.input_crc),
        static_cast<unsigned>(t.queued_bytes),static_cast<unsigned>(t.sent_bytes(d)),static_cast<unsigned>(t.frames),boolean(t.finish_queued),
        static_cast<unsigned>(t.sentence_count),text,boolean(t.expected_match),static_cast<unsigned>(t.heartbeats),static_cast<unsigned>(t.interims),
        static_cast<unsigned>(t.duplicates),static_cast<unsigned>(t.unmatched),static_cast<unsigned>(t.ignored),static_cast<unsigned>(t.busy_retries),
        static_cast<long long>(now_ms()-t.started));
    return n<0||static_cast<size_t>(n)>=cap?Limit:0;
}
} // namespace

extern "C" int p4_aoq_asr_trial_command(const char* command,char* output,size_t capacity){
    if(output&&capacity)output[0]=0;
    uint64_t source=0;
    if(!output||capacity<2048||!parse_generation(command,source))return Argument;
    const auto snapshot=cancel_epoch.load(std::memory_order_relaxed);
    Owner guard;if(!guard.held){std::snprintf(output,capacity,"{\"aoq_asr_trial\":true,\"pass\":false,\"error\":3,\"stage\":\"busy\"}");return Busy;}
    if(!run_generation)run_generation=uint64_t(esp_random())<<32;
    if(++run_generation==0)++run_generation;
    auto state=aoq::object_in_psram<Trial>();if(!state)return Memory;
    auto& trial=*state;trial.id=run_generation;trial.source_generation=source;trial.cancel_snapshot=snapshot;
    int error=trial.run();const auto d=trial.session.diagnostics();
    const bool pass=!error&&!trial.error&&!d.error&&trial.transport_complete(d)&&trial.task_started&&trial.task_finished&&
        !trial.task_failed&&trial.sentence_count&&trial.expected_match&&!trial.cancelled();
    if(!pass&&!error)error=trial.fail(trial.cancelled()?Cancelled:Protocol,"final_state_failed");
    trial.session.close();trial.input.clear();
    const int formatting=report(trial,d,pass,output,capacity);
    if(formatting){std::snprintf(output,capacity,"{\"aoq_asr_trial\":true,\"pass\":false,\"error\":5,\"stage\":\"report_capacity\"}");return formatting;}
    return error;
}
extern "C" void p4_aoq_asr_trial_cancel(void){cancel_epoch.fetch_add(1,std::memory_order_relaxed);}
