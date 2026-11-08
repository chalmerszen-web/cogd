// Fixed vectors from the official SDK's pure profile builder/serializer.
// BuildAoqMediaProfile -> NetMediaProfileToAoq -> ApplyAudioRedBundleConfig(true)
// -> Encode. No engine, network, credentials, or SDK is required by this test.
// SDK DLL SHA256: 9b9027c15967afb4db1699e9a45bb88d19508b8ca1ff1d86f5f43bf871a9d544
#include "aoq_wire.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace aoq_wire;
using Bytes=std::vector<uint8_t>;
static unsigned checks=0;
#define CHECK(x) do{++checks;assert(x);}while(0)
static Bytes hex(const char* input){
    Bytes out;auto digit=[](char c){assert((c>='0'&&c<='9')||(c>='a'&&c<='f'));return c<='9'?c-'0':c-'a'+10;};
    while(*input){assert(input[1]);out.push_back(uint8_t((digit(input[0])<<4)|digit(input[1])));input+=2;}return out;
}
struct Fixture {const char* name;bool audio_in,audio_out;size_t tracks,bytes;const char* wire;};
static const Fixture sdk[]={
    {"audio_realtime",true,true,6,388,
     "10005a01000100020004000000c903000e6d6f64656c5f617564696f5f696e10001a0100010b2000010121000400003e"
     "8022000101020004000003e810001e0100010b2000010121000400003e802200010123000101020004000003e8100037"
     "010001000200040000012d03000d6d6f64656c5f746578745f696e1000190100011430000b616f712e746578742e7631"
     "020004000003e810003a01000101020004000000ca03000f6d6f64656c5f617564696f5f6f757410001a0100010b2000"
     "010021000400005dc022000101020004000003e8100038010001010200040000012e03000e6d6f64656c5f746578745f"
     "6f75741000190100011430000b616f712e746578742e7631020004000003e8100037010001000200040000019103000a"
     "636f6d6d616e645f696e10001c0100011530000e616f712e636f6d6d616e642e7631020004000003e810003801000101"
     "0200040000019203000b636f6d6d616e645f6f757410001c0100011530000e616f712e636f6d6d616e642e7631020004"
     "000003e8"
    },
    {"tts",false,true,5,295,
     "100037010001000200040000012d03000d6d6f64656c5f746578745f696e1000190100011430000b616f712e74657874"
     "2e7631020004000003e810003a01000101020004000000ca03000f6d6f64656c5f617564696f5f6f757410001a010001"
     "0b2000010021000400005dc022000101020004000003e8100038010001010200040000012e03000e6d6f64656c5f7465"
     "78745f6f75741000190100011430000b616f712e746578742e7631020004000003e81000370100010002000400000191"
     "03000a636f6d6d616e645f696e10001c0100011530000e616f712e636f6d6d616e642e7631020004000003e810003801"
     "0001010200040000019203000b636f6d6d616e645f6f757410001c0100011530000e616f712e636f6d6d616e642e7631"
     "020004000003e8"
    },
    {"asr",true,false,5,327,
     "10005a01000100020004000000c903000e6d6f64656c5f617564696f5f696e10001a0100010b2000010121000400003e"
     "8022000101020004000003e810001e0100010b2000010121000400003e802200010123000101020004000003e8100037"
     "010001000200040000012d03000d6d6f64656c5f746578745f696e1000190100011430000b616f712e746578742e7631"
     "020004000003e8100038010001010200040000012e03000e6d6f64656c5f746578745f6f75741000190100011430000b"
     "616f712e746578742e7631020004000003e8100037010001000200040000019103000a636f6d6d616e645f696e10001c"
     "0100011530000e616f712e636f6d6d616e642e7631020004000003e8100038010001010200040000019203000b636f6d"
     "6d616e645f6f757410001c0100011530000e616f712e636f6d6d616e642e7631020004000003e8"
    },
};
int main(){
    std::string error;Bytes encoded;
    for(const auto& expected:sdk){
        const auto tracks=pcm_track_profile(expected.audio_in,expected.audio_out);
        CHECK(tracks.size()==expected.tracks);
        CHECK(encode_media_profile(tracks,encoded,error));
        CHECK(encoded.size()==expected.bytes);
        CHECK(encoded==hex(expected.wire));
        unsigned in=0,out=0,text_in=0,text_out=0,commands=0;
        for(const auto& track:tracks){
            if(track.alias==201){
                ++in;CHECK(track.name=="model_audio_in"&&track.direction==0&&track.media.size()==2);
                for(size_t i=0;i<2;++i){const auto& p=track.media[i];CHECK(p.codec==11&&p.audio_model==1&&p.sample_rate_hz==16000&&p.channels==1&&p.audio_payload_format==i);}
            }else if(track.alias==202){
                ++out;CHECK(track.name=="model_audio_out"&&track.direction==1&&track.media.size()==1);
                const auto& p=track.media[0];CHECK(p.codec==11&&p.audio_model==0&&p.sample_rate_hz==24000&&p.channels==1&&p.audio_payload_format==0);
            }else if(track.alias==301){++text_in;CHECK(track.direction==0&&track.media[0].schema=="aoq.text.v1");}
            else if(track.alias==302){++text_out;CHECK(track.direction==1&&track.media[0].schema=="aoq.text.v1");}
            else {++commands;CHECK((track.alias==401||track.alias==402)&&track.media[0].schema=="aoq.command.v1");}
        }
        CHECK(in==unsigned(expected.audio_in)&&out==unsigned(expected.audio_out));
        CHECK(text_in==1&&text_out==1&&commands==2);
        std::printf("SDK profile %s: %zu bytes matched\n",expected.name,encoded.size());
    }
    CHECK(encode_media_profile(pcm_six_track_profile(),encoded,error));
    CHECK(encoded==hex(sdk[0].wire));
    std::printf("PASS %u profile assertions against three independent SDK fixtures\n",checks);
}
