#include "aoq_video.h"
#include "aoq_media_control.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include "aoq_video_fixture.inc"
using namespace aoq_wire;
using Bytes=std::vector<uint8_t>;
static Bytes hex(const char* s){Bytes out;while(*s){unsigned n;assert(std::sscanf(s,"%2x",&n)==1);out.push_back(uint8_t(n));s+=2;}return out;}
int main(){
    // Independent official SDK v1.4.0 pure serializer vectors. No SDK/network
    // loaded. DLL SHA256 9b9027c15967afb4db1699e9a45bb88d19508b8ca1ff1d86f5f43bf871a9d544.
    Bytes encoded;std::string error;const auto track=jpeg_track(800,640);
    assert(encode_media_profile({track},encoded,error));
    assert(encoded==hex("10003d010001000200040000006503000e6d6f64656c5f766964656f5f696e10001e0100010110000400000320110004000002801200020001020004000003e8"));
    assert(encode_jpeg_properties(1234567,5,encoded)&&encoded==hex("068012d6871a010205"));
    SubgroupHeader h;h.track_alias=101;h.group=7;h.subgroup_id_mode=2;h.priority=128;
    assert(encode_subgroup_header(h,encoded)&&encoded==hex("154065070080"));
    MediaSetup selected;MediaBinding binding;binding.track=track.name;binding.alias=track.alias;binding.selected=track.media[0];selected.bindings={binding};
    assert(encode_media_setup(selected,encoded,error));MediaSetup decoded;
    assert(decode_media_setup(encoded.data(),encoded.size(),decoded,error)==ParseStatus::Ok);
    const auto& p=decoded.bindings[0].selected;assert(p.codec==1&&p.video_width==800&&p.video_height==640&&p.video_fps==1&&p.timescale==1000);
    assert(!encode_media_profile({jpeg_track(0,640)},encoded,error));
    assert(!encode_media_profile({jpeg_track(800,1081)},encoded,error));
    assert(!encode_media_profile({jpeg_track(800,640,31)},encoded,error));
    assert(!encode_jpeg_properties(uint64_t(1)<<62,0,encoded));
    for(const auto& image:{Bytes(fixture_red_square,fixture_red_square+sizeof(fixture_red_square)),Bytes(fixture_blue_circle,fixture_blue_circle+sizeof(fixture_blue_circle))}){
        assert(jpeg_dimensions_match(image.data(),image.size(),320,256));
        assert(!jpeg_dimensions_match(image.data(),image.size(),256,320));
        assert(!jpeg_dimensions_match(image.data(),image.size()-1,320,256));
        assert(encode_jpeg_stream(image.data(),image.size(),320,256,1234567,7,encoded));
        assert(decode_subgroup_header(encoded.data(),encoded.size(),h)==ParseStatus::Ok&&h.track_alias==101&&h.group==7&&h.subgroup==0);
        size_t at=h.consumed;ObjectCursor cursor;ObjectView object;
        assert(decode_object(encoded.data()+at,encoded.size()-at,true,cursor,object)==ParseStatus::Ok);
        assert(object.id==0&&object.status==0&&object.payload_size==image.size()&&!std::memcmp(image.data(),object.payload,image.size()));at+=object.consumed;
        assert(decode_object(encoded.data()+at,encoded.size()-at,true,cursor,object)==ParseStatus::Ok);
        assert(object.id==1&&object.status==3&&object.payload_size==0&&at+object.consumed==encoded.size());
    }
    Bytes broken(fixture_red_square,fixture_red_square+sizeof(fixture_red_square));broken[0]=0;
    assert(!jpeg_dimensions_match(broken.data(),broken.size(),320,256));
    assert(!encode_jpeg_stream(broken.data(),broken.size(),320,256,0,0,encoded)&&encoded.empty());
    std::puts("PASS JPEG SDK profile/LOC/header vectors, media selection, dimensions, independent frame+terminal encoding");
}
