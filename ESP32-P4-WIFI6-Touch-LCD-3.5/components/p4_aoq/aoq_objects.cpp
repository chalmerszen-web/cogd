#include "aoq_objects.h"
namespace aoq_wire {
namespace {
constexpr uint64_t kMaxVarint=(uint64_t(1)<<62)-1;
constexpr size_t kMaxProperties=4096,kMaxObject=65536;
void write(std::vector<uint8_t>& out,uint64_t v) {
    const size_t n=v<64?1:v<16384?2:v<(uint64_t(1)<<30)?4:8;
    const uint8_t prefix=n==1?0:n==2?0x40:n==4?0x80:0xc0;
    for(size_t i=n;i>0;--i) {
        const auto b=uint8_t(v>>((i-1)*8));
        out.push_back(i==n?uint8_t(b|prefix):b);
    }
}
bool read(const uint8_t* b,size_t n,size_t& p,uint64_t& v) {
    if(p>=n)return false;
    const size_t count=size_t(1)<<(b[p]>>6);
    if(count>n-p)return false;
    v=b[p]&0x3f;
    for(size_t i=1;i<count;++i)v=(v<<8)|b[p+i];
    p+=count;return true;
}
bool valid_status(uint64_t status) {return status==0||status==3||status==4;}
}
bool encode_subgroup_header(const SubgroupHeader& h,std::vector<uint8_t>& out) {
    out.clear();
    if(h.track_alias>kMaxVarint||h.group>kMaxVarint||h.subgroup>kMaxVarint||h.subgroup_id_mode>2)return false;
    const uint64_t type=0x10|(h.has_properties?1:0)|(uint64_t(h.subgroup_id_mode)<<1)|
        (h.end_of_group?8:0)|(h.default_priority?0x20:0)|(h.first_object?0x40:0);
    write(out,type);write(out,h.track_alias);write(out,h.group);
    if(h.subgroup_id_mode==2)write(out,h.subgroup);
    if(!h.default_priority)out.push_back(h.priority);
    return true;
}
ParseStatus decode_subgroup_header(const uint8_t* b,size_t n,SubgroupHeader& out) {
    out=SubgroupHeader{};
    if(!b&&n)return ParseStatus::Invalid;
    size_t p=0;uint64_t type=0;SubgroupHeader h;
    if(!read(b,n,p,type))return ParseStatus::NeedMore;
    if(type>0x7f||!(type&0x10)||(type&6)==6)return ParseStatus::Invalid;
    h.has_properties=(type&1)!=0;h.subgroup_id_mode=uint8_t((type>>1)&3);
    h.end_of_group=(type&8)!=0;h.default_priority=(type&0x20)!=0;h.first_object=(type&0x40)!=0;
    if(!read(b,n,p,h.track_alias)||!read(b,n,p,h.group))return ParseStatus::NeedMore;
    if(h.subgroup_id_mode==2&&!read(b,n,p,h.subgroup))return ParseStatus::NeedMore;
    h.priority=128;
    if(!h.default_priority) {
        if(p==n)return ParseStatus::NeedMore;
        h.priority=b[p++];
    }
    h.consumed=p;out=h;return ParseStatus::Ok;
}
bool encode_text_properties(uint64_t timestamp,std::vector<uint8_t>& out) {
    out.clear();if(timestamp>kMaxVarint)return false;
    write(out,6);write(out,timestamp);return true;
}
bool encode_pcm_properties(const AudioProperties& a,std::vector<uint8_t>& out) {
    out.clear();
    if(a.timestamp_us>kMaxVarint||a.sample_cursor>kMaxVarint||a.source_silence_us>kMaxVarint||
       !a.duration_samples||a.payload_kind!=0)return false;
    write(out,6);write(out,a.timestamp_us);
    write(out,50-6);write(out,a.sample_cursor);
    write(out,52-50);write(out,a.duration_samples);
    uint64_t previous=52;
    if(a.has_audio_level) {write(out,54-previous);write(out,a.audio_level);previous=54;}
    if(a.source_silence_us) {write(out,58-previous);write(out,a.source_silence_us);}
    return true;
}
ParseStatus decode_audio_properties(const uint8_t* b,size_t n,AudioProperties& out) {
    out=AudioProperties{};
    if((!b&&n)||n>kMaxProperties)return ParseStatus::Invalid;
    size_t pos=0,count=0;uint64_t type=0;unsigned required=0;AudioProperties a;
    while(pos<n) {
        uint64_t delta=0,value=0;
        if(++count>64||!read(b,n,pos,delta))return ParseStatus::Invalid;
        if((count>1&&!delta)||type>kMaxVarint-delta)return ParseStatus::Invalid;
        type+=delta;
        if(!read(b,n,pos,value))return ParseStatus::Invalid;
        if(type&1) {
            if(value>n-pos)return ParseStatus::Invalid;
            pos+=size_t(value);continue;
        }
        switch(type) {
        case 6:a.timestamp_us=value;required|=1;break;
        case 50:a.sample_cursor=value;required|=2;break;
        case 52:
            if(!value||value>0xffffffffu)return ParseStatus::Invalid;
            a.duration_samples=uint32_t(value);required|=4;break;
        case 54:
            if(value>255)return ParseStatus::Invalid;
            a.has_audio_level=true;a.audio_level=uint8_t(value);break;
        case 56:
            if(value>2)return ParseStatus::Invalid;
            a.payload_kind=uint8_t(value);break;
        case 58:a.source_silence_us=value;break;
        default:break;
        }
    }
    if(required!=7)return ParseStatus::Invalid;
    out=a;return ParseStatus::Ok;
}
bool pcm_payload_size_matches(const AudioProperties& a,uint8_t channels,size_t bytes) {
    return a.payload_kind==0&&channels&&a.duration_samples&&
        uint64_t(a.duration_samples)*channels*2==bytes;
}
bool opus48_to_pcm24_properties(const AudioProperties& a,AudioProperties& out) {
    // RFC 6716 packets cover at most 120 ms, in multiples of 2.5 ms.
    // Single-frame media only: never treat a RED bundle as an Opus packet.
    if(a.payload_kind || !a.duration_samples || a.duration_samples>5760 ||
       a.duration_samples%120 || a.sample_cursor%2)return false;
    out=a;out.duration_samples/=2;out.sample_cursor/=2;return true;
}
bool encode_object(uint64_t id,uint64_t status,bool has_properties,
                   const std::vector<uint8_t>& properties,const uint8_t* payload,size_t size,
                   ObjectCursor& cursor,std::vector<uint8_t>& out) {
    out.clear();
    if(id>kMaxVarint||size>kMaxObject||properties.size()>kMaxProperties||(!payload&&size)||
       (!has_properties&&!properties.empty())||!valid_status(status)||(status!=0&&size))return false;
    uint64_t delta=id;
    if(cursor.has_previous) {
        if(cursor.previous>=id)return false;
        delta=id-cursor.previous-1;
    }
    write(out,delta);
    if(has_properties) {write(out,properties.size());out.insert(out.end(),properties.begin(),properties.end());}
    write(out,size);
    if(size)out.insert(out.end(),payload,payload+size);
    else write(out,status);
    cursor.has_previous=true;cursor.previous=id;return true;
}
ParseStatus decode_object(const uint8_t* b,size_t n,bool has_properties,
                          ObjectCursor& cursor,ObjectView& out) {
    out=ObjectView{};
    if(!b&&n)return ParseStatus::Invalid;
    size_t p=0;uint64_t id=0,length=0;ObjectView obj;
    if(!read(b,n,p,id))return ParseStatus::NeedMore;
    if(cursor.has_previous) {
        if(id==kMaxVarint||cursor.previous>kMaxVarint-id-1)return ParseStatus::Invalid;
        id+=cursor.previous+1;
    }
    obj.id=id;
    if(has_properties) {
        if(!read(b,n,p,length))return ParseStatus::NeedMore;
        if(length>kMaxProperties)return ParseStatus::Invalid;
        if(length>n-p)return ParseStatus::NeedMore;
        obj.properties=b+p;obj.properties_size=size_t(length);p+=size_t(length);
    }
    if(!read(b,n,p,length))return ParseStatus::NeedMore;
    if(length>kMaxObject)return ParseStatus::Invalid;
    if(length) {
        if(length>n-p)return ParseStatus::NeedMore;
        obj.payload=b+p;obj.payload_size=size_t(length);p+=size_t(length);
    } else {
        if(!read(b,n,p,obj.status))return ParseStatus::NeedMore;
        if(!valid_status(obj.status))return ParseStatus::Invalid;
    }
    obj.consumed=p;out=obj;cursor.has_previous=true;cursor.previous=id;return ParseStatus::Ok;
}
bool encode_datagram(const DatagramHeader& h,const std::vector<uint8_t>& props,
                     const uint8_t* payload,size_t size,std::vector<uint8_t>& out){
    out.clear();
    if(h.track_alias>kMaxVarint||h.group>kMaxVarint||h.subgroup>kMaxVarint||h.id>kMaxVarint||
       (!payload&&size)||size>1200||props.size()>1200)return false;
    write(out,0x41);out.push_back(uint8_t((h.end_of_group?1:0)|(!props.empty()?2:0)));
    write(out,h.track_alias);write(out,h.group);write(out,h.subgroup);write(out,h.id);out.push_back(h.priority);
    if(!props.empty()){write(out,props.size());out.insert(out.end(),props.begin(),props.end());}
    write(out,size);if(size)out.insert(out.end(),payload,payload+size);
    if(out.size()>1200){out.clear();return false;}return true;
}
ParseStatus decode_datagram(const uint8_t* data,size_t size,DatagramHeader& header,ObjectView& object){
    header=DatagramHeader{};object=ObjectView{};
    if(!data||!size||size>1200)return ParseStatus::Invalid;
    size_t at=0;uint64_t type=0,length=0;DatagramHeader h;ObjectView o;
    if(!read(data,size,at,type)||type!=0x41||at==size)return ParseStatus::Invalid;
    const uint8_t flags=data[at++];if(flags&~3)return ParseStatus::Invalid;
    h.end_of_group=(flags&1)!=0;
    if(!read(data,size,at,h.track_alias)||!read(data,size,at,h.group)||
       !read(data,size,at,h.subgroup)||!read(data,size,at,h.id)||at==size)return ParseStatus::Invalid;
    h.priority=data[at++];o.id=h.id;
    if(flags&2){
        if(!read(data,size,at,length)||length>size-at)return ParseStatus::Invalid;
        o.properties=data+at;o.properties_size=static_cast<size_t>(length);at+=o.properties_size;
    }
    if(!read(data,size,at,length)||length!=size-at)return ParseStatus::Invalid;
    o.payload=data+at;o.payload_size=static_cast<size_t>(length);o.consumed=size;
    header=h;object=o;return ParseStatus::Ok;
}
} // namespace aoq_wire
