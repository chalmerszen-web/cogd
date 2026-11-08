#include <cassert>
#include <iostream>
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_speech.cpp"
int64_t test_now=1000;
static aoq::Diagnostics diagnostic;
static int send_error,poll_error;
static std::string submitted;
namespace aoq {
struct Session::Impl {};
Session::Session()=default;Session::~Session()=default;
int Session::open(p4_aoq_kind_t kind,const Options& o,const Callbacks&){
    assert(o.conversation&&o.lifetime_ms==(kind==P4_AOQ_ASR?35000u:95000u));return 0;
}
int Session::send_json(const char* p,size_t n){if(send_error)return send_error;submitted.assign(p,n);return 0;}
int Session::poll(unsigned n){test_now+=n;return poll_error;}
void Session::close(){}void Session::cancel(){}
Diagnostics Session::diagnostics()const{return diagnostic;}
}
static unsigned cases;
static int control(p4_aoq_speech* s,const char* action,const char* id="task"){
    std::string j="{\"header\":{\"action\":\""+std::string(action)+"\",\"task_id\":\""+id+"\"},\"payload\":{\"task\":\""+
        (s->asr?"asr":"tts")+"\",\"model\":\""+(s->asr?"qwen-audio-3.1-asr-flash-streaming":"qwen-audio-3.0-tts-flash")+
        "\",\"parameters\":{\"format\":\"pcm\",\"sample_rate\":16000,\"max_sentence_silence\":800},\"input\":{\"text\":\"hello\"}}}";
    return p4_aoq_speech_send(s,false,j.data(),j.size());
}
static int event(p4_aoq_speech* s,const char* name,const char* id="task"){
    std::string j="{\"header\":{\"event\":\""+std::string(name)+"\",\"task_id\":\""+id+"\"}}";
    aoq::Object o;o.payload={reinterpret_cast<const uint8_t*>(j.data()),j.size()};return s->data(o);
}
static int pcm(p4_aoq_speech* s,uint64_t stream=7,uint64_t id=0,size_t size=960){
    std::vector<uint8_t> data(size,3),properties;aoq_wire::AudioProperties p;p.duration_samples=size/2;
    assert(aoq_wire::encode_pcm_properties(p,properties));aoq::Object o;o.stream=stream;o.id=id;
    o.properties={properties.data(),properties.size()};o.payload={data.data(),data.size()};return s->audio(o);
}
static p4_aoq_speech_t* open(bool asr=false){
    diagnostic={};diagnostic.setup_accepted=diagnostic.pcm24k_selected=diagnostic.receive_drained=true;diagnostic.rtt_ms=50;
    send_error=poll_error=0;submitted.clear();
    p4_aoq_speech_t* s=nullptr;assert(!p4_aoq_speech_open(&s,asr,nullptr,[](void*){return false;}));return s;
}
static p4_aoq_speech_chunk_t read(p4_aoq_speech* s,size_t cap=2048,std::string* text=nullptr){
    char out[65536];p4_aoq_speech_chunk_t c{};assert(cap<=sizeof(out));assert(!p4_aoq_speech_read(s,out,cap,&c,0));
    if(text)text->assign(out,c.length);
    return c;
}
static void ready(p4_aoq_speech* s){assert(!control(s,"run-task"));assert(!event(s,"task-started"));read(s);}
int main(){
    {auto* s=open();ready(s);assert(!control(s,"continue-task"));assert(!control(s,"finish-task"));assert(!event(s,"task-finished"));
     assert(s->queue.empty());assert(!pcm(s));read(s);assert(s->completion.data);test_now+=299;assert(!s->drain()&&s->queue.empty());
     assert(!pcm(s,7,1));read(s);test_now+=300;assert(!s->drain());std::string j;read(s,2048,&j);assert(j.find("task-finished")!=std::string::npos);
     p4_aoq_speech_close(s);++cases;}
    {auto* s=open();ready(s);assert(!control(s,"continue-task"));assert(!pcm(s));auto a=read(s,101);assert(a.first&&!a.final&&a.opcode==2);
     auto b=read(s);assert(!b.first&&b.final&&b.length==859);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();ready(s);assert(!control(s,"continue-task"));assert(!pcm(s));assert(!control(s,"finish-task"));assert(!event(s,"task-finished"));
     diagnostic.receive_drained=false;test_now+=5000;assert(!s->drain()&&s->completion.data);diagnostic.receive_drained=true;
     aoq::End e;e.alias=202;e.stream=7;e.end_group=true;s->end(e);assert(!s->drain());read(s);std::string j;read(s,2048,&j);assert(j.find("task-finished")!=std::string::npos);
     assert(pcm(s,7,1)==Protocol);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();assert(!control(s,"run-task"));assert(event(s,"task-started","old-task")==Protocol);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();ready(s);assert(!control(s,"continue-task"));assert(!pcm(s));assert(pcm(s,11,1)==Protocol);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();ready(s);assert(!control(s,"continue-task"));assert(pcm(s,7,0,3)==Protocol);p4_aoq_speech_close(s);++cases;}
    {auto* s=open(true);ready(s);assert(submitted.find("max_sentence_silence")==std::string::npos);
     const uint8_t input[]={0,1,2,3};assert(!p4_aoq_speech_send(s,true,input,sizeof(input)));
     assert(submitted=="{\"type\":\"input_audio_buffer.append\",\"audio\":\"AAECAw==\"}");
     assert(!control(s,"finish-task"));assert(!event(s,"task-finished"));std::string j;read(s,2048,&j);assert(j.find("task-finished")!=std::string::npos);
     p4_aoq_speech_close(s);++cases;}
    {auto* s=open();send_error=Busy;assert(control(s,"run-task")==Busy&&!s->run&&!s->task[0]);send_error=0;ready(s);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();assert(pcm(s)==Protocol);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();ready(s);assert(event(s,"task-finished")==Protocol);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();ready(s);assert(!control(s,"continue-task"));for(unsigned i=0;i<8;++i)assert(!pcm(s,7,i,65536));
     assert(pcm(s,7,8)==Limit);p4_aoq_speech_close(s);++cases;}
    {auto* s=open();ready(s);assert(!control(s,"finish-task"));assert(!event(s,"task-finished"));test_now+=10000;assert(!s->drain()&&s->queue.empty());
     char out[4];p4_aoq_speech_chunk_t c;poll_error=15;assert(p4_aoq_speech_read(s,out,sizeof(out),&c,0)==15);p4_aoq_speech_close(s);++cases;}
    {auto* s=open(true);ready(s);assert(pcm(s)==Protocol);p4_aoq_speech_close(s);++cases;}
    std::cout<<"AOQ standalone speech: "<<cases<<" cases passed\n";
}
