#include "aoq_media_control.h"
#include <limits>

namespace aoq_wire { namespace {
using B=std::vector<uint8_t>;
struct Field {uint8_t type;const uint8_t* p;size_t n;};
bool tlv(const uint8_t* d,size_t n,size_t& at,Field& f) {
    if(n-at<3)return false;
    f={d[at],d+at+3,(size_t(d[at+1])<<8)|d[at+2]};at+=3;
    if(f.n>n-at)return false;
    at+=f.n;return true;
}
bool once(uint64_t& seen,unsigned bit) {uint64_t b=uint64_t(1)<<bit;if(seen&b)return false;seen|=b;return true;}
uint32_t u32(const uint8_t* p) {return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
B be(uint32_t n){return {uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};}
B bytes(const std::string& s){return B(s.begin(),s.end());}
void put(B& o,uint8_t t,const B& b){o.push_back(t);o.push_back(uint8_t(b.size()>>8));o.push_back(uint8_t(b.size()));o.insert(o.end(),b.begin(),b.end());}
bool bad(std::string& e,const char* s){e=s;return false;}
ParseStatus frame(const uint8_t*d,size_t n,uint64_t want,size_t& start,size_t& end,std::string& e){
    if(!d&&n){e="null input";return ParseStatus::Invalid;}
    if(!n)return ParseStatus::NeedMore;
    size_t k=size_t(1)<<(d[0]>>6);if(n<k)return ParseStatus::NeedMore;
    uint64_t t=d[0]&63;for(size_t i=1;i<k;++i)t=(t<<8)|d[i];
    if(t!=want){e="wrong control type";return ParseStatus::Invalid;}
    if(n-k<2)return ParseStatus::NeedMore;
    size_t len=(size_t(d[k])<<8)|d[k+1];
    if(len>16640){e="control too large";return ParseStatus::Invalid;}
    start=k+2;if(len>n-start)return ParseStatus::NeedMore;end=start+len;return ParseStatus::Ok;
}
void wrap(uint64_t type,const B& p,B& out){out={uint8_t(0x80|(type>>24)),uint8_t(type>>16),uint8_t(type>>8),uint8_t(type),uint8_t(p.size()>>8),uint8_t(p.size())};out.insert(out.end(),p.begin(),p.end());}
bool param(const uint8_t*d,size_t n,MediaParam& p){
    p=MediaParam{};p.schema.clear();uint64_t seen=0;size_t at=0;
    while(at<n){Field f;if(!tlv(d,n,at,f))return false;
        unsigned bit=0;switch(f.type){case 1:bit=0;break;case 2:bit=1;break;case 0x20:bit=2;break;case 0x21:bit=3;break;case 0x22:bit=4;break;case 0x23:bit=5;break;case 0x30:bit=6;break;case 0x10:bit=7;break;case 0x11:bit=8;break;case 0x12:bit=9;break;default:continue;}
        if(!once(seen,bit))return false;
        if(f.type==2||f.type==0x21){if(f.n!=4)return false;if(f.type==2)p.timescale=u32(f.p);else p.sample_rate_hz=u32(f.p);}
        else if(f.type==0x10||f.type==0x11){if(f.n!=4)return false;if(f.type==0x10)p.video_width=u32(f.p);else p.video_height=u32(f.p);}
        else if(f.type==0x12){if(f.n!=2)return false;p.video_fps=uint16_t((uint16_t(f.p[0])<<8)|f.p[1]);}
        else if(f.type==0x30){if(!f.n||f.n>128)return false;p.schema.assign(reinterpret_cast<const char*>(f.p),f.n);}
        else {if(f.n!=1)return false;switch(f.type){case 1:p.codec=*f.p;break;case 0x20:p.audio_model=*f.p;break;case 0x22:p.channels=*f.p;break;case 0x23:p.audio_payload_format=*f.p;break;default:return false;}}
    }
    if((seen&3)!=3||!p.timescale)return false;
    if(p.codec==10||p.codec==11)return (seen&0x1c)==0x1c&&p.audio_model<=1&&p.audio_payload_format<=1&&p.sample_rate_hz&&p.channels;
    if(p.codec==20||p.codec==21)return (seen&0x40)!=0;
    if(p.codec==1)return (seen&0x380)==0x380&&p.video_width&&p.video_height&&p.video_width<=1920&&p.video_height<=1080&&p.video_fps&&p.video_fps<=30;
    return true; // Unknown codec preserved; never treated as PCM.
}
bool binding(const uint8_t*d,size_t n,MediaBinding& b){
    uint64_t seen=0;size_t at=0;
    while(at<n){Field f;if(!tlv(d,n,at,f))return false;unsigned bit;
        switch(f.type){case 1:bit=0;break;case 2:bit=1;break;case 3:bit=2;break;case 4:bit=3;break;case 0x10:bit=4;break;default:continue;}
        if(!once(seen,bit))return false;
        switch(f.type){case 1:if(!f.n||f.n>128)return false;b.track.assign(reinterpret_cast<const char*>(f.p),f.n);break;
        case 2:if(f.n!=4)return false;b.alias=u32(f.p);break;
        case 3:if(f.n!=1||*f.p>1)return false;b.direction=*f.p;break;
        case 4:if(f.n!=1)return false;b.priority=*f.p;break;
        case 0x10:if(!param(f.p,f.n,b.selected))return false;break;default:return false;}
    }return (seen&0x17)==0x17;
}
bool encode_param(const MediaParam& p,B& out,std::string& e){
    std::vector<Track> t={{"x",0,1,{p}}};B profile;
    if(!encode_media_profile(t,profile,e))return false;
    // Reuse the already SDK-matched profile encoder's selected parameter bytes.
    size_t outer=0;Field entry;if(!tlv(profile.data(),profile.size(),outer,entry))return bad(e,"profile internal error");
    size_t at=0;while(at<entry.n){Field f;if(!tlv(entry.p,entry.n,at,f))return bad(e,"profile internal error");if(f.type==0x10){out.assign(f.p,f.p+f.n);return true;}}
    return bad(e,"profile parameter absent");
}
} // namespace
ParseStatus decode_media_setup(const uint8_t*d,size_t n,MediaSetup& out,std::string& e){
    out=MediaSetup{};e.clear();size_t at=0,end=0;auto r=frame(d,n,kMediaSetupType,at,end,e);if(r!=ParseStatus::Ok)return r;
    MediaSetup v;bool reason=false;
    while(at<end){Field f;if(!tlv(d,end,at,f)){e="bad media TLV";return ParseStatus::Invalid;}
        if(f.type==0x10){MediaBinding b;if(v.bindings.size()>=16||!binding(f.p,f.n,b)){e="bad binding";return ParseStatus::Invalid;}
            for(const auto& old:v.bindings)if(old.alias==b.alias||old.track==b.track){e="duplicate binding";return ParseStatus::Invalid;}
            v.bindings.push_back(b);
        }else if(f.type==0x11){if(reason||f.n!=1||*f.p>1){e="bad reason";return ParseStatus::Invalid;}reason=true;v.reason=*f.p;}
    }
    if(!reason){e="missing reason";return ParseStatus::Invalid;}v.consumed=end;out=std::move(v);return ParseStatus::Ok;
}
ParseStatus decode_notify(const uint8_t*d,size_t n,Notify& out,std::string& e){
    out=Notify{};e.clear();size_t at=0,end=0;auto r=frame(d,n,kNotifyType,at,end,e);if(r!=ParseStatus::Ok)return r;
    Notify v;uint64_t seen=0;
    while(at<end){Field f;if(!tlv(d,end,at,f)){e="bad notify TLV";return ParseStatus::Invalid;}
        if(f.type<1||f.type>3)continue;
        if(!once(seen,f.type)){e="duplicate notify field";return ParseStatus::Invalid;}
        if(f.type==1){if(f.n>16384){e="notify payload too large";return ParseStatus::Invalid;}v.payload.assign(f.p,f.p+f.n);}
        else if(f.type==2){if(f.n!=1||*f.p>2){e="invalid notify type";return ParseStatus::Invalid;}v.type=*f.p;}
        else {if(f.n>128){e="notify tag too large";return ParseStatus::Invalid;}v.tag.assign(reinterpret_cast<const char*>(f.p),f.n);}
    }
    if((seen&6)!=6||(v.type==1&&v.tag.empty())){e="missing notify fields";return ParseStatus::Invalid;}
    v.consumed=end;out=std::move(v);return ParseStatus::Ok;
}
ParseStatus decode_goaway(const uint8_t*d,size_t n,GoAway& out,std::string& e){
    out=GoAway{};e.clear();size_t at=0,end=0;auto r=frame(d,n,kGoAwayType,at,end,e);if(r!=ParseStatus::Ok)return r;
    GoAway v;uint64_t seen=0;
    while(at<end){Field f;if(!tlv(d,end,at,f)){e="bad goaway TLV";return ParseStatus::Invalid;}
        if(f.type<1||f.type>3)continue;
        if(!once(seen,f.type)){e="duplicate goaway field";return ParseStatus::Invalid;}
        if(f.type==1||f.type==2){
            if(f.n>(f.type==1?1024u:256u)){e="goaway text too large";return ParseStatus::Invalid;}
            auto& text=f.type==1?v.new_session_uri:v.reason_phrase;
            text.assign(reinterpret_cast<const char*>(f.p),f.n);
        }else{
            if(f.n!=8){e="bad goaway request id";return ParseStatus::Invalid;}
            for(size_t i=0;i<8;++i)v.largest_request_id=(v.largest_request_id<<8)|f.p[i];
        }
    }
    v.consumed=end;out=std::move(v);return ParseStatus::Ok;
}
bool encode_media_setup(const MediaSetup& s,B& out,std::string& e){
    out.clear();e.clear();if(s.reason>1||s.bindings.size()>16)return bad(e,"invalid media setup");B payload;
    for(size_t i=0;i<s.bindings.size();++i){const auto& b=s.bindings[i];if(b.track.empty()||b.track.size()>128||b.direction>1||b.selected.schema.size()>128)return bad(e,"invalid binding");
        for(size_t j=0;j<i;++j)if(s.bindings[j].alias==b.alias||s.bindings[j].track==b.track)return bad(e,"duplicate binding");
        B entry,p;if(!encode_param(b.selected,p,e))return false;
        put(entry,1,bytes(b.track));put(entry,2,be(b.alias));put(entry,3,{b.direction});if(b.priority)put(entry,4,{b.priority});put(entry,0x10,p);put(payload,0x10,entry);
    }put(payload,0x11,{s.reason});if(payload.size()>16640)return bad(e,"media setup too large");wrap(kMediaSetupType,payload,out);return true;
}
bool encode_notify(const Notify& n,B& out,std::string& e){
    out.clear();e.clear();if(n.type>2||n.tag.size()>128||n.payload.size()>16384||(n.type==1&&n.tag.empty()))return bad(e,"invalid notify");
    B p;put(p,1,n.payload);put(p,2,{n.type});if(!n.tag.empty())put(p,3,bytes(n.tag));wrap(kNotifyType,p,out);return true;
}
bool selected_pcm24k_segment(const MediaBinding& b){const auto& p=b.selected;return b.track=="model_audio_out"&&b.alias==202&&b.direction==1&&p.codec==11&&p.audio_model==0&&p.audio_payload_format==0&&p.sample_rate_hz==24000&&p.channels==1&&p.timescale==1000;}
bool selected_opus48k_segment(const MediaBinding& b){const auto& p=b.selected;return b.track=="model_audio_out"&&b.alias==202&&b.direction==1&&p.codec==10&&p.audio_model==0&&p.audio_payload_format==0&&p.sample_rate_hz==48000&&p.channels==1&&p.timescale==1000;}
} // namespace aoq_wire
