#include "p4_video_capture.h"
#include "driver/jpeg_encode.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstring>
#include <cstdlib>

namespace {
constexpr unsigned Width=320,Height=256,MaxBytes=65536;
std::atomic<unsigned> active{0};
SemaphoreHandle_t mutex=nullptr; // allocated only by serialized start calls
uint8_t *rgb=nullptr,*encoded=nullptr,*cached=nullptr;
size_t rgb_capacity=0,encoded_capacity=0,used=0;
unsigned cached_generation=0,sequence=0;
uint64_t captured=0;
int capture_error=0;
jpeg_encoder_handle_t encoder=nullptr;
// The buffers and engine have a fixed application lifetime once allocated.
// stop never frees memory while the camera worker may be encoding into it.
// Only this camera worker writes rgb/encoded or uses encoder.
unsigned producer_generation=0;
uint64_t next_frame=0;
int prepare(){
    if(encoder&&rgb&&encoded&&cached)return 0;
    jpeg_encode_memory_alloc_cfg_t allocation={};allocation.buffer_direction=JPEG_ENC_ALLOC_INPUT_BUFFER;
    if(!rgb)rgb=static_cast<uint8_t*>(jpeg_alloc_encoder_mem(Width*Height*3,&allocation,&rgb_capacity));
    allocation.buffer_direction=JPEG_ENC_ALLOC_OUTPUT_BUFFER;
    if(!encoded)encoded=static_cast<uint8_t*>(jpeg_alloc_encoder_mem(MaxBytes,&allocation,&encoded_capacity));
    if(!cached)cached=static_cast<uint8_t*>(heap_caps_malloc(MaxBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!rgb||!encoded||!cached)return 19;
    jpeg_encode_engine_cfg_t config={};config.timeout_ms=100;
    if(!encoder&&jpeg_new_encoder_engine(&config,&encoder)!=ESP_OK)return 16;
    return 0;
}
}

int p4_video_capture_start(unsigned generation){
    if(!generation)return 1;
    // start/stop are serialized by the runtime work owner. The producer never
    // observes a new generation until all stable allocations have succeeded.
    if(!mutex)mutex=xSemaphoreCreateMutex();
    if(!mutex)return 19;
    const int error=prepare();if(error)return error;
    xSemaphoreTake(mutex,portMAX_DELAY);
    used=0;sequence=0;captured=0;cached_generation=generation;capture_error=0;
    active.store(generation,std::memory_order_release);
    xSemaphoreGive(mutex);return 0;
}
void p4_video_capture_stop(unsigned generation){
    unsigned expected=generation;active.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(cached_generation==generation){used=0;capture_error=0;}
    xSemaphoreGive(mutex);
}
void p4_video_capture_push(unsigned generation,const uint16_t* pixels,unsigned width,unsigned height,uint64_t frame_time){
    if(!generation||active.load(std::memory_order_acquire)!=generation||!pixels||!width||!height)return;
    if(producer_generation!=generation){producer_generation=generation;next_frame=0;}
    if(frame_time<next_frame)return;
    next_frame=frame_time+1000000;
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){
        const uint16_t p=pixels[size_t(y)*height/Height*width+size_t(x)*width/Width];
        const size_t at=(size_t(y)*Width+x)*3;
        // ESP-IDF's RGB888 JPEG input uses BGR24 byte storage (as documented
        // by the driver's encoder example), despite the enum's RGB name.
        rgb[at]=uint8_t((p&31)*255/31);rgb[at+1]=uint8_t(((p>>5)&63)*255/63);rgb[at+2]=uint8_t(((p>>11)&31)*255/31);
    }
    jpeg_encode_cfg_t format={};format.width=Width;format.height=Height;format.src_type=JPEG_ENCODE_IN_FORMAT_RGB888;
    format.sub_sample=JPEG_DOWN_SAMPLING_YUV420;format.image_quality=75;
    uint32_t bytes=0;int error=jpeg_encoder_process(encoder,&format,rgb,Width*Height*3,encoded,encoded_capacity,&bytes)==ESP_OK?0:16;
    if(!error&&(!bytes||bytes>MaxBytes))error=5;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(active.load(std::memory_order_acquire)==generation&&cached_generation==generation){
        capture_error=error;
        if(!error){std::memcpy(cached,encoded,bytes);used=bytes;captured=frame_time;++sequence;}
        else used=0; // A failed new capture must not make an old frame look current.
    }
    xSemaphoreGive(mutex);
}
int p4_video_capture_copy(unsigned generation,unsigned after,uint8_t* out,size_t capacity,size_t* bytes,unsigned* number,uint64_t* timestamp){
    if(bytes)*bytes=0;
    if(number)*number=0;
    if(timestamp)*timestamp=0;
    if(!generation||!out||!bytes||!number||!timestamp)return 1;
    if(!mutex||active.load(std::memory_order_acquire)!=generation)return 4;
    if(xSemaphoreTake(mutex,0)!=pdTRUE)return 3;
    int error=0;const uint64_t now=uint64_t(esp_timer_get_time());
    if(active.load(std::memory_order_acquire)!=generation||cached_generation!=generation)error=4;
    else if(capture_error)error=capture_error;
    else if(!used||sequence<=after||now<captured||now-captured>1500000)error=3;
    else if(used>capacity)error=5;
    else {std::memcpy(out,cached,used);*bytes=used;*number=sequence;*timestamp=captured;}
    xSemaphoreGive(mutex);return error;
}
