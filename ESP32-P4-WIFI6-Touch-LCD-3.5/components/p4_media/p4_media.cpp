#include "p4_media.h"
#include "../p4_board/p4_agent.h"
#include "wels/codec_api.h"
#include "esp_aac_dec.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "driver/jpeg_encode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

extern "C" int p4_media_background_wait(unsigned generation) {
    while(p4_visual_current(generation) && p4_media_audio_reserve())vTaskDelay(pdMS_TO_TICKS(5));
    return p4_visual_current(generation)?0:4;
}
namespace {
// Larger bounded ranges avoid a TLS handshake for each small interleaved
// video/audio chunk. The 192 KiB increase fits the measured P4 PSRAM margin.
constexpr size_t FONT_END=0x180000, RANGE=256*1024, MAX_FILE=64*1024*1024;
constexpr size_t AUDIO_START=FONT_END+4096;
constexpr unsigned RATE=16000,W=320,H=480,MAX_SECONDS=15;
enum Stage {IDLE,PREPARING,PLAYING,PAUSED,ENDED,FAILED};
std::atomic<Stage> stage{IDLE};
std::atomic<bool> running{false},pause_requested{false};
std::atomic<bool> voice_hold{false};
bool paused(){return pause_requested || voice_hold;}
std::atomic<unsigned> cache_generation{0},position_samples{0},replays{0};
struct Free { void operator()(void *p) const {free(p);} };
using Memory=std::unique_ptr<uint8_t,Free>;
struct Reader {
    esp_http_client_handle_t http=nullptr;
    std::vector<uint8_t> cache;
    int64_t base=-1,total=0,range_total=0;
    unsigned generation,requests=0;
    int64_t network_us=0;
    explicit Reader(unsigned g):generation(g) {cache.resize(RANGE);}
    ~Reader(){if(http)esp_http_client_cleanup(http);}
    static esp_err_t event(esp_http_client_event_t *e) {
        auto r=static_cast<Reader *>(e->user_data);
        if(e->event_id==HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key,"Content-Range")) {
            auto slash=strchr(e->header_value,'/');
            if(slash)r->range_total=strtoll(slash+1,nullptr,10);
        }
        return ESP_OK;
    }
    bool open(const char *url) {
        if(strncmp(url,"https://",8) && strncmp(url,"http://",7))return false;
        esp_http_client_config_t c={};c.url=url;c.timeout_ms=20000;c.buffer_size=4096;
        c.crt_bundle_attach=esp_crt_bundle_attach;c.event_handler=event;c.user_data=this;
        c.disable_auto_redirect=true;c.keep_alive_enable=true;
        http=esp_http_client_init(&c);return http && fill(0);
    }
    bool fill(int64_t offset) {
        int64_t began=esp_timer_get_time();
        if(!p4_visual_current(generation) || offset<0 || offset>=MAX_FILE)return false;
        int64_t at=offset/RANGE*RANGE, end=at+RANGE-1;
        if(total)end=std::min(end,total-1);
        char value[90];snprintf(value,sizeof(value),"bytes=%lld-%lld",(long long)at,(long long)end);
        esp_http_client_set_header(http,"Range",value);range_total=0;
        if(esp_http_client_open(http,0)!=ESP_OK)return false;
        int64_t length=esp_http_client_fetch_headers(http);
        if(esp_http_client_get_status_code(http)!=206 || range_total<=0 || range_total>MAX_FILE ||
           (total && total!=range_total) || length<=0 || length>RANGE) {
            esp_http_client_close(http);return false;
        }
        total=range_total;
        if(length!=std::min<int64_t>(RANGE,total-at)){esp_http_client_close(http);return false;}
        size_t used=0;
        while(used<size_t(length)) {
            if(p4_media_background_wait(generation)){esp_http_client_close(http);return false;}
            int n=esp_http_client_read(http,reinterpret_cast<char *>(cache.data()+used),length-used);
            if(n<=0){esp_http_client_close(http);return false;}
            used+=n;
        }
        // Fully consuming the body permits reuse of this TLS connection.
        base=at;++requests;network_us+=esp_timer_get_time()-began;return true;
    }
    bool read(uint64_t at,void *to,size_t n) {
        if(at>uint64_t(total) || n>uint64_t(total)-at)return false;
        auto dst=static_cast<uint8_t *>(to);
        while(n) {
            if(base<0 || at<uint64_t(base) || at>=uint64_t(base)+RANGE)if(!fill(at))return false;
            size_t count=std::min(n,RANGE-size_t(at-base));
            memcpy(dst,cache.data()+at-base,count);dst+=count;at+=count;n-=count;
        }
        return true;
    }
    static int callback(int64_t at,void *dst,size_t n,void *ctx) {
        return at>=0 && static_cast<Reader *>(ctx)->read(at,dst,n)?0:1;
    }
};
uint32_t be32(const uint8_t *p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint64_t be64(const uint8_t *p){return uint64_t(be32(p))<<32|be32(p+4);}
constexpr uint32_t tag(char a,char b,char c,char d){return uint32_t(a)<<24|uint32_t(b)<<16|uint32_t(c)<<8|uint32_t(d);}
struct Timeline { std::vector<int32_t> ctts;int64_t edit=0; };
/* minimp4 supplies sample tables; retain composition offsets and edit-list
 * priming separately so B pictures and AAC priming share the same clock. */
bool timing(Reader &r,uint64_t start,uint64_t stop,std::vector<Timeline> &times,int track=-1,int depth=0) {
    if(depth>7)return false;
    while(start<stop) {
        uint8_t h[24];if(stop-start<8 || !r.read(start,h,8))return false;
        uint64_t size=be32(h);size_t head=8;auto type=be32(h+4);
        if(size==1){if(!r.read(start+8,h+8,8))return false;size=be64(h+8);head=16;}
        if(!size)size=stop-start;
        if(size<head || size>stop-start)return false;
        uint64_t pos=start+head,end=start+size;
        if(type==tag('t','r','a','k')) {
            if(times.size()>=4)return false;times.emplace_back();
            if(!timing(r,pos,end,times,int(times.size()-1),depth+1))return false;
        } else if(type==tag('m','o','o','v') || type==tag('m','d','i','a') ||
                  type==tag('m','i','n','f') || type==tag('s','t','b','l') || type==tag('e','d','t','s')) {
            if(!timing(r,pos,end,times,track,depth+1))return false;
        } else if(type==tag('c','t','t','s') && track>=0) {
            if(end-pos<8 || !r.read(pos,h,8))return false;
            unsigned count=be32(h+4);if(count>(end-pos-8)/8 || count>2000)return false;
            pos+=8;auto &out=times[track].ctts;
            for(unsigned i=0;i<count;i++,pos+=8) {
                if(!r.read(pos,h,8))return false;
                unsigned n=be32(h);int32_t shift=static_cast<int32_t>(be32(h+4));
                if(n>2000-out.size())return false;out.insert(out.end(),n,shift);
            }
        } else if(type==tag('e','l','s','t') && track>=0) {
            if(end-pos<8 || !r.read(pos,h,8))return false;
            unsigned count=be32(h+4),version=h[0];
            // Ordinary WAN MP4 has one media edit; do not silently ignore gaps.
            if(count!=1 || version>1 || end-pos<8+(version?20:12))return false;
            if(!r.read(pos+8,h,version?20:12))return false;
            times[track].edit=version?int64_t(be64(h+8)):int32_t(be32(h+4));
            if(times[track].edit<0)return false;
        }
        start=end;
    }
    return true;
}
struct Demux { MP4D_demux_t m={};bool opened=false;~Demux(){if(opened)MP4D_close(&m);} };
struct Packet {uint64_t offset;unsigned bytes,track,index;int64_t pts;};
struct Frame {unsigned offset,bytes;int64_t pts;};
struct Decoders {
    ISVCDecoder *video=nullptr;void *audio=nullptr;jpeg_encoder_handle_t jpeg=nullptr;
    ~Decoders(){if(video){video->Uninitialize();WelsDestroyDecoder(video);}if(audio)esp_aac_dec_close(audio);if(jpeg)jpeg_del_encoder_engine(jpeg);}
};
struct Cache {
    const esp_partition_t *partition=nullptr;
    unsigned audio_bytes=0,cursor=0,decoded=0;std::vector<Frame> frames;
    uint32_t min_heap=UINT32_MAX,min_internal=UINT32_MAX;int64_t duration=0,prepare_us=0;
    int64_t decode_us=0,convert_us=0,jpeg_us=0,flash_us=0,network_us=0;
    unsigned source_samples=0,requests=0,skipped=0;
    void progress(const char *stage,unsigned generation) {
        min_heap=std::min(min_heap,esp_get_free_heap_size());
        min_internal=std::min<uint32_t>(min_internal,heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        char s[320];snprintf(s,sizeof(s),"{\"state\":\"%s\",\"decoded_frames\":%u,\"cached_frames\":%u,\"cache_bytes\":%u,\"min_heap\":%lu,\"min_internal_heap\":%lu}",
            stage,decoded,unsigned(frames.size()),cursor-FONT_END,(unsigned long)min_heap,(unsigned long)min_internal);
        p4_cloud_media_status(generation,s);
        if(decoded==1 || (decoded%15)==0) {
            char progress[100];snprintf(progress,sizeof(progress),"视频准备中 %u/%u",decoded+skipped,source_samples);
            p4_visual_message(generation,progress);
        }
    }
};
uint8_t byte(int v){return uint8_t(std::clamp(v,0,255));}
int prepare(Reader &r,Cache &cache,unsigned generation,unsigned fps) {
    int64_t began=esp_timer_get_time();std::vector<Timeline> times;
    if(!timing(r,0,r.total,times))return 6;
    Demux dm;dm.opened=MP4D_open(&dm.m,Reader::callback,&r,r.total)!=0;
    if(!dm.opened || dm.m.track_count!=times.size())return 6;
    int vt=-1,at=-1;
    for(unsigned i=0;i<dm.m.track_count;i++) {
        if(dm.m.track[i].object_type_indication==0x21)vt=int(i);
        if(dm.m.track[i].object_type_indication==0x40)at=int(i);
    }
    if(vt<0)return 7;
    auto &v=dm.m.track[vt];
    cache.source_samples=v.sample_count;
    if(!v.timescale || !v.sample_count || v.sample_count>1000 || v.SampleDescription.video.width>1280 ||
       v.SampleDescription.video.height>1280 ||
       uint64_t(v.SampleDescription.video.width)*v.SampleDescription.video.height>1280*720)return 5;
    std::vector<Packet> packets;std::vector<int64_t> video_pts(v.sample_count);
    for(unsigned t=0;t<dm.m.track_count;t++)if(int(t)==vt || int(t)==at) {
        auto &tr=dm.m.track[t];if(!tr.timescale || tr.sample_count>2000)return 5;
        if(!times[t].ctts.empty() && times[t].ctts.size()!=tr.sample_count)return 6;
        for(unsigned i=0;i<tr.sample_count;i++) {
            unsigned bytes=0,dts=0,duration=0;auto offset=MP4D_frame_offset(&dm.m,t,i,&bytes,&dts,&duration);
            if(!bytes || bytes>1024*1024 || offset+bytes>uint64_t(r.total))return 5;
            int64_t pts=(int64_t(dts)+(times[t].ctts.empty()?0:times[t].ctts[i])-times[t].edit)*1000000/tr.timescale;
            packets.push_back({offset,bytes,t,i,pts});
            if(int(t)==vt) {video_pts[i]=pts;cache.duration=std::max(cache.duration,pts+int64_t(duration)*1000000/tr.timescale);}
        }
    }
    if(cache.duration<=0 || cache.duration>MAX_SECONDS*1000000LL)return 5;
    // A discarded non-reference AVC picture cannot feed a later picture.
    // Keep every reference picture and every picture selected for the LCD;
    // skip only non-reference pictures whose presentation time is not used.
    // Selection is in PTS order, independent of MP4/B-frame decode order.
    std::vector<unsigned> presentation(video_pts.size());
    for(unsigned i=0;i<presentation.size();i++)presentation[i]=i;
    std::sort(presentation.begin(),presentation.end(),[&](unsigned a,unsigned b){return video_pts[a]<video_pts[b];});
    std::vector<bool> display_sample(video_pts.size(),false);int64_t sample_at=0;
    for(auto i:presentation)if(video_pts[i]>=0 && video_pts[i]+2>=sample_at) {
        display_sample[i]=true;do {sample_at+=1000000/fps;}while(sample_at<=video_pts[i]);
    }
    std::sort(packets.begin(),packets.end(),[](auto &a,auto &b){return a.offset<b.offset;});
    cache.partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"media");
    if(!cache.partition)return 2;
    cache.audio_bytes=unsigned(cache.duration*RATE/1000000)*2;
    cache.cursor=AUDIO_START+((cache.audio_bytes+4095)&~4095u);
    int64_t erased_at=esp_timer_get_time();
    /* A multi-megabyte erase can monopolize the shared Flash/cache path even
     * at low task priority. Bound each operation and let speech refill first. */
    for(size_t at=FONT_END;at<cache.partition->size;at+=4096) {
        if(p4_media_background_wait(generation))return 4;
        if(esp_partition_erase_range(cache.partition,at,4096)!=ESP_OK)return 16;
        vTaskDelay(1);
    }
    cache.flash_us+=esp_timer_get_time()-erased_at;
    // NOR bytes must only be programmed once after erase. Unwritten int16
    // samples are -1 (one LSB of DC), also the silent track when AAC is absent.
    Decoders codecs;
    if(WelsCreateDecoder(&codecs.video) || !codecs.video)return 19;
    int level=WELS_LOG_ERROR;codecs.video->SetOption(DECODER_OPTION_TRACE_LEVEL,&level);
    SDecodingParam config={};config.sVideoProperty.eVideoBsType=VIDEO_BITSTREAM_AVC;config.eEcActiveIdc=ERROR_CON_DISABLE;
    if(codecs.video->Initialize(&config))return 7;
    unsigned arate=0,channels=0;
    if(at>=0) {
        auto &a=dm.m.track[at];arate=a.SampleDescription.audio.samplerate_hz;channels=a.SampleDescription.audio.channelcount;
        if(arate<8000 || arate>48000 || !channels || channels>2)return 7;
        esp_aac_dec_cfg_t c={};c.sample_rate=arate;c.channel=channels;c.bits_per_sample=16;c.no_adts_header=true;
        if(esp_aac_dec_open(&c,sizeof(c),&codecs.audio)!=ESP_AUDIO_ERR_OK)return 7;
    }
    jpeg_encode_engine_cfg_t jc={};jc.timeout_ms=1000;
    if(jpeg_new_encoder_engine(&jc,&codecs.jpeg)!=ESP_OK)return 19;
    jpeg_encode_memory_alloc_cfg_t alloc={};alloc.buffer_direction=JPEG_ENC_ALLOC_INPUT_BUFFER;
    size_t rgb_size=0,out_size=0;
    Memory rgb(static_cast<uint8_t *>(jpeg_alloc_encoder_mem(W*H*3,&alloc,&rgb_size)));
    alloc.buffer_direction=JPEG_ENC_ALLOC_OUTPUT_BUFFER;
    Memory jpeg(static_cast<uint8_t *>(jpeg_alloc_encoder_mem(W*H,&alloc,&out_size)));
    if(!rgb || !jpeg)return 19;
    jpeg_encode_cfg_t jpg={};jpg.width=W;jpg.height=H;jpg.src_type=JPEG_ENCODE_IN_FORMAT_RGB888;
    jpg.sub_sample=JPEG_DOWN_SAMPLING_YUV420;jpg.image_quality=70;
    std::vector<uint8_t> data;int64_t next_frame=0;unsigned phase=0;int16_t previous=0;int64_t audio_position=INT64_MIN;
    Memory scratch(static_cast<uint8_t *>(heap_caps_malloc(16384,MALLOC_CAP_SPIRAM)));
    if(!scratch)return 19;
    auto audio_pcm=reinterpret_cast<int16_t *>(scratch.get());auto mono=audio_pcm+4096;
    auto save_frame=[&](unsigned char **planes,SBufferInfo &info)->int {
        if(!info.iBufferStatus)return 0;
        if(p4_media_background_wait(generation))return 4;
        ++cache.decoded;uint64_t index=info.uiOutYuvTimeStamp;
        if(!index || index>video_pts.size())return 6;
        int64_t pts=video_pts[index-1];if(pts<0 || pts+2<next_frame)return 0;
        auto &s=info.UsrData.sSystemBuffer;int sw=s.iWidth,sh=s.iHeight;
        if(sw<=0 || sh<=0 || sw>1280 || sh>1280)return 5;
        int64_t measured=esp_timer_get_time();
        memset(rgb.get(),0,W*H*3);int dh=H,dw=sw*H/sh;if(dw>int(W)){dw=W;dh=sh*W/sw;}
        int ox=(W-dw)/2,oy=(H-dh)/2;
        for(int y=0;y<dh;y++)for(int x=0;x<dw;x++) {
            int sx=x*sw/dw,sy=y*sh/dh;
            int yy=int(planes[0][sy*s.iStride[0]+sx])-16;
            int u=int(planes[1][sy/2*s.iStride[1]+sx/2])-128,vv=int(planes[2][sy/2*s.iStride[1]+sx/2])-128;
            auto dst=rgb.get()+((y+oy)*W+x+ox)*3;
            dst[0]=byte((298*yy+516*u+128)>>8); // JPEG RGB888 API uses packed BGR24.
            dst[1]=byte((298*yy-100*u-208*vv+128)>>8);dst[2]=byte((298*yy+409*vv+128)>>8);
        }
        uint32_t bytes=0;
        cache.convert_us+=esp_timer_get_time()-measured;measured=esp_timer_get_time();
        unsigned count=(cache.duration*fps+999999)/1000000;
        unsigned budget=(cache.partition->size-AUDIO_START-((cache.audio_bytes+4095)&~4095u))/count;
        jpg.image_quality=70;
        for(;;) {
            if(jpeg_encoder_process(codecs.jpeg,&jpg,rgb.get(),W*H*3,jpeg.get(),out_size,&bytes)!=ESP_OK)return 16;
            if(bytes<=budget)break;
            if(jpg.image_quality<=15)return 5;
            jpg.image_quality=std::max(15,int(jpg.image_quality)*3/4);
        }
        if(!bytes || cache.cursor+bytes>cache.partition->size)return 5;
        cache.jpeg_us+=esp_timer_get_time()-measured;measured=esp_timer_get_time();
        for(size_t at=0;at<bytes;at+=4096) {
            if(p4_media_background_wait(generation))return 4;
            if(esp_partition_write(cache.partition,cache.cursor+at,jpeg.get()+at,std::min<size_t>(4096,bytes-at))!=ESP_OK)return 16;
            vTaskDelay(1);
        }
        cache.flash_us+=esp_timer_get_time()-measured;
        cache.frames.push_back({cache.cursor,bytes,pts});cache.cursor+=bytes;
        do {next_frame+=1000000/fps;}while(next_frame<=pts);
        return 0;
    };
    // avcC SPS/PPS precede length-prefixed sample payloads.
    for(unsigned kind=0;kind<2;kind++)for(unsigned n=0;n<32;n++) {
        int bytes=0;auto p=kind?MP4D_read_pps(&dm.m,vt,n,&bytes):MP4D_read_sps(&dm.m,vt,n,&bytes);
        if(!p)break;if(bytes<=0 || bytes>65536)return 6;
        auto source=static_cast<const uint8_t *>(p);
        data.assign({0,0,0,1});data.insert(data.end(),source,source+bytes);
        unsigned char *planes[3]={};SBufferInfo info={};
        int err=codecs.video->DecodeFrameNoDelay(data.data(),data.size(),planes,&info);if(err)return 7;
    }
    for(auto &p:packets) {
        if(p4_media_background_wait(generation))return 4;
        data.resize(p.bytes+4);if(!r.read(p.offset,data.data(),p.bytes))return 8;
        if(int(p.track)==vt) {
            // WAN AVC uses four-byte lengths. Preserve original compressed NALs.
            bool reference=false,vcl=false,other=false;
            size_t pos=0;while(pos<p.bytes) {
                if(p.bytes-pos<4)return 6;unsigned bytes=be32(data.data()+pos);
                if(!bytes || bytes>p.bytes-pos-4)return 6;
                unsigned nal=data[pos+4],kind=nal&31;
                if(kind==1 || kind==5){vcl=true;reference|=(nal&0x60)!=0 || kind==5;}
                else other=true;
                data[pos]=data[pos+1]=data[pos+2]=0;data[pos+3]=1;pos+=4+bytes;
            }
            if(vcl && !reference && !other && !display_sample[p.index]){++cache.skipped;continue;}
            unsigned char *planes[3]={};SBufferInfo info={};info.uiInBsTimeStamp=p.index+1;
            int64_t measured=esp_timer_get_time();
            int err=codecs.video->DecodeFrameNoDelay(data.data(),p.bytes,planes,&info);
            cache.decode_us+=esp_timer_get_time()-measured;
            if(err){printf("@media_decode_error %d packet=%u\n",err,p.index);return 7;}
            if((err=save_frame(planes,info)))return err;
            cache.progress("preparing",generation);
        } else {
            esp_audio_dec_in_raw_t raw={};raw.buffer=data.data();raw.len=p.bytes;
            esp_audio_dec_out_frame_t out={};out.buffer=reinterpret_cast<uint8_t *>(audio_pcm);out.len=8192;
            esp_audio_dec_info_t info={};
            if(esp_aac_dec_decode(codecs.audio,&raw,&out,&info)!=ESP_AUDIO_ERR_OK || raw.consumed!=p.bytes)return 7;
            if(audio_position==INT64_MIN)audio_position=p.pts*RATE/1000000;
            unsigned n=0;int64_t start=audio_position;
            for(unsigned i=0;i<out.decoded_size/2;i+=channels) {
                int16_t value=channels==2?int16_t((int(audio_pcm[i])+audio_pcm[i+1])/2):audio_pcm[i];
                phase+=RATE;
                while(phase>=arate){phase-=arate;mono[n++]=int16_t(value+(int(previous)-value)*int(phase)/int(RATE));++audio_position;}
                previous=value;
            }
            unsigned skip=start<0?std::min<int64_t>(n,-start):0;start+=skip;n-=skip;
            if(n && start<int64_t(cache.audio_bytes/2)) {
                n=std::min<unsigned>(n,cache.audio_bytes/2-unsigned(start));
                int64_t measured=esp_timer_get_time();
                if(esp_partition_write(cache.partition,AUDIO_START+start*2,mono+skip,n*2)!=ESP_OK)return 16;
                cache.flash_us+=esp_timer_get_time()-measured;
            }
        }
        vTaskDelay(1);
    }
    int eos=1;codecs.video->SetOption(DECODER_OPTION_END_OF_STREAM,&eos);
    for(unsigned i=0;i<32;i++) {
        unsigned char *planes[3]={};SBufferInfo info={};int e=codecs.video->FlushFrame(planes,&info);
        if(e)return 7;if(!info.iBufferStatus)break;if((e=save_frame(planes,info)))return e;
    }
    cache.prepare_us=esp_timer_get_time()-began;
    cache.network_us=r.network_us;cache.requests=r.requests;
    if(cache.frames.empty() || cache.decoded+cache.skipped!=v.sample_count ||
       cache.frames.size()!=std::count(display_sample.begin(),display_sample.end(),true))return 7;
    return 0;
}
struct DiskCache {
    uint32_t magic,version,bytes,count,audio_bytes,decoded,data_crc,header_crc;
    int64_t duration;
    Frame frames[225];
};
static_assert(sizeof(DiskCache)<=4096,"Cache index must fit one sector");
bool data_crc(Cache &cache,uint32_t &crc) {
    std::vector<uint8_t> bytes(8192);crc=0;
    for(unsigned at=AUDIO_START;at<cache.cursor;) {
        size_t n=std::min<size_t>(bytes.size(),cache.cursor-at);
        if(esp_partition_read(cache.partition,at,bytes.data(),n)!=ESP_OK)return false;
        crc=esp_rom_crc32_le(crc,bytes.data(),n);at+=n;
    }
    return true;
}
int commit_cache(Cache &cache) {
    if(cache.frames.size()>225)return 5;
    auto h=std::make_unique<DiskCache>();memset(h.get(),0,sizeof(*h));
    h->magic=0x32445643;h->version=1;h->bytes=cache.cursor;h->count=cache.frames.size();
    h->audio_bytes=cache.audio_bytes;h->duration=cache.duration;h->decoded=cache.decoded;
    memcpy(h->frames,cache.frames.data(),cache.frames.size()*sizeof(Frame));
    if(!data_crc(cache,h->data_crc))return 16;
    h->header_crc=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t *>(h.get()),sizeof(*h));
    // Commit only after all PCM, JPEG frames and their CRC are complete.
    return esp_partition_write(cache.partition,FONT_END,h.get(),sizeof(*h))==ESP_OK?0:16;
}
int load_cache(Cache &cache) {
    cache.partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"media");
    if(!cache.partition)return 2;
    auto h=std::make_unique<DiskCache>();
    if(esp_partition_read(cache.partition,FONT_END,h.get(),sizeof(*h))!=ESP_OK)return 16;
    uint32_t check=h->header_crc;h->header_crc=0;
    if(h->magic!=0x32445643 || h->version!=1 || !h->count || h->count>225 ||
       h->bytes<AUDIO_START || h->bytes>cache.partition->size || h->audio_bytes>RATE*2*MAX_SECONDS ||
       h->duration<=0 || h->duration>MAX_SECONDS*1000000LL ||
       h->audio_bytes!=unsigned(h->duration*RATE/1000000)*2 ||
       check!=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t *>(h.get()),sizeof(*h)))return 6;
    unsigned end=AUDIO_START+((h->audio_bytes+4095)&~4095u);int64_t pts=-1;
    for(unsigned i=0;i<h->count;i++) {
        auto &f=h->frames[i];if(!f.bytes || f.offset!=end || f.bytes>h->bytes-end || f.pts<0 || f.pts<pts || f.pts>=h->duration)return 6;
        end+=f.bytes;pts=f.pts;
    }
    if(end!=h->bytes)return 6;
    cache.cursor=h->bytes;cache.audio_bytes=h->audio_bytes;cache.duration=h->duration;cache.decoded=h->decoded;
    cache.frames.assign(h->frames,h->frames+h->count);uint32_t crc;
    return data_crc(cache,crc) && crc==h->data_crc?0:6;
}
int playback(Cache &cache,unsigned generation,char *out,size_t cap) {
    std::vector<uint8_t> jpeg;
    Memory ring(static_cast<uint8_t *>(heap_caps_malloc(cache.audio_bytes+1024,MALLOC_CAP_SPIRAM)));
    Memory pcm(static_cast<uint8_t *>(heap_caps_malloc(cache.audio_bytes,MALLOC_CAP_SPIRAM)));
    if(!ring || !pcm)return 19;
    if(esp_partition_read(cache.partition,AUDIO_START,pcm.get(),cache.audio_bytes)!=ESP_OK)return 16;
    unsigned audio_peak=0;
    for(unsigned i=0;i<cache.audio_bytes/2;++i)
        audio_peak=std::max(audio_peak,unsigned(abs(int(reinterpret_cast<int16_t *>(pcm.get())[i]))));
    int error=0;bool lease=false;unsigned offset=0;
    auto open_audio=[&]()->int {
        int e=3;int64_t until=esp_timer_get_time()+30000000;
        while(e==3 && esp_timer_get_time()<until && p4_visual_current(generation) && !paused()) {
            e=p4_media_audio_open(ring.get(),cache.audio_bytes+1024);if(e==3)vTaskDelay(pdMS_TO_TICKS(20));
        }
        if(!p4_visual_current(generation))return 4;
        if(e)return paused()?0:e;
        lease=true;e=p4_media_audio_write(reinterpret_cast<int16_t *>(pcm.get())+offset,cache.audio_bytes/2-offset);
        p4_media_audio_seal();return e;
    };
    stage=PLAYING;cache.progress("playing",generation);
    unsigned shown=0;size_t frame=0;int64_t began=esp_timer_get_time(),first=0,last=0;
    unsigned late_max=0,early_max=0,paint_max=0,underruns=0;int64_t paused_us=0;
    // The decoder has been released. A bounded full PCM ring makes LCD work
    // independent of audio delivery without creating another feeder task.
    error=open_audio();
    while(!error && (frame<cache.frames.size() || position_samples<cache.audio_bytes/2)) {
        if(!p4_visual_current(generation)){error=4;break;}
        if(paused()) {
            int64_t paused_at=esp_timer_get_time();
            if(lease){offset+=p4_media_audio_position();offset=std::min(offset,cache.audio_bytes/2);
                position_samples=offset;underruns+=p4_media_audio_underruns();p4_media_audio_finish(4);lease=false;}
            stage=PAUSED;cache.progress("paused",generation);
            while(paused() && p4_visual_current(generation))vTaskDelay(pdMS_TO_TICKS(10));
            paused_us+=esp_timer_get_time()-paused_at;
            if(!p4_visual_current(generation)){error=4;break;}
            if(offset>=cache.audio_bytes/2)break;
            stage=PLAYING;cache.progress("playing",generation);error=open_audio();if(error)break;
        }
        if(!lease){error=open_audio();continue;}
        if(esp_timer_get_time()-began-paused_us>cache.duration+35000000){error=15;break;}
        if(!p4_media_audio_started()){vTaskDelay(1);continue;}
        position_samples=offset+p4_media_audio_position();
        if(frame==cache.frames.size()){vTaskDelay(1);continue;}
        int64_t clock=int64_t(position_samples.load())*1000000/RATE;auto &f=cache.frames[frame];
        // Allow for SPI transfer and the LVGL flush/scan interval.
        if(f.pts<=clock+80000) {
            jpeg.resize(f.bytes);
            if(esp_partition_read(cache.partition,f.offset,jpeg.data(),f.bytes)!=ESP_OK){error=16;break;}
            int64_t paint=esp_timer_get_time();
            error=p4_visual_image(generation,jpeg.data(),jpeg.size());if(error)break;
            last=esp_timer_get_time();if(!first)first=last;
            paint_max=std::max<unsigned>(paint_max,(last-paint)/1000);
            clock=int64_t(offset+p4_media_audio_position())*1000000/RATE;
            late_max=std::max<unsigned>(late_max,std::max<int64_t>(0,clock-f.pts)/1000);
            early_max=std::max<unsigned>(early_max,std::max<int64_t>(0,f.pts-clock)/1000);++shown;++frame;
        } else vTaskDelay(pdMS_TO_TICKS(2));
    }
    if(!error && frame!=cache.frames.size())error=15;
    if(lease){underruns+=p4_media_audio_underruns();error=p4_media_audio_finish(error);}
    int64_t elapsed=esp_timer_get_time()-began-paused_us;
    printf("@media_audio_peak %u\n",audio_peak);
    snprintf(out,cap,"{\"state\":\"%s\",\"error\":%d,\"source\":\"original_mp4_high_aac\",\"processor\":\"p4_only\",\"frames\":%u,\"cached_frames\":%u,\"decoded_frames\":%u,\"width\":320,\"height\":480,\"duration_ms\":%lld,\"prepare_ms\":%lld,\"play_ms\":%lld,\"play_fps\":%.2f,\"schedule_late_max_ms\":%u,\"schedule_early_max_ms\":%u,\"presentation_lead_ms\":80,\"paint_max_ms\":%u,\"audio_underruns\":%u,\"cache_bytes\":%u,\"min_heap\":%lu,\"min_internal_heap\":%lu,\"audio_rate\":16000}",
        error?"failed":"played",error,shown,unsigned(cache.frames.size()),cache.decoded,(long long)(cache.duration/1000),
        (long long)(cache.prepare_us/1000),(long long)(elapsed/1000),last>first?(shown-1)*1e6/(last-first):0,
        late_max,early_max,paint_max,underruns,cache.cursor-FONT_END,(unsigned long)cache.min_heap,(unsigned long)cache.min_internal);
    // Profile separately from the playback receipt to keep its schema stable.
    printf("@media_prepare {\"network_ms\":%lld,\"decode_ms\":%lld,\"convert_ms\":%lld,\"jpeg_ms\":%lld,\"flash_ms\":%lld,\"range_requests\":%u,\"paused_ms\":%lld}\n",
        (long long)(cache.network_us/1000),(long long)(cache.decode_us/1000),(long long)(cache.convert_us/1000),
        (long long)(cache.jpeg_us/1000),(long long)(cache.flash_us/1000),cache.requests,(long long)(paused_us/1000));
    stage=error?FAILED:ENDED;
    size_t used=strlen(out);
    if(used && out[used-1]=='}')snprintf(out+used-1,cap-used+1,
        ",\"profile_ms\":{\"network\":%lld,\"decode\":%lld,\"convert\":%lld,\"jpeg\":%lld,\"flash\":%lld},\"skipped_nonref\":%u}",
        (long long)(cache.network_us/1000),(long long)(cache.decode_us/1000),(long long)(cache.convert_us/1000),
        (long long)(cache.jpeg_us/1000),(long long)(cache.flash_us/1000),cache.skipped);
    return error;
}
void replay(void *arg) {
    unsigned generation=unsigned(uintptr_t(arg));Cache cache;char out[1800]={};
    int e=load_cache(cache);if(!e && p4_visual_current(generation)) {++replays;e=playback(cache,generation,out,sizeof(out));}
    if(out[0])p4_cloud_media_status(generation,out);
    if(e)stage=FAILED;
    running=false;vTaskDelete(nullptr);
}
} // namespace
extern "C" int p4_media_available(void) {
    auto p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"media");
    return p && p->size==0x780000?0:2;
}
extern "C" int p4_media_run(const char *url,unsigned generation,unsigned fps,char *out,size_t cap) {
    if(fps<10 || fps>15 || !url)return 1;
    int error=p4_media_available();if(error)return error;
    bool expected=false;if(!running.compare_exchange_strong(expected,true))return 3;
    struct Release {~Release(){running=false;}} release;
    cache_generation=generation;pause_requested=false;position_samples=0;stage=PREPARING;
    Cache cache;
    { Reader r(generation);if(!r.open(url)){stage=FAILED;return 8;}error=prepare(r,cache,generation,fps); }
    if(!error)error=commit_cache(cache);
    if(error){stage=FAILED;snprintf(out,cap,"{\"state\":\"prepare_failed\",\"error\":%d,\"decoded_frames\":%u,\"cached_frames\":%u,\"min_heap\":%lu,\"min_internal_heap\":%lu}",error,cache.decoded,unsigned(cache.frames.size()),(unsigned long)cache.min_heap,(unsigned long)cache.min_internal);return error;}
    return playback(cache,generation,out,cap);
}
extern "C" void p4_media_pause(void){if(running && (stage==PLAYING || stage==PAUSED))pause_requested=true;}
extern "C" void p4_media_voice_hold(bool hold){voice_hold=hold;}
extern "C" int p4_media_playback_active(void){return running && (stage==PLAYING || stage==PAUSED);}
extern "C" void p4_media_touch(unsigned generation) {
    unsigned current=cache_generation.load();if(!generation)generation=current;
    if(!generation || generation!=current || !p4_visual_current(generation) || p4_wifi_visible())return;
    if(running){if(stage==PLAYING || stage==PAUSED)pause_requested=!pause_requested.load();return;}
    if(stage!=ENDED)return;
    bool expected=false;if(!running.compare_exchange_strong(expected,true))return;
    pause_requested=false;position_samples=0;stage=PLAYING;
    if(xTaskCreatePinnedToCore(replay,"video_replay",8192,reinterpret_cast<void *>(uintptr_t(generation)),2,nullptr,0)!=pdPASS){running=false;stage=FAILED;}
}
extern "C" void p4_media_control_status(char *out,size_t cap) {
    const char *names[]={"idle","preparing","playing","paused","ended","failed"};
    snprintf(out,cap,"{\"state\":\"%s\",\"running\":%s,\"position_ms\":%u,\"generation\":%u,\"replays\":%u}",
        names[stage.load()],running?"true":"false",position_samples.load()/16,cache_generation.load(),replays.load());
}
