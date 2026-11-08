#include "aoq_objects.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace aoq_wire;
using Bytes=std::vector<uint8_t>;
static Bytes hex(const char* s) {
    Bytes b;auto digit=[](char c){return c>='a'?c-'a'+10:c-'0';};
    while(*s){b.push_back(uint8_t((digit(s[0])<<4)|digit(s[1])));s+=2;}return b;
}
static void match(const char* name,const Bytes& b,const char* expected) {
    assert(b==hex(expected));
    std::printf("{\"name\":\"%s\",\"length\":%zu,\"hex\":\"",name,b.size());
    for(auto v:b)std::printf("%02x",v);
    std::puts("\"}");
}
int main() {
    SubgroupHeader h,decoded;Bytes b;
    assert(encode_subgroup_header(h,b));match("subgroup_default",b,"11412d0000");
    h.has_properties=false;assert(encode_subgroup_header(h,b));match("subgroup_no_properties",b,"10412d0000");
    h.has_properties=true;h.track_alias=401;h.group=123;h.subgroup=7;h.subgroup_id_mode=2;h.priority=128;
    assert(encode_subgroup_header(h,b));match("subgroup_explicit",b,"154191407b0780");
    h.end_of_group=h.default_priority=h.first_object=true;
    assert(encode_subgroup_header(h,b));match("subgroup_all_flags",b,"407d4191407b07");
    for(size_t n=0;n<b.size();++n)assert(decode_subgroup_header(b.data(),n,decoded)==ParseStatus::NeedMore);
    assert(decode_subgroup_header(b.data(),b.size(),decoded)==ParseStatus::Ok);
    assert(decoded.track_alias==401&&decoded.group==123&&decoded.subgroup==7&&decoded.priority==128&&decoded.has_properties&&decoded.end_of_group&&decoded.first_object);
    for(uint8_t type:{uint8_t(0x00),uint8_t(0x16),uint8_t(0x26)}) {
        assert(decode_subgroup_header(&type,1,decoded)==ParseStatus::Invalid);
    }
    Bytes props;assert(encode_text_properties(0,props));match("text_properties_zero",props,"0600");
    assert(encode_text_properties(1234567,props));match("text_properties_timestamp",props,"068012d687");
    const std::string payload="{\"type\":\"synthetic.test\"}";
    ObjectCursor cursor;const auto* p=reinterpret_cast<const uint8_t*>(payload.data());
    assert(encode_object(0,0,false,{},p,payload.size(),cursor,b));match("object_plain",b,"00197b2274797065223a2273796e7468657469632e74657374227d");
    assert(encode_object(1,0,false,{},p,payload.size(),cursor,b));match("object_second_delta",b,"00197b2274797065223a2273796e7468657469632e74657374227d");
    props=hex("068012d6870d03010203");
    assert(encode_object(4,0,true,props,p,payload.size(),cursor,b));match("object_with_properties_delta",b,"020a068012d6870d03010203197b2274797065223a2273796e7468657469632e74657374227d");
    ObjectView view;ObjectCursor reader{true,1};
    for(size_t n=0;n<b.size();++n) {
        assert(decode_object(b.data(),n,true,reader,view)==ParseStatus::NeedMore);
        assert(reader.previous==1&&view.consumed==0);
    }
    assert(decode_object(b.data(),b.size(),true,reader,view)==ParseStatus::Ok);
    assert(reader.previous==4&&view.id==4&&view.payload_size==payload.size()&&view.properties_size==10);
    assert(std::memcmp(view.payload,p,payload.size())==0);
    b.push_back(0x7f);reader={true,1};
    assert(decode_object(b.data(),b.size(),true,reader,view)==ParseStatus::Ok&&view.consumed==b.size()-1);
    for(uint64_t status:{uint64_t(0),uint64_t(3),uint64_t(4)}) {
        cursor={};assert(encode_object(0,status,true,{},nullptr,0,cursor,b));
        reader={};assert(decode_object(b.data(),b.size(),true,reader,view)==ParseStatus::Ok&&view.status==status&&view.payload_size==0);
    }
    for(const char* bytes:{"00000001","00000002","00000005","005001","0080010001"}) {
        b=hex(bytes);reader={};assert(decode_object(b.data(),b.size(),true,reader,view)==ParseStatus::Invalid);
        assert(!reader.has_previous&&view.consumed==0);
    }
    b=hex("01000000");reader={true,(uint64_t(1)<<62)-2};
    assert(decode_object(b.data(),b.size(),true,reader,view)==ParseStatus::Invalid);
    cursor={true,4};assert(!encode_object(4,0,true,{},p,payload.size(),cursor,b)&&b.empty());
    assert(!encode_object(5,0,true,{},nullptr,1,cursor,b)&&cursor.previous==4);
    assert(!encode_object(5,1,true,{},nullptr,0,cursor,b));
    assert(!encode_text_properties(uint64_t(1)<<62,b));
    AudioProperties ap,got;ap.duration_samples=320;
    assert(encode_pcm_properties(ap,b));match("pcm16k_properties_zero",b,"06002c00024140");
    ap.timestamp_us=1234567;ap.sample_cursor=320;
    assert(encode_pcm_properties(ap,b));match("pcm16k_properties_20ms",b,"068012d6872c4140024140");
    ap.sample_cursor=640;ap.has_audio_level=true;ap.audio_level=42;ap.source_silence_us=100000;
    assert(encode_pcm_properties(ap,b));match("pcm16k_properties_level_silence",b,"068012d6872c4280024140022a04800186a0");
    assert(decode_audio_properties(b.data(),b.size(),got)==ParseStatus::Ok&&got.audio_level==42&&got.source_silence_us==100000);
    assert(pcm_payload_size_matches(got,1,640)&&!pcm_payload_size_matches(got,1,960));
    ap=AudioProperties{};ap.timestamp_us=1234567;ap.duration_samples=480;
    assert(encode_pcm_properties(ap,b));match("pcm24k_properties_20ms",b,"068012d6872c000241e0");
    assert(decode_audio_properties(b.data(),b.size(),got)==ParseStatus::Ok&&got.duration_samples==480);
    Bytes pcm(960);for(size_t i=0;i<pcm.size();++i)pcm[i]=uint8_t(i);
    cursor={};assert(encode_object(0,0,true,b,pcm.data(),pcm.size(),cursor,props));
    reader={};assert(decode_object(props.data(),props.size(),true,reader,view)==ParseStatus::Ok);
    assert(pcm_payload_size_matches(got,1,view.payload_size)&&std::memcmp(view.payload,pcm.data(),pcm.size())==0);
    std::printf("{\"name\":\"pcm24k_singleframe_object\",\"length\":%zu,\"hex\":\"",props.size());
    for(auto v:props)std::printf("%02x",v);
    std::puts("\"}");
    for(const char* invalid:{"","0600","06002c000200","06002c000241400001","06002c000241400403"}) {
        b=hex(invalid);assert(decode_audio_properties(b.data(),b.size(),got)==ParseStatus::Invalid);
    }
    b=hex("06002c000241400401");
    assert(decode_audio_properties(b.data(),b.size(),got)==ParseStatus::Ok&&got.payload_kind==1);
    assert(!pcm_payload_size_matches(got,1,640)&&!encode_pcm_properties(got,b));
    AudioProperties wire,normalized;wire.timestamp_us=1234567;wire.sample_cursor=960;wire.duration_samples=960;
    assert(opus48_to_pcm24_properties(wire,normalized));
    assert(normalized.timestamp_us==1234567&&normalized.sample_cursor==480&&normalized.duration_samples==480);
    assert(pcm_payload_size_matches(normalized,1,960));
    for(unsigned duration:{120u,240u,480u,960u,1920u,2880u,3840u,4800u,5760u}){
        wire.duration_samples=duration;assert(opus48_to_pcm24_properties(wire,normalized)&&normalized.duration_samples==duration/2);
    }
    for(unsigned duration:{0u,1u,119u,121u,5761u,5880u,0xffffffffu}){
        wire.duration_samples=duration;assert(!opus48_to_pcm24_properties(wire,normalized));
    }
    wire.duration_samples=960;wire.sample_cursor=1;assert(!opus48_to_pcm24_properties(wire,normalized));
    wire.sample_cursor=0;wire.payload_kind=1;assert(!opus48_to_pcm24_properties(wire,normalized));
    wire.payload_kind=2;assert(!opus48_to_pcm24_properties(wire,normalized));
    std::puts("{\"result\":\"PASS\",\"scope\":\"SDK object fixtures, partial input, bounds, delta ownership, Opus sample clock\"}");
}
