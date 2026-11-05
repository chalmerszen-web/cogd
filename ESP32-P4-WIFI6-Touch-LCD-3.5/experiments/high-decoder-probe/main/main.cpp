#include "wels/codec_api.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
extern const unsigned char data_start[] asm("_binary_wan3_high_head_h264_start");
extern const unsigned char data_end[] asm("_binary_wan3_high_head_h264_end");
static void run() {
    ISVCDecoder *dec=nullptr;
    int error=WelsCreateDecoder(&dec);
    if(error || !dec) { printf("@high_probe create_error=%d\n",error);return; }
    int level=WELS_LOG_ERROR;dec->SetOption(DECODER_OPTION_TRACE_LEVEL,&level);
    SDecodingParam params={};params.sVideoProperty.eVideoBsType=VIDEO_BITSTREAM_AVC;
    params.eEcActiveIdc=ERROR_CON_DISABLE;
    error=dec->Initialize(&params);
    printf("@high_probe init=%d bytes=%u heap=%lu\n",error,unsigned(data_end-data_start),(unsigned long)esp_get_free_heap_size());
    const unsigned char *cursor=data_start;
    unsigned frames=0;int64_t began=esp_timer_get_time(),decode_us=0;
    while(!error && cursor<data_end) {
        const unsigned char *next=cursor+4;
        while(next+4<data_end && (next[0]||next[1]||next[2]!=1) &&
            (next[0]||next[1]||next[2]||next[3]!=1))++next;
        if(next+4>=data_end)next=data_end;
        unsigned char *planes[3]={};SBufferInfo info={};
        int64_t start=esp_timer_get_time();
        error=dec->DecodeFrameNoDelay(cursor,int(next-cursor),planes,&info);
        decode_us+=esp_timer_get_time()-start;
        if(info.iBufferStatus)++frames;
        printf("@high_frame frames=%u status=%d error=%d w=%d h=%d heap=%lu\n",frames,info.iBufferStatus,error,
            info.UsrData.sSystemBuffer.iWidth,info.UsrData.sSystemBuffer.iHeight,(unsigned long)esp_get_free_heap_size());
        cursor=next;vTaskDelay(1);
    }
    int eos=1;dec->SetOption(DECODER_OPTION_END_OF_STREAM,&eos);
    for(unsigned i=0;i<16 && !error;i++) {
        unsigned char *planes[3]={};SBufferInfo info={};
        int64_t start=esp_timer_get_time();error=dec->FlushFrame(planes,&info);decode_us+=esp_timer_get_time()-start;
        if(!info.iBufferStatus)break;
        ++frames;
    }
    printf("@high_probe_done {\"frames\":%u,\"error\":%d,\"decode_us\":%lld,\"wall_us\":%lld,\"free_heap\":%lu}\n",
        frames,error,(long long)decode_us,(long long)(esp_timer_get_time()-began),(unsigned long)esp_get_free_heap_size());
    dec->Uninitialize();WelsDestroyDecoder(dec);
}
extern "C" void app_main() {
    esp_pm_config_t pm={};pm.max_freq_mhz=200;pm.min_freq_mhz=200;ESP_ERROR_CHECK(esp_pm_configure(&pm));
    vTaskDelay(pdMS_TO_TICKS(3000));run();
}
