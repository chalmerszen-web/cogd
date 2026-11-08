#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_conversation.cpp"
int64_t test_now=1000;
static unsigned poll_calls;
namespace aoq {
struct Session::Impl {};
Session::Session()=default;Session::~Session()=default;
int Session::open(p4_aoq_kind_t,const Options& o,const Callbacks&){assert(o.conversation&&o.lifetime_ms==180000);return 0;}
int Session::poll(unsigned){++poll_calls;return 0;}
int Session::send_json(const char*,size_t){return 0;}
int Session::send_pcm16(const int16_t*,size_t,uint64_t){return 0;}
int Session::send_jpeg(const uint8_t*,size_t,uint64_t){return 0;}
void Session::close(){}void Session::cancel(){}
Diagnostics Session::diagnostics()const{return {};}
}
static unsigned cases;
static int json(p4_aoq_conversation& s,const std::string& value){aoq::Object o;o.payload={reinterpret_cast<const uint8_t*>(value.data()),value.size()};return s.data(o);}
static int created(p4_aoq_conversation& s,const char* id="r1"){return json(s,std::string("{\"type\":\"response.created\",\"response\":{\"id\":\"")+id+"\"}}");}
static int done(p4_aoq_conversation& s,bool audio=true,const char* id="r1"){
    return json(s,std::string("{\"type\":\"response.done\",\"response\":{\"id\":\"")+id+"\",\"status\":\"completed\",\"output\":"+
        (audio?"[{\"content\":[{\"type\":\"audio\"}]}]":"[]")+"}}");
}
static int pcm(p4_aoq_conversation& s,uint64_t stream=7,uint64_t id=0,size_t n=960){
    std::vector<uint8_t> payload(n,1),properties;aoq_wire::AudioProperties p;p.timestamp_us=1000000+id*20000;p.duration_samples=n/2;
    assert(aoq_wire::encode_pcm_properties(p,properties));aoq::Object o;
    o.stream=stream;o.id=id;o.properties={properties.data(),properties.size()};o.payload={payload.data(),payload.size()};return s.audio(o);
}
static int end(p4_aoq_conversation& s,uint64_t stream=7,bool terminal=true){aoq::End e;e.alias=202;e.stream=stream;e.stream_fin=true;e.end_group=terminal;return s.end(e);}
static p4_aoq_conversation_message_t take(p4_aoq_conversation& s,std::string* text=nullptr){
    char data[65537];p4_aoq_conversation_message_t m{};assert(p4_aoq_conversation_read(&s,data,sizeof(data),&m)==0);
    if(text)text->assign(data,m.bytes);
    return m;
}
int main(){
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));
     unsigned id=0;while(s.audio_ready(960))assert(!pcm(s,7,id++));
     assert(!done(s)&&!end(s)&&!s.active);
     // A completed reply may still occupy the consumer queue. Early Audio
     // from the next reply must reserve its future flush space there too.
     assert(!s.audio_ready(960));assert(!created(s,"r2"));assert(!s.audio_ready(960));
     assert(!take(s).pcm);assert(take(s).pcm);assert(s.audio_ready(960));
     assert(!pcm(s,11,0));++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));
     for(int i=0;i<13;++i){assert(s.audio_ready(65536));assert(!pcm(s,7,i,65536));}
     assert(!s.audio_ready(65536));const auto bytes=s.queued;const auto polls=poll_calls;
     assert(!p4_aoq_conversation_pump(&s)&&poll_calls==polls+1&&s.queued==bytes);
     // Data retains reserved capacity even when Audio is backpressured.
     assert(!json(s,"{\"type\":\"input_audio_buffer.speech_started\"}"));
     assert(!take(s).pcm);assert(take(s).pcm);assert(s.audio_ready(65536));++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(!done(s));
     assert(s.active&&s.done.data);assert(!end(s));assert(!s.active);assert(!take(s).pcm);auto m=take(s);assert(m.pcm&&!std::strcmp(m.response,"r1"));
     std::string text;assert(!take(s,&text).pcm&&text.find("response.done")!=std::string::npos);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!pcm(s));assert(s.queue.empty());assert(!end(s));assert(!created(s));assert(!done(s));
     assert(!take(s).pcm);assert(take(s).pcm);assert(!take(s).pcm);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!done(s));assert(s.active&&s.queue.size()==1);
     assert(!pcm(s));assert(s.active);assert(!end(s));assert(s.queue.size()==3);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(!end(s));assert(s.active);assert(!done(s));assert(!s.active);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!done(s,false));assert(!s.active);assert(s.queue.size()==2);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(!done(s));assert(!end(s));assert(!end(s));
     assert(!created(s,"r2"));assert(!pcm(s,11));assert(!end(s,11));assert(!done(s,true,"r2"));
     assert(!take(s).pcm);auto a=take(s);assert(!std::strcmp(a.response,"r1"));assert(!take(s).pcm);assert(!take(s).pcm);
     a=take(s);assert(a.pcm&&!std::strcmp(a.response,"r2"));assert(!take(s).pcm);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(!done(s));assert(!end(s));assert(!created(s,"r2"));assert(pcm(s,7,1)==Protocol);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(pcm(s,11)==Protocol);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(created(s,"r2")==Protocol);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(done(s,true,"other")==Protocol);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(!end(s,7,false));assert(!done(s));assert(s.active);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(pcm(s,7,2)==Protocol);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));assert(!pcm(s));assert(!end(s));assert(pcm(s,7,1)==Protocol);++cases;}
    {p4_aoq_conversation s;assert(pcm(s)==Protocol);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!pcm(s,7,0,65536));assert(!pcm(s,7,1,65536));assert(pcm(s,7,2)==Limit);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));for(int i=0;i<15;++i)assert(!pcm(s,7,i,65536));assert(pcm(s,7,15,65536)==Limit);++cases;}
    {p4_aoq_conversation s;s.ready=true;assert(!created(s));char data[2];p4_aoq_conversation_message_t m;
     assert(p4_aoq_conversation_read(&s,data,sizeof(data),&m)==Limit);assert(s.queue.size()==1);++cases;}
    {p4_aoq_conversation_t* s=nullptr;p4_aoq_conversation_options_t options{};options.cancelled=[](void*){return false;};
     assert(!p4_aoq_conversation_open(&s,&options)&&s);p4_aoq_conversation_close(s);++cases;}
    for(const auto* status:{"cancelled","failed"}){
        p4_aoq_conversation s;s.ready=true;assert(!created(s));
        assert(!json(s,std::string("{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"")+status+"\",\"output\":[{\"content\":[{\"type\":\"audio\"}]}]}}"));
        assert(s.abandoned&&s.queue.size()==2);assert(!pcm(s));assert(!created(s,"r2"));assert(!pcm(s,11));
        assert(s.queue.size()==2);std::string text;take(s);take(s,&text);assert(text.find(status)!=std::string::npos);++cases;
    }
    std::cout<<"AOQ conversation: "<<cases<<" cases passed\n";
}
