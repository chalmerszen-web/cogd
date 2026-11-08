#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_dialogue_trial.cpp"
int64_t test_now=1000;
enum class Scenario {Pass,EarlyAudio,NoReady,WrongReady,TwoResponses,WrongDone,Gap,ProviderError,Cancelled,Busy,NoSource,NotDrained,SecondStream,VideoWrongAnswer,VideoUnselected,CameraWrongVad,DelayedResponse,EndpointOnly,ManualWrongReady,ManualAutomatic,ManualNoCommit,ManualOmitted,ManualEmpty,ManualNoTranscript};
static Scenario scenario=Scenario::Pass;static unsigned cases=0,accepted=0;
struct SentPcm {uint64_t timestamp;int64_t sent_at;std::vector<int16_t> pcm;};
static bool trace_pcm=false;static std::vector<SentPcm> pcm_trace;
extern "C" int p4_aoq_tts_trial_copy_pcm(uint64_t expected,void* target,size_t capacity,size_t* written){
    *written=0;if(expected!=9||scenario==Scenario::NoSource)return 23;assert(capacity>=1920);
    auto* p=static_cast<int16_t*>(target);for(size_t i=0;i<960;++i)p[i]=int16_t((i%20)*100);*written=1920;return 0;
}
namespace aoq {
struct Session::Impl {
    Options options;Callbacks callbacks;Diagnostics diag;bool configured=false,updated=false,started=false,replied=false,busy=false;
    bool video=false,camera=false;int fixture=-1;
    bool manual=false,commit=false,commit_ack=false,transcribed=false,create=false;
    int64_t endpoint_at=0;
    int dispatch(const char* text){Object object;object.payload={reinterpret_cast<const uint8_t*>(text),std::strlen(text)};return callbacks.data(callbacks.userdata,object);}
    int pcm(uint64_t id,uint64_t stream=7){
        uint8_t payload[]={0x34,0x12,0x34,0x12};std::vector<uint8_t> properties;aoq_wire::AudioProperties props;
        props.timestamp_us=1000000+id*20000;props.duration_samples=2;assert(aoq_wire::encode_pcm_properties(props,properties));
        Object object;object.stream=stream;object.id=id;object.properties={properties.data(),properties.size()};object.payload={payload,sizeof(payload)};
        return callbacks.audio(callbacks.userdata,object);
    }
};
Session::Session()=default;Session::~Session()=default;
int Session::open(p4_aoq_kind_t kind,const Options& options,const Callbacks& callbacks){
    assert(kind==P4_AOQ_AUDIO||kind==P4_AOQ_OMNI);impl_.reset(new Impl);auto& p=*impl_;p.options=options;p.callbacks=callbacks;
    p.video=kind==P4_AOQ_OMNI;if(p.video)assert(options.video_width==320&&options.video_height==256);
    p.diag.jpeg_selected=p.video&&scenario!=Scenario::VideoUnselected;
    p.diag.closed=false;p.diag.setup_accepted=true;p.diag.pcm24k_selected=p.diag.pcm16k_input_selected=true;p.diag.receive_drained=scenario!=Scenario::NotDrained;p.diag.rtt_ms=50;return 0;
}
int Session::send_json(const char* text,size_t size){
    auto& p=*impl_;
    Json j{cJSON_ParseWithLength(text,size)};assert(j.p);
    if(!std::strcmp(string(j.p,"type"),"input_audio_buffer.commit")){
        assert(p.manual&&p.updated&&p.diag.audio_tx_samples>=27840&&!p.commit);p.commit=true;return 0;
    }
    if(!std::strcmp(string(j.p,"type"),"response.create")){
        assert(p.manual&&p.commit&&p.commit_ack&&p.transcribed&&!p.create);p.create=true;return 0;
    }
    assert(!p.configured&&!std::strcmp(string(j.p,"type"),"session.update"));
    const auto* settings=field(j.p,"session");assert(!std::strcmp(string(settings,"voice"),p.video?"Tina":"longanqian_v3.1"));
    p.manual=cJSON_IsNull(field(settings,"turn_detection"));
    p.camera=cJSON_IsObject(field(settings,"input_audio_transcription"));
    if(p.camera){
        assert(p.video&&!std::strcmp(string(field(settings,"input_audio_transcription"),"model"),"qwen3-asr-flash-realtime"));
        const auto* vad=field(settings,"turn_detection");assert(!std::strcmp(string(vad,"type"),"server_vad"));
        assert(field(vad,"silence_duration_ms")->valueint==900&&field(vad,"threshold")->valuedouble==.1);
    }
    assert(cJSON_GetArraySize(field(settings,"tools"))==0);p.configured=true;return 0;
}
int Session::send_pcm16(const int16_t* pcm,size_t count,uint64_t timestamp){
    auto& p=*impl_;assert(p.configured&&p.updated&&pcm&&count&&count<=320&&timestamp);
    if(scenario==Scenario::Busy&&!p.busy){p.busy=true;return 3;}
    if(trace_pcm)pcm_trace.push_back({timestamp,test_now,std::vector<int16_t>(pcm,pcm+count)});
    p.diag.audio_tx_samples+=count;++p.diag.audio_datagrams_sent;++accepted;return 0;
}
int Session::send_jpeg(const uint8_t* data,size_t size,uint64_t timestamp){
    auto& p=*impl_;assert(p.video&&p.updated&&p.started&&p.diag.audio_tx_samples&&timestamp);
    if(!p.diag.jpeg_selected)return 3;
    if(size==sizeof(fixture_red_square)&&!std::memcmp(data,fixture_red_square,size))p.fixture=0;
    else {assert(size==sizeof(fixture_blue_circle)&&!std::memcmp(data,fixture_blue_circle,size));p.fixture=1;}
    ++p.diag.video_frames_queued;++p.diag.video_frames_sent;p.diag.video_tx_bytes+=size;return 0;
}
int Session::poll(unsigned ms){
    auto& p=*impl_;test_now+=ms;
    if(p.options.cancelled&&p.options.cancelled(p.options.cancel_userdata))return 4;
    if(scenario==Scenario::NoReady){test_now+=1000;return 0;}
    if(p.configured&&!p.updated){
        p.updated=true;
        if(scenario==Scenario::WrongReady)return p.dispatch(R"({"type":"session.updated","session":{"model":"wrong","voice":"longanqian_v3.1","output_audio_format":"pcm"}})");
        if(p.manual&&scenario==Scenario::ManualOmitted)return p.dispatch(R"({"type":"session.updated","session":{"model":"qwen-audio-3.1-realtime-flash","voice":"longanqian_v3.1","output_audio_format":"pcm"}})");
        if(p.manual)return p.dispatch(scenario==Scenario::ManualWrongReady?
            R"({"type":"session.updated","session":{"model":"qwen-audio-3.1-realtime-flash","voice":"longanqian_v3.1","output_audio_format":"pcm","turn_detection":{"type":"server_vad"}}})":
            R"({"type":"session.updated","session":{"model":"qwen-audio-3.1-realtime-flash","voice":"longanqian_v3.1","output_audio_format":"pcm","turn_detection":null}})");
        if(p.camera){
            if(scenario==Scenario::CameraWrongVad)return p.dispatch(R"({"type":"session.updated","session":{"model":"qwen3.8-omni-flash-realtime","voice":"Tina","output_audio_format":"pcm","turn_detection":{"type":"semantic_vad","threshold":0.1,"silence_duration_ms":900},"input_audio_transcription":{"model":"qwen3-asr-flash-realtime"}}})");
            return p.dispatch(R"({"type":"session.updated","session":{"model":"qwen3.8-omni-flash-realtime","voice":"Tina","output_audio_format":"pcm","turn_detection":{"type":"server_vad","threshold":0.1,"silence_duration_ms":900},"input_audio_transcription":{"model":"qwen3-asr-flash-realtime"}}})");
        }
        if(p.video)return p.dispatch(R"({"type":"session.updated","session":{"model":"qwen3.8-omni-flash-realtime","voice":"Tina","output_audio_format":"pcm"}})");
        return p.dispatch(R"({"type":"session.updated","session":{"model":"qwen-audio-3.1-realtime-flash","voice":"longanqian_v3.1","output_audio_format":"pcm"}})");
    }
    if(p.manual){
        if(scenario==Scenario::ManualAutomatic&&p.diag.audio_tx_samples)
            return p.dispatch(R"({"type":"input_audio_buffer.speech_started","item_id":"user-1"})");
        if(p.commit&&!p.commit_ack){
            if(scenario==Scenario::ManualNoCommit)return 0;
            p.commit_ack=true;return p.dispatch(R"({"type":"input_audio_buffer.committed","item_id":"user-1"})");
        }
        if(p.commit_ack&&!p.transcribed){
            if(scenario==Scenario::ManualNoTranscript)return 0;
            p.transcribed=true;
            return p.dispatch(scenario==Scenario::ManualEmpty?
                R"({"type":"conversation.item.input_audio_transcription.completed","item_id":"user-1","transcript":" "})":
                R"({"type":"conversation.item.input_audio_transcription.completed","item_id":"user-1","transcript":"原生语音测试成功。"})");
        }
        if(!p.create)return 0;
    }
    if(!p.manual&&p.diag.audio_tx_samples&&!p.started){
        p.started=true;
        if(scenario==Scenario::Cancelled){p4_aoq_dialogue_trial_cancel();return 0;}
        if(scenario==Scenario::ProviderError)return p.dispatch(R"({"type":"error","error":{"message":"PRIVATE_ERROR_NOT_LOGGED"}})");
        return p.dispatch(R"({"type":"input_audio_buffer.speech_started","item_id":"user-1"})");
    }
    if(p.diag.audio_tx_samples>=3840&&!p.replied){
        if(scenario==Scenario::DelayedResponse||scenario==Scenario::EndpointOnly){
            if(!p.endpoint_at){p.endpoint_at=test_now;return p.dispatch(R"({"type":"input_audio_buffer.speech_stopped","item_id":"user-1"})");}
            if(scenario==Scenario::EndpointOnly||test_now-p.endpoint_at<700)return 0;
        }
        p.replied=true;int e=p.manual?0:p.dispatch(R"({"type":"input_audio_buffer.speech_stopped","item_id":"user-1"})");if(e)return e;
        e=p.dispatch(R"({"type":"conversation.item.input_audio_transcription.completed","item_id":"user-1","transcript":"原生语音测试成功。"})");if(e)return e;
        if(scenario==Scenario::EarlyAudio){e=p.pcm(0);if(e)return e;}
        e=p.dispatch(R"({"type":"response.created","response":{"id":"answer-1"}})");if(e)return e;
        if(scenario==Scenario::TwoResponses)return p.dispatch(R"({"type":"response.created","response":{"id":"answer-2"}})");
        if(scenario!=Scenario::EarlyAudio){e=p.pcm(0);if(e)return e;}
        if(scenario==Scenario::Gap)return p.pcm(2);
        if(scenario==Scenario::SecondStream)return p.pcm(0,11);
        e=p.dispatch(p.video&&scenario!=Scenario::VideoWrongAnswer?
            (p.fixture==0?R"({"type":"response.audio_transcript.done","response_id":"answer-1","transcript":"红色正方形。"})":R"({"type":"response.audio_transcript.done","response_id":"answer-1","transcript":"蓝色圆形。"})"):
            R"({"type":"response.audio_transcript.done","response_id":"answer-1","transcript":"收到。"})");if(e)return e;
        End end;end.stream=7;end.alias=202;end.end_group=true;e=p.callbacks.end(p.callbacks.userdata,end);if(e)return e;
        return p.dispatch(scenario==Scenario::WrongDone?R"({"type":"response.done","response":{"id":"other","status":"completed"}})":R"({"type":"response.done","response":{"id":"answer-1","status":"completed"}})");
    }
    return 0;
}
void Session::close(){if(impl_)impl_->diag.closed=true;}
void Session::cancel(){p4_aoq_dialogue_trial_cancel();}
Diagnostics Session::diagnostics()const{return impl_?impl_->diag:Diagnostics{};}
}
std::string run(Scenario next,int expected){
    scenario=next;char out[2048];const auto before=accepted;
    const int error=p4_aoq_dialogue_trial_command("dialogue probe 9",out,sizeof(out));
    assert(error==expected);Json j{cJSON_Parse(out)};assert(j.p);
    assert(cJSON_IsTrue(field(j.p,"pass"))==(expected==0));
    assert(!std::strstr(out,"PRIVATE_ERROR_NOT_LOGGED")&&!std::strstr(out,"answer-1"));
    if(!expected){assert(accepted==before+87);assert(cJSON_IsTrue(field(j.p,"input_done")));assert(field(j.p,"pcm_bytes")->valueint==4);}
    ++cases;return out;
}
struct Live {
    unsigned reads=0,written=0,played=0,opens=0,finishes=0,stops=0;
    bool sealed=false,stopped=false,cancel=false,partial=false,empty=false,cancel_write=false;
    bool camera=false,stale=false;int camera_error=0;int64_t camera_next=0;
    bool realtime=false;uint64_t origin_us=0;size_t supplied=0;
    int open_error=0,write_error=0,finish_error=0;
    int64_t opened_at=0,first_written_at=0;
    void* ring=nullptr;size_t ring_bytes=0;
    static int read(void* ctx,int16_t* pcm,size_t cap,size_t* count){
        auto& s=*static_cast<Live*>(ctx);++s.reads;assert(!s.stopped);
        *count=s.empty?0:s.partial?std::min<size_t>(cap,80):cap;
        if(s.realtime){
            const size_t produced=size_t((uint64_t(test_now)*1000-s.origin_us)*16000/1000000);
            assert(produced>=s.supplied);*count=std::min(*count,produced-s.supplied);
        }
        for(size_t i=0;i<*count;++i)pcm[i]=s.realtime?int16_t(int((s.supplied+i)%60001)-30000):1000;
        s.supplied+=*count;
        return 0;
    }
    static int open(void* ctx,unsigned rate,void* ring,size_t bytes){
        auto& s=*static_cast<Live*>(ctx);assert(rate==24000&&ring&&bytes==12288);++s.opens;
        assert(!s.stopped);s.opened_at=test_now;
        s.ring=ring;s.ring_bytes=bytes;return s.open_error;
    }
    static unsigned space(void*){return 6144;}
    static int write(void* ctx,const int16_t* pcm,size_t count){
        auto& s=*static_cast<Live*>(ctx);assert(!s.sealed&&pcm&&count<=960&&s.opens==1);
        if(s.cancel_write)s.cancel=true;
        if(s.write_error)return s.write_error;
        if(!s.first_written_at)s.first_written_at=test_now;
        s.written+=count;s.played+=count;return 0;
    }
    static unsigned remaining(void*){return 0;}
    static unsigned played_count(void* ctx){return static_cast<Live*>(ctx)->played;}
    static void seal(void* ctx){static_cast<Live*>(ctx)->sealed=true;}
    static int finish(void* ctx,int error){
        auto& s=*static_cast<Live*>(ctx);assert(s.stopped&&s.opens&&s.ring&&s.ring_bytes);++s.finishes;
        // ASan checks that the borrowed ring is still live until this join.
        std::memset(s.ring,0xaa,s.ring_bytes);return s.finish_error?s.finish_error:error;
    }
    static bool cancelled(void* ctx){return static_cast<Live*>(ctx)->cancel;}
    static void stop(void* ctx){auto& s=*static_cast<Live*>(ctx);s.stopped=true;++s.stops;}
    static int frame(void* ctx,unsigned after,uint8_t* out,size_t capacity,size_t* bytes,unsigned* sequence,uint64_t* timestamp){
        auto& s=*static_cast<Live*>(ctx);*bytes=0;
        if(s.camera_error)return s.camera_error;
        if(test_now<s.camera_next)return 3;
        assert(capacity>=sizeof(fixture_red_square));std::memcpy(out,fixture_red_square,sizeof(fixture_red_square));
        *bytes=sizeof(fixture_red_square);*sequence=after+1;*timestamp=uint64_t(test_now)*1000-(s.stale?1500001:0);
        s.camera_next=test_now+1000;return 0;
    }
    p4_aoq_dialogue_live_ops_t ops(){
        p4_aoq_dialogue_live_ops_t out{};auto& p=out.playback;p.ctx=this;p.open=open;p.space=space;p.write=write;
        p.remaining=remaining;p.played=played_count;p.seal=seal;p.finish=finish;p.cancelled=cancelled;
        out.read=read;out.stop_capture=stop;out.video_frame=camera?frame:nullptr;
        out.capture_origin_us=origin_us?origin_us:uint64_t(test_now)*1000;return out;
    }
};
void run_live(Live& live,Scenario next,int expected){
    scenario=next;auto ops=live.ops();char out[2048];const int result=p4_aoq_dialogue_live(&ops,out,sizeof(out));
    assert(result==expected);Json j{cJSON_Parse(out)};assert(j.p);
    assert(cJSON_IsTrue(field(j.p,"aoq_live_dialogue")));assert(cJSON_IsTrue(field(j.p,"pass"))==!expected);
    assert(!cJSON_IsTrue(field(j.p,"pcm_retained")));assert(live.stops==1);
    if(live.opens&&!live.open_error)assert(live.finishes==1);
    if(!expected)assert(live.played==2&&live.sealed&&live.finishes==1&&live.reads);
    ++cases;
}
int main(){
    // A 1.5s handshake must not become a permanent 1.5s microphone delay.
    // Catch up without discarding PCM, changing sample spacing or future PTS.
    for(const auto next:{Scenario::Pass,Scenario::Busy}){
        test_now=100000;scenario=next;pcm_trace.clear();trace_pcm=true;
        Live s;s.realtime=true;s.origin_us=uint64_t(test_now)*1000;auto ops=s.ops();
        Trial t;t.live=&ops;aoq::Options options;aoq::Callbacks callbacks{&t,Trial::data,Trial::audio,Trial::end};
        assert(!t.session.open(P4_AOQ_AUDIO,options,callbacks));test_now+=1500;
        assert(!t.configure()&&!t.session.poll(5));
        for(unsigned i=0;i<200;++i){assert(!t.send_microphone());test_now+=5;}
        assert(t.microphone_initial_lag_ms==1505&&t.microphone_last_lag_ms<=40&&t.sent_samples>38000);
        size_t cursor=0;int64_t previous=0;
        for(const auto& frame:pcm_trace){
            assert(frame.timestamp==s.origin_us+cursor*1000000/16000);
            assert(frame.timestamp<=uint64_t(frame.sent_at)*1000);
            if(previous)assert(frame.sent_at-previous>=5);
            for(const auto sample:frame.pcm){assert(sample==int16_t(int(cursor%60001)-30000));++cursor;}
            previous=frame.sent_at;
        }
        assert(cursor==t.sent_samples&&s.supplied==cursor);
        trace_pcm=false;t.session.close();++cases;
    }
    {Live s;auto ops=s.ops();char out[2048];ops.capture_origin_us=uint64_t(test_now+1)*1000;
     assert(p4_aoq_dialogue_live(&ops,out,sizeof(out))==1&&!s.reads&&!s.opens);++cases;}
    {
        Json j{cJSON_Parse(R"({"code":"Invalid.Image","message":"PRIVATE_ERROR_NOT_LOGGED image before audio buffer is empty"})")};
        char code[65]{},hint[160]{};error_summary(j.p,code,sizeof(code),hint,sizeof(hint));
        assert(!std::strcmp(code,"Invalid.Image")&&!std::strcmp(hint,"image audio buffer empty before"));
        assert(!std::strstr(hint,"PRIVATE_ERROR_NOT_LOGGED"));
        Json unsafe{cJSON_Parse(R"({"code":"private value: token","message":"PRIVATE_ERROR_NOT_LOGGED"})")};
        code[0]=hint[0]=0;error_summary(unsafe.p,code,sizeof(code),hint,sizeof(hint));assert(!code[0]&&!hint[0]);++cases;
    }
    assert(audio_timestamp_us(1234,4480)-audio_timestamp_us(1234,4160)==20000);
    assert(audio_timestamp_us(1234,160000)==11234000);++cases;
    run(Scenario::Pass,0);run(Scenario::EarlyAudio,0);run(Scenario::Busy,0);
    uint8_t pcm[4];size_t size=0;const auto previous=retained_generation;
    assert(p4_aoq_dialogue_trial_copy_pcm(previous,pcm,sizeof(pcm),&size)==0&&size==4&&pcm[0]==0x34);
    assert(p4_aoq_dialogue_trial_copy_pcm(previous+1,pcm,sizeof(pcm),&size)==23&&size==0);++cases;
    run(Scenario::NoReady,15);assert(p4_aoq_dialogue_trial_copy_pcm(previous,pcm,sizeof(pcm),&size)==23);++cases;
    run(Scenario::WrongReady,7);run(Scenario::TwoResponses,7);run(Scenario::WrongDone,15);run(Scenario::Gap,7);
    run(Scenario::ProviderError,14);run(Scenario::Cancelled,4);run(Scenario::NoSource,23);
    run(Scenario::NotDrained,15);
    char out[2048];assert(p4_aoq_dialogue_trial_command("dialogue probe 18446744073709551616",out,sizeof(out))==1);++cases;
    // PTT must retain the entire input, wait for the matching commit ACK and
    // issue exactly one create. Reject an ignored/null configuration or an
    // automatic speech turn rather than silently returning to cloud VAD.
    for(const auto next:{Scenario::Pass,Scenario::Busy,Scenario::ManualWrongReady,Scenario::ManualAutomatic,Scenario::ManualNoCommit,Scenario::ManualOmitted,Scenario::ManualEmpty,Scenario::ManualNoTranscript}){
        scenario=next;const int expected=next==Scenario::ManualNoCommit||next==Scenario::ManualNoTranscript?15:
            next==Scenario::ManualWrongReady||next==Scenario::ManualAutomatic||next==Scenario::ManualEmpty?7:0;
        assert(p4_aoq_dialogue_trial_command("dialogue manual 9",out,sizeof(out))==expected);
        Json j{cJSON_Parse(out)};assert(j.p&&cJSON_IsTrue(field(j.p,"manual_mode")));
        assert(cJSON_IsTrue(field(j.p,"pass"))==(expected==0));
        if(!expected){assert(cJSON_IsTrue(field(j.p,"commit_acknowledged"))&&cJSON_IsTrue(field(j.p,"create_queued")));
            assert(field(j.p,"speech_starts")->valueint==0&&field(j.p,"speech_stops")->valueint==0);}
        ++cases;
    }
    {Live s;run_live(s,Scenario::Pass,0);}
    {
        char dump[2048];assert(!p4_aoq_dialogue_input_command("capture",dump,sizeof(dump)));
        Live s;run_live(s,Scenario::Pass,0);assert(retained_input_bytes==3840*2&&retained_input_generation&&!capture_next_input);
        std::string read="read "+std::to_string(retained_input_generation)+" 0";
        assert(!p4_aoq_dialogue_input_command(read.c_str(),dump,sizeof(dump)));
        Json j{cJSON_Parse(dump)};assert(j.p&&!std::strncmp(string(j.p,"hex"),"e803e803",8));
        assert(p4_aoq_dialogue_input_command("read 18446744073709551616 0",dump,sizeof(dump))==1);
        assert(p4_aoq_dialogue_input_command("read 0 0",dump,sizeof(dump))==23);
        Live next;run_live(next,Scenario::Pass,0);assert(!retained_input_bytes&&!retained_input_generation);
        assert(p4_aoq_dialogue_input_command(read.c_str(),dump,sizeof(dump))==23);
        assert(!p4_aoq_dialogue_input_command("free",dump,sizeof(dump)));++cases;
    }
    // The cue must open before a delayed answer, once only despite a duplicate
    // endpoint, while network polling continues. No PCM or capture is lost.
    {Live s;run_live(s,Scenario::DelayedResponse,0);
     assert(s.opens==1&&s.stops==1&&s.first_written_at-s.opened_at>=700);}
    // A model that never answers must still join the already-open cue stream.
    {Live s;run_live(s,Scenario::EndpointOnly,15);
     assert(s.opens==1&&s.finishes==1&&!s.written&&!s.first_written_at);}
    {Live s;s.partial=true;run_live(s,Scenario::Busy,0);}
    {Live s;run_live(s,Scenario::EarlyAudio,0);}
    {Live s;run_live(s,Scenario::SecondStream,7);}
    {Live s;s.empty=true;run_live(s,Scenario::Pass,15);assert(!s.opens);}
    {Live s;run_live(s,Scenario::NoReady,15);assert(!s.reads);}
    {Live s;s.open_error=3;run_live(s,Scenario::Pass,3);assert(!s.finishes);}
    {Live s;s.write_error=8;run_live(s,Scenario::Pass,8);}
    {Live s;s.finish_error=8;run_live(s,Scenario::Pass,8);}
    {Live s;s.cancel_write=true;run_live(s,Scenario::Pass,4);}
    {Live s;run_live(s,Scenario::NotDrained,15);assert(!s.sealed);}
    {Live s;s.camera=true;run_live(s,Scenario::Pass,0);}
    {Live s;s.camera=true;run_live(s,Scenario::CameraWrongVad,7);assert(!s.reads&&!s.opens);}
    {Live s;s.camera=true;s.stale=true;run_live(s,Scenario::Pass,7);assert(s.reads&&!s.opens);}
    {Live s;s.camera=true;s.camera_error=4;run_live(s,Scenario::Pass,4);assert(s.reads&&!s.opens);}
    assert(p4_aoq_dialogue_live(nullptr,out,sizeof(out))==1);++cases;
    for(const char* command:{"video red 9","video blue 9"}){
        scenario=Scenario::Pass;assert(p4_aoq_dialogue_trial_command(command,out,sizeof(out))==0);
        Json j{cJSON_Parse(out)};assert(j.p&&cJSON_IsTrue(field(j.p,"aoq_video_trial"))&&cJSON_IsTrue(field(j.p,"fixture_answer_match")));
        assert(field(j.p,"video_frames_sent")->valueint>=2&&!cJSON_IsTrue(field(j.p,"camera_used")));++cases;
    }
    for(const auto next:{Scenario::VideoWrongAnswer,Scenario::VideoUnselected}){
        scenario=next;assert(p4_aoq_dialogue_trial_command("video red 9",out,sizeof(out))==15);++cases;
    }
    std::cout<<"PASS "<<cases<<" bounded dialogue trial cases; mocked media/model, real resampler\n";
}
