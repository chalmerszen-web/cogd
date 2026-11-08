#include "aoq_wire.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace aoq_wire;
static std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    auto digit=[](char c){return c>='a'?c-'a'+10:c-'0';};
    while(*s){out.push_back(uint8_t((digit(s[0])<<4)|digit(s[1])));s+=2;}return out;
}
static void print(const char* name,const std::vector<uint8_t>& b) {
    std::printf("{\"name\":\"%s\",\"length\":%zu,\"hex\":\"",name,b.size());
    for(auto v:b)std::printf("%02x",v);
    std::printf("\"}\n");
}
int main() {
    std::string error;std::vector<uint8_t> out;
    const std::string token="0123456789abcdef0123456789abcdef";
    auto profile=pcm_six_track_profile();
    assert(encode_media_profile(profile,out,error));print("profile_pcm_six_tracks",out);
    assert(encode_client_setup(token,profile,out,error));print("client_pcm_six_tracks_control",out);
    assert(encode_client_setup(token,{},out,error));
    print("client_minimal_payload_draft",std::vector<uint8_t>(out.begin()+4,out.end()));
    assert(!encode_client_setup("",profile,out,error)&&out.empty());
    assert(!encode_client_setup(std::string(1025,'a'),profile,out,error));
    assert(encode_client_setup(token.c_str(),out));
    assert(!encode_client_setup(nullptr,out));
    profile[0].media[0].codec=255;assert(!encode_media_profile(profile,out,error));
    auto good=hex("6f000008010180ff0e120100");ServerSetup parsed;
    for(size_t n=0;n<good.size();++n)assert(decode_server_setup(good.data(),n,parsed,error)==ParseStatus::NeedMore);
    assert(decode_server_setup(good.data(),good.size(),parsed,error)==ParseStatus::Ok);
    assert(parsed.accepted()&&parsed.selected_version==1&&parsed.consumed==good.size());
    uint64_t version=9,status=9;size_t consumed=9;
    assert(parse_server_setup(good.data(),good.size(),&version,&status,&consumed)==ParseStatus::Ok);
    assert(version==1&&status==0&&consumed==good.size());
    assert(parse_server_setup(good.data(),3,&version,&status,&consumed)==ParseStatus::NeedMore);
    assert(version==0&&status==0&&consumed==0);
    good.push_back(0x42);assert(decode_server_setup(good.data(),good.size(),parsed,error)==ParseStatus::Ok&&parsed.consumed==good.size()-1);
    for(const char* fixture:{"6f00000fc0000000ff0000010180ff0e120100","6f000008070180ff0e120100"}) {
        auto b=hex(fixture);assert(decode_server_setup(b.data(),b.size(),parsed,error)==ParseStatus::Ok);
        assert(parsed.known_version()==(parsed.selected_version==kClientVersion));
    }
    auto rejected=hex("6f000008010180ff0e120101");
    assert(decode_server_setup(rejected.data(),rejected.size(),parsed,error)==ParseStatus::Ok&&!parsed.accepted());
    for(const char* fixture:{"0100","6f0000020100","6f000009010180ff0e12010000","6f000008010180ff0e120200","6f00000e010280ff0e12010080ff0e120100"}) {
        auto b=hex(fixture);assert(decode_server_setup(b.data(),b.size(),parsed,error)==ParseStatus::Invalid);
    }
    auto unknown=hex("6f00000b010209016180ff0e120100");
    assert(decode_server_setup(unknown.data(),unknown.size(),parsed,error)==ParseStatus::Ok&&parsed.accepted());
    std::puts("{\"result\":\"PASS\",\"scope\":\"setup codec bounds and SDK fixture comparison input\"}");
}
