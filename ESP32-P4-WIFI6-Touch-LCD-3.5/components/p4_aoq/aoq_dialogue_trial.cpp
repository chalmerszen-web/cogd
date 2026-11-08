#include "aoq_dialogue_trial.h"
#include "aoq_tts_trial.h"
#include "aoq_session.h"
#include "aoq_objects.h"
#include "aoq_pcm_fixture.h"
#include "aoq_heap.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include "aoq_video_fixture.inc"

namespace {
constexpr int Argument=1,Busy=3,Cancelled=4,Limit=5,JsonError=6,Protocol=7,Server=14,Timeout=15,Memory=19,NotFound=23;
constexpr size_t PcmMax=512*1024,SourceSamplesMax=8*24000,InputSamplesMax=8*16000;
std::atomic_flag owner=ATOMIC_FLAG_INIT;
std::atomic<uint32_t> cancel_epoch{0};
uint64_t generation=0,retained_generation=0;size_t retained_bytes=0;
int64_t now_ms(){return esp_timer_get_time()/1000;}
uint64_t audio_timestamp_us(uint64_t epoch_ms,uint32_t samples){
    // ESP32 size_t is 32 bits. Promote before multiplying, including the first
    // second of input: 4480 * 1000000 already overflows a uint32_t.
    return epoch_ms*1000+uint64_t(samples)*1000000/16000;
}
void wipe(void* data,size_t n){auto* p=static_cast<volatile uint8_t*>(data);while(n--)*p++=0;}
struct Owner {bool held=!owner.test_and_set(std::memory_order_acquire);~Owner(){if(held)owner.clear(std::memory_order_release);}};
struct Buffer {
    uint8_t* data=nullptr;size_t size=0;
    bool allocate(size_t n){data=static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));size=data?n:0;return data;}
    void clear(){if(data){wipe(data,size);heap_caps_free(data);}data=nullptr;size=0;}
    ~Buffer(){clear();}
};
Buffer retained;
Buffer retained_input;
bool capture_next_input=false;
uint64_t retained_input_generation=0;
size_t retained_input_bytes=0;
const cJSON* field(const cJSON* object,const char* key){
    if(!cJSON_IsObject(object))return nullptr;
    const cJSON* out=nullptr;for(const auto* p=object->child;p;p=p->next)if(p->string&&!std::strcmp(p->string,key)){if(out)return nullptr;out=p;}return out;
}
const char* string(const cJSON* object,const char* key){const auto* v=field(object,key);return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";}
struct Json {cJSON* p=nullptr;~Json(){cJSON_Delete(p);}};
bool copy(char* target,size_t capacity,const char* value){const size_t n=std::strlen(value);if(n>=capacity)return false;std::memcpy(target,value,n+1);return true;}
void error_summary(const cJSON* error,char* code,size_t capacity,char* hint,size_t hint_capacity){
    // Never log an arbitrary provider message: it may echo private inputs.
    // Keep a bounded identifier and only a fixed diagnostic vocabulary.
    const char* value=string(error,"code");size_t n=std::strlen(value);
    if(n&&n<capacity){
        bool safe=true;for(size_t i=0;i<n;++i)if(!((value[i]>='a'&&value[i]<='z')||(value[i]>='A'&&value[i]<='Z')||
            (value[i]>='0'&&value[i]<='9')||value[i]=='_'||value[i]=='.'||value[i]=='-'))safe=false;
        if(safe)std::memcpy(code,value,n+1);
    }
    const char* message=string(error,"message");char lower[512]{};
    for(size_t i=0;i+1<sizeof(lower)&&message[i];++i)lower[i]=message[i]>='A'&&message[i]<='Z'?char(message[i]-'A'+'a'):message[i];
    size_t used=0;
    for(const char* word:{"image","jpeg","video","audio","timestamp","buffer","empty","first","base64","decode","format","invalid","negative","before","missing","rate","size","internal"}){
        n=std::strlen(word);if(std::strstr(lower,word)&&used+n+1<hint_capacity){if(used)hint[used++]=' ';std::memcpy(hint+used,word,n+1);used+=n;}
    }
    wipe(lower,sizeof(lower));
}
bool parse_source(const char* command,uint64_t& source){
    source=0;if(!command)return false;
    const char* prefix=!std::strncmp(command,"video red ",10)?"video red ":!std::strncmp(command,"video blue ",11)?"video blue ":
        !std::strncmp(command,"dialogue manual ",16)?"dialogue manual ":"dialogue probe ";
    if(std::strncmp(command,prefix,std::strlen(prefix)))return false;
    const char* p=command+std::strlen(prefix);unsigned count=0;
    while(*p){if(++count>20||*p<'0'||*p>'9'||source>(UINT64_MAX-uint64_t(*p-'0'))/10)return false;source=source*10+uint64_t(*p++-'0');}
    return count&&source;
}
struct Trial {
    aoq::Session session;Buffer input,output;
    const p4_aoq_dialogue_live_ops_t* live=nullptr;
    int video_fixture=-1;int64_t next_video_at=0;
    Buffer camera_jpeg;unsigned camera_sequence=0;uint64_t camera_last_us=0;
    Buffer play_ring;int16_t microphone[320]{};size_t microphone_count=0,play_written=0;
    bool play_opened=false,play_sealed=false,play_joined=false,capture_stopped=false;
    unsigned play_completed=0;int play_finish_error=0;
    int64_t speech_stop_at=0,play_start_at=0;
    uint64_t id=0,source=0;uint32_t cancel_snapshot=0;
    const char* stage="starting";int error=0;
    int64_t started=now_ms(),deadline=started+30000,ready_at=0,uplink_at=0,first_audio_at=0,last_audio_at=0,done_at=0;
    size_t input_samples=0,source_bytes=0,sent_samples=0,output_bytes=0;
    unsigned speech_starts=0,speech_stops=0,pcm_objects=0,peak=0,unmatched=0,userdata_objects=0,userdata_matches=0;
    bool config_queued=false,ready=false,input_done=false,created=false,done=false,terminal=false,drained=false;
    bool manual=false,commit_queued=false,committed=false,create_queued=false;
    bool manual_turn_omitted=false;
    char response[129]{},user_item[129]{},transcript[257]{},answer[257]{};
    char provider_code[65]{},provider_hint[160]{};
    bool camera_config_confirmed=false;
    bool capture_input=false;
    char vad_type[25]{},response_status[16]{};
    int vad_silence_ms=-1;
    int64_t cloud_audio_start_ms=-1,cloud_audio_end_ms=-1;
    uint64_t microphone_abs_sum=0;unsigned microphone_peak=0;
    int64_t microphone_next_at=0,microphone_progress_at=0;
    unsigned microphone_initial_lag_ms=0,microphone_last_lag_ms=0;
    struct Stream {uint64_t stream=0,group=0,subgroup=0,next=0;bool used=false;} streams[8];
    int fail(int value,const char* why){if(!error){error=value;stage=why;}return error;}
    bool cancelled()const{return cancel_epoch.load(std::memory_order_relaxed)!=cancel_snapshot||
        (live&&live->playback.cancelled(live->playback.ctx));}
    void notice(const char* name,const char* text=nullptr){if(live&&live->event)live->event(live->playback.ctx,name,text);}
    bool camera()const{return live&&live->video_frame;}
    bool video()const{return video_fixture>=0||camera();}
    static bool stop(void* p){return static_cast<Trial*>(p)->cancelled();}
    static int data(void* p,const aoq::Object& o){return static_cast<Trial*>(p)->on_data(o);}
    static int audio(void* p,const aoq::Object& o){return static_cast<Trial*>(p)->on_audio(o);}
    static int end(void* p,const aoq::End& e){return static_cast<Trial*>(p)->on_end(e);}
    int on_data(const aoq::Object& object){
        for(size_t i=0;i+6<=object.payload.size;++i)if(!std::memcmp(object.payload.data+i,"\\u0000",6))return fail(Protocol,"escaped_nul_in_event");
        Json j{cJSON_ParseWithLength(reinterpret_cast<const char*>(object.payload.data),object.payload.size)};
        if(!j.p)return fail(JsonError,"event_json_invalid");
        const char* type=string(j.p,"type");
        if(!std::strcmp(type,"error")){
            error_summary(field(j.p,"error"),provider_code,sizeof(provider_code),provider_hint,sizeof(provider_hint));
            return fail(Server,"provider_error_event");
        }
        if(!std::strcmp(type,"session.updated")){
            const auto* settings=field(j.p,"session");
            const auto* vad=field(settings,"turn_detection");
            copy(vad_type,sizeof(vad_type),string(vad,"type"));
            const auto* silence=field(vad,"silence_duration_ms");
            if(cJSON_IsNumber(silence)&&silence->valuedouble>=0&&silence->valuedouble<=60000)vad_silence_ms=silence->valueint;
            if(!config_queued||std::strcmp(string(settings,"model"),video()?"qwen3.8-omni-flash-realtime":"qwen-audio-3.1-realtime-flash")||
               std::strcmp(string(settings,"voice"),video()?"Tina":"longanqian_v3.1")||std::strcmp(string(settings,"output_audio_format"),"pcm"))return fail(Protocol,"model_configuration_mismatch");
            /* The actual AOQ service omits this field after an explicit null
             * update. Omission is provisional: the trial still rejects any
             * automatic turn and requires commit ACK before response.create. */
            if(manual&&vad&&!cJSON_IsNull(vad))return fail(Protocol,"manual_turn_configuration_mismatch");
            if(manual)manual_turn_omitted=vad==nullptr;
            if(camera()){
                const auto* vad=field(settings,"turn_detection");const auto* silence=field(vad,"silence_duration_ms");
                const auto* threshold=field(vad,"threshold");
                if(std::strcmp(string(vad,"type"),"server_vad")||!cJSON_IsNumber(silence)||silence->valuedouble!=900||
                   !cJSON_IsNumber(threshold)||threshold->valuedouble!=.1||
                   std::strcmp(string(field(settings,"input_audio_transcription"),"model"),"qwen3-asr-flash-realtime"))
                    return fail(Protocol,"camera_turn_configuration_mismatch");
                camera_config_confirmed=true;
            }
            if(!ready){ready=true;ready_at=now_ms();notice("aoq_ready");}return 0;
        }
        if(!std::strcmp(type,"input_audio_buffer.speech_started")){
            if(manual)return fail(Protocol,"automatic_turn_in_manual_mode");
            if(!ready||!sent_samples||++speech_starts>1)return fail(Protocol,"unexpected_extra_turn");
            if(!copy(user_item,sizeof(user_item),string(j.p,"item_id"))||!user_item[0])return fail(Protocol,"user_item_missing");
            const auto* stamp=field(j.p,"audio_start_ms");
            if(cJSON_IsNumber(stamp)&&stamp->valuedouble>=0&&stamp->valuedouble<=9007199254740991.0)cloud_audio_start_ms=int64_t(stamp->valuedouble);
            return 0;
        }
        if(!std::strcmp(type,"input_audio_buffer.speech_stopped")){
            if(manual)return fail(Protocol,"automatic_turn_in_manual_mode");
            if(!ready||speech_starts!=1||!user_item[0]||std::strcmp(user_item,string(j.p,"item_id")))return fail(Protocol,"speech_stop_item_mismatch");
            if(speech_stops)return 0; // A duplicate must not replay the cue or reset latency.
            ++speech_stops;speech_stop_at=now_ms();
            const auto* stamp=field(j.p,"audio_end_ms");
            if(cJSON_IsNumber(stamp)&&stamp->valuedouble>=0&&stamp->valuedouble<=9007199254740991.0)cloud_audio_end_ms=int64_t(stamp->valuedouble);
            if(live){input_done=true;notice("aoq_speech_stopped");}return 0;
        }
        if(!std::strcmp(type,"input_audio_buffer.committed")&&manual){
            const char* item=string(j.p,"item_id");
            if(!commit_queued||!input_done||!item[0])return fail(Protocol,"unexpected_manual_commit");
            if(committed)return std::strcmp(user_item,item)?fail(Protocol,"multiple_manual_commits"):0;
            if(!copy(user_item,sizeof(user_item),item))return fail(Limit,"user_item_capacity");
            committed=true;return 0;
        }
        if(!std::strcmp(type,"conversation.item.input_audio_transcription.completed")){
            if(std::strcmp(user_item,string(j.p,"item_id"))){++unmatched;return 0;}
            if(!copy(transcript,sizeof(transcript),string(j.p,"transcript")))return fail(Limit,"transcript_capacity");
            if(manual&&!transcript[std::strspn(transcript," \t\r\n")])return fail(Protocol,"manual_empty_transcript");
            notice("aoq_input_text",transcript);return 0;
        }
        if(!std::strcmp(type,"response.created")){
            if(manual&&!create_queued)return fail(Protocol,"response_before_manual_create");
            const char* identity=string(field(j.p,"response"),"id");
            if(!identity[0]||!ready||!sent_samples)return fail(Protocol,"response_before_input");
            if(created){if(std::strcmp(response,identity))return fail(Protocol,"multiple_responses");return 0;}
            if(!copy(response,sizeof(response),identity))return fail(Limit,"response_identity_capacity");
            created=true;return 0;
        }
        if(!std::strcmp(type,"response.audio_transcript.done")){
            if(!created||std::strcmp(response,string(j.p,"response_id"))){++unmatched;return 0;}
            if(!copy(answer,sizeof(answer),string(j.p,"transcript")))return fail(Limit,"answer_capacity");
            notice("aoq_answer_text",answer);return 0;
        }
        if(!std::strcmp(type,"response.function_call_arguments.done"))return fail(Protocol,"unexpected_tool_call");
        if(!std::strcmp(type,"response.done")){
            const auto* value=field(j.p,"response");
            if(!created||std::strcmp(response,string(value,"id"))){++unmatched;return 0;}
            const char* status=string(value,"status");
            copy(response_status,sizeof(response_status),!std::strcmp(status,"completed")?"completed":!std::strcmp(status,"cancelled")?"cancelled":!std::strcmp(status,"failed")?"failed":"other");
            if(std::strcmp(status,"completed")){
                error_summary(field(field(value,"status_details"),"error"),provider_code,sizeof(provider_code),provider_hint,sizeof(provider_hint));
                return fail(Server,"response_not_completed");
            }
            done=true;done_at=now_ms();return 0;
        }
        return 0;
    }
    int on_audio(const aoq::Object& object){
        if(play_sealed)return fail(Protocol,"audio_after_live_playback_sealed");
        if(!ready||!sent_samples||!session.diagnostics().pcm24k_selected)return fail(Protocol,"audio_before_ready_input");
        // Fresh allocation + exactly one permitted input/response. Media may
        // arrive before response.created across tracks; it is buffered, never
        // relabelled as the latest response in a reusable conversation.
        Stream* stream=nullptr;
        for(auto& s:streams)if(s.used&&s.stream==object.stream){stream=&s;break;}
        // Until cross-segment ordering has been verified against this model,
        // live playback admits one stream only. Never play arrival-ordered
        // independent streams as if they were one continuous response.
        if(live&&!stream&&streams[0].used)return fail(Protocol,"live_multiple_audio_streams");
        if(!stream)for(auto& s:streams)if(!s.used){s={object.stream,object.group,object.subgroup,object.id,true};stream=&s;break;}
        if(!stream)return fail(Limit,"audio_stream_capacity");
        if(stream->group!=object.group||stream->subgroup!=object.subgroup||stream->next!=object.id)return fail(Protocol,"audio_object_gap_or_tuple");
        aoq_wire::AudioProperties props;
        if(aoq_wire::decode_audio_properties(object.properties.data,object.properties.size,props)!=aoq_wire::ParseStatus::Ok||
           !aoq_wire::pcm_payload_size_matches(props,1,object.payload.size))return fail(Protocol,"invalid_audio_format");
        if(object.payload.size>PcmMax-output_bytes)return fail(Limit,"audio_capacity");
        std::memcpy(output.data+output_bytes,object.payload.data,object.payload.size);output_bytes+=object.payload.size;++pcm_objects;++stream->next;
        for(size_t i=0;i+1<object.payload.size;i+=2){const int16_t value=int16_t(uint16_t(object.payload.data[i])|(uint16_t(object.payload.data[i+1])<<8));peak=std::max(peak,unsigned(value<0?-int(value):value));}
        if(!first_audio_at){first_audio_at=now_ms();notice("aoq_first_audio");}
        last_audio_at=now_ms();drained=false;
        if(object.opaque_userdata.size){
            ++userdata_objects;
            if(created&&object.opaque_userdata.size==std::strlen(response)&&!std::memcmp(object.opaque_userdata.data,response,object.opaque_userdata.size))++userdata_matches;
        }
        return 0;
    }
    int on_end(const aoq::End& event){
        if(event.alias!=202||(!event.end_group&&!event.end_track))return 0;
        for(const auto& s:streams)if(s.used&&s.stream==event.stream&&s.group==event.group&&s.subgroup==event.subgroup)terminal=true;
        return 0;
    }
    bool complete()const{
        const auto d=session.diagnostics();return !error&&!d.error&&ready&&input_done&&
            (manual?(committed&&create_queued):(speech_starts==1&&speech_stops))&&
            created&&done&&transcript[0]&&answer[0]&&output_bytes&&peak>256&&drained&&
            (video_fixture<0||(d.video_frames_sent>=2&&!d.video_pending&&video_answer_matches()))&&
            (!camera()||(d.video_frames_sent&&!d.video_pending));
    }
    bool video_answer_matches()const{
        return video_fixture==0?(std::strstr(answer,"红")&&(std::strstr(answer,"方")||std::strstr(answer,"矩形"))):
            video_fixture==1?(std::strstr(answer,"蓝")&&std::strstr(answer,"圆")):false;
    }
    int configure(){
        constexpr const char request[]=R"({"event_id":"native-fixed-config","type":"session.update","session":{"modalities":["text","audio"],"voice":"longanqian_v3.1","input_audio_format":"pcm","output_audio_format":"pcm","instructions":"这是设备链路测试。听到用户说话后，只回答收到，不要进行其他操作。","turn_detection":{"type":"server_vad","silence_duration_ms":600},"tools":[],"enable_search":false,"max_history_turns":1}})";
        constexpr const char live_request[]=R"({"event_id":"native-live-config","type":"session.update","session":{"modalities":["text","audio"],"voice":"longanqian_v3.1","input_audio_format":"pcm","output_audio_format":"pcm","instructions":"你是COGD语音助手。请用简短中文直接回答用户的问题，每次回答不超过六十字。当前是单轮语音链路测试，不调用工具，不假称操作了设备。","turn_detection":{"type":"server_vad","silence_duration_ms":600},"tools":[],"enable_search":false,"max_history_turns":1}})";
        constexpr const char video_request[]=R"({"event_id":"native-video-config","type":"session.update","session":{"modalities":["text","audio"],"voice":"Tina","input_audio_format":"pcm","output_audio_format":"pcm","instructions":"这是视频理解链路测试。听到用户说话后，只用一句简短中文描述当前画面中图形的颜色和形状，不复述用户的话，不调用工具。","turn_detection":{"type":"server_vad","silence_duration_ms":600},"tools":[]}})";
        constexpr const char camera_request[]=R"({"event_id":"native-camera-config","type":"session.update","session":{"modalities":["text","audio"],"voice":"Tina","input_audio_format":"pcm","output_audio_format":"pcm","input_audio_transcription":{"model":"qwen3-asr-flash-realtime"},"instructions":"你是COGD助手。根据用户说话和当前摄像头画面，用简短中文直接回答，不超过六十字。看不清时说明，不猜测细小文字。当前是单轮音视频链路测试，不调用工具，不假称操作了设备。","turn_detection":{"type":"server_vad","threshold":0.1,"silence_duration_ms":900},"tools":[]}})";
        Json manual_request;
        const char* text=camera()?camera_request:video_fixture>=0?video_request:live?live_request:request;
        char manual_text[1024];
        if(manual){
            manual_request.p=cJSON_Parse(request);if(!manual_request.p)return fail(Memory,"manual_configuration_memory");
            auto* settings=cJSON_GetObjectItemCaseSensitive(manual_request.p,"session");
            if(!cJSON_ReplaceItemInObjectCaseSensitive(settings,"turn_detection",cJSON_CreateNull())||
               !cJSON_PrintPreallocated(manual_request.p,manual_text,sizeof(manual_text),false))return fail(Limit,"manual_configuration_capacity");
            text=manual_text;
        }
        const int e=session.send_json(text,std::strlen(text));if(e==Busy)return 0;if(e)return fail(e,"configuration_send_failed");config_queued=true;return 0;
    }
    int manual_control(){
        if(!manual||!ready||!input_done)return 0;
        const char* text=!commit_queued?"{\"type\":\"input_audio_buffer.commit\"}":
            committed&&transcript[0]&&!create_queued?"{\"type\":\"response.create\"}":nullptr;
        if(!text)return 0;
        const int e=session.send_json(text,std::strlen(text));
        if(e==Busy)return 0;
        if(e)return fail(e,"manual_control_send_failed");
        if(!commit_queued)commit_queued=true;else create_queued=true;
        return 0;
    }
    int send_video(){
        if(!video()||!ready||input_done||now_ms()<next_video_at)return 0;
        // Omni rejects images before audio has reached its input buffer. A
        // locally accepted DATAGRAM is not delivery confirmation; wait for
        // the matching cloud speech-start event rather than a guessed delay.
        // Camera capture continues locally and only its newest frame is used.
        if(!speech_starts)return 0;
        const auto* bytes=video_fixture==0?fixture_red_square:fixture_blue_circle;
        size_t size=video_fixture==0?sizeof(fixture_red_square):sizeof(fixture_blue_circle);
        uint64_t timestamp=uint64_t(now_ms())*1000;unsigned sequence=camera_sequence;
        if(camera()){
            const int e=live->video_frame(live->playback.ctx,camera_sequence,camera_jpeg.data,camera_jpeg.size,&size,&sequence,&timestamp);
            if(e==Busy)return 0;
            if(e)return fail(e,"camera_frame_failed");
            const uint64_t now=uint64_t(now_ms())*1000;
            if(!size||size>camera_jpeg.size||sequence<=camera_sequence||timestamp>now||now-timestamp>1500000||timestamp<=camera_last_us)
                return fail(Protocol,"camera_frame_identity_or_age");
            // During microphone catch-up, wait until uploaded audio covers
            // this current camera timestamp. Never retime or reuse old images.
            if(timestamp>live->capture_origin_us+uint64_t(sent_samples)*1000000/16000)return 0;
            bytes=camera_jpeg.data;
        }
        const int e=session.send_jpeg(bytes,size,timestamp);
        if(e==Busy)return 0;
        if(e)return fail(e,"video_uplink_failed");
        if(camera()){camera_sequence=sequence;camera_last_us=timestamp;notice("aoq_camera_frame");}
        next_video_at=now_ms()+1000;return 0;
    }
    int send_microphone(){
        if(!ready||input_done)return 0;
        const int64_t now=now_ms();
        if(!uplink_at){
            uplink_at=now;microphone_progress_at=now;
            microphone_initial_lag_ms=unsigned(uint64_t(now)-live->capture_origin_us/1000);
        }
        const uint64_t timestamp=live->capture_origin_us+uint64_t(sent_samples)*1000000/16000;
        // Capture already ran during allocation. Preserve every queued sample
        // and its original time instead of imposing a second real-time delay.
        // At most one 20ms frame per 5ms; never send future capture timestamps.
        if(now<microphone_next_at||timestamp>uint64_t(now)*1000)return 0;
        if(now-microphone_progress_at>200)return fail(Timeout,"microphone_progress_stall");
        if(sent_samples>=16*16000)return fail(Limit,"microphone_utterance_limit");
        if(microphone_count<320){
            size_t got=0;const int e=live->read(live->playback.ctx,microphone+microphone_count,320-microphone_count,&got);
            if(e)return fail(e,"microphone_read_failed");
            if(got>320-microphone_count)return fail(Protocol,"microphone_count_invalid");
            microphone_count+=got;if(microphone_count<320)return 0;
        }
        const int e=session.send_pcm16(microphone,320,timestamp);
        if(e==Busy)return 0;
        if(e)return fail(e,"microphone_send_failed");
        if(capture_input)std::memcpy(input.data+sent_samples*2,microphone,640);
        for(const int16_t sample:microphone){
            const unsigned magnitude=unsigned(sample<0?-int(sample):sample);
            microphone_abs_sum+=magnitude;microphone_peak=std::max(microphone_peak,magnitude);
        }
        microphone_last_lag_ms=unsigned(uint64_t(now)-timestamp/1000);
        microphone_next_at=now+5;microphone_progress_at=now;
        sent_samples+=320;input_samples=sent_samples;microphone_count=0;return 0;
    }
    int pump_playback(){
        if(!live||!speech_stop_at)return 0;
        const auto& p=live->playback;
        if(!play_opened){
            // Start the existing audio owner's cue/warmup at the endpoint,
            // concurrently with cloud inference, not after the first reply.
            const int e=p.open(p.ctx,24000,play_ring.data,play_ring.size);if(e)return fail(e,"live_playback_open_failed");
            play_opened=true;
            notice("aoq_endpoint_cue_requested");
            // Open first to retain the active session's 80ms prebuffer policy.
            // No microphone/camera samples are admitted after this endpoint.
            live->stop_capture(p.ctx);capture_stopped=true;
        }
        if(!created||!output_bytes)return 0;
        size_t count=output_bytes/2-play_written;
        const auto space=p.space(p.ctx);
        if(space>play_ring.size/2)return fail(Protocol,"live_playback_space_invalid");
        count=std::min<size_t>({count,space,960});
        if(count){
            const int e=p.write(p.ctx,reinterpret_cast<const int16_t*>(output.data)+play_written,count);
            if(e)return fail(e,"live_playback_write_failed");
            play_written+=count;
        }
        if(drained&&play_written==output_bytes/2&&!play_sealed){p.seal(p.ctx);play_sealed=true;}
        play_completed=p.played(p.ctx);
        if(play_completed>play_written)return fail(Protocol,"live_playback_counts_invalid");
        if(play_completed&&!play_start_at){play_start_at=now_ms();notice("aoq_playback_started");}
        return 0;
    }
    void cleanup_live(){
        if(!live)return;
        const auto& p=live->playback;
        // The audio board's task owns both capture and output. Its stop request
        // does not release the borrowed speaker ring; finish is still required.
        if(!capture_stopped){live->stop_capture(p.ctx);capture_stopped=true;}
        if(play_opened){
            play_finish_error=p.finish(p.ctx,error);play_joined=true;play_completed=p.played(p.ctx);
            if(play_finish_error)fail(play_finish_error,"live_playback_join_failed");
            if(!error&&(play_completed!=output_bytes/2||p.remaining(p.ctx)))fail(Protocol,"live_playback_incomplete");
        }
        if(!error&&cancelled())fail(Cancelled,"cancelled");
    }
    int send_audio(){
        if(!ready||input_done)return 0;
        if(!uplink_at)uplink_at=now_ms();
        const int64_t due=uplink_at+static_cast<int64_t>(sent_samples)*1000/16000;
        if(now_ms()<due)return 0;
        if(now_ms()-due>200)return fail(Timeout,"audio_pacing_stall");
        // Explicit 100ms leading + 1600ms trailing silence; no microphone data.
        constexpr size_t leading=1600,trailing=25600;
        const size_t total=leading+input_samples+trailing;
        if(sent_samples>=total){input_done=true;return 0;}
        const size_t count=std::min<size_t>(320,total-sent_samples);int16_t frame[320]{};
        const auto* pcm=reinterpret_cast<const int16_t*>(input.data);
        for(size_t i=0;i<count;++i){const size_t at=sent_samples+i;if(at>=leading&&at<leading+input_samples)frame[i]=pcm[at-leading];}
        const uint64_t timestamp=audio_timestamp_us(static_cast<uint64_t>(uplink_at),static_cast<uint32_t>(sent_samples));
        const int e=session.send_pcm16(frame,count,timestamp);wipe(frame,sizeof(frame));
        if(e==Busy)return 0;
        if(e)return fail(e,"audio_uplink_failed");
        sent_samples+=count;return 0;
    }
    int run(){
        int e=0;
        if(!output.allocate(PcmMax))return fail(Memory,"pcm_memory");
        if(camera()&&!camera_jpeg.allocate(65536))return fail(Memory,"camera_frame_memory");
        if(live){
            if(!play_ring.allocate(P4_AOQ_PLAY_RING_BYTES))return fail(Memory,"play_ring_memory");
            if(capture_input&&!input.allocate(16*16000*2))return fail(Memory,"input_capture_memory");
        }
        else {
            Buffer original;if(!original.allocate(PcmMax)||!input.allocate(InputSamplesMax*2))return fail(Memory,"pcm_memory");
            e=p4_aoq_tts_trial_copy_pcm(source,original.data,original.size,&source_bytes);if(e)return fail(e,"source_pcm_unavailable");
            if(!source_bytes||(source_bytes&1)||source_bytes/2>SourceSamplesMax)return fail(Limit,"source_pcm_size");
            e=p4_aoq_pcm24_to16(reinterpret_cast<const int16_t*>(original.data),source_bytes/2,
                reinterpret_cast<int16_t*>(input.data),InputSamplesMax,&input_samples);if(e)return fail(e,"resample_failed");
        }
        aoq::Options options;options.deadline_ms=deadline;options.cancelled=stop;options.cancel_userdata=this;options.trace_metadata=live==nullptr;
        if(video()){options.video_width=320;options.video_height=256;}
        aoq::Callbacks callbacks{this,data,audio,end};stage="opening_session";
        e=session.open(video()?P4_AOQ_OMNI:P4_AOQ_AUDIO,options,callbacks);if(e)return fail(e,"session_open_failed");
        while(!error){
            if(cancelled())return fail(Cancelled,"cancelled");
            if(now_ms()>=deadline)return fail(Timeout,!ready?"missing_session_updated":!created?"missing_response":!done?"missing_response_done":"incomplete_response_media");
            if(!config_queued&&configure())return error;
            if(send_video())return error;
            if(live?send_microphone():send_audio())return error;
            if(manual_control())return error;
            e=session.poll(5);if(e)return fail(e,"session_poll_failed");
            const auto d=session.diagnostics();
            const auto guard=d.rtt_ms?std::clamp<uint64_t>(uint64_t(d.rtt_ms)*4+100,250,2000):1000;
            // A terminal belongs to one segment, not to every segment of a
            // response. Require the explicit bounded receive-drain policy
            // even if one stream has ended. This is not a cross-track fence.
            if(done&&output_bytes&&d.receive_drained&&now_ms()-std::max(done_at,last_audio_at)>=static_cast<int64_t>(guard))drained=true;
            if(pump_playback())return error;
            if(complete()&&(!live||(play_sealed&&play_completed==output_bytes/2&&!live->playback.remaining(live->playback.ctx)))){
                stage=live?"live_single_turn_played":"single_turn_media_received_not_played";return 0;
            }
        }
        return error;
    }
};
int report(const Trial& t,const aoq::Diagnostics& d,bool pass,char* out,size_t capacity){
    Json j{cJSON_CreateObject()};if(!j.p)return Memory;
    cJSON_AddBoolToObject(j.p,t.video_fixture>=0?"aoq_video_trial":t.live?"aoq_live_dialogue":"aoq_dialogue_trial",true);cJSON_AddBoolToObject(j.p,"pass",pass);
    // Preserve uint64 generations exactly in JSON (cJSON number is double).
    char id[24],source[24];std::snprintf(id,sizeof(id),"%llu",static_cast<unsigned long long>(t.id));std::snprintf(source,sizeof(source),"%llu",static_cast<unsigned long long>(t.source));
    cJSON_AddRawToObject(j.p,"generation",id);cJSON_AddRawToObject(j.p,"source_generation",source);
    cJSON_AddStringToObject(j.p,"stage",t.stage);cJSON_AddNumberToObject(j.p,"error",t.error);
    cJSON_AddStringToObject(j.p,"session_stage",d.stage);cJSON_AddNumberToObject(j.p,"session_error",d.error);
    if(t.provider_code[0])cJSON_AddStringToObject(j.p,"provider_error_code",t.provider_code);
    if(t.provider_hint[0])cJSON_AddStringToObject(j.p,"provider_error_hint",t.provider_hint);
    cJSON_AddBoolToObject(j.p,"ready",t.ready);cJSON_AddBoolToObject(j.p,"pcm16k_input_selected",d.pcm16k_input_selected);
    cJSON_AddNumberToObject(j.p,"input_samples",t.input_samples);cJSON_AddNumberToObject(j.p,"uplink_samples",d.audio_tx_samples);
    cJSON_AddNumberToObject(j.p,"audio_datagrams_sent",d.audio_datagrams_sent);cJSON_AddBoolToObject(j.p,"input_done",t.input_done);
    cJSON_AddNumberToObject(j.p,"speech_starts",t.speech_starts);cJSON_AddNumberToObject(j.p,"speech_stops",t.speech_stops);
    if(t.manual){
        cJSON_AddBoolToObject(j.p,"manual_turn_omitted",t.manual_turn_omitted);
        cJSON_AddBoolToObject(j.p,"manual_mode",true);cJSON_AddBoolToObject(j.p,"commit_queued",t.commit_queued);
        cJSON_AddBoolToObject(j.p,"commit_acknowledged",t.committed);cJSON_AddBoolToObject(j.p,"create_queued",t.create_queued);
    }
    cJSON_AddBoolToObject(j.p,"response_created",t.created);cJSON_AddBoolToObject(j.p,"response_done",t.done);
    cJSON_AddStringToObject(j.p,"input_transcript",t.transcript);cJSON_AddStringToObject(j.p,"answer",t.answer);
    cJSON_AddNumberToObject(j.p,"pcm_bytes",t.output_bytes);cJSON_AddNumberToObject(j.p,"pcm_objects",t.pcm_objects);cJSON_AddNumberToObject(j.p,"pcm_peak",t.peak);
    cJSON_AddNumberToObject(j.p,"audio_userdata_objects",t.userdata_objects);cJSON_AddNumberToObject(j.p,"userdata_exact_response_matches",t.userdata_matches);
    cJSON_AddNumberToObject(j.p,"ready_ms",t.ready_at?t.ready_at-t.started:0);cJSON_AddNumberToObject(j.p,"first_audio_ms",t.first_audio_at?t.first_audio_at-t.started:0);
    cJSON_AddNumberToObject(j.p,"elapsed_ms",now_ms()-t.started);cJSON_AddBoolToObject(j.p,"audio_terminal",t.terminal);cJSON_AddBoolToObject(j.p,"model_receive_drained",t.drained);
    cJSON_AddBoolToObject(j.p,"pcm_retained",pass&&!t.live);cJSON_AddBoolToObject(j.p,"capture_used",t.live!=nullptr);cJSON_AddBoolToObject(j.p,"playback_used",t.play_opened);
    if(t.live){
        cJSON_AddBoolToObject(j.p,"joined",t.play_joined);cJSON_AddNumberToObject(j.p,"played_samples",t.play_completed);
        cJSON_AddNumberToObject(j.p,"input_peak",t.microphone_peak);
        cJSON_AddNumberToObject(j.p,"input_mean_abs",t.sent_samples?t.microphone_abs_sum/t.sent_samples:0);
        cJSON_AddNumberToObject(j.p,"initial_capture_lag_ms",t.microphone_initial_lag_ms);
        cJSON_AddNumberToObject(j.p,"last_capture_lag_ms",t.microphone_last_lag_ms);
        cJSON_AddNumberToObject(j.p,"speech_stop_ms",t.speech_stop_at?t.speech_stop_at-t.started:0);
        cJSON_AddNumberToObject(j.p,"play_start_ms",t.play_start_at?t.play_start_at-t.started:0);
        cJSON_AddStringToObject(j.p,"vad_type",t.vad_type);cJSON_AddNumberToObject(j.p,"vad_silence_ms",t.vad_silence_ms);
        cJSON_AddNumberToObject(j.p,"cloud_audio_start_ms",double(t.cloud_audio_start_ms));
        cJSON_AddNumberToObject(j.p,"cloud_audio_end_ms",double(t.cloud_audio_end_ms));
        cJSON_AddStringToObject(j.p,"response_status",t.response_status);
        cJSON_AddBoolToObject(j.p,"input_capture_retained",t.capture_input&&t.sent_samples);
        cJSON_AddBoolToObject(j.p,"human_verified",false);
    }
    if(t.video_fixture>=0){
        cJSON_AddStringToObject(j.p,"video_fixture",t.video_fixture==0?"red_square":"blue_circle");
        cJSON_AddBoolToObject(j.p,"camera_used",false);cJSON_AddBoolToObject(j.p,"jpeg_selected",d.jpeg_selected);
        cJSON_AddNumberToObject(j.p,"video_frames_sent",d.video_frames_sent);cJSON_AddNumberToObject(j.p,"video_bytes",d.video_tx_bytes);
        cJSON_AddBoolToObject(j.p,"fixture_answer_match",t.video_answer_matches());
    }
    if(t.camera()){
        cJSON_AddBoolToObject(j.p,"camera_used",true);cJSON_AddNumberToObject(j.p,"camera_sequence",t.camera_sequence);
        cJSON_AddNumberToObject(j.p,"video_frames_sent",d.video_frames_sent);cJSON_AddNumberToObject(j.p,"video_bytes",d.video_tx_bytes);
        cJSON_AddBoolToObject(j.p,"camera_config_confirmed",t.camera_config_confirmed);
    }
    return cJSON_PrintPreallocated(j.p,out,static_cast<int>(capacity),false)?0:Limit;
}
}
extern "C" int p4_aoq_dialogue_trial_command(const char* command,char* out,size_t capacity){
    if(out&&capacity)out[0]=0;
    uint64_t source=0;
    if(!out||capacity<2048||!parse_source(command,source))return Argument;
    const auto snapshot=cancel_epoch.load(std::memory_order_relaxed);Owner guard;if(!guard.held)return Busy;
    retained.clear();retained_bytes=0;retained_generation=0;
    if(!generation)generation=uint64_t(esp_random())<<32;
    if(++generation==0)++generation;
    auto state=aoq::object_in_psram<Trial>();if(!state)return Memory;
    auto& trial=*state;trial.id=generation;trial.source=source;trial.cancel_snapshot=snapshot;
    trial.manual=!std::strncmp(command,"dialogue manual ",16);
    if(!std::strncmp(command,"video ",6))trial.video_fixture=command[6]=='r'?0:1;
    int error=trial.run();const auto d=trial.session.diagnostics();
    const bool pass=!error&&trial.complete()&&!trial.cancelled();if(!pass&&!error)error=trial.fail(Cancelled,"cancelled");
    trial.session.close();trial.input.clear();
    if(pass){retained.data=trial.output.data;retained.size=trial.output.size;trial.output.data=nullptr;trial.output.size=0;retained_bytes=trial.output_bytes;retained_generation=trial.id;}
    const int format=report(trial,d,pass,out,capacity);if(format){retained.clear();retained_bytes=0;retained_generation=0;out[0]=0;return format;}return error;
}
extern "C" void p4_aoq_dialogue_trial_cancel(void){cancel_epoch.fetch_add(1,std::memory_order_relaxed);}
extern "C" int p4_aoq_dialogue_input_command(const char* command,char* out,size_t capacity){
    if(!command||!out||capacity<2048)return Argument;
    out[0]=0;Owner guard;if(!guard.held)return Busy;
    if(!std::strcmp(command,"capture")){
        retained_input.clear();retained_input_bytes=0;retained_input_generation=0;capture_next_input=true;
    }else if(!std::strcmp(command,"free")){
        retained_input.clear();retained_input_bytes=0;retained_input_generation=0;capture_next_input=false;
    }else if(!std::strncmp(command,"read ",5)){
        const char* p=command+5;
        auto number=[&p](uint64_t& value){
            value=0;unsigned digits=0;
            while(*p>='0'&&*p<='9'){
                if(++digits>20||value>(UINT64_MAX-uint64_t(*p-'0'))/10)return false;
                value=value*10+uint64_t(*p++-'0');
            }
            return digits!=0;
        };
        uint64_t expected=0,offset=0;
        if(!number(expected)||*p++!=' '||!number(offset)||*p)return Argument;
        if(!expected||expected!=retained_input_generation||!retained_input.data)return NotFound;
        if(offset>=retained_input_bytes)return Limit;
        const size_t count=std::min<size_t>(768,retained_input_bytes-size_t(offset));
        int used=std::snprintf(out,capacity,"{\"generation\":%llu,\"offset\":%u,\"bytes\":%u,\"hex\":\"",
            static_cast<unsigned long long>(expected),unsigned(offset),unsigned(retained_input_bytes));
        constexpr char hex[]="0123456789abcdef";
        for(size_t i=0;i<count;++i){const uint8_t v=retained_input.data[size_t(offset)+i];out[used++]=hex[v>>4];out[used++]=hex[v&15];}
        std::memcpy(out+used,"\"}",3);return 0;
    }else if(std::strcmp(command,"status"))return Argument;
    std::snprintf(out,capacity,"{\"armed\":%s,\"generation\":%llu,\"bytes\":%u,\"rate\":16000,\"channels\":1}",
        capture_next_input?"true":"false",static_cast<unsigned long long>(retained_input_generation),unsigned(retained_input_bytes));
    return 0;
}
extern "C" int p4_aoq_dialogue_live(const p4_aoq_dialogue_live_ops_t* ops,char* out,size_t capacity){
    if(out&&capacity)out[0]=0;
    if(!ops||!out||capacity<2048||!ops->read||!ops->stop_capture)return Argument;
    const uint64_t now_us=uint64_t(now_ms())*1000;
    if(!ops->capture_origin_us||ops->capture_origin_us>now_us||now_us-ops->capture_origin_us>6000000)return Argument;
    const auto& p=ops->playback;
    if(!p.open||!p.space||!p.write||!p.remaining||!p.played||!p.seal||!p.finish||!p.cancelled)return Argument;
    const auto snapshot=cancel_epoch.load(std::memory_order_relaxed);Owner guard;if(!guard.held)return Busy;
    retained.clear();retained_bytes=0;retained_generation=0;
    auto state=aoq::object_in_psram<Trial>();if(!state)return Memory;
    if(!generation)generation=uint64_t(esp_random())<<32;
    if(++generation==0)++generation;
    auto& trial=*state;trial.id=generation;trial.live=ops;trial.cancel_snapshot=snapshot;
    retained_input.clear();retained_input_bytes=0;retained_input_generation=0;
    trial.capture_input=capture_next_input;capture_next_input=false;
    trial.notice("aoq_connecting");trial.run();const auto d=trial.session.diagnostics();
    trial.session.close();trial.cleanup_live();
    if(trial.capture_input&&trial.sent_samples){
        retained_input.data=trial.input.data;retained_input.size=trial.input.size;
        trial.input.data=nullptr;trial.input.size=0;
        retained_input_bytes=trial.sent_samples*2;retained_input_generation=trial.id;
    }
    const bool pass=!trial.error&&trial.complete()&&trial.play_joined;
    const int formatted=report(trial,d,pass,out,capacity);if(formatted){out[0]=0;return formatted;}
    return trial.error;
}
extern "C" int p4_aoq_dialogue_trial_copy_pcm(uint64_t expected,void* destination,size_t capacity,size_t* written){
    if(written)*written=0;
    if(!expected||!destination||!written)return Argument;
    Owner guard;if(!guard.held)return Busy;
    if(!retained.data||!retained_bytes||expected!=retained_generation)return NotFound;
    if(capacity<retained_bytes)return Limit;
    std::memcpy(destination,retained.data,retained_bytes);*written=retained_bytes;return 0;
}
