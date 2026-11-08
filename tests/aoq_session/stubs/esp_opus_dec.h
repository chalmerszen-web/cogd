#pragma once
// Scripted platform boundary, NOT an Opus decoder. Real codec/audio behavior
// is checked on P4; these host cases exercise consumption/format validation.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr int ESP_AUDIO_ERR_OK=0;
constexpr int ESP_OPUS_DEC_FRAME_DURATION_120_MS=120;
struct esp_opus_dec_cfg_t {int sample_rate=0,channel=0,frame_duration=0;bool self_delimited=false;};
struct esp_audio_dec_in_raw_t {uint8_t* buffer=nullptr;size_t len=0,consumed=0;};
struct esp_audio_dec_out_frame_t {uint8_t* buffer=nullptr;size_t len=0,decoded_size=0;};
struct esp_audio_dec_info_t {unsigned sample_rate=0,channel=0,bits_per_sample=0;};
namespace test_opus {
inline unsigned fault=0,opened=0,decoded=0,live=0;
inline void reset(unsigned next=0){assert(live==0);fault=next;opened=decoded=0;}
}
inline int esp_opus_dec_open(const esp_opus_dec_cfg_t* c,size_t bytes,void** out){
 assert(bytes==sizeof(*c)&&c->sample_rate==24000&&c->channel==1);
 assert(c->frame_duration==120&&!c->self_delimited);
 ++test_opus::opened;
 if(test_opus::fault==7)return -1;
 *out=new unsigned(0xabc);++test_opus::live;return ESP_AUDIO_ERR_OK;
}
inline int esp_opus_dec_decode(void* state,esp_audio_dec_in_raw_t* in,
 esp_audio_dec_out_frame_t* out,esp_audio_dec_info_t* info){
 assert(state&&in->buffer&&in->len==3&&out->buffer&&out->len>=960);
 ++test_opus::decoded;
 if(test_opus::fault==6)return -1;
 in->consumed=in->len-(test_opus::fault==1);
 out->decoded_size=test_opus::fault==2?958:960;
 std::memset(out->buffer,0,out->decoded_size);
 info->sample_rate=test_opus::fault==3?48000:24000;
 info->channel=test_opus::fault==4?2:1;
 info->bits_per_sample=test_opus::fault==5?8:16;
 return ESP_AUDIO_ERR_OK;
}
inline void esp_opus_dec_close(void* p){assert(p&&test_opus::live);delete static_cast<unsigned*>(p);--test_opus::live;}
