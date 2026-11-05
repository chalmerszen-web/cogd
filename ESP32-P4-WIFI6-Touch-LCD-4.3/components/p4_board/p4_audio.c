#include "p4_audio.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <string.h>

/* The codecs share BCLK/LRCK. Keep both at 16 kHz for their entire lifetime;
 * resample speech output rather than retiming the live microphone clock. */
static esp_codec_dev_handle_t input, output;
static QueueHandle_t frames;
static atomic_bool capture;
static atomic_uint dropped;
static atomic_int input_error;
static bool initialized;
static unsigned source_rate=16000, phase;
static int16_t previous;
static esp_err_t codecs_init(void)
{
    esp_err_t err=bsp_i2c_init();if(err)return err;
    i2c_master_bus_handle_t bus=bsp_i2c_get_handle();
    unsigned mic_address=0;
    for(unsigned address=0x40;address<=0x43;++address)
        if(i2c_master_probe(bus,address,50)==ESP_OK){mic_address=address;break;}
    if(!mic_address){ESP_LOGE("p4_audio","ES7210 absent at 0x40..0x43");return ESP_ERR_NOT_FOUND;}
    ESP_LOGI("p4_audio","ES7210 I2C address 0x%02x",mic_address);
    i2s_chan_handle_t tx=NULL,rx=NULL;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM,I2S_ROLE_MASTER);
    channel.auto_clear=true;
    err=i2s_new_channel(&channel,&tx,&rx);if(err)return err;
    i2s_std_config_t standard={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=BSP_I2S_MCLK,.bclk=BSP_I2S_SCLK,.ws=BSP_I2S_LCLK,.dout=BSP_I2S_DOUT,.din=BSP_I2S_DSIN}};
    if((err=i2s_channel_init_std_mode(tx,&standard)) || (err=i2s_channel_init_std_mode(rx,&standard)) ||
       (err=i2s_channel_enable(tx)) || (err=i2s_channel_enable(rx)))return err;
    audio_codec_i2s_cfg_t i2s={.port=CONFIG_BSP_I2S_NUM,.rx_handle=rx,.tx_handle=tx};
    const audio_codec_data_if_t *data=audio_codec_new_i2s_data(&i2s);
    const audio_codec_gpio_if_t *gpio=audio_codec_new_gpio();
    audio_codec_i2c_cfg_t ctl={.port=BSP_I2C_NUM,.addr=ES8311_CODEC_DEFAULT_ADDR,.bus_handle=bus};
    const audio_codec_ctrl_if_t *spk_ctrl=audio_codec_new_i2c_ctrl(&ctl);
    ctl.addr=(uint8_t)(mic_address<<1);
    const audio_codec_ctrl_if_t *mic_ctrl=audio_codec_new_i2c_ctrl(&ctl);
    if(!data || !gpio || !spk_ctrl || !mic_ctrl)return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t dac={.ctrl_if=spk_ctrl,.gpio_if=gpio,.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin=BSP_POWER_AMP_IO,.use_mclk=true,.hw_gain={.pa_voltage=5.0,.codec_dac_voltage=3.3}};
    const audio_codec_if_t *spk=es8311_codec_new(&dac);if(!spk)return ESP_FAIL;
    esp_codec_dev_cfg_t dev={.dev_type=ESP_CODEC_DEV_TYPE_OUT,.codec_if=spk,.data_if=data};
    output=esp_codec_dev_new(&dev);if(!output)return ESP_ERR_NO_MEM;
    es7210_codec_cfg_t adc={.ctrl_if=mic_ctrl,.mic_selected=ES7210_SEL_MIC1|ES7210_SEL_MIC2};
    const audio_codec_if_t *mic=es7210_codec_new(&adc);if(!mic)return ESP_FAIL;
    dev.dev_type=ESP_CODEC_DEV_TYPE_IN;dev.codec_if=mic;
    input=esp_codec_dev_new(&dev);
    return input?ESP_OK:ESP_ERR_NO_MEM;
}
static void capture_task(void *unused)
{
    (void)unused;
    int16_t stereo[256], mono[128];
    for(;;) {
        int ret=esp_codec_dev_read(input,stereo,sizeof(stereo));
        if(ret!=ESP_CODEC_DEV_OK) { atomic_store(&input_error,ret);vTaskDelay(pdMS_TO_TICKS(20));continue; }
        atomic_store(&input_error,0);
        if(!atomic_load(&capture)) continue;
        /* MIC1 is the primary near-field microphone; keep MIC2 available for AEC. */
        for(unsigned i=0;i<128;++i) mono[i]=stereo[2*i];
        if(xQueueSend(frames,mono,0)!=pdTRUE) atomic_fetch_add(&dropped,128);
    }
}
esp_err_t p4_audio_init(void)
{
    if(initialized) return ESP_OK;
    if(frames) return ESP_ERR_INVALID_STATE;
    frames=xQueueCreate(32,128*sizeof(int16_t));
    if(!frames) return ESP_ERR_NO_MEM;
    esp_err_t err=codecs_init();if(err)return err;
    esp_codec_dev_sample_info_t fmt={.sample_rate=16000,.channel=2,.channel_mask=3,.bits_per_sample=16,.mclk_multiple=256};
    if(esp_codec_dev_open(output,&fmt)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    if(esp_codec_dev_open(input,&fmt)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    if(esp_codec_dev_set_out_vol(output,65)!=ESP_CODEC_DEV_OK || esp_codec_dev_set_in_gain(input,24)!=ESP_CODEC_DEV_OK)
        return ESP_FAIL;
    esp_codec_dev_set_out_mute(output,true);
    initialized=xTaskCreatePinnedToCore(capture_task,"p4_mic",4096,NULL,20,NULL,1)==pdPASS;
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
{ return output && esp_codec_dev_set_out_mute(output,true)==ESP_CODEC_DEV_OK?ESP_OK:ESP_FAIL; }
