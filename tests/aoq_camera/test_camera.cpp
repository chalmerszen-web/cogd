#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_board/p4_video_capture.cpp"
int64_t test_now=1000;
static unsigned encoded_calls=0;
static int encoder_error=0;
static bool oversized=false;
static std::function<void()> during_encode;
void* jpeg_alloc_encoder_mem(size_t n,const jpeg_encode_memory_alloc_cfg_t*,size_t* capacity){*capacity=n;return std::malloc(n);}
int jpeg_new_encoder_engine(const jpeg_encode_engine_cfg_t* config,jpeg_encoder_handle_t* out){assert(config->timeout_ms==100);*out=reinterpret_cast<void*>(1);return 0;}
int jpeg_encoder_process(jpeg_encoder_handle_t,const jpeg_encode_cfg_t* config,const void* in,size_t n,void* out,size_t capacity,uint32_t* count){
    assert(config->width==320&&config->height==256&&config->sub_sample==JPEG_DOWN_SAMPLING_YUV420&&n==320*256*3&&capacity==65536);
    ++encoded_calls;const auto* rgb=static_cast<const uint8_t*>(in);
    assert(rgb[1]==0&&((rgb[0]==0&&rgb[2]==255)||(rgb[0]==255&&rgb[2]==0))); // BGR24 red or blue
    std::memset(out,rgb[2],32);*count=oversized?65537:32;
    if(during_encode)during_encode();
    return encoder_error;
}
int main(){
    std::vector<uint16_t> red(640*512,0xf800),blue(640*512,0x001f);uint8_t out[65536];size_t bytes=99;unsigned seq=99;uint64_t timestamp=99;
    assert(p4_video_capture_start(0)==1);assert(p4_video_capture_start(1)==0);
    assert(p4_video_capture_copy(1,0,out,sizeof(out),&bytes,&seq,&timestamp)==3&&bytes==0);
    p4_video_capture_push(1,red.data(),640,512,1000000);assert(encoded_calls==1);
    assert(p4_video_capture_copy(1,0,out,sizeof(out),&bytes,&seq,&timestamp)==0&&bytes==32&&seq==1&&timestamp==1000000&&out[0]==255);
    assert(p4_video_capture_copy(1,seq,out,sizeof(out),&bytes,&seq,&timestamp)==3&&bytes==0);
    p4_video_capture_push(1,blue.data(),640,512,1999999);assert(encoded_calls==1);
    test_now=2501;assert(p4_video_capture_copy(1,0,out,sizeof(out),&bytes,&seq,&timestamp)==3); // stale
    test_now=2000;p4_video_capture_push(1,blue.data(),640,512,2000000);
    assert(p4_video_capture_copy(1,1,out,31,&bytes,&seq,&timestamp)==5&&bytes==0);
    assert(p4_video_capture_copy(1,1,out,sizeof(out),&bytes,&seq,&timestamp)==0&&out[0]==0&&seq==2);
    test_now=3000;encoder_error=1;p4_video_capture_push(1,red.data(),640,512,3000000);
    assert(p4_video_capture_copy(1,0,out,sizeof(out),&bytes,&seq,&timestamp)==16&&bytes==0);encoder_error=0;
    test_now=4000;oversized=true;p4_video_capture_push(1,red.data(),640,512,4000000);
    assert(p4_video_capture_copy(1,0,out,sizeof(out),&bytes,&seq,&timestamp)==5);oversized=false;
    assert(p4_video_capture_start(2)==0);
    p4_video_capture_push(1,red.data(),640,512,5000000); // stale producer cannot publish
    assert(p4_video_capture_copy(2,0,out,sizeof(out),&bytes,&seq,&timestamp)==3);
    during_encode=[](){assert(p4_video_capture_start(3)==0);};test_now=5000;
    p4_video_capture_push(2,red.data(),640,512,5000000);during_encode={};
    assert(p4_video_capture_copy(2,0,out,sizeof(out),&bytes,&seq,&timestamp)==4);
    assert(p4_video_capture_copy(3,0,out,sizeof(out),&bytes,&seq,&timestamp)==3);
    p4_video_capture_stop(2);p4_video_capture_push(3,blue.data(),640,512,5000000);
    assert(p4_video_capture_copy(3,0,out,sizeof(out),&bytes,&seq,&timestamp)==0&&seq==1&&out[0]==0);
    during_encode=[](){p4_video_capture_stop(3);};test_now=6000;
    p4_video_capture_push(3,red.data(),640,512,6000000);during_encode={};
    assert(p4_video_capture_copy(3,0,out,sizeof(out),&bytes,&seq,&timestamp)==4&&bytes==0);
    std::cout<<"PASS camera cache: freshness, generation supersession during encode, stop, failed capture revocation, size/rate bounds; JPEG driver mocked\n";
}
