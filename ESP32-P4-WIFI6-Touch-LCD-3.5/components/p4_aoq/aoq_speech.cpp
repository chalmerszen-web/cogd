#include "aoq_speech.h"
#include "aoq_session.h"
#include "aoq_objects.h"
#include "aoq_heap.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int Argument=1,Busy=3,Limit=5,JsonError=6,Protocol=7,Memory=19;
constexpr size_t QueueMax=512*1024;
uint64_t now_ms(){return uint64_t(esp_timer_get_time()/1000);}
void wipe(void* p,size_t n){auto* b=static_cast<volatile unsigned char*>(p);while(n--)*b++=0;}
const cJSON* field(const cJSON* p,const char* n){return cJSON_GetObjectItemCaseSensitive(p,n);}
const char* str(const cJSON* p,const char* n){const auto* v=field(p,n);return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";}
struct Json{cJSON* p;~Json(){cJSON_Delete(p);}};
struct Blob{
    uint8_t* data=nullptr;size_t size=0,offset=0;bool pcm=false;
    Blob()=default;
    Blob(const void* p,size_t n,bool audio):size(n),pcm(audio){
        data=static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(data)std::memcpy(data,p,n);
    }
    Blob(Blob&& b)noexcept:data(b.data),size(b.size),offset(b.offset),pcm(b.pcm){b.data=nullptr;}
    Blob& operator=(Blob&& b)noexcept{
        if(this!=&b){clear();data=b.data;size=b.size;offset=b.offset;pcm=b.pcm;b.data=nullptr;}return *this;
    }
    Blob(const Blob&)=delete;Blob& operator=(const Blob&)=delete;
    void clear(){if(data){wipe(data,size);heap_caps_free(data);}data=nullptr;size=offset=0;}
    ~Blob(){clear();}
};
}
struct p4_aoq_speech{
    aoq::Session session;bool asr=false,run=false,started=false,text=false,finishing=false,finished=false,terminal=false,tuple=false;
    void* context=nullptr;bool(*cancelled)(void*)=nullptr;
    char task[129]{};int error=0;size_t queued=0,audio_bytes=0;
    uint64_t stream=0,group=0,subgroup=0,next_object=0,finished_at=0,last_pcm=0;
    std::deque<Blob> queue;Blob completion;
    int fail(int e){if(!error)error=e;return error;}
    int enqueue(const void* p,size_t n,bool pcm=false){
        if(!n||n>QueueMax-queued||queue.size()>=2048)return fail(Limit);
        Blob b(p,n,pcm);if(!b.data)return fail(Memory);
        queued+=n;queue.push_back(std::move(b));return 0;
    }
    int data(const aoq::Object& o){
        Json j{cJSON_ParseWithLength(reinterpret_cast<const char*>(o.payload.data),o.payload.size)};
        if(!j.p)return fail(JsonError);
        const auto* header=field(j.p,"header");const auto* event=str(header,"event");
        if(!*event)return 0;
        if(!run||std::strcmp(str(header,"task_id"),task))return fail(Protocol);
        if(!std::strcmp(event,"task-started")){
            if(started||finished)return fail(Protocol);
            started=true;
        } else if(!std::strcmp(event,"task-finished")){
            if(!started||!finishing||finished)return fail(Protocol);
            finished=true;finished_at=now_ms();
            if(!asr){completion=Blob(o.payload.data,o.payload.size,false);return completion.data?0:fail(Memory);}
        } else if(!std::strcmp(event,"result-generated")){
            if(!started||finished)return fail(Protocol);
        } else if(std::strcmp(event,"task-failed"))return 0;
        return enqueue(o.payload.data,o.payload.size);
    }
    int audio(const aoq::Object& o){
        if(asr||!started||!text||terminal||!session.diagnostics().pcm24k_selected)return fail(Protocol);
        aoq_wire::AudioProperties props;
        if(aoq_wire::decode_audio_properties(o.properties.data,o.properties.size,props)!=aoq_wire::ParseStatus::Ok||
           !aoq_wire::pcm_payload_size_matches(props,1,o.payload.size))return fail(Protocol);
        if(!tuple){tuple=true;stream=o.stream;group=o.group;subgroup=o.subgroup;next_object=o.id;}
        if(o.stream!=stream||o.group!=group||o.subgroup!=subgroup||o.id!=next_object||o.id==UINT64_MAX)return fail(Protocol);
        if(o.payload.size>24000u*2*95-audio_bytes)return fail(Limit);
        ++next_object;audio_bytes+=o.payload.size;last_pcm=now_ms();
        return enqueue(o.payload.data,o.payload.size,true);
    }
    int end(const aoq::End& e){
        if(e.alias==202&&tuple&&e.stream==stream&&e.group==group&&e.subgroup==subgroup&&(e.end_group||e.end_track))terminal=true;
        return 0;
    }
    int drain(){
        if(!completion.data||!audio_bytes)return 0;
        const auto d=session.diagnostics();
        const unsigned quiet=d.rtt_ms?unsigned(std::clamp<uint64_t>(uint64_t(d.rtt_ms)*4+100,250,2000)):1000;
        // Some Inference TTS sessions keep the Audio stream open. Match the
        // verified single-task trial policy; never fabricate a transport EOS.
        if(!d.queue_bytes&&d.receive_drained&&(terminal||now_ms()-std::max(last_pcm,finished_at)>=quiet)){
            const int e=enqueue(completion.data,completion.size);if(!e)completion.clear();return e;
        }
        return 0;
    }
};
extern "C" int p4_aoq_speech_open(p4_aoq_speech_t** out,bool asr,void* context,bool(*cancelled)(void*)){
    if(!out||!cancelled)return Argument;
    *out=nullptr;
    void* memory=heap_caps_malloc(sizeof(p4_aoq_speech),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!memory)return Memory;
    auto* s=new(memory)p4_aoq_speech;s->asr=asr;s->context=context;s->cancelled=cancelled;
    aoq::Options options;options.conversation=true;options.lifetime_ms=asr?35000:95000;
    options.cancel_userdata=context;options.cancelled=cancelled;
    aoq::Callbacks callbacks;callbacks.userdata=s;
    callbacks.data=[](void* p,const aoq::Object& o){return static_cast<p4_aoq_speech*>(p)->data(o);};
    callbacks.audio=[](void* p,const aoq::Object& o){return static_cast<p4_aoq_speech*>(p)->audio(o);};
    callbacks.end=[](void* p,const aoq::End& e){return static_cast<p4_aoq_speech*>(p)->end(e);};
    int e=s->session.open(asr?P4_AOQ_ASR:P4_AOQ_TTS,options,callbacks);
    if(e){p4_aoq_speech_close(s);return e;}*out=s;return 0;
}
extern "C" int p4_aoq_speech_send(p4_aoq_speech_t* s,bool binary,const void* input,size_t n){
    if(!s||!input||!n)return Argument;
    if(s->error)return s->error;
    if(binary){
        if(!s->asr||!s->started||s->finishing||n%2||n>2048)return Argument;
        char encoded[2733];size_t written=0;
        if(mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded),sizeof(encoded),&written,static_cast<const unsigned char*>(input),n))return Limit;
        std::string packet="{\"type\":\"input_audio_buffer.append\",\"audio\":\"";packet.append(encoded,written);packet+="\"}";
        int e=s->session.send_json(packet.data(),packet.size());wipe(encoded,sizeof(encoded));wipe(packet.data(),packet.size());return e;
    }
    Json j{cJSON_ParseWithLength(static_cast<const char*>(input),n)};if(!j.p)return JsonError;
    const auto* header=field(j.p,"header");const auto* action=str(header,"action");const char* id=str(header,"task_id");
    const bool run=!std::strcmp(action,"run-task"),finish=!std::strcmp(action,"finish-task"),text=!std::strcmp(action,"continue-task");
    if(!*id||std::strlen(id)>128||(!run&&!finish&&!text)||s->finishing)return Protocol;
    if(run){
        if(s->run)return Protocol;
        auto* payload=cJSON_GetObjectItemCaseSensitive(j.p,"payload");
        if(std::strcmp(str(payload,"task"),s->asr?"asr":"tts")||
           std::strcmp(str(payload,"model"),s->asr?"qwen-audio-3.1-asr-flash-streaming":"qwen-audio-3.0-tts-flash"))return Protocol;
        if(s->asr){
            auto* parameters=cJSON_CreateObject();if(!parameters)return Memory;
            cJSON_AddStringToObject(parameters,"format","pcm");cJSON_AddNumberToObject(parameters,"sample_rate",16000);
            cJSON_ReplaceItemInObjectCaseSensitive(payload,"parameters",parameters);
        }
    } else if(!s->started||std::strcmp(id,s->task)||(text&&s->asr))return Protocol;
    char* serialized=cJSON_PrintUnformatted(j.p);if(!serialized)return Memory;
    int e=s->session.send_json(serialized,std::strlen(serialized));wipe(serialized,std::strlen(serialized));cJSON_free(serialized);
    if(!e){if(run){s->run=true;std::strcpy(s->task,id);}if(text)s->text=true;if(finish)s->finishing=true;}
    return e;
}
extern "C" int p4_aoq_speech_read(p4_aoq_speech_t* s,void* out,size_t cap,p4_aoq_speech_chunk_t* chunk,unsigned wait){
    if(!s||!out||!cap||!chunk||wait>50)return Argument;
    *chunk={};if(s->error)return s->error;
    if(s->queue.empty()){
        int e=s->session.poll(wait);if(e)return s->fail(e);
        if(s->drain())return s->error;
    }
    if(s->queue.empty())return Busy;
    auto& m=s->queue.front();size_t n=std::min(cap,m.size-m.offset);
    std::memcpy(out,m.data+m.offset,n);chunk->length=n;chunk->opcode=m.pcm?2:1;
    chunk->first=!m.offset;m.offset+=n;chunk->final=m.offset==m.size;
    if(chunk->final){s->queued-=m.size;s->queue.pop_front();}return 0;
}
extern "C" void p4_aoq_speech_close(p4_aoq_speech_t* s){if(s){s->session.close();s->~p4_aoq_speech();heap_caps_free(s);}}
