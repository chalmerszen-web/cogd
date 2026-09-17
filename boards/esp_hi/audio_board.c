#include "audio_board.h"
#include "phase.h"
#if !defined(AGENT_BACKGROUND_VERIFY) || !AGENT_PACKED_CLIP
#error "Pipeline study requires original packed background verifier"
#endif
#include "busy_trace.h"
#include "clip.h"
#include "audio_work.h"
#include "endpoint.h"
#include "speech_backend.h"
#ifdef AGENT_MIC_OVERSAMPLE
#include "decimate.h"
#define MIC_ADC_RATE (AGENT_MIC_RATE*2u)
static agent_decimate_t decimator;
#else
#define MIC_ADC_RATE AGENT_MIC_RATE
#endif
#define MIC_POOL_BYTES 10240u
#ifdef AGENT_VOICE_VERIFY
#include "verify.h"
#endif
#ifdef AGENT_BACKGROUND_VERIFY
#include "vad_worker.h"
#include "vad_backend.h"
#endif
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_adc/adc_continuous.h"
#include "esp_rom_gpio.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

enum { PDM_P = 6, PDM_N = 7, PA = 3, PCM_CHUNK = 240, ADC_SAMPLES = 128 };
typedef enum { AUDIO_SCORE,AUDIO_CAPTURE,AUDIO_REPLAY,AUDIO_SONG } audio_kind_t;
typedef struct { audio_kind_t kind; union { agent_score_t score; unsigned ms; }; } audio_command_t;
#define MIC_CHANNEL ADC_CHANNEL_2
#ifndef AGENT_MIC_ATTENUATION
#define AGENT_MIC_ATTENUATION ADC_ATTEN_DB_12
#endif
static QueueHandle_t score_queue;
static SemaphoreHandle_t control_lock;
static TaskHandle_t audio_task_handle;
static i2s_chan_handle_t speaker;
static adc_continuous_handle_t mic;
static bool speaker_enabled,mic_started;
static atomic_bool playing, stop_requested, mic_requested, mic_enabled;
static atomic_bool recording,clip_ready,mic_valid;
static atomic_uint mic_updated,clip_ms,capture_samples,capture_stage;
static atomic_uint mic_max_gap,mic_max_read_gap;
static atomic_uint record_adc_clipped,start_cue_adc_clipped;
static atomic_bool record_input_valid;
static bool cue_collecting;
static atomic_uint speaker_max_write, speaker_max_open, speaker_max_close,mic_max_work;
static atomic_int capture_error;
static atomic_uint volume = 80, job_id, rendered, total, mic_samples, mic_rms, mic_peak, mic_bias, mic_clipped, mic_overruns;
static atomic_int play_error, mic_error, sdk_error;
static agent_mic_meter_t meter;
static int16_t pcm[PCM_CHUNK];
static uint8_t adc_raw[ADC_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES];
static adc_continuous_data_t adc_data[ADC_SAMPLES];
static const esp_partition_t *clip_partition;
static agent_clip_t clip;
static esp_hi_audio_work_t audio_work;
static agent_song_t songs[2];
static atomic_uint last_song_index,last_song_job,music_render_us,music_limited;
static atomic_bool song_ready;
static uint64_t mic_polled,mic_received;
static unsigned capture_used,captured;
static bool capture_collecting;
#ifdef AGENT_BACKGROUND_VERIFY
static struct { atomic_uint calls,total_us,max_us; } clip_io[3]; /* read, write, erase */
static atomic_uint capture_us;
static void clip_timed(unsigned kind,int64_t start)
{
    if(!atomic_load(&recording)) return;
    unsigned elapsed=(unsigned)(esp_timer_get_time()-start);
    atomic_fetch_add(&clip_io[kind].calls,1); atomic_fetch_add(&clip_io[kind].total_us,elapsed);
    unsigned prior=atomic_load(&clip_io[kind].max_us);
    while(elapsed>prior && !atomic_compare_exchange_weak(&clip_io[kind].max_us,&prior,elapsed)) {}
}
#endif
enum { WAKE_OFF,WAKE_LOADING,WAKE_LISTENING,WAKE_DING,WAKE_RECORDING,WAKE_FINISHING,WAKE_COOLDOWN,WAKE_PAUSED,WAKE_FAILED };
static atomic_bool wake_requested,wake_loaded,wake_network_busy;
static atomic_uint wake_stage,wake_count,wake_done,wake_empty,wake_limited,wake_infer_us,wake_heap;
static atomic_uint wake_detected_at,wake_record_at,wake_end_at,wake_cue_end_at,wake_last_samples;
static atomic_int wake_error,wake_reason;
static bool wake_hit,wake_capture,wake_mute_mic;
static atomic_uint wake_dma_lost,wake_clipped;
static uint64_t wake_rearm_at;
static unsigned wake_used,vad_used;
static agent_endpoint_t endpoint;
static agent_voice_t vad_filter;
static agent_biquad_t cue_filter;
static atomic_uint preparation_bytes,preparation_us;
static agent_activity_t activity;
static atomic_uint wake_noise,wake_level,wake_speech_ms;
static atomic_uint wake_threshold=550,wake_gain=1;
static unsigned applied_threshold;
#ifdef AGENT_KEYWORD_PCM
static keyword_pcm_t pcm_observation;
keyword_pcm_t *esp_hi_keyword_pcm(void) { return &pcm_observation; }
#endif
#ifdef AGENT_VOICE_VERIFY
static micro_verify_t verifier;
static atomic_uint verify_peak,verify_sum,verify_us;
static atomic_bool verify_confirmed,verify_supported;
#endif

static void wake_update(void);
static void wake_cycle(void);

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
static void duration(atomic_uint *maximum,uint64_t start)
{ unsigned elapsed=(unsigned)(now_ms()-start); if(elapsed>atomic_load(maximum)) atomic_store(maximum,elapsed); }

static agent_err_t audio_error(esp_err_t error)
{
    atomic_store(&sdk_error, error);
    return error == ESP_OK ? AGENT_OK : error == ESP_ERR_NO_MEM ? AGENT_ERR_MEMORY :
        error == ESP_ERR_TIMEOUT ? AGENT_ERR_TIMEOUT : AGENT_ERR_TOOL;
}

static esp_err_t speaker_close(void)
{
    uint64_t start=now_ms();
    gpio_set_level(PA, 0);
    esp_err_t error;
    if (speaker_enabled) {
        if((error=i2s_channel_disable(speaker))) return error;
        speaker_enabled=false;
    }
    if (speaker && (error=i2s_del_channel(speaker))) return error;
    speaker = NULL;
    speaker_enabled = false;
    gpio_reset_pin(PDM_N);
    duration(&speaker_max_close,start);
    return ESP_OK;
}

static esp_err_t speaker_open(void)
{
    uint64_t start=now_ms();
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 4;
    channel.dma_frame_num = PCM_CHUNK;
    channel.auto_clear = true;
    esp_err_t error = i2s_new_channel(&channel, &speaker, NULL);
    if (error) return error;
    i2s_pdm_tx_config_t config = {
        .clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(AGENT_AUDIO_RATE),
        .slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = GPIO_NUM_NC, .dout = PDM_P }
    };
    /* Direct differential speaker output uses DAC line mode and unity filter scales. */
    error = i2s_channel_init_pdm_tx_mode(speaker, &config);
    if (error) return error;
    gpio_config_t negative = { .pin_bit_mask = 1ULL << PDM_N, .mode = GPIO_MODE_OUTPUT };
    if ((error = gpio_config(&negative))) return error;
    esp_rom_gpio_connect_out_signal(PDM_N, I2SO_SD_OUT_IDX, true, false);
    gpio_set_drive_capability(PDM_P, GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability(PDM_N, GPIO_DRIVE_CAP_0);
    memset(pcm, 0, sizeof(pcm));
    size_t loaded = 0;
    if ((error = i2s_channel_preload_data(speaker, pcm, sizeof(pcm), &loaded))) return error;
    if ((error = i2s_channel_enable(speaker))) return error;
    speaker_enabled = true;
    duration(&speaker_max_open,start);
    return gpio_set_level(PA, 1);
}

static esp_err_t speaker_write(size_t samples)
{
    uint64_t start=now_ms();
    size_t bytes = 0;
    esp_err_t error = i2s_channel_write(speaker, pcm, samples * sizeof(*pcm), &bytes, 100);
    duration(&speaker_max_write,start);
    return error ? error : bytes == samples * sizeof(*pcm) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static bool IRAM_ATTR mic_overflow(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *event, void *ctx)
{
    (void)handle; (void)event; (void)ctx;
    atomic_fetch_add(&mic_overruns, 1);
    if(atomic_load(&wake_requested)) atomic_fetch_add(&wake_dma_lost,1);
    return false;
}

static esp_err_t mic_close(void)
{
    esp_err_t error;
    atomic_store(&mic_valid,false);
    if(mic_started) {
        if((error=adc_continuous_stop(mic))) return error;
        mic_started=false;
    }
    if (mic && (error=adc_continuous_deinit(mic))) return error;
    mic = NULL;
    atomic_store(&mic_enabled, false);
    return ESP_OK;
}

static esp_err_t mic_open(void)
{
    adc_continuous_handle_cfg_t handle = { .max_store_buf_size = MIC_POOL_BYTES,
        .conv_frame_size = sizeof(adc_raw), .flags.flush_pool = true };
    esp_err_t error = adc_continuous_new_handle(&handle, &mic);
    if (error) return error;
    adc_digi_pattern_config_t pattern = { .atten = AGENT_MIC_ATTENUATION, .channel = MIC_CHANNEL,
        .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12 };
    adc_continuous_config_t config = { .pattern_num = 1, .adc_pattern = &pattern,
        .sample_freq_hz = MIC_ADC_RATE, .conv_mode = ADC_CONV_SINGLE_UNIT_1 };
    adc_continuous_evt_cbs_t callbacks = { .on_pool_ovf = mic_overflow };
    if ((error = adc_continuous_config(mic, &config)) ||
        (error = adc_continuous_register_event_callbacks(mic, &callbacks, NULL)) ||
        (error = adc_continuous_start(mic))) return error;
    memset(&meter, 0, sizeof(meter));
#ifdef AGENT_MIC_OVERSAMPLE
    memset(&decimator,0,sizeof(decimator));
#endif
    atomic_store(&mic_samples, 0); atomic_store(&mic_overruns, 0);
    atomic_store(&mic_clipped, 0); atomic_store(&mic_rms, 0); atomic_store(&mic_peak, 0);
    atomic_store(&mic_enabled, true);
    mic_started=true;
    atomic_store(&mic_valid,false);
    mic_polled=mic_received=now_ms(); atomic_store(&mic_max_gap,0); atomic_store(&mic_max_read_gap,0);
    return ESP_OK;
}

static esp_err_t mic_pause(void)
{
    if(!mic || !mic_started) return ESP_ERR_INVALID_STATE;
    esp_err_t error=adc_continuous_stop(mic);
    if(!error) mic_started=false;
    return error;
}
static esp_err_t mic_resume(void)
{
    if(!mic || mic_started) return ESP_ERR_INVALID_STATE;
    esp_err_t error=adc_continuous_flush_pool(mic);
    if(!error) error=adc_continuous_start(mic);
    if(!error) {
        mic_started=true; mic_received=mic_polled=now_ms();
#ifdef AGENT_MIC_OVERSAMPLE
        /* A deliberate discontinuity must not reuse pre-boundary FIR samples. */
        memset(&decimator,0,sizeof(decimator));
#endif
    }
    return error;
}

/* Per-job observations survive the live meter's reset on automatic rearm.
 * A loaded clip has no runtime observations until a new sample is stored. */
static void capture_input_reset(void)
{
    atomic_store(&record_input_valid,false);
    atomic_store(&record_adc_clipped,0); atomic_store(&start_cue_adc_clipped,0);
}

static agent_err_t capture_flush(void)
{
    /* Erase only the next sector, ahead of writes. Full pre-erasure would delay
     * the ding by seconds, or destroy the previous clip while just listening. */
    while(
#if AGENT_PACKED_CLIP
          !clip.packed &&
#endif
          32+(clip.written+capture_used)*2>clip.erased) {
        bool done; agent_err_t e=agent_clip_prepare(&clip,&done); if(e) return e;
    }
    agent_err_t e=agent_clip_write(&clip,audio_work.input.capture,capture_used);
    if(!e) capture_used=0;
#ifdef AGENT_BACKGROUND_VERIFY
    /* The last compressed record may still occupy a partial page. Publish it
     * at the capture limit before the worker is joined, preserving all10s. */
    if(!e && wake_capture && (captured==clip.samples || esp_hi_confirmation_capture_done())) e=agent_clip_flush(&clip);
    if(!e && wake_capture) {
        size_t samples,bytes;agent_clip_progress(&clip,&samples,&bytes);
        e=esp_hi_confirmation_publish(samples,bytes);
    }
#endif
    return e;
}

static void wake_feed(int16_t value)
{
#ifdef AGENT_BACKGROUND_VERIFY
    /* Producer preprocessing runs through confirmation_source; do not feed twice. */
    if(wake_capture) return;
#endif
    int16_t filtered=agent_voice_filter(&vad_filter,value);
    unsigned stage=atomic_load(&wake_stage);
    if(wake_capture) {
        if(endpoint.state>=AGENT_EP_DONE) return;
#ifdef AGENT_VOICE_VERIFY
        /* Keyword state has been released. Reuse its fixed PCM buffer for raw
         * input; filtering and captured samples retain their existing paths. */
        audio_work.input.wake[vad_used]=value;
#endif
        audio_work.input.vad[vad_used++]=agent_voice_reject_cue(&cue_filter,filtered);
        if(vad_used==320) {
            bool voice=endpoint.elapsed_ms>=40 && esp_hi_speech_vad(audio_work.input.vad);
            voice=agent_activity_feed(&activity,audio_work.input.vad,320,false,voice,endpoint.state==AGENT_EP_SPEECH);
            agent_endpoint_feed(&endpoint,voice); vad_used=0;
#ifdef AGENT_VOICE_VERIFY
            int64_t started=esp_timer_get_time();
            if(!micro_verify_feed(&verifier,audio_work.input.wake,320)) atomic_store(&capture_error,AGENT_ERR_TOOL);
            unsigned us=(unsigned)(esp_timer_get_time()-started);
            if(us>atomic_load(&verify_us)) atomic_store(&verify_us,us);
            if(agent_endpoint_support(&endpoint,micro_verify_strong(&verifier))) atomic_store(&verify_supported,true);
            agent_endpoint_verify(&endpoint,verifier.confirmed);
            atomic_store(&verify_peak,verifier.peak); atomic_store(&verify_sum,verifier.peak_sum);
            atomic_store(&verify_confirmed,verifier.confirmed);
#endif
            atomic_store(&wake_noise,activity.noise); atomic_store(&wake_level,activity.level);
            atomic_store(&wake_speech_ms,endpoint.speech_ms);
        }
    } else if((stage==WAKE_LISTENING || stage==WAKE_COOLDOWN) && !wake_hit) {
        audio_work.input.vad[vad_used++]=filtered;
        if(vad_used==320) { agent_activity_feed(&activity,audio_work.input.vad,320,true,false,false); vad_used=0; }
        if(stage!=WAKE_LISTENING) return;
        /* Bounded keyword-only gain under acoustic calibration. Preserve
         * recorded PCM/VAD levels and expose input saturation; a larger
         * value does not by itself establish better recognition. */
        int32_t amplified=(int32_t)value*(int32_t)atomic_load(&wake_gain);
        if(amplified>32767) { amplified=32767; atomic_fetch_add(&wake_clipped,1); }
        if(amplified< -32768) { amplified= -32768; atomic_fetch_add(&wake_clipped,1); }
        audio_work.input.wake[wake_used++]=(int16_t)amplified;
        if(wake_used==esp_hi_speech_chunk()) {
#ifdef AGENT_KEYWORD_PCM
            bool observing=keyword_pcm_active(&pcm_observation);
            keyword_pcm_frame_t *frame=NULL;
            esp_hi_keyword_stats_t before={0};
            if(observing) {
                if(esp_hi_speech_chunk()!=KEYWORD_PCM_SAMPLES) {
                    keyword_pcm_halt(&pcm_observation,KEYWORD_PCM_IO);
                    atomic_store(&wake_requested,false);wake_used=0;return;
                }
                esp_hi_speech_keyword_stats(&before);
                int64_t copying=esp_timer_get_time();
                frame=keyword_pcm_begin(&pcm_observation,audio_work.input.wake,(uint32_t)now_ms());
                if(!frame) { atomic_store(&wake_requested,false);wake_used=0;return; }
                frame->copy_us=(unsigned)(esp_timer_get_time()-copying);
            }
#endif
            int64_t started=esp_timer_get_time();
            wake_hit=esp_hi_speech_wake(audio_work.input.wake); wake_used=0;
            unsigned us=(unsigned)(esp_timer_get_time()-started);
            if(us>atomic_load(&wake_infer_us)) atomic_store(&wake_infer_us,us);
#ifdef AGENT_KEYWORD_PCM
            if(observing) {
                esp_hi_keyword_stats_t after;esp_hi_speech_keyword_stats(&after);
                unsigned flags=(after.raw!=before.raw?KEYWORD_PCM_RAW:0) |
                    (wake_hit?KEYWORD_PCM_ACCEPTED:0) |
                    (after.invalid!=before.invalid?KEYWORD_PCM_INVALID:0) |
                    (after.incomplete!=before.incomplete?KEYWORD_PCM_INCOMPLETE:0);
                bool valid=(flags&KEYWORD_PCM_RAW) && !(flags&(KEYWORD_PCM_INVALID|KEYWORD_PCM_INCOMPLETE));
                if(!keyword_pcm_commit(&pcm_observation,frame,us,flags,valid?after.positive:0,valid?after.negative:0))
                    keyword_pcm_halt(&pcm_observation,KEYWORD_PCM_IO);
                /* Explicit observation only: the borrowed region belongs to
                 * USB, so no ding/recording/background verifier can start. */
                wake_hit=false;
                if(keyword_pcm_reason(&pcm_observation)!=KEYWORD_PCM_NONE) atomic_store(&wake_requested,false);
            }
#endif
        }
    }
}

static bool wake_recording_active(void)
{
#ifdef AGENT_BACKGROUND_VERIFY
    return !esp_hi_confirmation_done();
#else
    return endpoint.state<AGENT_EP_DONE;
#endif
}

static void mic_update(void)
{
    if (wake_mute_mic || (!atomic_load(&mic_requested) && !capture_collecting && !atomic_load(&wake_loaded))) {
        if(mic) atomic_store(&mic_error,audio_error(mic_close()));
        return;
    }
    esp_err_t error = ESP_OK;
    if (!mic) error = mic_open();
    uint64_t polled=now_ms(); unsigned gap=(unsigned)(polled-mic_polled);
    if(gap>atomic_load(&mic_max_gap)) atomic_store(&mic_max_gap,gap);
    mic_polled=polled;
    /* Bound work by the same 64 ms of delivered audio at either ADC rate.
     * A fixed raw-block count would halve throughput after decimation, making
     * a task yield and sector erases accumulate an unbounded backlog.
     * This is at most one complete pool; USB/I2S get the usual yields. */
    for (unsigned batch = 0; !error && batch < 8u*MIC_ADC_RATE/AGENT_MIC_RATE; ++batch) {
        uint32_t bytes = 0, samples = 0;
        phase_stamp_t phase_read=phase_enter();
        error = adc_continuous_read(mic, adc_raw, sizeof(adc_raw), &bytes, 0);
        phase_leave(PH_READ,phase_read);
        if (error == ESP_ERR_TIMEOUT) { error = ESP_OK; break; }
        phase_stamp_t phase_parse=phase_enter();
        if (!error) error = adc_continuous_parse_data(mic, adc_raw, bytes, adc_data, &samples);
        phase_leave(PH_PARSE,phase_parse);
        if (error) break;
        if(samples) {
            duration(&mic_max_read_gap,mic_received); mic_received=now_ms();
        }
        /* The audio task owns wake_loaded and capture setup. Publish counters
         * per ADC block (4 ms at32 kHz), keeping every delivered PCM sample.
         * Completion may advance during a Flash flush, so recheck there too. */
        const bool feed_wake=atomic_load(&wake_loaded);
        bool collect=capture_collecting && !atomic_load(&capture_error) &&
            (!wake_capture || (wake_recording_active() && !esp_hi_confirmation_capture_done()));
        unsigned produced=0;
        for (unsigned i = 0; i < samples; ++i) {
            if (!adc_data[i].valid || adc_data[i].unit != ADC_UNIT_1 || adc_data[i].channel != MIC_CHANNEL) continue;
            uint16_t raw=(uint16_t)adc_data[i].raw_data;
#ifdef AGENT_MIC_OVERSAMPLE
            bool clipped;
            bool delivered=agent_decimate_feed(&decimator,raw,&raw,&clipped);
            if(!delivered) continue;
#endif
            unsigned before_clipped=meter.clipped;
            agent_mic_meter_feed(&meter,raw);
#ifdef AGENT_MIC_OVERSAMPLE
            /* Count delivered intervals with ADC rails even if filtering masks
             * the peak. A filter overshoot and raw rail count only once. */
            if(clipped && meter.clipped==before_clipped) ++meter.clipped;
#endif
            unsigned limited=meter.clipped-before_clipped;
            if(limited && cue_collecting) atomic_fetch_add(&start_cue_adc_clipped,limited);
            int32_t value=((int32_t)raw-meter.bias_q8/256)*16;
            if(value>32767) value=32767;
            if(value< -32768) value=-32768;
            if(collect && captured<clip.samples) {
                if(limited) atomic_fetch_add(&record_adc_clipped,limited);
                if(!captured) atomic_store(&record_input_valid,true);
                audio_work.input.capture[capture_used++]=(int16_t)value; ++captured;
                if(wake_capture) {
                    agent_err_t e=esp_hi_confirmation_source((int16_t)value);
                    if(e) { atomic_store(&capture_error,e);collect=false;break; }
                }
                unsigned batch=ESP_HI_CAPTURE_CHUNK;
#ifdef AGENT_BACKGROUND_VERIFY
                if(wake_capture && !clip.written) batch=AGENT_CLIP_FIRST_BATCH;
#endif
                if(capture_used==batch || captured==clip.samples || (wake_capture && esp_hi_confirmation_capture_done())) {
                    phase_stamp_t phase_flush=phase_enter();
                    agent_err_t flushed=capture_flush();
                    phase_leave(PH_FLUSH,phase_flush);
                    atomic_store(&capture_error,flushed);
                    collect=!flushed && (!wake_capture || (wake_recording_active() && !esp_hi_confirmation_capture_done()));
                }
            }
            if(feed_wake) wake_feed((int16_t)value);
            ++produced;
            if (meter.count >= AGENT_MIC_RATE / 10) {
                phase_stamp_t phase_rms=phase_enter();
                atomic_store(&mic_rms, agent_mic_meter_rms(&meter));
                phase_leave(PH_RMS,phase_rms);
                atomic_store(&mic_peak, meter.peak); atomic_store(&mic_bias, (unsigned)meter.bias_q8 / 256);
                atomic_fetch_add(&mic_clipped, meter.clipped);
                atomic_store(&mic_updated,(unsigned)now_ms()); atomic_store(&mic_valid,true);
                meter.squares = 0; meter.count = meter.peak = meter.clipped = 0;
            }
        }
        if(produced) atomic_fetch_add(&mic_samples,produced);
        if(capture_collecting) atomic_store(&capture_samples,captured);
        if(wake_capture && (esp_hi_confirmation_capture_done() || atomic_load(&capture_error)))break;
    }
    if(!error && now_ms()-mic_received>500) error=ESP_ERR_TIMEOUT;
    atomic_store(&mic_error, audio_error(error));
    duration(&mic_max_work,polled);
    if (error) { mic_close(); atomic_store(&mic_requested, false); }
}

static agent_err_t clip_read(void *ctx,size_t offset,void *data,size_t size)
{
    if(atomic_load(&recording) && atomic_load(&stop_requested)) return AGENT_ERR_CANCELLED;
#ifdef AGENT_BACKGROUND_VERIFY
    int64_t start=esp_timer_get_time();
#endif
    agent_err_t error=audio_error(esp_partition_read(ctx,offset,data,size));
#ifdef AGENT_BACKGROUND_VERIFY
    clip_timed(0,start);
#endif
    return error;
}
static agent_err_t clip_write(void *ctx,size_t offset,const void *data,size_t size)
{
    if(atomic_load(&recording) && atomic_load(&stop_requested)) return AGENT_ERR_CANCELLED;
#ifdef AGENT_BACKGROUND_VERIFY
    int64_t start=esp_timer_get_time();
#endif
    phase_stamp_t phase_write=phase_enter();
    agent_err_t error=audio_error(esp_partition_write(ctx,offset,data,size));
    phase_leave(PH_WRITE,phase_write);
#ifdef AGENT_BACKGROUND_VERIFY
    clip_timed(1,start);
#endif
    return error;
}
static agent_err_t clip_erase(void *ctx,size_t offset,size_t size)
{
    if(atomic_load(&recording) && atomic_load(&stop_requested)) return AGENT_ERR_CANCELLED;
#ifdef AGENT_BACKGROUND_VERIFY
    int64_t start=esp_timer_get_time();
#endif
    phase_stamp_t phase_erase=phase_enter();
    agent_err_t error=audio_error(esp_partition_erase_range(ctx,offset,size));
    phase_leave(PH_ERASE,phase_erase);
#ifdef AGENT_BACKGROUND_VERIFY
    clip_timed(2,start);
#endif
    return error;
}

static agent_err_t wake_cue(bool finish)
{
    cue_collecting=!finish;
    bool fresh=speaker==NULL;
    agent_err_t error=fresh?audio_error(speaker_open()):AGENT_OK;
    memset(pcm,0,sizeof(pcm));
    for(unsigned i=0;fresh && i<18 && !error && !atomic_load(&stop_requested);++i) {
        mic_update(); error=audio_error(speaker_write(PCM_CHUNK));
    }
    uint32_t phase=0; const unsigned length=finish?4320:3840;
    for(unsigned at=0;at<length && !error && !atomic_load(&stop_requested);at+=PCM_CHUNK) {
        for(unsigned i=0;i<PCM_CHUNK;++i) {
            unsigned n=at+i,remain=length-n;
            /* Keep the falling cue above the small speaker's weak low band. */
            unsigned hz=finish?2300-1400*n/length:1320;
            phase+=(uint32_t)(((uint64_t)hz<<32)/AGENT_AUDIO_RATE);
            int32_t value=agent_audio_sine(phase);
            unsigned envelope=n<240?n:240;
            value=value*(int32_t)envelope/240;
            value=value*(int32_t)remain/(int32_t)length;
            unsigned level=atomic_load(&volume); if(level>80) level=80;
            /* Short cues need less level than voice/music. Their acoustic tail
             * otherwise leaks into the first VAD frames after the ding. */
            pcm[i]=(int16_t)(value*(int32_t)level/(finish?320:960));
        }
        mic_update(); error=audio_error(speaker_write(PCM_CHUNK));
    }
    memset(pcm,0,sizeof(pcm));
    for(unsigned i=0;i<4 && !error && !atomic_load(&stop_requested);++i) {
        mic_update(); error=audio_error(speaker_write(PCM_CHUNK));
    }
    /* Keep digital zero between cues so switching the amplifier off cannot
     * inject a transient into the first spoken syllable. Only finish closes it. */
    if(speaker && finish) {
        esp_err_t closed=speaker_close(); if(!error) error=audio_error(closed);
    }
    cue_collecting=false;
    return atomic_load(&stop_requested)?AGENT_ERR_CANCELLED:error;
}

static void wake_cycle(void)
{
    wake_hit=false;
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return;
    if(atomic_load(&playing) || atomic_load(&recording) || atomic_load(&wake_network_busy) || !atomic_load(&wake_requested)) {
        xSemaphoreGive(control_lock); return;
    }
    atomic_store(&recording,true); xSemaphoreGive(control_lock);
    capture_input_reset();
    esp_hi_speech_disarm();
    atomic_store(&wake_detected_at,(unsigned)now_ms()); atomic_fetch_add(&wake_count,1);
    atomic_store(&wake_record_at,0); atomic_store(&wake_end_at,0); atomic_store(&wake_cue_end_at,0);
    atomic_store(&wake_last_samples,0); atomic_store(&wake_speech_ms,0);
    atomic_store(&wake_reason,AGENT_EP_WAIT); atomic_store(&wake_error,AGENT_OK);
    atomic_store(&capture_samples,0);
    atomic_store(&wake_stage,WAKE_DING); atomic_store(&recording,true);
    atomic_store(&capture_error,AGENT_OK);
    agent_err_t error=agent_endpoint_init(&endpoint,1000,4000,AGENT_CLIP_MAX_MS);
#ifdef AGENT_BACKGROUND_VERIFY
    /* Reserve resources before the audible cue. ADC resumes with its warmed
     * bias estimate, then starts a fresh post-cue sample clock as before. */
    atomic_store(&capture_us,0);
    atomic_store(&preparation_bytes,0);atomic_store(&preparation_us,0);
    for(unsigned i=0;i<3;++i) {
        atomic_store(&clip_io[i].calls,0); atomic_store(&clip_io[i].total_us,0); atomic_store(&clip_io[i].max_us,0);
    }
    if(!error) error=audio_error(mic_pause());
    if(!error) error=esp_hi_confirmation_open(&clip,activity.noise,audio_work.input.wake,audio_work.input.vad);
    /* The ADC is paused and the worker has not started. Prepare only four
     * sectors before the ding; the post-cue raw clock remains unchanged.
     * Use the original erase cursor, CRC and header-last commit protocol. */
#if AGENT_PACKED_CLIP
    if(!error) error=agent_clip_begin_packed(&clip,AGENT_CLIP_MAX_MS);
#else
    if(!error) error=agent_clip_begin(&clip,AGENT_CLIP_MAX_MS);
#endif
    if(!error) {
        atomic_store(&clip_ready,false);
        int64_t began=esp_timer_get_time();
        for(unsigned i=0;i<4 && !error && !atomic_load(&stop_requested);++i) {
            bool done;error=agent_clip_prepare(&clip,&done);
            atomic_store(&preparation_bytes,(unsigned)clip.erased);
            vTaskDelay(1);
        }
        atomic_store(&preparation_us,(unsigned)(esp_timer_get_time()-began));
        if(!error && atomic_load(&stop_requested))error=AGENT_ERR_CANCELLED;
    }
    if(!error) error=audio_error(mic_resume());
#endif
#ifdef AGENT_VOICE_VERIFY
    atomic_store(&verify_peak,0); atomic_store(&verify_sum,0); atomic_store(&verify_confirmed,false);
    atomic_store(&verify_supported,false);
    /* Prepare before the audible cue, never between cue and capture. Drain ADC
     * after this bounded initialization; raw post-cue input starts unchanged. */
#ifdef AGENT_MIC_OVERSAMPLE
    /* Frontend allocation can exceed the faster ADC pool's remaining margin.
     * Pause before that work, while capture has not begun and no cue sounded.
     * Do not reset overrun counters or hide loss from the keyword phase. */
    if(!error) error=audio_error(mic_pause());
#endif
    if(!error && !micro_verify_open(&verifier)) error=AGENT_ERR_MEMORY;
#ifdef AGENT_MIC_OVERSAMPLE
    if(!error) error=audio_error(mic_resume());
#endif
    if(!error) { mic_update(); error=atomic_load(&mic_error); }
#endif
    if(!error) error=wake_cue(false);
#ifndef AGENT_BACKGROUND_VERIFY
    if(!error) {
#if AGENT_PACKED_CLIP
        error=agent_clip_begin_packed(&clip,AGENT_CLIP_MAX_MS);
#else
        error=agent_clip_begin(&clip,AGENT_CLIP_MAX_MS);
#endif
    }
#endif
    /* Drop ADC samples produced before the recording boundary (the cue), not
     * newly spoken samples. Stop/flush/restart retains the warmed DC estimate. */
    if(!error) error=audio_error(mic_pause());
    if(!error) error=audio_error(mic_resume());
    mic_received=mic_polled=now_ms(); agent_voice_init(&vad_filter);
    memset(&cue_filter,0,sizeof(cue_filter));
    if(!error) {
        capture_used=captured=vad_used=0; wake_capture=capture_collecting=true;
        atomic_store(&clip_ready,false); atomic_store(&capture_samples,0);
        atomic_store(&wake_stage,WAKE_RECORDING); atomic_store(&capture_stage,2);
        atomic_store(&wake_record_at,(unsigned)now_ms());
#ifdef AGENT_BACKGROUND_VERIFY
        int64_t capture_start=esp_timer_get_time();
        phase_start();
        phase_stamp_t calibration=phase_enter();
        for(unsigned i=0;i<128;++i) {phase_stamp_t empty=phase_enter();phase_leave(PH_EMPTY,empty);}
        phase_leave(PH_CALIBRATION,calibration);
        esp_hi_confirmation_start();
#endif
        uint64_t deadline=now_ms()+AGENT_CLIP_MAX_MS+1500;
        while(!error && wake_recording_active() && !atomic_load(&stop_requested)) {
            phase_stamp_t phase_mic=phase_enter();
            mic_update(); error=atomic_load(&mic_error);
            phase_leave(PH_MIC,phase_mic);
            if(!error) error=atomic_load(&capture_error);
            if(!error && esp_hi_confirmation_capture_done() && !wake_mute_mic) {
                /* Rounded raw target and final packed page already published.
                 * Bound ends acquisition only; actual model decides the result. */
                capture_collecting=false;wake_mute_mic=true;
                error=audio_error(mic_close());esp_hi_confirmation_capture_stopped();
            }
            if(!error && atomic_load(&mic_overruns)) error=AGENT_ERR_LIMIT;
            if(!error && now_ms()>deadline) error=AGENT_ERR_TIMEOUT;
#ifdef AGENT_BACKGROUND_VERIFY
            esp_hi_confirmation_stats_t current; esp_hi_confirmation_stats(&current);
            atomic_store(&wake_level,current.level); atomic_store(&wake_noise,current.noise);
            atomic_store(&wake_speech_ms,current.speech_ms);
#endif
            vTaskDelay(1);
        }
        wake_capture=capture_collecting=false;
        wake_mute_mic=true;
        agent_err_t closed=audio_error(mic_close()); if(!error) error=closed;
#ifdef AGENT_BACKGROUND_VERIFY
        esp_hi_confirmation_capture_stopped();
        atomic_store(&capture_us,(unsigned)(esp_timer_get_time()-capture_start));
        if(error || atomic_load(&stop_requested)) esp_hi_confirmation_cancel();
        agent_err_t joined=esp_hi_confirmation_join(&endpoint); if(!error) error=joined;
#endif
        atomic_store(&wake_end_at,(unsigned)now_ms());
        if(atomic_load(&stop_requested)) agent_endpoint_cancel(&endpoint);
        atomic_store(&wake_reason,endpoint.state); atomic_store(&wake_last_samples,captured);
        atomic_store(&wake_speech_ms,endpoint.speech_ms);
        if(atomic_load(&stop_requested)) error=AGENT_ERR_CANCELLED;
        if(!error && endpoint.state==AGENT_EP_NO_SPEECH) { atomic_fetch_add(&wake_empty,1); error=AGENT_ERR_TIMEOUT; }
        if(!error && endpoint.state==AGENT_EP_LIMIT) atomic_fetch_add(&wake_limited,1);
        if(!error && capture_used) {
            phase_stamp_t phase_flush=phase_enter();error=capture_flush();phase_leave(PH_FLUSH,phase_flush);
        }
        atomic_store(&wake_stage,WAKE_FINISHING); atomic_store(&capture_stage,3);
        if(!error) {
#ifdef AGENT_BACKGROUND_VERIFY
            phase_stamp_t phase_commit=phase_enter();
            error=agent_clip_finish_at(&clip,(size_t)endpoint.elapsed_ms*AGENT_MIC_RATE/1000);
            phase_leave(PH_COMMIT,phase_commit);
#else
            error=agent_clip_finish(&clip);
#endif
        }
        if(error) agent_clip_abort(&clip);
        atomic_store(&clip_ready,clip.ready);
        atomic_store(&clip_ms,clip.ready?(unsigned)(clip.samples*1000/AGENT_MIC_RATE):0);
        phase_finish();
        /* Completion cue is emitted only after the clip CRC and commit succeed. */
        if(!error) { error=wake_cue(true); if(!error) atomic_fetch_add(&wake_done,1); }
    }
#ifdef AGENT_BACKGROUND_VERIFY
    /* Also covers cancellation/open/cue failures before recording starts. */
    esp_hi_confirmation_cancel();
    agent_err_t joined=esp_hi_confirmation_join(NULL); if(!error) error=joined;
#endif
    atomic_store(&wake_cue_end_at,(unsigned)now_ms());
    if(error==AGENT_ERR_CANCELLED) atomic_store(&wake_reason,AGENT_EP_CANCELLED);
    if(error && clip.writing) agent_clip_abort(&clip);
    atomic_store(&clip_ready,clip.ready);
    atomic_store(&clip_ms,clip.ready?(unsigned)(clip.samples*1000/AGENT_MIC_RATE):0);
    if(speaker) { esp_err_t closed=speaker_close(); if(!error) error=audio_error(closed); }
    atomic_store(&capture_error,error); atomic_store(&wake_error,error);
#ifdef AGENT_VOICE_VERIFY
    micro_verify_close(&verifier);
#endif
    atomic_store(&recording,false); atomic_store(&capture_stage,0);
    /* The pinned C3 library's clean() dereferences a null convolution queue.
     * Destroy/recreate is verified separately and clears VAD state as well. */
    esp_hi_speech_close(); atomic_store(&wake_loaded,false);
    wake_used=0; wake_rearm_at=now_ms()+1000;
    atomic_store(&wake_stage,WAKE_COOLDOWN);
    if(error && error!=AGENT_ERR_TIMEOUT && error!=AGENT_ERR_CANCELLED) atomic_store(&wake_requested,false);
}

static void wake_update(void)
{
    bool requested=atomic_load(&wake_requested);
    bool paused=atomic_load(&wake_network_busy) || atomic_load(&playing) || atomic_load(&recording);
    if(!requested || paused) {
        if(atomic_load(&wake_loaded)) {
            esp_hi_speech_close(); atomic_store(&wake_loaded,false); wake_used=0; wake_hit=false;
        }
        wake_mute_mic=false;
        atomic_store(&wake_stage,requested?WAKE_PAUSED:WAKE_OFF); return;
    }
    if(!atomic_load(&wake_loaded)) {
        /* Publish model admission under the same lock as network admission.
         * Without this recheck TLS can start between paused=false and create. */
        if(xSemaphoreTake(control_lock,0)!=pdTRUE) return;
        if(!atomic_load(&wake_requested) || atomic_load(&wake_network_busy) || atomic_load(&playing) || atomic_load(&recording)) {
            xSemaphoreGive(control_lock); return;
        }
        atomic_store(&wake_stage,WAKE_LOADING);
        /* A requested meter can outlive playback. Release its ADC pool before
         * model creation: initialization otherwise overlaps that allocation
         * and can overflow a running pool. No capture is active here; the
         * next mic_update reopens it after admission, with mic_valid cleared. */
        agent_err_t error=audio_error(mic_close());
        if(!error && !esp_hi_speech_open()) error=AGENT_ERR_MEMORY;
        if(error) {
            atomic_store(&wake_error,error); atomic_store(&wake_stage,WAKE_FAILED);
            atomic_store(&wake_requested,false); xSemaphoreGive(control_lock); return;
        }
        atomic_store(&wake_heap,esp_hi_speech_heap()); atomic_store(&wake_loaded,true);
        applied_threshold=0;
        wake_mute_mic=false;
        wake_used=vad_used=0; wake_hit=false; agent_voice_init(&vad_filter);
        memset(&activity,0,sizeof(activity));
        if(wake_rearm_at<now_ms()+500) wake_rearm_at=now_ms()+500;
        xSemaphoreGive(control_lock);
    }
    unsigned threshold=atomic_load(&wake_threshold);
    if(threshold!=applied_threshold) {
        if(!esp_hi_speech_threshold(threshold)) {
            atomic_store(&wake_error,AGENT_ERR_CONFIG); atomic_store(&wake_requested,false); return;
        }
        applied_threshold=threshold;
    }
    atomic_store(&wake_stage,now_ms()<wake_rearm_at?WAKE_COOLDOWN:WAKE_LISTENING);
}

#ifdef AGENT_CAPTURE_WARM_PDM
/* Corpus-only profile: reproduce two acquisition conditions of wake recording.
 * It does not recognize a keyword, run VAD, or prove the full wake workflow. */
static agent_err_t capture_warm_pdm(void)
{
    bool requested=atomic_exchange(&mic_requested,true);
    agent_err_t error=AGENT_OK;
    uint64_t until=now_ms()+1000;
    while(!error && now_ms()<until && !atomic_load(&stop_requested)) {
        mic_update(); error=atomic_load(&mic_error); vTaskDelay(1);
    }
    if(!error && !atomic_load(&stop_requested)) error=wake_cue(false);
    if(!error && !atomic_load(&stop_requested)) error=audio_error(mic_pause());
    if(!error && !atomic_load(&stop_requested)) error=audio_error(mic_resume());
    atomic_store(&mic_requested,requested);
    return atomic_load(&stop_requested)?AGENT_ERR_CANCELLED:error;
}
#endif

static agent_err_t record_clip(unsigned ms)
{
    capture_input_reset();
    agent_err_t error=audio_error(mic_close());
    if(!error) error=agent_clip_begin(&clip,ms);
    atomic_store(&clip_ready,false); atomic_store(&capture_stage,1);
    uint64_t deadline=now_ms()+10000; bool prepared=false;
    while(!error && !prepared && !atomic_load(&stop_requested)) {
        error=agent_clip_prepare(&clip,&prepared);
        if(now_ms()>deadline) error=AGENT_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    if(!error && !atomic_load(&stop_requested)) error=audio_error(mic_open());
#ifdef AGENT_CAPTURE_WARM_PDM
    if(!error && !atomic_load(&stop_requested)) error=capture_warm_pdm();
#endif
    capture_collecting=!error && !atomic_load(&stop_requested);
    capture_used=captured=0; atomic_store(&capture_stage,2); deadline=now_ms()+ms+2000;
    while(!error && capture_collecting && captured<clip.samples && !atomic_load(&stop_requested)) {
        mic_update(); error=atomic_load(&mic_error);
        if(!error) error=atomic_load(&capture_error);
        if(!error && atomic_load(&mic_overruns)) error=AGENT_ERR_LIMIT;
        if(!error && now_ms()>deadline) error=AGENT_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    capture_collecting=false;
    agent_err_t closed=audio_error(mic_close()); if(!error) error=closed;
#ifdef AGENT_CAPTURE_WARM_PDM
    if(speaker) { closed=audio_error(speaker_close()); if(!error) error=closed; }
#endif
    if(atomic_load(&stop_requested)) error=AGENT_ERR_CANCELLED;
    if(!error && capture_used) error=agent_clip_write(&clip,audio_work.input.capture,capture_used);
    atomic_store(&capture_stage,3);
    if(!error) error=agent_clip_commit(&clip);
    if(error) agent_clip_abort(&clip);
    atomic_store(&clip_ready,clip.ready); atomic_store(&clip_ms,clip.ready?ms:0);
    atomic_store(&capture_stage,0);
    return error;
}

typedef struct { agent_flash_ops_t flash; uint64_t yielded; } replay_check_t;
static agent_err_t replay_read(void *ctx,size_t offset,void *out,size_t bytes)
{
    replay_check_t *check=ctx;
    if(atomic_load(&stop_requested)) return AGENT_ERR_CANCELLED;
    agent_err_t error=check->flash.read(check->flash.ctx,offset,out,bytes);
    if(error) return error;
    /* Whole-clip CRC exceeds the ADC pool interval. Service the meter between
     * bounded reads, while input collection and keyword inference are paused. */
    bool metering=atomic_load(&mic_requested);
    mic_update();
    if(metering) {
        error=atomic_load(&mic_error); if(error) return error;
        if(atomic_load(&mic_overruns)) return AGENT_ERR_LIMIT;
    }
    if(now_ms()-check->yielded>=4) { vTaskDelay(1); check->yielded=now_ms(); }
    return atomic_load(&stop_requested)?AGENT_ERR_CANCELLED:AGENT_OK;
}
static agent_err_t replay_clip(void)
{
    if(capture_collecting || atomic_load(&wake_loaded)) return AGENT_ERR_BUSY;
    replay_check_t check={.flash=clip.flash,.yielded=now_ms()};
    agent_flash_ops_t reads=check.flash; reads.read=replay_read; reads.ctx=&check;
    agent_err_t error=agent_clip_open(&audio_work.check,&reads);
    if(error) {
        if(error!=AGENT_ERR_CANCELLED) { clip.ready=false; atomic_store(&clip_ready,false); }
        return error;
    }
    /* No live input/confirmation or USB reader owns either object. Restore the
     * permanent Flash context before reusing the union for replay rendering. */
    clip=audio_work.check; clip.flash=check.flash;
    if(atomic_load(&stop_requested)) return AGENT_ERR_CANCELLED;
    error=agent_replay_init(&audio_work.replay,&clip);
    bool prepared=false; unsigned frames=0; uint64_t deadline=now_ms()+2000;
    while(!error && !prepared && !atomic_load(&stop_requested)) {
        error=agent_replay_prepare(&audio_work.replay,&prepared); mic_update();
        if(now_ms()>deadline) error=AGENT_ERR_TIMEOUT;
        if(++frames%16==0) vTaskDelay(1);
    }
    if(!error && !atomic_load(&stop_requested)) error=audio_error(speaker_open());
    atomic_store(&total,(unsigned)(clip.samples*3/2));
    while(!error && !atomic_load(&stop_requested)) {
        size_t count=0;
        error=agent_replay_render(&audio_work.replay,pcm,PCM_CHUNK,atomic_load(&volume),&count);
        if(error || !count) break;
        error=audio_error(speaker_write(count)); atomic_store(&rendered,(unsigned)audio_work.replay.rendered);
        mic_update();
    }
    return error;
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_command_t command;
    for (;;) {
        wake_update();
        mic_update();
        if(wake_hit && atomic_load(&wake_requested) && !atomic_load(&playing)) wake_cycle();
        if (xQueueReceive(score_queue, &command, pdMS_TO_TICKS(10)) != pdTRUE) continue;
        /* Admission can occur while the queue receive is blocked. Recheck at
         * the hardware boundary so replay/capture cannot inherit a live model. */
        wake_update(); mic_update();
        if(command.kind==AUDIO_CAPTURE) {
            atomic_store(&capture_error,record_clip(command.ms));
            /* Do not publish completion while the ADC still owns its channel. */
            while(mic && !atomic_load(&mic_requested)) {
                atomic_store(&mic_error,audio_error(mic_close())); vTaskDelay(pdMS_TO_TICKS(10));
            }
            atomic_store(&recording,false); continue;
        }
        agent_synth_t synth;
        agent_song_synth_t band;
        agent_err_t error=AGENT_OK;
        if(command.kind==AUDIO_REPLAY) error=replay_clip();
        else {
            error=command.kind==AUDIO_SONG?agent_song_synth_init(&band,&songs[command.ms]):agent_synth_init(&synth,&command.score);
            if(!error && !atomic_load(&stop_requested)) error=audio_error(speaker_open());
            /* Measured startup attenuation loses the first ~150ms. Settle at digital zero before song notes. */
            if(command.kind==AUDIO_SONG && !error) {
                memset(pcm,0,sizeof(pcm));
                for(unsigned i=0;i<30 && !error && !atomic_load(&stop_requested);++i) {
                    mic_update(); error=audio_error(speaker_write(PCM_CHUNK));
                }
            }
        }
        while ((command.kind==AUDIO_SCORE || command.kind==AUDIO_SONG) && !error && !atomic_load(&stop_requested)) {
            mic_update();
            int64_t started=esp_timer_get_time();
            size_t samples=command.kind==AUDIO_SONG?agent_song_render(&band,pcm,PCM_CHUNK,atomic_load(&volume)):
                agent_synth_render(&synth, pcm, PCM_CHUNK, atomic_load(&volume));
            if(command.kind==AUDIO_SONG) {
                unsigned us=(unsigned)(esp_timer_get_time()-started);
                if(us>atomic_load(&music_render_us)) atomic_store(&music_render_us,us);
                atomic_store(&music_limited,band.soft_limited);
            }
            if (!samples) break;
            error = audio_error(speaker_write(samples));
            atomic_store(&rendered,command.kind==AUDIO_SONG?band.rendered:synth.rendered);
        }
        /* Drain queued sound with silence before muting the amplifier. */
        memset(pcm, 0, sizeof(pcm));
        for (unsigned i = 0; !error && !atomic_load(&stop_requested) && i < 4; ++i) {
            error = audio_error(speaker_write(PCM_CHUNK));
            mic_update();
        }
        if (atomic_load(&stop_requested)) error = AGENT_ERR_CANCELLED;
        esp_err_t closed=speaker_close();
        while(closed) {
            atomic_store(&play_error,audio_error(closed));
            vTaskDelay(pdMS_TO_TICKS(10)); closed=speaker_close();
        }
        atomic_store(&play_error, error);
        atomic_store(&playing, false);
    }
}

static agent_err_t play(void *ctx, const agent_score_t *score)
{
    (void)ctx;
    agent_err_t error = agent_score_validate(score);
    if (error) return error;
    if (!audio_task_handle) return AGENT_ERR_CONFIG;
    if (xSemaphoreTake(control_lock, pdMS_TO_TICKS(50)) != pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    if (atomic_load(&playing) || atomic_load(&recording)) error = busy_result(BUSY_AUDIO_ACTIVE,AGENT_ERR_BUSY);
    else {
        atomic_store(&stop_requested, false); atomic_store(&playing, true);
        atomic_store(&rendered, 0); atomic_store(&total, score->samples);
        atomic_store(&play_error, AGENT_OK);
        audio_command_t command={.kind=AUDIO_SCORE,.score=*score};
        if (xQueueSend(score_queue, &command, 0) != pdTRUE) { atomic_store(&playing, false); error = busy_result(BUSY_AUDIO_QUEUE,AGENT_ERR_BUSY); }
        else atomic_fetch_add(&job_id, 1);
    }
    xSemaphoreGive(control_lock);
    return error;
}

static agent_err_t stop(void *ctx)
{
    (void)ctx;
    if (!audio_task_handle) return AGENT_ERR_CONFIG;
    if (xSemaphoreTake(control_lock, pdMS_TO_TICKS(50)) != pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    atomic_store(&stop_requested, true);
    atomic_store(&wake_requested,false);
    /* A following play in the same tool batch must see the old score stopped. */
    for (unsigned i = 0; (atomic_load(&playing) || atomic_load(&recording)) && i < 20; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    agent_err_t error = atomic_load(&playing) || atomic_load(&recording) ? AGENT_ERR_TIMEOUT : AGENT_OK;
    xSemaphoreGive(control_lock);
    return error;
}

static agent_err_t play_song(void *ctx,const agent_song_t *song)
{
    (void)ctx; agent_err_t error=agent_song_validate(song); if(error) return error;
    if(!audio_task_handle) return AGENT_ERR_CONFIG;
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    if(atomic_load(&playing) || atomic_load(&recording)) error=busy_result(BUSY_AUDIO_ACTIVE,AGENT_ERR_BUSY);
    else {
        unsigned next=atomic_load(&last_song_index)^1;
        songs[next]=*song;
        audio_command_t command={.kind=AUDIO_SONG,.ms=next};
        atomic_store(&stop_requested,false); atomic_store(&playing,true);
        atomic_store(&rendered,0); atomic_store(&total,agent_song_samples(song));
        atomic_store(&play_error,AGENT_OK); atomic_store(&music_limited,0);
        if(xQueueSend(score_queue,&command,0)!=pdTRUE) { atomic_store(&playing,false); error=busy_result(BUSY_AUDIO_QUEUE,AGENT_ERR_BUSY); }
        else {
            atomic_store(&last_song_index,next); atomic_store(&song_ready,true);
            atomic_store(&last_song_job,atomic_fetch_add(&job_id,1)+1);
        }
    }
    xSemaphoreGive(control_lock); return error;
}
static agent_err_t read_song(void *ctx,agent_write_fn write,void *out)
{
    (void)ctx; if(!audio_task_handle) return AGENT_ERR_CONFIG;
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    agent_err_t error=atomic_load(&song_ready)?agent_song_write(&songs[atomic_load(&last_song_index)],write,out):AGENT_ERR_NOT_FOUND;
    xSemaphoreGive(control_lock); return error;
}

static agent_err_t set_volume(void *ctx, unsigned percent)
{
    (void)ctx;
    if (percent > 100) return AGENT_ERR_ARGUMENT;
    atomic_store(&volume, percent);
    return AGENT_OK;
}

static agent_err_t microphone(void *ctx, bool enabled)
{
    (void)ctx;
    if (!audio_task_handle) return AGENT_ERR_CONFIG;
    atomic_store(&mic_requested, enabled);
    return AGENT_OK;
}

static agent_err_t listen(void *ctx,bool enabled)
{
    (void)ctx;
    if(!audio_task_handle || !clip_partition) return AGENT_ERR_CONFIG;
    if(!enabled) return stop(NULL);
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    agent_err_t error=AGENT_OK;
    if(atomic_load(&playing) || atomic_load(&recording) || atomic_load(&wake_network_busy)) error=AGENT_ERR_BUSY;
    else { atomic_store(&stop_requested,false); atomic_store(&wake_error,AGENT_OK); atomic_store(&wake_requested,true); }
    xSemaphoreGive(control_lock); return error;
}
agent_err_t esp_hi_wake_network(bool active)
{
    if(!audio_task_handle) return AGENT_OK;
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return busy_result(BUSY_NETWORK_LOCK,AGENT_ERR_BUSY);
    if(active && atomic_load(&recording) && atomic_load(&wake_requested)) {
        xSemaphoreGive(control_lock); return busy_result(BUSY_NETWORK_CAPTURE,AGENT_ERR_BUSY);
    }
    atomic_store(&wake_network_busy,active);
    xSemaphoreGive(control_lock);
    if(active) {
        for(unsigned i=0;i<100 && (atomic_load(&wake_loaded) || atomic_load(&wake_stage)==WAKE_LOADING);++i) vTaskDelay(pdMS_TO_TICKS(10));
        if(atomic_load(&wake_loaded)) { atomic_store(&wake_network_busy,false); return AGENT_ERR_TIMEOUT; }
    }
    return AGENT_OK;
}
agent_err_t esp_hi_wake_threshold(unsigned value)
{
    if(value<500 || value>950) return AGENT_ERR_ARGUMENT;
    atomic_store(&wake_threshold,value); return AGENT_OK;
}
agent_err_t esp_hi_wake_gain(unsigned value)
{
    if(value<1 || value>4) return AGENT_ERR_ARGUMENT;
    if(!control_lock) return AGENT_ERR_CONFIG;
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    agent_err_t error=atomic_load(&wake_requested)?AGENT_ERR_BUSY:AGENT_OK;
    if(!error) atomic_store(&wake_gain,value);
    xSemaphoreGive(control_lock); return error;
}
agent_err_t esp_hi_wake_status(char *out,size_t cap)
{
    static const char *const names[]={"off","loading","listening","ding","recording","finishing","cooldown","paused","failed"};
    unsigned state=atomic_load(&wake_stage);
    int n=snprintf(out,cap,"{\"enabled\":%s,\"state\":\"%s\",\"word\":\"%s\",\"model\":\"%s\","
        "\"threshold\":%u,\"input_gain\":%u,\"input_clipped\":%u,\"model_heap\":%u,\"chunk\":%u,\"inference_max_us\":%u,\"wakes\":%u,\"completed\":%u,\"empty\":%u,\"limited\":%u,\"dma_lost\":%u,"
        "\"detected_at\":%u,\"record_at\":%u,\"end_at\":%u,\"cue_end_at\":%u,\"samples\":%u,\"reason\":%d,\"noise\":%u,\"level\":%u,\"speech_ms\":%u,\"error\":\"%s\"}",
        atomic_load(&wake_requested)?"true":"false",names[state],esp_hi_speech_word(),esp_hi_speech_model(),atomic_load(&wake_threshold),atomic_load(&wake_gain),atomic_load(&wake_clipped),atomic_load(&wake_heap),esp_hi_speech_chunk(),
        atomic_load(&wake_infer_us),atomic_load(&wake_count),atomic_load(&wake_done),atomic_load(&wake_empty),atomic_load(&wake_limited),atomic_load(&wake_dma_lost),
        atomic_load(&wake_detected_at),atomic_load(&wake_record_at),atomic_load(&wake_end_at),atomic_load(&wake_cue_end_at),
        atomic_load(&wake_last_samples),atomic_load(&wake_reason),atomic_load(&wake_noise),atomic_load(&wake_level),atomic_load(&wake_speech_ms),agent_err_name(atomic_load(&wake_error)));
#ifdef AGENT_VOICE_VERIFY
    if(n>0 && (size_t)n<cap) {
        --n;
        int extra=snprintf(out+n,cap-(size_t)n,",\"verify\":{\"peak\":%u,\"sum5\":%u,\"confirmed\":%s,\"max_us\":%u,\"supported\":%s}}",
            atomic_load(&verify_peak),atomic_load(&verify_sum),atomic_load(&verify_confirmed)?"true":"false",atomic_load(&verify_us),
            atomic_load(&verify_supported)?"true":"false");
        if(extra<0) return AGENT_ERR_LIMIT;
        n+=extra;
    }
#endif
#ifdef AGENT_BACKGROUND_VERIFY
    if(n>0 && (size_t)n<cap) {
        esp_hi_confirmation_stats_t s; esp_hi_confirmation_stats(&s); --n;
        int extra=snprintf(out+n,cap-(size_t)n,",\"verify\":{\"backend\":\"%s\",\"frames\":%u,\"peak\":%u,\"sum5\":%u,"
            "\"confirmed\":%s,\"source_ms\":%u,\"confirm_ms\":%u,\"confirm_wall_ms\":%u,\"backlog_samples\":%u,\"max_us\":%u,"
            "\"stack\":%u,\"model_bytes\":%u,\"heap_min\":%u,\"wall_limit_ms\":%u,\"deadline\":%s,"
            "\"heap_bytes\":%u,\"arena_bytes\":%u,\"cpu_timing\":%s,\"cpu_us\":%u,\"producer_cpu_us\":%u,\"nn_cpu_us\":%u,\"nn_wall_us\":%u,\"nn_cpu_max_us\":%u,"
            "\"packed_storage\":%s,\"capture_us\":%u,\"io\":[[%u,%u,%u],[%u,%u,%u],[%u,%u,%u]]}}",
            ESP_HI_VAD_ID,s.frames,s.peak,s.sum5,s.confirmed?"true":"false",s.elapsed_ms,s.confirmed_ms,s.confirmed_wall_ms,
            s.backlog_samples,s.max_us,s.stack_bytes,s.model_bytes,s.heap_min,ESP_HI_CONFIRM_WALL_MS,s.deadline?"true":"false",
            s.heap_bytes,s.arena_bytes,s.cpu_timing?"true":"false",s.cpu_us,s.producer_cpu_us,s.nn_cpu_us,s.nn_wall_us,s.nn_cpu_max_us,
            AGENT_PACKED_CLIP?"true":"false",atomic_load(&capture_us),atomic_load(&clip_io[0].calls),atomic_load(&clip_io[0].total_us),atomic_load(&clip_io[0].max_us),
            atomic_load(&clip_io[1].calls),atomic_load(&clip_io[1].total_us),atomic_load(&clip_io[1].max_us),
            atomic_load(&clip_io[2].calls),atomic_load(&clip_io[2].total_us),atomic_load(&clip_io[2].max_us));
        if(extra<0) return AGENT_ERR_LIMIT;
        n+=extra;
    }
#endif
    if(n>0 && (size_t)n<cap) {
        esp_hi_confirmation_stats_t s;esp_hi_confirmation_stats(&s);--n;
        int extra=snprintf(out+n,cap-(size_t)n,",\"capture_pipeline_v1\":[%u,%u,%u,%u,%u]}",
            s.source_samples,s.source_frames,s.bound_ms,s.target_samples,s.source_stop_wall_ms);
        if(extra<0)return AGENT_ERR_LIMIT;
        n+=extra;
    }
    if(n>0 && (size_t)n<cap) {
        esp_hi_confirmation_stats_t s;esp_hi_confirmation_stats(&s);--n;
        int extra=snprintf(out+n,cap-(size_t)n,",\"confirmation_tail_v1\":[%u,%u,%u]}",s.tail_possible_ms,s.tail_started_ms,s.tail_steps);
        if(extra<0)return AGENT_ERR_LIMIT;
        n+=extra;
    }
    if(n>0 && (size_t)n<cap) {
        --n;
        int extra=snprintf(out+n,cap-(size_t)n,",\"capture_preparation_v1\":[%u,%u]}",atomic_load(&preparation_bytes),atomic_load(&preparation_us));
        if(extra<0)return AGENT_ERR_LIMIT;
        n+=extra;
    }
    if(n>0 && (size_t)n<cap) {
        esp_hi_confirmation_stats_t tonal;esp_hi_confirmation_stats(&tonal);--n;
        int extra=snprintf(out+n,cap-(size_t)n,",\"tonal_metadata_v1\":[3000,%u,%u,%u]}",
            tonal.tonal_frames,tonal.tonal_vetoes,tonal.tonal_clean);
        if(extra<0)return AGENT_ERR_LIMIT;
        n+=extra;
    }
/* Keyword verification reports lifetime raw/veto counters separately from the
 * public accepted-wake count. Reading telemetry never mutates the audio state. */
#ifdef AGENT_KEYWORD_VERIFY
    if(n>0 && (size_t)n<cap) {
        esp_hi_keyword_stats_t k; esp_hi_speech_keyword_stats(&k);
        --n;
        int extra=snprintf(out+n,cap-(size_t)n,",\"keyword\":{\"bank\":\"%s\",\"raw\":%u,\"rejected\":%u,\"invalid\":%u,\"incomplete\":%u,\"positive\":%u,\"negative\":%u,\"max_us\":%u}}",
            esp_hi_speech_keyword_bank(),k.raw,k.rejected,k.invalid,k.incomplete,k.positive,k.negative,k.max_us);
        if(extra<0) return AGENT_ERR_LIMIT;
        n+=extra;
    }
#endif
    return n<0 || (size_t)n>=cap?AGENT_ERR_LIMIT:AGENT_OK;
}
agent_err_t esp_hi_clip_export(unsigned offset,unsigned count,char *out,size_t cap)
{
    if(!count || count>256 || cap<64+count*4) return AGENT_ERR_ARGUMENT;
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    int16_t block[256];
    agent_err_t error=atomic_load(&recording) || atomic_load(&playing) || atomic_load(&wake_requested)?AGENT_ERR_BUSY:agent_clip_read(&clip,offset,block,count);
    if(!error) {
        int n=snprintf(out,cap,"{\"offset\":%u,\"pcm\":\"",offset);
        static const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<count;++i) for(unsigned b=0;b<2;++b) {
            uint8_t value=(uint16_t)block[i]>>(8*b); out[n++]=hex[value>>4]; out[n++]=hex[value&15];
        }
        memcpy(out+n,"\"}",3);
    }
    xSemaphoreGive(control_lock); return error;
}

static agent_err_t clip_command(audio_kind_t kind,unsigned ms)
{
    if(!audio_task_handle || !clip_partition) return AGENT_ERR_CONFIG;
    if(kind==AUDIO_CAPTURE && (ms<100 || ms>AGENT_CLIP_MAX_MS)) return AGENT_ERR_ARGUMENT;
    if(xSemaphoreTake(control_lock,pdMS_TO_TICKS(50))!=pdTRUE) return busy_result(BUSY_AUDIO_LOCK,AGENT_ERR_BUSY);
    agent_err_t error=AGENT_OK;
    if(atomic_load(&playing) || atomic_load(&recording)) error=busy_result(BUSY_AUDIO_ACTIVE,AGENT_ERR_BUSY);
    else if(kind==AUDIO_REPLAY && !atomic_load(&clip_ready)) error=AGENT_ERR_NOT_FOUND;
    else {
        audio_command_t command={.kind=kind,.ms=ms};
        atomic_store(&stop_requested,false); atomic_store(&rendered,0);
        if(kind==AUDIO_CAPTURE) {
            atomic_store(&recording,true); atomic_store(&capture_error,AGENT_OK);
            atomic_store(&capture_samples,0); atomic_store(&capture_stage,1);
        } else { atomic_store(&playing,true); atomic_store(&play_error,AGENT_OK); }
        if(xQueueSend(score_queue,&command,0)!=pdTRUE) {
            atomic_store(&playing,false); atomic_store(&recording,false); error=AGENT_ERR_BUSY;
        } else atomic_fetch_add(&job_id,1);
    }
    xSemaphoreGive(control_lock); return error;
}
static agent_err_t capture(void *ctx,unsigned ms) { (void)ctx; return clip_command(AUDIO_CAPTURE,ms); }
static agent_err_t replay(void *ctx) { (void)ctx; return clip_command(AUDIO_REPLAY,0); }
static agent_err_t inspect(void *ctx,agent_audio_state_t *out)
{
    (void)ctx; if(!out) return AGENT_ERR_ARGUMENT;
    uint64_t now=now_ms(); uint32_t age=(uint32_t)now-atomic_load(&mic_updated);
    *out=(agent_audio_state_t){.ready=audio_task_handle!=NULL,.playing=atomic_load(&playing),
        .mic_requested=atomic_load(&mic_requested),.mic_enabled=atomic_load(&mic_enabled),
        .mic_valid=atomic_load(&mic_valid) && atomic_load(&mic_enabled) && age<500,
        .mic_rms=atomic_load(&mic_rms),.mic_updated_ms=now-age,.volume=atomic_load(&volume),
        .recording=atomic_load(&recording),.clip_ready=atomic_load(&clip_ready),.clip_storage=clip_partition!=NULL,
        .listening=atomic_load(&wake_requested) || atomic_load(&wake_loaded),
        .clip_ms=atomic_load(&clip_ms),.play_error=atomic_load(&play_error),
        .capture_error=atomic_load(&capture_error),.mic_error=atomic_load(&mic_error)};
    return AGENT_OK;
}

static agent_err_t status(void *ctx, char *output, size_t capacity)
{
    (void)ctx;
    agent_audio_state_t state; inspect(NULL,&state);
    int n = snprintf(output, capacity,
        "{\"audio_ready\":%s,\"playing\":%s,\"volume\":%u,\"job\":%u,\"rendered\":%u,\"total\":%u,\"sample_rate\":%u,"
        "\"mic_requested\":%s,\"mic_enabled\":%s,\"mic_rate\":%u,\"mic_adc_rate\":%u,\"mic_samples\":%u,\"mic_rms\":%u,\"mic_peak\":%u,"
        "\"mic_bias\":%u,\"mic_clipped\":%u,\"mic_overruns\":%u,\"audio_stack\":%u,\"play_error\":\"%s\",\"mic_error\":\"%s\",\"sdk_error\":%d,"
        "\"mic_valid\":%s,\"mic_age_ms\":%u,\"recording\":%s,\"capture_stage\":%u,\"capture_samples\":%u,\"capture_error\":\"%s\","
        "\"record_input_valid\":%s,\"record_adc_clipped\":%u,\"start_cue_adc_clipped\":%u,"
        "\"clip_storage\":%s,\"clip_ready\":%s,\"clip_ms\":%u,\"voice_filter\":true,\"noise_reduction\":true,\"mic_max_gap_ms\":%u,\"mic_max_read_gap_ms\":%u,\"audio_max_ms\":[%u,%u,%u,%u],"
        "\"song_ready\":%s,\"last_song_job\":%u,\"music_render_us\":%u,\"music_limited\":%u"
#ifdef AGENT_CAPTURE_WARM_PDM
        ",\"capture_profile\":\"warm-pdm\""
#endif
        "}",
        audio_task_handle ? "true" : "false", atomic_load(&playing) ? "true" : "false", atomic_load(&volume),
        atomic_load(&job_id), atomic_load(&rendered), atomic_load(&total), AGENT_AUDIO_RATE,
        atomic_load(&mic_requested) ? "true" : "false", atomic_load(&mic_enabled) ? "true" : "false", AGENT_MIC_RATE,MIC_ADC_RATE,
        atomic_load(&mic_samples), atomic_load(&mic_rms), atomic_load(&mic_peak), atomic_load(&mic_bias),
        atomic_load(&mic_clipped), atomic_load(&mic_overruns),
        audio_task_handle ? (unsigned)uxTaskGetStackHighWaterMark(audio_task_handle) : 0,
        agent_err_name(atomic_load(&play_error)), agent_err_name(atomic_load(&mic_error)), atomic_load(&sdk_error),
        state.mic_valid?"true":"false",(unsigned)(now_ms()-state.mic_updated_ms),state.recording?"true":"false",
        atomic_load(&capture_stage),atomic_load(&capture_samples),agent_err_name(state.capture_error),
        atomic_load(&record_input_valid)?"true":"false",atomic_load(&record_adc_clipped),atomic_load(&start_cue_adc_clipped),
        state.clip_storage?"true":"false",state.clip_ready?"true":"false",state.clip_ms,atomic_load(&mic_max_gap),atomic_load(&mic_max_read_gap),
        atomic_load(&speaker_max_open),atomic_load(&speaker_max_write),atomic_load(&speaker_max_close),atomic_load(&mic_max_work),
        atomic_load(&song_ready)?"true":"false",atomic_load(&last_song_job),atomic_load(&music_render_us),atomic_load(&music_limited));
    return n < 0 || (size_t)n >= capacity ? AGENT_ERR_LIMIT : AGENT_OK;
}

const agent_audio_ops_t esp_hi_audio_ops = { .status = status, .play = play, .stop = stop,
    .volume = set_volume, .microphone = microphone,.inspect=inspect,.capture=capture,.replay=replay,
    .play_song=play_song,.read_song=read_song,.listen=listen };

agent_err_t esp_hi_audio_init(void)
{
    if (audio_task_handle) return AGENT_OK;
    gpio_config_t config = { .pin_bit_mask = 1ULL << PA, .mode = GPIO_MODE_OUTPUT };
    esp_err_t error = gpio_config(&config);
    if (!error) error = gpio_set_level(PA, 0);
    if (error) return audio_error(error);
    clip_partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x41,"clip");
    if(clip_partition) {
        agent_flash_ops_t flash={clip_read,clip_write,clip_erase,(void *)clip_partition,clip_partition->size,4096};
        agent_err_t loaded=agent_clip_open(&clip,&flash);
        atomic_store(&clip_ready,loaded==AGENT_OK);
        atomic_store(&clip_ms,loaded==AGENT_OK?(unsigned)(clip.samples*1000/AGENT_MIC_RATE):0);
        atomic_store(&capture_error,loaded==AGENT_ERR_NOT_FOUND?AGENT_OK:loaded);
    }
    control_lock = xSemaphoreCreateMutex();
    score_queue = xQueueCreate(1, sizeof(audio_command_t));
    /* Bounded DMA work must run before the Wi-Fi/TCP tasks on this single core. */
    if (control_lock && score_queue && xTaskCreate(audio_task, "agent_audio", 4096, NULL, configMAX_PRIORITIES-1, &audio_task_handle) == pdPASS)
        return AGENT_OK;
    if (control_lock) vSemaphoreDelete(control_lock);
    if (score_queue) vQueueDelete(score_queue);
    control_lock = NULL; score_queue = NULL;
    return AGENT_ERR_MEMORY;
}
