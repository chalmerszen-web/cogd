#include "p4_status_voice.h"
#include "p4_status_voice_assets.h"
#include "esp_opus_dec.h"
#include <cstring>
#include <new>
extern const uint8_t cache_start[] asm("_binary_p4_status_voice_bin_start");
extern const uint8_t cache_end[] asm("_binary_p4_status_voice_bin_end");
namespace {
struct Voice { void *decoder=nullptr;const uint8_t *p=nullptr,*end=nullptr;unsigned skip=0,left=0; };
}
extern "C" const char *p4_status_voice_text(p4_status_phrase_t phrase)
{return unsigned(phrase)<P4_STATUS_COUNT?status_assets[phrase].text:"";}
extern "C" void p4_status_voice_close(void *context)
{
    auto *s=static_cast<Voice*>(context);if(!s)return;
    if(s->decoder)esp_opus_dec_close(s->decoder);
    delete s;
}
extern "C" void *p4_status_voice_open(p4_status_phrase_t phrase)
{
    if(unsigned(phrase)>=P4_STATUS_COUNT)return nullptr;
    const auto &a=status_assets[phrase];
    if(a.offset>size_t(cache_end-cache_start) || a.bytes>size_t(cache_end-cache_start)-a.offset)return nullptr;
    auto *s=new(std::nothrow) Voice;if(!s)return nullptr;
    esp_opus_dec_cfg_t config{};config.sample_rate=24000;config.channel=1;
    config.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_20_MS;config.self_delimited=false;
    if(esp_opus_dec_open(&config,sizeof(config),&s->decoder)!=ESP_AUDIO_ERR_OK){delete s;return nullptr;}
    s->p=cache_start+a.offset;s->end=s->p+a.bytes;s->skip=a.skip;s->left=a.samples;return s;
}
extern "C" int p4_status_voice_read(void *context,int16_t out[480])
{
    auto *s=static_cast<Voice*>(context);if(!s || !out)return -1;
    if(!s->left)return 0;
    if(s->end-s->p<2)return -1;
    unsigned n=s->p[0]|(unsigned(s->p[1])<<8);s->p+=2;
    if(!n || size_t(s->end-s->p)<n)return -1;
    esp_audio_dec_in_raw_t raw{};raw.buffer=const_cast<uint8_t*>(s->p);raw.len=n;
    esp_audio_dec_out_frame_t frame{};frame.buffer=reinterpret_cast<uint8_t*>(out);frame.len=960;
    esp_audio_dec_info_t info{};
    if(esp_opus_dec_decode(s->decoder,&raw,&frame,&info)!=ESP_AUDIO_ERR_OK ||
       raw.consumed!=n || frame.decoded_size!=960 || info.sample_rate!=24000 || info.channel!=1 || info.bits_per_sample!=16)return -1;
    s->p+=n;unsigned samples=480;
    if(s->skip){if(s->skip>=samples)return -1;samples-=s->skip;std::memmove(out,out+s->skip,samples*2);s->skip=0;}
    if(samples>s->left)samples=s->left;
    s->left-=samples;return static_cast<int>(samples);
}
