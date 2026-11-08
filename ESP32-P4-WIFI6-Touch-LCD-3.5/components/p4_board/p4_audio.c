#include "p4_audio.h"
#include "p4_near_onset.h"
#include "p4_near_correlation.h"
#if defined(AGENT_P4_AOQ_DUPLEX)
#include "p4_vad_stream.h"
#include "p4_near_votes.h"
#include "p4_near_alignment.h"
#include "p4_voice_periodicity.h"
#endif
#if defined(AGENT_P4_AEC_BENCH)
#include "p4_aec_bench.h"
#endif
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
#if defined(AGENT_P4_AOQ_DUPLEX)
static vad_handle_t near_linear_vad;
#endif
static atomic_bool near_monitor;
static atomic_uint confirmed_onset;
static atomic_uint confirmed_at,near_epoch;
static int16_t *echo_mic,*echo_ref,*echo_out,*echo_linear;
static atomic_uint echo_frames,echo_max_us,echo_mic_level,echo_ref_level,echo_out_level;
static atomic_uint capture_frames,capture_max_us,near_max_us,capture_overruns,rx_overflows;
static atomic_bool capture_discontinuous;
#if defined(AGENT_P4_AEC_BENCH)
static atomic_bool benchmark_active;
#endif
#if defined(AGENT_P4_AOQ_DUPLEX)
/* Keep the acoustic path stationary across cue/playback/interrupt boundaries.
 * A 12 dB ADC gain jump invalidates a warm echo filter, including its tail.
 * Retain playback's ADC headroom and compensate only after cancellation. */
static atomic_uint microphone_scale=4;
static atomic_bool output_active;
static atomic_uint quiet_levels[64],quiet_count;
#else
static atomic_uint microphone_scale=1;
#endif
static unsigned echo_bytes,echo_internal;
/* Explicit finite diagnostic capture; zero sample storage while inactive.
 * The capture owner never waits for USB or allocates on the audio path. */
static SemaphoreHandle_t probe_lock,probe_sent_lock;
static int16_t *probe_pcm;
static unsigned probe_used,probe_limit;
typedef struct {
    uint32_t at_ms;
    unsigned mic,ref,out,scale,flags,votes,strong,last_mic,last_ref,mic_corr,out_corr;
    unsigned linear_us,nlp_us,processing_us;
    unsigned wide_votes,wide_strong;
} near_probe_t;
typedef struct {uint64_t capture_us,sent_us;} sent_probe_t;
static near_probe_t near_probe,*probe_near;
static sent_probe_t *probe_sent_clock;
static int16_t *probe_sent_pcm;
static unsigned probe_near_used,probe_sent_used,probe_sent_frames;
static uint64_t probe_requested_us;
static bool probe_stopped;
static atomic_bool probe_raw_active,probe_sent_active;
static atomic_bool probe_linear;
static atomic_bool probe_rolling,probe_trigger_requested;
static unsigned probe_raw_total;
static atomic_uint probe_raw_skips,probe_sent_skips;
enum { NEAR_MONITOR=1,NEAR_VOICE=2,NEAR_DUAL=4,NEAR_ENERGY=8,
       NEAR_MIC_ECHO=16,NEAR_OUT_ECHO=32,NEAR_ELIGIBLE=64,NEAR_STRONG=128,
       NEAR_CONFIRMED=256,NEAR_CORRELATED=512,NEAR_LINEAR=1024,NEAR_ALIGNED=4096,
       NEAR_LINEAR_VOICE=8192,NEAR_VAD_BRIDGE=16384,NEAR_ONSET_HINT=32768 };
static void probe_free(void)
{
    atomic_store(&probe_raw_active,false);atomic_store(&probe_sent_active,false);
    free(probe_pcm);free(probe_near);free(probe_sent_pcm);free(probe_sent_clock);
    probe_pcm=NULL;probe_near=NULL;probe_sent_pcm=NULL;probe_sent_clock=NULL;
    probe_limit=probe_used=probe_near_used=probe_sent_used=probe_sent_frames=0;
    probe_requested_us=0;probe_stopped=false;atomic_store(&probe_linear,false);
    probe_raw_total=0;atomic_store(&probe_rolling,false);atomic_store(&probe_trigger_requested,false);
}
void p4_audio_probe_trigger(void)
{
    if(atomic_load(&probe_rolling) && atomic_load(&probe_raw_active))
        atomic_store(&probe_trigger_requested,true);
}
static int probe_hex(const uint8_t *data,unsigned bytes,unsigned offset,char *out,size_t cap,unsigned start)
{
    if(!data || offset>=bytes || cap<16400)return 3;
    unsigned n=bytes-offset;if(n>8000)n=8000;
    int used=snprintf(out,cap,"{\"offset\":%u,\"hex\":\"",offset);
    const char *hex="0123456789abcdef";
    unsigned at=(start+offset)%bytes;
    for(unsigned i=0;i<n;i++){out[used++]=hex[data[at]>>4];out[used++]=hex[data[at]&15];if(++at==bytes)at=0;}
    snprintf(out+used,cap-used,"\"}");return 0;
}
void p4_audio_probe_sent(const int16_t *pcm,size_t samples,uint64_t capture_us)
{
    if(!atomic_load(&probe_sent_active))return;
    if(!pcm || samples!=320){atomic_fetch_add(&probe_sent_skips,1);return;}
    if(xSemaphoreTake(probe_sent_lock,0)!=pdTRUE){atomic_fetch_add(&probe_sent_skips,1);return;}
    if(probe_sent_pcm && probe_sent_used<probe_limit && samples<=320 &&
       probe_sent_frames<(probe_limit+319)/320) {
        unsigned n=probe_limit-probe_sent_used;if(n>samples)n=(unsigned)samples;
        memcpy(probe_sent_pcm+probe_sent_used,pcm,n*sizeof(*pcm));
        probe_sent_clock[probe_sent_frames++]=(sent_probe_t){capture_us,(uint64_t)esp_timer_get_time()};
        probe_sent_used+=n;
        if(probe_sent_used==probe_limit)atomic_store(&probe_sent_active,false);
    }
    xSemaphoreGive(probe_sent_lock);
}
int p4_audio_probe(const char *command,char *out,size_t cap)
{
#if defined(AGENT_P4_AEC_BENCH)
    if(!strcmp(command,"tasks"))return p4_aec_tasks(out,cap);
    if(!strcmp(command,"bench")) {
#if defined(AGENT_P4_AOQ_DUPLEX)
        bool playing=atomic_load(&output_active);
#else
        bool playing=atomic_load(&microphone_scale)!=1;
#endif
        if(cap<4096 || atomic_load(&capture) || atomic_load(&near_monitor) || playing ||
           atomic_load(&probe_raw_active) || atomic_load(&probe_sent_active))return 3;
        atomic_store(&benchmark_active,true);
        int result=p4_aec_bench(out,cap);
        atomic_store(&benchmark_active,false);return result;
    }
#endif
    if(!probe_lock || xSemaphoreTake(probe_lock,pdMS_TO_TICKS(50))!=pdTRUE)return 3;
    /* PCM capture and accepted-uplink copies have independent owners. They
     * must not drop diagnostic frames merely because the other copy runs.
     * Only control takes both locks, always raw then sent; neither real-time
     * producer waits or takes the other owner's lock. */
    if(xSemaphoreTake(probe_sent_lock,pdMS_TO_TICKS(50))!=pdTRUE){xSemaphoreGive(probe_lock);return 3;}
    unsigned ms,offset;char extra;int error=0;
    bool rolling=sscanf(command,"linear-roll %u %c",&ms,&extra)==1;
    bool linear=rolling || sscanf(command,"linear-start %u %c",&ms,&extra)==1;
    if((linear || sscanf(command,"start %u %c",&ms,&extra)==1) && ms>=1000 && ms<=8000) {
        if(atomic_load(&probe_raw_active))error=3;
        else {probe_free();unsigned count=ms*16;if(rolling)count-=count%512;
            probe_pcm=heap_caps_malloc(count*(linear?8:6),MALLOC_CAP_SPIRAM);
            probe_near=heap_caps_malloc(((count+511)/512)*sizeof(*probe_near),MALLOC_CAP_SPIRAM);
            probe_sent_pcm=heap_caps_malloc(count*2,MALLOC_CAP_SPIRAM);
            probe_sent_clock=heap_caps_malloc(((count+319)/320)*sizeof(*probe_sent_clock),MALLOC_CAP_SPIRAM);
            if(!probe_pcm || !probe_near || !probe_sent_pcm || !probe_sent_clock){probe_free();error=19;}
            else {probe_limit=count;probe_requested_us=(uint64_t)esp_timer_get_time();atomic_store(&probe_linear,linear);
                atomic_store(&probe_rolling,rolling);
                atomic_store(&probe_raw_skips,0);atomic_store(&probe_sent_skips,0);
                atomic_store(&probe_raw_active,true);atomic_store(&probe_sent_active,true);}}
    } else if(!strcmp(command,"free"))probe_free();
    else if(!strcmp(command,"status"))snprintf(out,cap,"{\"samples\":%u,\"target\":%u,\"channels\":%u,\"rate\":16000,\"order\":\"%s\",\"requested_us\":%llu,\"near_frames\":%u,\"sent_samples\":%u,\"sent_frames\":%u,\"raw_skips\":%u,\"sent_skips\":%u,\"stopped\":%s,\"rolling\":%s,\"triggered\":%s,\"raw_total_samples\":%u}",
        probe_used,probe_limit,atomic_load(&probe_linear)?4u:3u,atomic_load(&probe_linear)?"mic,dac_reference,aec_scaled,linear_scaled":"mic,dac_reference,aec_scaled",
        (unsigned long long)probe_requested_us,probe_near_used,probe_sent_used,probe_sent_frames,atomic_load(&probe_raw_skips),atomic_load(&probe_sent_skips),probe_stopped?"true":"false",
        atomic_load(&probe_rolling)?"true":"false",atomic_load(&probe_trigger_requested)?"true":"false",probe_raw_total);
    else if(sscanf(command,"read %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_raw_active))error=3;
        else {unsigned width=atomic_load(&probe_linear)?8:6;
            unsigned start=atomic_load(&probe_rolling) && probe_limit && probe_raw_total>=probe_limit?probe_raw_total%probe_limit:0;
            error=probe_hex((const uint8_t *)probe_pcm,probe_used*width,offset,out,cap,start*width);}
    } else if(sscanf(command,"sent-read %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_sent_active))error=3;
        else error=probe_hex((const uint8_t *)probe_sent_pcm,probe_sent_used*2,offset,out,cap,0);
    } else if(sscanf(command,"near %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_raw_active) || offset>=probe_near_used || cap<4096)error=3;
        else {unsigned end=offset+16;if(end>probe_near_used)end=probe_near_used;
            int used=snprintf(out,cap,"{\"offset\":%u,\"columns\":\"ms,mic,ref,aec,scale,flags,votes,strong,mic_envelope,ref_envelope,mic_corr_milli,aec_corr_milli,linear_us,nlp_us,processing_us,wide_votes,wide_strong\",\"rows\":[",offset);
            unsigned start=atomic_load(&probe_rolling) && probe_limit && probe_raw_total>=probe_limit?(probe_raw_total/512)%(probe_limit/512):0;
            for(unsigned i=offset;i<end;i++){const near_probe_t *v=&probe_near[(start+i)%probe_near_used];
                used+=snprintf(out+used,cap-used,"%s[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",i==offset?"":",",(unsigned)v->at_ms,v->mic,v->ref,v->out,v->scale,v->flags,v->votes,v->strong,v->last_mic,v->last_ref,v->mic_corr,v->out_corr,v->linear_us,v->nlp_us,v->processing_us,v->wide_votes,v->wide_strong);}
            snprintf(out+used,cap-used,"]}");}
    } else if(sscanf(command,"sent-clock %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_sent_active) || offset>=probe_sent_frames || cap<4096)error=3;
        else {unsigned end=offset+16;if(end>probe_sent_frames)end=probe_sent_frames;
            int used=snprintf(out,cap,"{\"offset\":%u,\"columns\":\"capture_us,accepted_us\",\"rows\":[",offset);
            for(unsigned i=offset;i<end;i++)used+=snprintf(out+used,cap-used,"%s[%llu,%llu]",i==offset?"":",",(unsigned long long)probe_sent_clock[i].capture_us,(unsigned long long)probe_sent_clock[i].sent_us);
            snprintf(out+used,cap-used,"]}");}
    } else if(!strcmp(command,"stop")) {
        atomic_store(&probe_raw_active,false);atomic_store(&probe_sent_active,false);
        probe_stopped=true;
    } else error=1;
    xSemaphoreGive(probe_sent_lock);xSemaphoreGive(probe_lock);return error;
}
void p4_audio_echo_stats(unsigned v[7])
{
    v[0]=atomic_load(&echo_frames);v[1]=atomic_load(&echo_max_us);
    v[2]=atomic_load(&echo_mic_level);v[3]=atomic_load(&echo_ref_level);v[4]=atomic_load(&echo_out_level);
    v[5]=echo_bytes;v[6]=echo_internal;
}
void p4_audio_capture_stats(unsigned v[5])
{
    v[0]=atomic_load(&capture_frames);v[1]=atomic_load(&capture_max_us);
    v[2]=atomic_load(&near_max_us);v[3]=atomic_load(&capture_overruns);
    v[4]=atomic_load(&rx_overflows);
}
void p4_audio_near_monitor(bool enable)
{atomic_store(&confirmed_at,0);atomic_store(&confirmed_onset,0);atomic_fetch_add(&near_epoch,1);atomic_store(&near_monitor,enable);}
uint32_t p4_audio_confirmed_at(void){return atomic_load(&capture_discontinuous)?0:atomic_load(&confirmed_at);}
uint32_t p4_audio_confirmed_onset(void){return atomic_load(&capture_discontinuous)?0:atomic_load(&confirmed_onset);}
unsigned p4_audio_idle_noise(void)
{
#if defined(AGENT_P4_AOQ_DUPLEX)
    unsigned count=atomic_load_explicit(&quiet_count,memory_order_acquire),n=count<64?count:64;
    if(!n)return 140;
    unsigned values[64];
    for(unsigned i=0;i<n;i++){
        unsigned value=atomic_load(&quiet_levels[i]),j=i;
        while(j && values[j-1]>value){values[j]=values[j-1];--j;}
        values[j]=value;
    }
    return values[n/4];
#else
    return 0;
#endif
}
/* Compare synchronous physical channels, never a queued microphone frame
 * against a later DAC reading. Reject speech-shaped leakage of the speaker. */
#if defined(AGENT_P4_AOQ_DUPLEX)
static bool near_vad_frame(void *context,int16_t *pcm)
{return vad_process((vad_handle_t)context,pcm,16000,30)==VAD_SPEECH;}
#endif
static void near_frame(unsigned mic,unsigned ref,unsigned out,unsigned scale,int16_t *signal,bool linear)
{
    static unsigned votes,strong,last_mic,last_ref,epoch;
    static p4_near_onset_t onset;
#if defined(AGENT_P4_AOQ_DUPLEX)
    static p4_vad_stream_t vad_stream,linear_vad_stream;
    static p4_near_alignment_t alignment;
    static p4_near_onset_t hinted_onset;
#else
    static int16_t previous_ref[1536];
#endif
    unsigned current=atomic_load(&near_epoch);
    if(epoch!=current){
        votes=strong=last_mic=last_ref=0;onset=(p4_near_onset_t){0};epoch=current;
#if defined(AGENT_P4_AOQ_DUPLEX)
        vad_stream.used=linear_vad_stream.used=0;
        memset(&alignment,0,sizeof(alignment));
        hinted_onset=(p4_near_onset_t){0};
#else
        memset(previous_ref,0,sizeof(previous_ref));
#endif
    }
    near_probe=(near_probe_t){.at_ms=(unsigned)(esp_timer_get_time()/1000),.mic=mic,.ref=ref,.out=out,.scale=scale};
    if(linear)near_probe.flags|=NEAR_LINEAR;
    if(!atomic_load(&near_monitor))return;
    near_probe.flags|=NEAR_MONITOR;
    /* A missing DMA block invalidates the sample clock and echo history.
     * Never turn a gap into an apparently valid interruption candidate. */
    if(atomic_load(&capture_discontinuous))return;
    unsigned suppressed=0;
    if(linear)for(unsigned i=0;i<512;i++)suppressed+=(unsigned)abs(echo_out[i]);
#if defined(AGENT_P4_AOQ_DUPLEX)
    bool voiced=p4_vad_stream_feed(&vad_stream,linear?echo_out:signal,512,near_vad_frame,near_vad);
    bool residual_voice=p4_vad_stream_feed(&linear_vad_stream,signal,512,near_vad_frame,near_linear_vad);
    if(residual_voice)near_probe.flags|=NEAR_LINEAR_VOICE;
    if(linear && !voiced && p4_near_speech(false,residual_voice,suppressed/512)){
        voiced=true;near_probe.flags|=NEAR_VAD_BRIDGE;
    }
#else
    bool voiced=vad_process(near_vad,linear?echo_out:signal,16000,30)==VAD_SPEECH;
#endif
#if defined(AGENT_P4_AOQ_DUPLEX)
    /* The probe's raw levels retain their original capture clock. Only the
     * echo decision/envelopes use the microphone/reference aligned to AEC. */
    const int16_t *aligned_mic=alignment.mic,*aligned_ref=alignment.reference+1536;
    unsigned aligned_m=0,aligned_r=0;
    for(unsigned i=0;i<512;++i){aligned_m+=(unsigned)abs(aligned_mic[i]);aligned_r+=(unsigned)abs(aligned_ref[i]);}
    mic=aligned_m/512;ref=aligned_r/512;
    if(alignment.ready)near_probe.flags|=NEAR_ALIGNED;
    bool dual=atomic_load(&output_active) || ref>96;
#else
    bool dual=scale>1 || ref>96;
#endif
    if(voiced)near_probe.flags|=NEAR_VOICE;
    if(dual)near_probe.flags|=NEAR_DUAL;
    /* The FD filter has overlap. Compare against the recent acoustic
     * envelope even during a syllable gap in this particular DAC frame. */
    last_mic=mic>last_mic*4/5?mic:last_mic*4/5;last_ref=ref>last_ref*4/5?ref:last_ref*4/5;
    near_probe.last_mic=last_mic;near_probe.last_ref=last_ref;
    /* A cancelled linear residual is not proportional to the loud DAC or
     * echo-dominated raw microphone. Requiring those ratios rejects the
     * recorded intelligible near utterance even before VAD/correlation.
     * Keep the absolute floor and independent-speech/correlation checks. */
    bool energy=dual?(linear?out>620:(out>620 && out>100+last_mic*9/10 && out>120+last_ref/3)):out>280;
    if(energy)near_probe.flags|=NEAR_ENERGY;
    bool near=voiced && energy;
#if defined(AGENT_P4_AOQ_DUPLEX)
    near=near && alignment.ready;
    /* Retain a possible early syllable independently of the NLP vote.
     * It must still pass pitch, spectrum and the same echo rejection.
     * This hint affects replay only after the normal votes confirm a cut. */
    bool onset_hint=linear && alignment.ready && residual_voice && out>280 &&
        p4_voice_above_low_band(signal) && p4_voice_periodic(signal);
#else
    bool onset_hint=false;
#endif
    if((near || onset_hint) && dual && last_ref>96) {
        /* Passive double-talk check: reject correlated raw AND AEC residual
         * echo including 96 ms of filter/acoustic tail. Never silence the speaker to test a candidate. */
        bool diagnostic=atomic_load(&probe_raw_active);
#if defined(AGENT_P4_AOQ_DUPLEX)
        p4_near_correlation_t c=p4_near_correlate_refined(aligned_mic,signal,aligned_ref,alignment.reference,diagnostic);
#else
        p4_near_correlation_t c=p4_near_correlate(echo_mic,signal,echo_ref,previous_ref,diagnostic);
#endif
        if(diagnostic){near_probe.flags|=NEAR_CORRELATED;near_probe.mic_corr=c.mic_permille;near_probe.out_corr=c.output_permille;}
        if(c.mic_echo)near_probe.flags|=NEAR_MIC_ECHO;
        if(c.output_echo)near_probe.flags|=NEAR_OUT_ECHO;
        if(c.mic_echo || c.output_echo){near=false;onset_hint=false;}
    }
#if defined(AGENT_P4_AOQ_DUPLEX)
    p4_near_alignment_push(&alignment,echo_mic,echo_ref);
#else
    memmove(previous_ref,previous_ref+512,1024*sizeof(int16_t));
    memcpy(previous_ref+1024,echo_ref,512*sizeof(int16_t));
#endif
#if defined(AGENT_P4_AOQ_DUPLEX)
    const unsigned vote_mask=65535;
#else
    const unsigned vote_mask=4095;
#endif
    votes=((votes<<1)|(unsigned)near)&vote_mask;
    if(near)near_probe.flags|=NEAR_ELIGIBLE;
    p4_near_onset_observe(&onset,near_probe.at_ms,near);
#if defined(AGENT_P4_AOQ_DUPLEX)
    if(onset_hint)near_probe.flags|=NEAR_ONSET_HINT;
    p4_near_onset_hint(&hinted_onset,near_probe.at_ms,onset_hint);
#endif
    /* Native duplex uses independent voiced evidence below; the raw
     * microphone envelope contains our own speaker. WSS keeps its energy
     * criterion. Neither path changes the samples sent to the provider. */
    bool solid=near && (linear?suppressed/512>620:out>200+last_mic*scale/2);
#if defined(AGENT_P4_AOQ_DUPLEX)
    /* Keep NLP attenuation from vetoing independently voiced linear speech.
     * All near-admission and multi-frame confirmation checks still apply. */
    if(near && linear)solid=p4_voice_duplex_strong(echo_out,signal,residual_voice,suppressed/512);
#endif
    strong=((strong<<1)|(unsigned)solid)&vote_mask;
    if(solid)near_probe.flags|=NEAR_STRONG;
    near_probe.votes=__builtin_popcount(votes&4095);near_probe.strong=__builtin_popcount(strong&4095);
    /* Require sustained independent speech, not a two-frame echo transient.
     * Full-volume playback continues throughout the confirmation window. */
    bool confirmed=near && near_probe.votes>=(dual?6:3) && (!dual || near_probe.strong>=2);
#if defined(AGENT_P4_AOQ_DUPLEX)
    near_probe.wide_votes=__builtin_popcount(votes);
    near_probe.wide_strong=__builtin_popcount(strong);
    if(dual)confirmed=p4_near_duplex_confirm(near,votes,strong);
#endif
    if(confirmed) {
        uint32_t retained_onset=onset.first;
#if defined(AGENT_P4_AOQ_DUPLEX)
        retained_onset=p4_near_onset_select(&onset,&hinted_onset,near_probe.at_ms);
#endif
        near_probe.flags|=NEAR_CONFIRMED;atomic_store(&confirmed_onset,retained_onset);
        atomic_store(&confirmed_at,(unsigned)(esp_timer_get_time()/1000));
    }
}
static bool IRAM_ATTR tx_sent(i2s_chan_handle_t channel, i2s_event_data_t *event, void *ctx)
{
    (void)channel;(void)ctx;
    atomic_fetch_add_explicit(&tx_samples,event->size/4,memory_order_relaxed);
    return false;
}
static bool IRAM_ATTR rx_overflow(i2s_chan_handle_t channel,i2s_event_data_t *event,void *ctx)
{
    (void)channel;(void)ctx;
    atomic_fetch_add_explicit(&rx_overflows,1,memory_order_relaxed);
    if(atomic_load_explicit(&capture,memory_order_relaxed)) {
        atomic_store_explicit(&capture_discontinuous,true,memory_order_relaxed);
        atomic_store_explicit(&confirmed_at,0,memory_order_relaxed);
        atomic_store_explicit(&confirmed_onset,0,memory_order_relaxed);
        atomic_fetch_add_explicit(&near_epoch,1,memory_order_relaxed);
        atomic_fetch_add_explicit(&dropped,event->size/4,memory_order_relaxed);
    }
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
    i2s_event_callbacks_t rx_callbacks={.on_recv_q_ovf=rx_overflow};
    if((err=i2s_channel_register_event_callback(tx,&callbacks,NULL)) ||
       (err=i2s_channel_register_event_callback(rx,&rx_callbacks,NULL)) ||
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
        int64_t processing_at=esp_timer_get_time();
        unsigned m=0,r=0,o=0,linear_us=0,nlp_us=0;
        for(unsigned i=0;i<512;++i) {m+=abs(echo_mic[i]);r+=abs(echo_ref[i]);}
        if(r>512*4)tail=20; /* 640 ms acoustic tail after the last output. */
        bool linear_probe=atomic_load(&probe_raw_active) && atomic_load(&probe_linear);
        bool native_duplex=false;
#if defined(AGENT_P4_AOQ_DUPLEX)
        /* NLP-only upload repeatedly suppressed parts of real interruptions.
         * Preserve the linear signal for native upload; keep NLP for the
         * independent local voice/strong-energy gates. Cloud cancellation
         * recovery must retain the validated boundary without trimming words. */
        native_duplex=atomic_load(&near_monitor);
#endif
        bool need_linear=linear_probe || native_duplex;
        bool process_echo=tail!=0;
#if defined(AGENT_P4_AOQ_DUPLEX)
        /* The adaptive filter and its overlap must see every capture frame,
         * including silence between replies. Bypassing those frames splices
         * unrelated turns into its delay line and changes uplink latency at
         * the exact boundary where a near-end interruption must be retained. */
        process_echo=true;
#endif
#if defined(AGENT_P4_AEC_BENCH)
        process_echo=process_echo && !atomic_load(&benchmark_active);
#endif
        if(process_echo) {
            if(tail)--tail;
            int64_t begin=esp_timer_get_time();
            if(need_linear) {
                /* Run each stage once, keeping both results for the explicit
                 * capture selection below. The SDK requires the NLP call
                 * immediately after its linear stage. */
                aec_linear_process(echo,echo_mic,echo_ref,echo_linear);
                int64_t linear_end=esp_timer_get_time();
                linear_us=(unsigned)(linear_end-begin);
                aec_nlp_process(echo,echo_out);
                nlp_us=(unsigned)(esp_timer_get_time()-linear_end);
            } else aec_process(echo,echo_mic,echo_ref,echo_out);
            unsigned us=(unsigned)(esp_timer_get_time()-begin);
            if(us>atomic_load(&echo_max_us))atomic_store(&echo_max_us,us);
            atomic_fetch_add(&echo_frames,1);
        } else {
            /* No reference/tail: bypass AEC, then apply the configured ADC
             * compensation below (native AOQ keeps its gain fixed). */
            memcpy(echo_out,echo_mic,1024);
            if(need_linear)memcpy(echo_linear,echo_mic,1024);
        }
        unsigned scale=atomic_load(&microphone_scale);
#if defined(AGENT_P4_AOQ_DUPLEX)
        if(!atomic_load(&near_monitor) && !atomic_load(&output_active) && r<512*16){
            unsigned at=atomic_load_explicit(&quiet_count,memory_order_relaxed);
            atomic_store(&quiet_levels[at%64],m/512*scale);
            atomic_store_explicit(&quiet_count,at+1,memory_order_release);
        }
#endif
        int16_t *delivered=native_duplex?echo_linear:echo_out;
        for(unsigned i=0;i<512;++i) {
            int value=echo_out[i]*(int)scale;
            echo_out[i]=value>32767?32767:value< -32768?-32768:value;
            if(need_linear){value=echo_linear[i]*(int)scale;echo_linear[i]=value>32767?32767:value< -32768?-32768:value;}
            o+=abs(delivered[i]);
        }
        atomic_store(&echo_mic_level,m/512);atomic_store(&echo_ref_level,r/512);atomic_store(&echo_out_level,o/512);
        int64_t near_at=esp_timer_get_time();
        near_frame(m/512,r/512,o/512,scale,delivered,native_duplex);
        unsigned near_us=(unsigned)(esp_timer_get_time()-near_at);
        unsigned processing_us=(unsigned)(esp_timer_get_time()-processing_at);
        near_probe.linear_us=linear_us;near_probe.nlp_us=nlp_us;near_probe.processing_us=processing_us;
        if(near_us>atomic_load(&near_max_us))atomic_store(&near_max_us,near_us);
        if(processing_us>atomic_load(&capture_max_us))atomic_store(&capture_max_us,processing_us);
        if(processing_us>32000)atomic_fetch_add(&capture_overruns,1);
        atomic_fetch_add(&capture_frames,1);
        if(atomic_load(&probe_raw_active) && xSemaphoreTake(probe_lock,0)==pdTRUE) {
            bool rolling=atomic_load(&probe_rolling);
            if(probe_pcm && (probe_used<probe_limit || rolling)) {
                unsigned write_at=rolling?probe_raw_total%probe_limit:probe_used;
                unsigned n=probe_limit-write_at;if(n>512)n=512;
                unsigned channels=atomic_load(&probe_linear)?4:3;
                /* A capture started mid-frame has no corresponding linear
                 * residual yet. Skip that diagnostic copy, never substitute
                 * stale linear audio or delay the real-time capture owner. */
                if(channels==4 && !need_linear){atomic_fetch_add(&probe_raw_skips,1);n=0;}
                for(unsigned i=0;i<n;i++){unsigned at=(write_at+i)*channels;
                    probe_pcm[at]=echo_mic[i];probe_pcm[at+1]=echo_ref[i];probe_pcm[at+2]=channels==4?echo_out[i]:delivered[i];
                    if(channels==4)probe_pcm[at+3]=echo_linear[i];}
                if(n){
                    unsigned at=rolling?(probe_raw_total/512)%(probe_limit/512):probe_near_used;
                    probe_near[at]=near_probe;
                    if(probe_near_used<(probe_limit+511)/512)++probe_near_used;
                }
                probe_raw_total+=n;probe_used=probe_raw_total<probe_limit?probe_raw_total:probe_limit;
                if(!rolling && probe_used==probe_limit)atomic_store(&probe_raw_active,false);
                if(rolling && atomic_load(&probe_trigger_requested)){
                    atomic_store(&probe_raw_active,false);atomic_store(&probe_sent_active,false);probe_stopped=true;
                }
            }
            xSemaphoreGive(probe_lock);
        } else if(atomic_load(&probe_raw_active))atomic_fetch_add(&probe_raw_skips,1);
        if(!atomic_load(&capture))continue;
        for(unsigned i=0;i<512;i+=128)
            if(xQueueSend(frames,delivered+i,0)!=pdTRUE)atomic_fetch_add(&dropped,128);
    }
}
esp_err_t p4_audio_init(void)
{
    if(initialized) return ESP_OK;
    if(frames) return ESP_ERR_INVALID_STATE;
    probe_lock=xSemaphoreCreateMutex();probe_sent_lock=xSemaphoreCreateMutex();
    if(!probe_lock || !probe_sent_lock)return ESP_ERR_NO_MEM;
    frames=xQueueCreate(32,128*sizeof(int16_t));
    if(!frames) return ESP_ERR_NO_MEM;
    esp_err_t err=codecs_init();if(err)return err;
    esp_codec_dev_sample_info_t fmt={.sample_rate=16000,.channel=2,.channel_mask=3,.bits_per_sample=16,.mclk_multiple=256};
    if(esp_codec_dev_open(output,&fmt)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    /* The user-facing master remains 0..100. At the old 0 dB DAC setting,
     * speaker leakage masked near-end wake speech even with a lower ADC gain.
     * Reserve 8 dB output headroom for duplex; still 9.5 dB above the old 65
     * codec setting. This is an output calibration, not a weaker KWS gate. */
#if defined(AGENT_P4_AOQ_DUPLEX)
    const int input_gain=30;
#else
    const int input_gain=42;
#endif
    if(esp_codec_dev_set_out_vol(output,84)!=ESP_CODEC_DEV_OK || esp_codec_dev_set_in_gain(input,input_gain)!=ESP_CODEC_DEV_OK)
        return ESP_FAIL;
    unsigned before=heap_caps_get_free_size(MALLOC_CAP_8BIT),internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    aec_config_t config={.mic_num=1,.ref_num=1,.out_num=1,.filter_length=4,.sample_rate=16000,
        .caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,.mode=AEC_MODE_FD_HIGH_PERF,.nlp_level=AEC_NLP_LEVEL_NORMAL};
    /* NORMAL preserves double-talk that AGGR erased in the captured missed
     * interruption. Independent aligned correlation, strength and multi-frame
     * confirmation still reject echo; native upload uses the linear residual. */
    /* Keep the original high-performance full-duplex filter. The low-cost
     * experiment did not fix the inherited-priority capture stalls; those
     * are fixed at the workspace gate. Revalidate the live deadline with
     * this filter, without weakening near-speech or echo rejection gates. */
    echo=aec_create_from_config(&config);
    /* The near-end detector consumes the suppressed residual, separately
     * from the linear uplink. Use the noise-rejecting mode for both transports;
     * the onset replay still retains speech before this confirmation. */
    near_vad=vad_create_with_param(VAD_MODE_3,16000,30,60,100);
#if defined(AGENT_P4_AOQ_DUPLEX)
    near_linear_vad=vad_create_with_param(VAD_MODE_3,16000,30,60,100);
    if(!near_linear_vad)return ESP_ERR_NO_MEM;
#endif
    echo_mic=heap_caps_aligned_alloc(16,4*512*sizeof(int16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!echo || !near_vad || aec_get_chunksize(echo)!=512 || !echo_mic)return ESP_ERR_NO_MEM;
    echo_ref=echo_mic+512;echo_out=echo_ref+512;echo_linear=echo_out+512;
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
    atomic_store(&capture_discontinuous,false);
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
#if defined(AGENT_P4_AOQ_DUPLEX)
    if(esp_codec_dev_set_out_mute(output,false)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    /* Idle speech/noise votes cannot become this reply's barge-in evidence.
     * Reset the detector epoch, not the acoustic filter or capture ring. */
    atomic_store(&confirmed_at,0);atomic_store(&confirmed_onset,0);
    atomic_fetch_add(&near_epoch,1);
    atomic_store(&output_active,true);return ESP_OK;
#else
    /* Prevent the on-board speaker from clipping the microphone ADC before
     * echo cancellation. Restore the same nominal KWS level AFTER AEC. */
    if(esp_codec_dev_set_in_gain(input,30)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    atomic_store(&microphone_scale,4);
    return esp_codec_dev_set_out_mute(output,false)==ESP_CODEC_DEV_OK?ESP_OK:ESP_FAIL;
#endif
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
#if defined(AGENT_P4_AOQ_DUPLEX)
    atomic_store(&output_active,false);return ESP_OK;
#else
    atomic_store(&microphone_scale,1);
    return esp_codec_dev_set_in_gain(input,42)==ESP_CODEC_DEV_OK?ESP_OK:ESP_FAIL;
#endif
}
