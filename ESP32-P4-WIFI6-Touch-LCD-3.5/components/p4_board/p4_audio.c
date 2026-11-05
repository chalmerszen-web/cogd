#include "p4_audio.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_aec.h"
#include "esp_vad.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>

/* The ES8311 ADC and DAC share BCLK/LRCK. Keep both at 16 kHz;
 * resample speech output rather than retiming the live microphone clock. */
static esp_codec_dev_handle_t input, output;
static QueueHandle_t frames;
static atomic_bool capture;
static atomic_uint dropped;
static atomic_int input_error;
static atomic_uint tx_samples;
static aec_handle_t *echo;
static vad_handle_t near_vad;
static atomic_bool near_monitor;
static atomic_uint confirmed_at,near_epoch;
static int16_t *echo_mic,*echo_ref,*echo_out;
static atomic_uint echo_frames,echo_max_us,echo_mic_level,echo_ref_level,echo_out_level;
static atomic_uint microphone_scale=1;
static unsigned echo_bytes,echo_internal;
/* Explicit finite diagnostic capture; zero sample storage while inactive.
 * The capture owner never waits for USB or allocates on the audio path. */
static SemaphoreHandle_t probe_lock;
static int16_t *probe_pcm;
static unsigned probe_used,probe_limit;
int p4_audio_probe(const char *command,char *out,size_t cap)
{
    if(!probe_lock || xSemaphoreTake(probe_lock,pdMS_TO_TICKS(50))!=pdTRUE)return 3;
    unsigned ms,offset;char extra;int error=0;
    if(sscanf(command,"start %u %c",&ms,&extra)==1 && ms>=1000 && ms<=4000) {
        if(probe_used<probe_limit)error=3;
        else {free(probe_pcm);probe_pcm=heap_caps_malloc(ms*16*6,MALLOC_CAP_SPIRAM);
            probe_used=0;probe_limit=probe_pcm?ms*16:0;if(!probe_pcm)error=19;}
    } else if(!strcmp(command,"free")) {probe_limit=probe_used=0;free(probe_pcm);probe_pcm=NULL;}
    else if(!strcmp(command,"status"))snprintf(out,cap,"{\"samples\":%u,\"target\":%u,\"channels\":3,\"rate\":16000,\"order\":\"mic,dac_reference,aec_scaled\"}",probe_used,probe_limit);
    else if(sscanf(command,"read %u %c",&offset,&extra)==1) {
        if(probe_used<probe_limit || !probe_pcm || offset>=probe_used*6 || cap<16400)error=3;
        else {unsigned n=probe_used*6-offset;if(n>8000)n=8000;
            int used=snprintf(out,cap,"{\"offset\":%u,\"hex\":\"",offset);const uint8_t *data=(uint8_t *)probe_pcm+offset;
            const char *hex="0123456789abcdef";
            for(unsigned i=0;i<n;i++){out[used++]=hex[data[i]>>4];out[used++]=hex[data[i]&15];}
            snprintf(out+used,cap-used,"\"}");}
    } else error=1;
    xSemaphoreGive(probe_lock);return error;
}
void p4_audio_echo_stats(unsigned v[7])
{
    v[0]=atomic_load(&echo_frames);v[1]=atomic_load(&echo_max_us);
    v[2]=atomic_load(&echo_mic_level);v[3]=atomic_load(&echo_ref_level);v[4]=atomic_load(&echo_out_level);
    v[5]=echo_bytes;v[6]=echo_internal;
}
void p4_audio_near_monitor(bool enable)
{atomic_store(&confirmed_at,0);atomic_fetch_add(&near_epoch,1);atomic_store(&near_monitor,enable);}
uint32_t p4_audio_confirmed_at(void){return atomic_load(&confirmed_at);}
/* Compare synchronous physical channels, never a queued microphone frame
 * against a later DAC reading. Reject speech-shaped leakage of the speaker. */
static void near_frame(unsigned mic,unsigned ref,unsigned out,unsigned scale)
{
    static unsigned votes,strong,last_mic,last_ref,epoch;
    static int16_t previous_ref[1536];
    unsigned current=atomic_load(&near_epoch);
    if(epoch!=current){votes=strong=last_mic=last_ref=0;memset(previous_ref,0,sizeof(previous_ref));epoch=current;}
    if(!atomic_load(&near_monitor))return;
    bool voiced=vad_process(near_vad,echo_out,16000,30)==VAD_SPEECH;
    bool dual=scale>1 || ref>96;
    /* The FD filter has overlap. Compare against the recent acoustic
     * envelope even during a syllable gap in this particular DAC frame. */
    last_mic=mic>last_mic*4/5?mic:last_mic*4/5;last_ref=ref>last_ref*4/5?ref:last_ref*4/5;
    bool near=voiced && (dual?(out>620 && out>100+last_mic*9/10 && out>120+last_ref/3):out>280);
    if(near && dual && last_ref>96) {
        /* Passive double-talk check: reject correlated raw AND AEC residual
         * echo including 96 ms of filter/acoustic tail. Never silence the speaker to test a candidate. */
        for(int lag=0;lag<=1536 && near;lag+=8) {
            int64_t xy=0,xx=0,yy=0,zy=0,zz=0;
            for(int i=0;i<512;i+=8) {
                int x=echo_mic[i]>>3,z=echo_out[i]>>3,j=i-lag,y=(j<0?previous_ref[1536+j]:echo_ref[j])>>3;
                xy+=x*y;xx+=x*x;yy+=y*y;zy+=z*y;zz+=z*z;
            }
            if((xx && yy && (double)xy*xy>.50*(double)xx*yy) ||
               (zz && yy && (double)zy*zy>.25*(double)zz*yy))near=false;
        }
    }
    memmove(previous_ref,previous_ref+512,1024*sizeof(int16_t));
    memcpy(previous_ref+1024,echo_ref,512*sizeof(int16_t));
    votes=((votes<<1)|(unsigned)near)&4095;
    /* AEC PCM is restored by scale after a 12 dB ADC reduction. Require
     * two frames retaining at least half of the recent raw-mic envelope;
     * weak amplified residuals alone cannot cancel a spoken sentence. */
    strong=((strong<<1)|(unsigned)(near && out>200+last_mic*scale/2))&4095;
    /* Require sustained independent speech, not a two-frame echo transient.
     * Full-volume playback continues throughout the confirmation window. */
    if(near && __builtin_popcount(votes)>=(dual?6:3) && (!dual || __builtin_popcount(strong)>=2))atomic_store(&confirmed_at,(unsigned)(esp_timer_get_time()/1000));
}
static bool IRAM_ATTR tx_sent(i2s_chan_handle_t channel, i2s_event_data_t *event, void *ctx)
{
    (void)channel;(void)ctx;
    atomic_fetch_add_explicit(&tx_samples,event->size/4,memory_order_relaxed);
    return false;
}
uint32_t p4_audio_tx_samples(void) {return atomic_load_explicit(&tx_samples,memory_order_relaxed);}
static bool initialized;
static unsigned source_rate=16000, phase;
static int16_t previous;
static esp_err_t codecs_init(void)
{
    esp_err_t err=bsp_i2c_init();if(err)return err;
    i2c_master_bus_handle_t bus=bsp_i2c_get_handle();
    if(i2c_master_probe(bus,ES8311_CODEC_DEFAULT_ADDR>>1,50)!=ESP_OK)return ESP_ERR_NOT_FOUND;
    ESP_LOGI("p4_audio","ES8311 duplex codec at I2C 0x18");
    i2s_chan_handle_t tx=NULL,rx=NULL;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM,I2S_ROLE_MASTER);
    channel.auto_clear=true;
    channel.dma_desc_num=P4_AUDIO_DMA_COUNT;channel.dma_frame_num=P4_AUDIO_DMA_FRAMES;
    err=i2s_new_channel(&channel,&tx,&rx);if(err)return err;
    i2s_std_config_t standard={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=BSP_I2S_MCLK,.bclk=BSP_I2S_SCLK,.ws=BSP_I2S_LCLK,.dout=BSP_I2S_DOUT,.din=BSP_I2S_DSIN}};
    if((err=i2s_channel_init_std_mode(tx,&standard)) || (err=i2s_channel_init_std_mode(rx,&standard)))return err;
    i2s_event_callbacks_t callbacks={.on_sent=tx_sent};
    if((err=i2s_channel_register_event_callback(tx,&callbacks,NULL)) ||
       (err=i2s_channel_enable(tx)) || (err=i2s_channel_enable(rx)))return err;
    audio_codec_i2s_cfg_t i2s={.port=CONFIG_BSP_I2S_NUM,.rx_handle=rx,.tx_handle=tx};
    const audio_codec_data_if_t *data=audio_codec_new_i2s_data(&i2s);
    const audio_codec_gpio_if_t *gpio=audio_codec_new_gpio();
    audio_codec_i2c_cfg_t ctl={.port=BSP_I2C_NUM,.addr=ES8311_CODEC_DEFAULT_ADDR,.bus_handle=bus};
    const audio_codec_ctrl_if_t *spk_ctrl=audio_codec_new_i2c_ctrl(&ctl);
    if(!data || !gpio || !spk_ctrl)return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t dac={.ctrl_if=spk_ctrl,.gpio_if=gpio,.codec_mode=ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin=BSP_POWER_AMP_IO,.use_mclk=true,.hw_gain={.pa_voltage=5.0,.codec_dac_voltage=3.3}};
    const audio_codec_if_t *spk=es8311_codec_new(&dac);if(!spk)return ESP_FAIL;
    esp_codec_dev_cfg_t dev={.dev_type=ESP_CODEC_DEV_TYPE_IN_OUT,.codec_if=spk,.data_if=data};
    output=esp_codec_dev_new(&dev);if(!output)return ESP_ERR_NO_MEM;
    input=output;
    return ESP_OK;
}
static void capture_task(void *unused)
{
    (void)unused;
    int16_t stereo[256];unsigned used=0,tail=0;
    for(;;) {
        int ret=esp_codec_dev_read(input,stereo,sizeof(stereo));
        if(ret!=ESP_CODEC_DEV_OK) { atomic_store(&input_error,ret);vTaskDelay(pdMS_TO_TICKS(20));continue; }
        atomic_store(&input_error,0);
        /* Both channels share the codec clock: ADCL + DACR is explicitly
         * enabled by esp_codec_dev's default no_dac_ref=false configuration.
         * Keep the echo filter warm even while an ASR/network owner pauses
         * delivery. Never substitute the microphone for the reference. */
        for(unsigned i=0;i<128;++i) {echo_mic[used+i]=stereo[2*i];echo_ref[used+i]=stereo[2*i+1];}
        used+=128;if(used<512)continue;used=0;
        unsigned m=0,r=0,o=0;
        for(unsigned i=0;i<512;++i) {m+=abs(echo_mic[i]);r+=abs(echo_ref[i]);}
        if(r>512*4)tail=20; /* 640 ms acoustic tail after the last output. */
        if(tail) {
            --tail;int64_t begin=esp_timer_get_time();aec_process(echo,echo_mic,echo_ref,echo_out);
            unsigned us=(unsigned)(esp_timer_get_time()-begin);
            if(us>atomic_load(&echo_max_us))atomic_store(&echo_max_us,us);
            atomic_fetch_add(&echo_frames,1);
        } else memcpy(echo_out,echo_mic,1024); /* Quiet wake input remains bit-exact. */
        unsigned scale=atomic_load(&microphone_scale);
        for(unsigned i=0;i<512;++i) {
            int value=echo_out[i]*(int)scale;
            echo_out[i]=value>32767?32767:value< -32768?-32768:value;o+=abs(echo_out[i]);
        }
        atomic_store(&echo_mic_level,m/512);atomic_store(&echo_ref_level,r/512);atomic_store(&echo_out_level,o/512);
        near_frame(m/512,r/512,o/512,scale);
        if(probe_lock && xSemaphoreTake(probe_lock,0)==pdTRUE) {
            if(probe_pcm && probe_used<probe_limit) {
                unsigned n=probe_limit-probe_used;if(n>512)n=512;
                for(unsigned i=0;i<n;i++){unsigned at=(probe_used+i)*3;
                    probe_pcm[at]=echo_mic[i];probe_pcm[at+1]=echo_ref[i];probe_pcm[at+2]=echo_out[i];}
                probe_used+=n;
            }
            xSemaphoreGive(probe_lock);
        }
        if(!atomic_load(&capture))continue;
        for(unsigned i=0;i<512;i+=128)
            if(xQueueSend(frames,echo_out+i,0)!=pdTRUE)atomic_fetch_add(&dropped,128);
    }
}
esp_err_t p4_audio_init(void)
{
    if(initialized) return ESP_OK;
    if(frames) return ESP_ERR_INVALID_STATE;
    probe_lock=xSemaphoreCreateMutex();if(!probe_lock)return ESP_ERR_NO_MEM;
    frames=xQueueCreate(32,128*sizeof(int16_t));
    if(!frames) return ESP_ERR_NO_MEM;
    esp_err_t err=codecs_init();if(err)return err;
    esp_codec_dev_sample_info_t fmt={.sample_rate=16000,.channel=2,.channel_mask=3,.bits_per_sample=16,.mclk_multiple=256};
    if(esp_codec_dev_open(output,&fmt)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    /* The user-facing master remains 0..100. At the old 0 dB DAC setting,
     * speaker leakage masked near-end wake speech even with a lower ADC gain.
     * Reserve 8 dB output headroom for duplex; still 9.5 dB above the old 65
     * codec setting. This is an output calibration, not a weaker KWS gate. */
    if(esp_codec_dev_set_out_vol(output,84)!=ESP_CODEC_DEV_OK || esp_codec_dev_set_in_gain(input,42)!=ESP_CODEC_DEV_OK)
        return ESP_FAIL;
    unsigned before=heap_caps_get_free_size(MALLOC_CAP_8BIT),internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    aec_config_t config={.mic_num=1,.ref_num=1,.out_num=1,.filter_length=4,.sample_rate=16000,
        .caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,.mode=AEC_MODE_FD_HIGH_PERF,.nlp_level=AEC_NLP_LEVEL_NORMAL};
    echo=aec_create_from_config(&config);
    near_vad=vad_create_with_param(VAD_MODE_3,16000,30,60,100);
    echo_mic=heap_caps_aligned_alloc(16,3*512*sizeof(int16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!echo || !near_vad || aec_get_chunksize(echo)!=512 || !echo_mic)return ESP_ERR_NO_MEM;
    echo_ref=echo_mic+512;echo_out=echo_ref+512;
    echo_bytes=before-heap_caps_get_free_size(MALLOC_CAP_8BIT);
    echo_internal=internal-heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    esp_codec_dev_set_out_mute(output,true);
    initialized=xTaskCreatePinnedToCore(capture_task,"p4_mic",8192,NULL,20,NULL,1)==pdPASS;
    return initialized?ESP_OK:ESP_ERR_NO_MEM;
}
void p4_audio_capture(bool enable)
{
    atomic_store(&capture,false);
    if(frames) xQueueReset(frames);
    atomic_store(&dropped,0);
    atomic_store(&capture,enable);
}
esp_err_t p4_audio_read(int16_t pcm[128],uint32_t *samples,unsigned *lost)
{
    *samples=0;*lost=atomic_exchange(&dropped,0);
    if(atomic_load(&input_error))return ESP_FAIL;
    if(!frames || xQueueReceive(frames,pcm,0)!=pdTRUE)return ESP_ERR_TIMEOUT;
    *samples=128;return ESP_OK;
}
esp_err_t p4_audio_output(unsigned rate)
{
    if(rate<8000 || rate>48000 || !output)return ESP_ERR_INVALID_ARG;
    source_rate=rate;phase=0;previous=0;
    /* Prevent the on-board speaker from clipping the microphone ADC before
     * echo cancellation. Restore the same nominal KWS level AFTER AEC. */
    if(esp_codec_dev_set_in_gain(input,30)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    atomic_store(&microphone_scale,4);
    return esp_codec_dev_set_out_mute(output,false)==ESP_CODEC_DEV_OK?ESP_OK:ESP_FAIL;
}
esp_err_t p4_audio_write(const int16_t *pcm,size_t samples)
{
    int16_t stereo[960];unsigned used=0;
    if(samples>240)return ESP_ERR_INVALID_SIZE;
    for(size_t i=0;i<samples;++i) {
        phase+=16000;
        while(phase>=source_rate) {
            phase-=source_rate;
            int32_t value=pcm[i]+((int32_t)previous-pcm[i])*(int32_t)phase/16000;
            stereo[used++]=(int16_t)value;stereo[used++]=(int16_t)value;
        }
        previous=pcm[i];
    }
    return !used || esp_codec_dev_write(output,stereo,(int)(used*sizeof(*stereo)))==ESP_CODEC_DEV_OK?ESP_OK:ESP_FAIL;
}
esp_err_t p4_audio_mute(void)
{
    if(!output || esp_codec_dev_set_out_mute(output,true)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    atomic_store(&microphone_scale,1);
    return esp_codec_dev_set_in_gain(input,42)==ESP_CODEC_DEV_OK?ESP_OK:ESP_FAIL;
}
