#include "aoq_media_control.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace aoq_wire;
using B=std::vector<uint8_t>;
static unsigned checks=0;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static B hex(const char* s) {B b;auto digit=[](char c){return c>='a'?c-'a'+10:c-'0';};while(*s){b.push_back(uint8_t((digit(s[0])<<4)|digit(s[1])));s+=2;}return b;}
static void print(const char* name,const B& b){std::printf("{\"name\":\"%s\",\"length\":%zu,\"hex\":\"",name,b.size());for(auto v:b)std::printf("%02x",v);std::printf("\"}\n");}
static void length(B& b){size_t n=b.size()-6;b[4]=uint8_t(n>>8);b[5]=uint8_t(n);}
int main(){
    std::string error;B out;MediaSetup s,p;Notify n,q;
    const auto profile=pcm_six_track_profile();
    MediaBinding audio;audio.track=profile[2].name;audio.alias=profile[2].alias;audio.direction=profile[2].direction;audio.selected=profile[2].media[0];
    MediaBinding text;text.track=profile[1].name;text.alias=profile[1].alias;text.direction=profile[1].direction;text.priority=128;text.selected=profile[1].media[0];
    struct Fixture{const char*name;const char*data;};
    const Fixture setups[]={
        {"media_setup_empty","80ff0e20000411000100"},
        {"media_setup_pcm24k","80ff0e20004110003a01000f6d6f64656c5f617564696f5f6f7574020004000000ca0300010110001a0100010b2000010021000400005dc022000101020004000003e811000100"},
        {"media_setup_pcm24k_reason1_priority10","80ff0e20004510003e01000f6d6f64656c5f617564696f5f6f7574020004000000ca030001010400010a10001a0100010b2000010021000400005dc022000101020004000003e811000101"},
        {"media_setup_text301","80ff0e20004210003b01000d6d6f64656c5f746578745f696e0200040000012d03000100040001801000190100011430000b616f712e746578742e7631020004000003e811000100"}
    };
    for(size_t i=0;i<4;++i){
        s=MediaSetup{};if(i==1||i==2)s.bindings={audio};if(i==2){s.reason=1;s.bindings[0].priority=10;}if(i==3)s.bindings={text};
        CHECK(encode_media_setup(s,out,error));CHECK(out==hex(setups[i].data));print(setups[i].name,out);
        for(size_t j=0;j<out.size();++j){p.bindings={audio};CHECK(decode_media_setup(out.data(),j,p,error)==ParseStatus::NeedMore);CHECK(p.bindings.empty()&&p.consumed==0);}
        CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Ok);CHECK(p.reason==s.reason&&p.bindings.size()==s.bindings.size()&&p.consumed==out.size());
        B again;CHECK(encode_media_setup(p,again,error));CHECK(again==out);
        if(i==1||i==2)CHECK(selected_pcm24k_segment(p.bindings[0]));
        const B copy=out;const size_t first=out.size();out.insert(out.end(),copy.begin(),copy.end());CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Ok&&p.consumed==first);
    }
    const Fixture notifies[]={
        {"notify_type0","80ff0e3000090100027b7d02000100"},
        {"notify_type1","80ff0e3000150100027b7d0200010103000973796e746865746963"},
        {"notify_type2","80ff0e3000150100027b7d0200010203000973796e746865746963"}
    };
    for(uint8_t type=0;type<3;++type){n=Notify{};n.type=type;n.payload={'{','}'};if(type)n.tag="synthetic";
        CHECK(encode_notify(n,out,error));CHECK(out==hex(notifies[type].data));print(notifies[type].name,out);
        for(size_t j=0;j<out.size();++j)CHECK(decode_notify(out.data(),j,q,error)==ParseStatus::NeedMore&&q.payload.empty()&&q.consumed==0);
        CHECK(decode_notify(out.data(),out.size(),q,error)==ParseStatus::Ok&&q.payload==n.payload&&q.tag==n.tag&&q.type==n.type);
    }
    CHECK(decode_media_setup(nullptr,1,p,error)==ParseStatus::Invalid);
    CHECK(decode_notify(nullptr,1,q,error)==ParseStatus::Invalid);
    // A profile and a ServerSetup acceptance are not a MEDIA_SETUP selection.
    CHECK(encode_media_profile(profile,out,error));CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Invalid);
    out=hex("6f000008010180ff0e120100");CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Invalid);
    // Format, alias, name and direction mismatch must never arm PCM playback.
    for(unsigned i=0;i<9;++i){auto b=audio;switch(i){case 0:b.alias=201;break;case 1:b.track="model_audio_in";break;case 2:b.direction=0;break;case 3:b.selected.codec=10;break;case 4:b.selected.sample_rate_hz=16000;break;case 5:b.selected.channels=2;break;case 6:b.selected.audio_model=1;break;case 7:b.selected.audio_payload_format=1;break;case 8:b.selected.timescale=90000;break;}CHECK(!selected_pcm24k_segment(b));}
    for(const char* malformed:{"80ff0e200000","80ff0e20000411000200","80ff0e2000081100010011000100","80ff0e20000411000102","80ff0e20000110","80ff0e204101","80ff0e300000","80ff0e3000090100027b7d02000101","80ff0e3000090100027b7d02000103","80ff0e30000d0100027b7d0200010002000100"}){
        out=hex(malformed);if(out[3]==0x20)CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Invalid);else CHECK(decode_notify(out.data(),out.size(),q,error)==ParseStatus::Invalid);
    }
    // Unknown extension TLV can be skipped; duplicate tracks cannot change binding.
    out=hex(setups[1].data);out.insert(out.end(),{0xfe,0,1,7});length(out);CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Ok&&selected_pcm24k_segment(p.bindings[0]));
    B original=hex(setups[1].data);out=original;out.insert(out.end(),original.begin()+6,original.end()-4);length(out);CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Invalid&&p.bindings.empty());
    // Nested parameter and binding TLV lengths are checked, independent of frame length.
    for(size_t at:{size_t(7),size_t(10),size_t(35)}){out=original;out[at]=0xff;CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Invalid);}
    s.bindings={audio,audio};CHECK(!encode_media_setup(s,out,error)&&out.empty());
    s.bindings={audio};s.reason=2;CHECK(!encode_media_setup(s,out,error)&&out.empty());
    n=Notify{};n.type=1;CHECK(!encode_notify(n,out,error));n.type=0;n.payload.resize(16385);CHECK(!encode_notify(n,out,error));
    n.payload.clear();n.tag.assign(129,'x');CHECK(!encode_notify(n,out,error));
    n=Notify{};CHECK(encode_notify(n,out,error));CHECK(decode_notify(out.data(),out.size(),q,error)==ParseStatus::Ok&&q.payload.empty());
    auto opus=audio;opus.selected.codec=10;opus.selected.sample_rate_hz=48000;
    CHECK(selected_opus48k_segment(opus)&&!selected_pcm24k_segment(opus));
    s.reason=0;s.bindings={opus};CHECK(encode_media_setup(s,out,error));
    CHECK(decode_media_setup(out.data(),out.size(),p,error)==ParseStatus::Ok&&selected_opus48k_segment(p.bindings[0]));
    for(unsigned i=0;i<9;++i){auto b=opus;switch(i){case 0:b.alias=201;break;case 1:b.track="model_audio_in";break;case 2:b.direction=0;break;case 3:b.selected.codec=11;break;case 4:b.selected.sample_rate_hz=24000;break;case 5:b.selected.channels=2;break;case 6:b.selected.audio_model=1;break;case 7:b.selected.audio_payload_format=1;break;case 8:b.selected.timescale=90000;break;}CHECK(!selected_opus48k_segment(b));}
    std::printf("{\"checks\":%u,\"pass\":true}\n",checks);
}
