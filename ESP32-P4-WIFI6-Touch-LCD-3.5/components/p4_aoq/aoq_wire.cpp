#include "aoq_wire.h"
#include <limits>

namespace aoq_wire {
namespace {
using Bytes = std::vector<uint8_t>;
constexpr uint64_t kMediaProfile = 0xff0e10;
constexpr uint64_t kAcceptStatus = 0xff0e12;
constexpr uint64_t kToken = 0xff0e13;
constexpr uint64_t kSessionConfig = 0xff0e14;
constexpr size_t kMaxPayload = 65535;
void vint(Bytes& out, uint64_t value) {
    size_t n = value < 64 ? 1 : value < 16384 ? 2 : value < (uint64_t(1) << 30) ? 4 : 8;
    uint8_t prefix = n == 1 ? 0 : n == 2 ? 0x40 : n == 4 ? 0x80 : 0xc0;
    for (size_t i = n; i; --i) {
        uint8_t byte = uint8_t(value >> ((i - 1) * 8));
        out.push_back(i == n ? uint8_t(byte | prefix) : byte);
    }
}
Bytes u32(uint32_t v) { return {uint8_t(v>>24),uint8_t(v>>16),uint8_t(v>>8),uint8_t(v)}; }
Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
void tlv(Bytes& out, uint8_t type, const Bytes& value) {
    out.push_back(type);out.push_back(uint8_t(value.size()>>8));out.push_back(uint8_t(value.size()));
    out.insert(out.end(),value.begin(),value.end());
}
void option(Bytes& out, uint64_t type, const Bytes& value) {
    vint(out,type);vint(out,value.size());out.insert(out.end(),value.begin(),value.end());
}
bool fail(std::string& error, const char* why) {error=why;return false;}
bool read_vint(const uint8_t* data,size_t size,size_t& pos,uint64_t& out) {
    if(pos>=size)return false;
    size_t n=size_t(1)<<(data[pos]>>6);
    if(n>size-pos)return false;
    uint64_t v=data[pos]&0x3f;
    for(size_t i=1;i<n;++i)v=(v<<8)|data[pos+i];
    pos+=n;out=v;return true;
}
}

std::vector<Track> pcm_six_track_profile() { return pcm_track_profile(true,true); }
std::vector<Track> pcm_track_profile(bool audio_in,bool audio_out) {
    MediaParam audio;audio.codec=11;audio.audio_model=1;audio.sample_rate_hz=16000;audio.channels=1;audio.schema.clear();
    MediaParam red=audio;red.audio_payload_format=1;
    MediaParam down=audio;down.audio_model=0;down.sample_rate_hz=24000;
    MediaParam text;text.codec=20;text.schema="aoq.text.v1";
    MediaParam command;
    std::vector<Track> tracks;
    if(audio_in)tracks.push_back({"model_audio_in",0,201,{audio,red}});
    tracks.push_back({"model_text_in",0,301,{text}});
    if(audio_out)tracks.push_back({"model_audio_out",1,202,{down}});
    tracks.push_back({"model_text_out",1,302,{text}});
    tracks.push_back({"command_in",0,401,{command}});
    tracks.push_back({"command_out",1,402,{command}});
    return tracks;
}

bool encode_media_profile(const std::vector<Track>& tracks,Bytes& out,std::string& error) {
    out.clear();error.clear();Bytes result;
    if(tracks.size()>16)return fail(error,"too many tracks");
    for(const auto& track:tracks) {
        if(track.name.empty()||track.name.size()>1024||track.direction>1||track.media.empty()||track.media.size()>8)
            return fail(error,"invalid track");
        Bytes entry;tlv(entry,1,{track.direction});tlv(entry,2,u32(track.alias));tlv(entry,3,bytes(track.name));
        for(const auto& p:track.media) {
            Bytes params;tlv(params,1,{p.codec});
            if(p.codec==1) {
                if(!p.video_width||p.video_width>1920||!p.video_height||p.video_height>1080||!p.video_fps||p.video_fps>30)
                    return fail(error,"invalid JPEG video parameters");
                tlv(params,0x10,u32(p.video_width));tlv(params,0x11,u32(p.video_height));
                tlv(params,0x12,{uint8_t(p.video_fps>>8),uint8_t(p.video_fps)});
            } else if(p.codec==11||p.codec==10) {
                if(p.audio_model>1||p.audio_payload_format>1||!p.sample_rate_hz||!p.channels)
                    return fail(error,"invalid audio parameters");
                tlv(params,0x20,{p.audio_model});tlv(params,0x21,u32(p.sample_rate_hz));tlv(params,0x22,{p.channels});
                if(p.audio_payload_format)tlv(params,0x23,{p.audio_payload_format});
            } else if(p.codec==20||p.codec==21) {
                if(p.schema.empty()||p.schema.size()>1024)return fail(error,"invalid text schema");
                tlv(params,0x30,bytes(p.schema));
            } else return fail(error,"codec outside audited setup scope");
            if(!p.timescale)return fail(error,"invalid timescale");
            tlv(params,2,u32(p.timescale));tlv(entry,0x10,params);
        }
        if(entry.size()>kMaxPayload||result.size()+3+entry.size()>kMaxPayload)
            return fail(error,"profile too large");
        tlv(result,0x10,entry);
    }
    out.swap(result);return true;
}

bool encode_client_setup(const std::string& token,const std::vector<Track>& tracks,Bytes& out,std::string& error) {
    out.clear();error.clear();
    if(token.empty()||token.size()>1024)return fail(error,"invalid token length");
    Bytes profile;if(!encode_media_profile(tracks,profile,error))return false;
    Bytes payload;vint(payload,1);vint(payload,kClientVersion);vint(payload,profile.empty()?1:2);
    option(payload,kToken,bytes(token));
    if(!profile.empty())option(payload,kMediaProfile,profile);
    if(payload.size()+4>16384)return fail(error,"setup too large");
    Bytes result;vint(result,kSetupType);result.push_back(uint8_t(payload.size()>>8));result.push_back(uint8_t(payload.size()));
    result.insert(result.end(),payload.begin(),payload.end());out.swap(result);return true;
}

bool encode_client_setup(const char* token,Bytes& out) {
    out.clear();if(!token)return false;
    size_t n=0;while(n<=1024&&token[n])++n;
    if(!n||n>1024)return false;
    std::string error;return encode_client_setup(std::string(token,n),pcm_six_track_profile(),out,error);
}

ParseStatus decode_server_setup(const uint8_t* data,size_t size,ServerSetup& out,std::string& error) {
    out=ServerSetup{};error.clear();
    if(!data&&size){error="null input";return ParseStatus::Invalid;}
    size_t pos=0;uint64_t type=0;
    if(!read_vint(data,size,pos,type))return ParseStatus::NeedMore;
    if(type!=kSetupType){error="not a setup frame";return ParseStatus::Invalid;}
    if(size-pos<2)return ParseStatus::NeedMore;
    size_t payload_size=(size_t(data[pos])<<8)|data[pos+1];pos+=2;
    if(payload_size>size-pos)return ParseStatus::NeedMore;
    const size_t end=pos+payload_size;
    ServerSetup parsed;uint64_t version=0,count=0;
    auto invalid=[&](const char* why){error=why;return ParseStatus::Invalid;};
    if(!read_vint(data,end,pos,version)||version>std::numeric_limits<uint32_t>::max()||
       !read_vint(data,end,pos,count)||count>64)return invalid("invalid setup fields");
    parsed.selected_version=uint32_t(version);bool status_seen=false,config_seen=false;
    for(uint64_t i=0;i<count;++i) {
        uint64_t key=0,len=0;
        if(!read_vint(data,end,pos,key)||!read_vint(data,end,pos,len)||len>end-pos)
            return invalid("invalid option length");
        size_t next=pos+size_t(len);
        if(key==kAcceptStatus) {
            uint64_t value=0;
            if(status_seen||!read_vint(data,next,pos,value)||pos!=next||value>std::numeric_limits<uint32_t>::max())
                return invalid("invalid accept status");
            status_seen=true;parsed.accept_status=uint32_t(value);
        } else if(key==kSessionConfig) {
            if(config_seen)return invalid("duplicate session config");
            config_seen=true;parsed.sdk_config.assign(reinterpret_cast<const char*>(data+pos),size_t(len));
        }
        pos=next;
    }
    if(!status_seen||pos!=end)return invalid("missing status or trailing setup bytes");
    parsed.consumed=end;out=parsed;return ParseStatus::Ok;
}

ParseStatus parse_server_setup(const uint8_t* data,size_t size,
                              uint64_t* version,uint64_t* accept_status,size_t* consumed) {
    if(version)*version=0;
    if(accept_status)*accept_status=0;
    if(consumed)*consumed=0;
    ServerSetup parsed;std::string error;auto status=decode_server_setup(data,size,parsed,error);
    if(status==ParseStatus::Ok){if(version)*version=parsed.selected_version;if(accept_status)*accept_status=parsed.accept_status;if(consumed)*consumed=parsed.consumed;}
    return status;
}
} // namespace aoq_wire
