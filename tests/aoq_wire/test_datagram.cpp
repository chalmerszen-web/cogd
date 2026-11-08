#include "aoq_objects.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace aoq_wire;
std::vector<uint8_t> hex(const char* text){
    std::vector<uint8_t> out;unsigned n=0;
    while(*text){assert(std::sscanf(text,"%2x",&n)==1);out.push_back(uint8_t(n));text+=2;}return out;
}
int main(){
    // Literal outputs from the official Windows SDK 1.4.0 pure serializer;
    // DLL SHA256 9b9027c15967afb4db1699e9a45bb88d19508b8ca1ff1d86f5f43bf871a9d544.
    const char* wires[]={"40410040c90000000a03102030","40410240c90000000a0706002c0002414003102030",
        "40410040c90700040a03102030","40410140c90000000a03102030","40410040c90000000a00",
        "4041028000ffff8012d687004309ff02060103102030","40410040c90003000a03102030",
        "4041003f40407fff800040000a03102030",
        "404100ffffffffffffffffc00000004000000080010000ffffffffffffffff0a03102030",
        "40410340c90000000a02060000"};
    const uint8_t pcm[]={0x10,0x20,0x30};unsigned assertions=0;
    for(unsigned i=0;i<10;++i){
        DatagramHeader h;std::vector<uint8_t> props,out;size_t bytes=3;
        if(i==1)props={6,0,44,0,2,0x41,0x40};
        if(i==2){h.group=7;h.id=4;}
        if(i==3)h.end_of_group=true;
        if(i==4)bytes=0;
        if(i==5){h.track_alias=65535;h.group=1234567;h.id=777;h.priority=255;props={6,1};}
        if(i==6)h.subgroup=3;
        if(i==7){h.track_alias=63;h.group=64;h.subgroup=16383;h.id=16384;}
        if(i==8){h.track_alias=(uint64_t(1)<<62)-1;h.group=uint64_t(1)<<30;h.subgroup=65536;h.id=h.track_alias;}
        if(i==9){h.end_of_group=true;props={6,0};bytes=0;}
        assert(encode_datagram(h,props,bytes?pcm:nullptr,bytes,out));assert(out==hex(wires[i]));assertions+=2;
        DatagramHeader got;ObjectView object;
        assert(decode_datagram(out.data(),out.size(),got,object)==ParseStatus::Ok);
        assert(got.track_alias==h.track_alias&&got.group==h.group&&got.subgroup==h.subgroup&&got.id==h.id&&got.priority==h.priority&&got.end_of_group==h.end_of_group);
        assert(object.properties_size==props.size()&&object.payload_size==bytes&&object.status==0);assertions+=3;
        for(size_t n=0;n<out.size();++n){assert(decode_datagram(out.data(),n,got,object)==ParseStatus::Invalid);++assertions;}
        out.push_back(0);assert(decode_datagram(out.data(),out.size(),got,object)==ParseStatus::Invalid);++assertions;
    }
    DatagramHeader h;std::vector<uint8_t> out,large(1200);ObjectView object;
    assert(!encode_datagram(h,{},large.data(),large.size(),out)&&out.empty());
    h.id=uint64_t(1)<<62;assert(!encode_datagram(h,{},pcm,3,out));h.id=0;
    assert(!encode_datagram(h,{},nullptr,3,out));
    auto invalid=hex(wires[0]);invalid[2]=4;assert(decode_datagram(invalid.data(),invalid.size(),h,object)==ParseStatus::Invalid);
    std::printf("PASS %u SDK literal datagram checks plus bounds\n",assertions);
}
