#include "p4_audio.h"
#include "p4_near_onset.h"
#include "p4_near_correlation.h"
#if defined(AGENT_P4_AOQ_DUPLEX)
#include "p4_vad_stream.h"
#include "p4_near_votes.h"
#include "p4_near_quality.h"
#include "p4_probe_windows.h"
#include "p4_near_alignment.h"
#include "p4_echo_witness.h"
#include "p4_voice_periodicity.h"
#include "../../../platform/espidf/p4_adaptive_energy.h"
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
static p4_echo_witness_t *echo_witness;
static p4_echo_witness_result_t echo_witness_previous;
static unsigned echo_witness_overflows;
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
static atomic_uint wake_energy_history[64],wake_energy_count,wake_energy_at;
static atomic_uint wake_energy_noise,wake_energy_foreground,wake_energy_sequence;
static atomic_uint admission_noise,admission_foreground,admission_upper,admission_sequence;
static atomic_bool admission_calibrated;
/* 256 acoustic frames cover just over eight seconds. Epoch tags bind
 * producer observations to the current near-monitor lifecycle. */
enum {IDLE_VOICE_HISTORY=256};
static atomic_uint idle_voice_times[IDLE_VOICE_HISTORY],idle_voice_flags[IDLE_VOICE_HISTORY],idle_voice_write;
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
    uint32_t input_energy,noise_energy,foreground_energy,adaptive_threshold,noise_upper;
    int witness_corr_milli;
    uint32_t witness_residual,witness_trained,witness_us;
    uint32_t witness_ratio_milli,witness_ratio_limit_milli,witness_ratio_ready;
    uint32_t witness_state,residual_input_milli,anchor_prev_ms,anchor_gap_ms,anchor_age_ms;
} near_probe_t;
typedef struct {uint64_t capture_us,sent_us;} sent_probe_t;
static near_probe_t near_probe,*probe_near;
static sent_probe_t *probe_sent_clock;
static int16_t *probe_sent_pcm;
static unsigned probe_near_used,probe_sent_used,probe_sent_frames;
static p4_probe_bound_t *probe_bound;
static unsigned probe_bound_used,probe_bound_limit;
static uint64_t probe_requested_us;
static bool probe_stopped;
static atomic_bool probe_raw_active,probe_sent_active;
static atomic_bool probe_linear;
static atomic_bool probe_rolling,probe_trigger_requested;
static unsigned probe_raw_total;
static bool probe_long_mode;
static p4_probe_windows_t probe_windows;
static int16_t *probe_watch_ring;
static atomic_bool probe_watch_armed;
static atomic_uint probe_watch_dropped;
static uint32_t probe_watch_armed_ms,probe_watch_trigger_ms,probe_watch_candidate_ms,probe_watch_cursor;
static atomic_uint probe_raw_skips,probe_sent_skips;
enum { NEAR_MONITOR=1,NEAR_VOICE=2,NEAR_DUAL=4,NEAR_ENERGY=8,
       NEAR_MIC_ECHO=16,NEAR_OUT_ECHO=32,NEAR_ELIGIBLE=64,NEAR_STRONG=128,
       NEAR_CONFIRMED=256,NEAR_CORRELATED=512,NEAR_LINEAR=1024,NEAR_REFERENCE_GAP_CONFIRMED=2048,NEAR_ALIGNED=4096,
       NEAR_LINEAR_VOICE=8192,NEAR_VAD_BRIDGE=16384,NEAR_ONSET_HINT=32768,
       NEAR_ADAPTIVE_READY=65536,NEAR_ADAPTIVE_PASS=131072,NEAR_CLIPPED=262144,
       NEAR_TRANSFER_ECHO=524288,NEAR_NLP_VOICE=1048576,NEAR_STRUCTURE=2097152,
       NEAR_WITNESS_ABSOLUTE=4194304,NEAR_WITNESS_RELATIVE=8388608,
       NEAR_INDEPENDENT_LINEAR=16777216,NEAR_PROVISIONAL_GAP=33554432,
       NEAR_GAP_CONFIRMED=67108864,NEAR_PITCH_EDGE=134217728,NEAR_PITCH_CONFIRMED=268435456,
       NEAR_RESIDUAL_CANDIDATE=536870912,NEAR_RESIDUAL_CONFIRMED=1073741824 };
#define NEAR_EARLY_REPLAY_ONSET (UINT32_C(1)<<31)
static void probe_free(void)
{
    atomic_store(&probe_raw_active,false);atomic_store(&probe_sent_active,false);
    atomic_store(&probe_watch_armed,false);
    free(probe_pcm);free(probe_near);free(probe_sent_pcm);free(probe_sent_clock);free(probe_bound);
    free(probe_watch_ring);probe_watch_ring=NULL;
    probe_pcm=NULL;probe_near=NULL;probe_sent_pcm=NULL;probe_sent_clock=NULL;
    probe_limit=probe_used=probe_near_used=probe_sent_used=probe_sent_frames=0;
    probe_bound=NULL;probe_bound_used=probe_bound_limit=0;
    probe_requested_us=0;probe_stopped=false;atomic_store(&probe_linear,false);
    probe_long_mode=false;probe_windows=(p4_probe_windows_t){0};
    probe_watch_armed_ms=probe_watch_trigger_ms=0;
    atomic_store(&probe_watch_dropped,0);
    probe_raw_total=0;atomic_store(&probe_rolling,false);atomic_store(&probe_trigger_requested,false);
}

/* Called only by the capture owner and under the probe lock. */
static void probe_copy_pcm(int16_t *destination,unsigned write_at,unsigned n,unsigned channels,const int16_t *delivered)
{
    for(unsigned i=0;i<n;++i){
        unsigned at=(write_at+i)*channels;
        destination[at]=echo_mic[i];destination[at+1]=echo_ref[i];
        destination[at+2]=channels==4?echo_out[i]:delivered[i];
        if(channels==4)destination[at+3]=echo_linear[i];
    }
}
void p4_audio_probe_trigger(void)
{
    if(atomic_load(&probe_rolling) && atomic_load(&probe_raw_active))
        atomic_store(&probe_trigger_requested,true);
}

void p4_audio_probe_candidate(uint32_t candidate_ms,uint32_t cursor)
{
    if(!atomic_load(&probe_watch_armed) || !atomic_load(&probe_raw_active))return;
    if(xSemaphoreTake(probe_lock,0)!=pdTRUE){atomic_fetch_add(&probe_watch_dropped,1);return;}
    if(atomic_load(&probe_watch_armed) && atomic_load(&probe_raw_active)){
        if(probe_watch_trigger_ms)atomic_fetch_add(&probe_watch_dropped,1);
        else {
            probe_watch_candidate_ms=candidate_ms;
            probe_watch_cursor=cursor;
            probe_watch_trigger_ms=(uint32_t)(esp_timer_get_time()/1000);
        }
    }
    xSemaphoreGive(probe_lock);
}

/* The chronological near table also indexes the rolling 4-channel PCM. Copy
 * the chosen window over several frames to bound work on the capture owner. */
static void probe_watch_frame(void)
{
    const unsigned written=probe_near_used;
    if(!probe_windows.pending && probe_watch_trigger_ms &&
       (int32_t)(probe_near[written-1].at_ms-probe_watch_candidate_ms)>=0){
        unsigned lo=written>P4_PROBE_WATCH_RING?written-P4_PROBE_WATCH_RING:0,hi=written-1;
        bool in_history=(int32_t)(probe_watch_candidate_ms-probe_near[lo].at_ms)>=0;
        while(lo<hi){
            unsigned mid=lo+(hi-lo)/2;
            if((int32_t)(probe_near[mid].at_ms-probe_watch_candidate_ms)<0)lo=mid+1;
            else hi=mid;
        }
        if(!in_history || !p4_probe_watch_mark(&probe_windows,lo,written,probe_limit/512,
                probe_watch_trigger_ms,probe_watch_candidate_ms,probe_watch_cursor))
            atomic_fetch_add(&probe_watch_dropped,1);
        probe_watch_trigger_ms=0;
    }
    for(unsigned i=0;i<P4_PROBE_WATCH_COPY_BATCH && probe_windows.pending;++i){
        const p4_probe_span_t *span=&probe_windows.spans[probe_windows.used-1];
        unsigned source=span->start_frame+span->frames;
        if(!span->trigger_ms || source>=written)break;
        unsigned frame;
        if(!p4_probe_take(&probe_windows,&frame))break;
        memcpy(probe_pcm+frame*512*4,probe_watch_ring+(source%P4_PROBE_WATCH_RING)*512*4,4096);
    }
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
    if(!atomic_load(&probe_sent_active) && !probe_bound)return;
    if(!pcm || samples!=320){atomic_fetch_add(&probe_sent_skips,1);return;}
    if(xSemaphoreTake(probe_sent_lock,0)!=pdTRUE){atomic_fetch_add(&probe_sent_skips,1);return;}
    if(probe_sent_frames<(probe_bound_limit?probe_bound_limit:(probe_limit+319)/320)) {
        uint64_t accepted_us=(uint64_t)esp_timer_get_time();
        if(probe_sent_clock)probe_sent_clock[probe_sent_frames]=(sent_probe_t){capture_us,accepted_us};
        if(atomic_load(&probe_sent_active) && probe_sent_pcm && probe_sent_used<probe_limit) {
            unsigned n=probe_limit-probe_sent_used;if(n>samples)n=(unsigned)samples;
            memcpy(probe_sent_pcm+probe_sent_used,pcm,n*sizeof(*pcm));
            probe_sent_used+=n;
            if(probe_sent_used==probe_limit)atomic_store(&probe_sent_active,false);
        }
        ++probe_sent_frames;
    }
    xSemaphoreGive(probe_sent_lock);
}
bool p4_audio_probe_binding_active(void)
{ return probe_bound && atomic_load(&probe_raw_active); }
void p4_audio_probe_bound(const p4_probe_bound_t *bound,const int16_t *pcm,size_t samples,
    uint32_t aoq_start_cursor,uint64_t capture_us,bool aoq_local_accept)
{
    if(!bound || !pcm || samples!=320 || !aoq_local_accept)return;
    p4_audio_probe_sent(pcm,samples,capture_us);
    if(!probe_bound || !probe_lock || xSemaphoreTake(probe_lock,0)!=pdTRUE)return;
    if(probe_bound_used<probe_bound_limit){
        p4_probe_bound_t row=*bound;
        row.uplink=p4_probe_stage_measure(pcm,samples);
        row.aoq_start_cursor=aoq_start_cursor;row.aoq_end_cursor=aoq_start_cursor+(uint32_t)samples;
        row.aoq_local_accept=1;row.n_samples=(uint32_t)samples;
        row.sent_index=probe_sent_frames?probe_sent_frames-1u:UINT32_MAX;
        if(row.sent_index<probe_sent_frames){
            row.sent_clock_us=probe_sent_clock[row.sent_index].sent_us;
            row.accepted_us=row.sent_clock_us;
        }else row.sent_clock_us=row.accepted_us=0;
        row.stage_id=P4_PROBE_STAGE_LINEAR|P4_PROBE_STAGE_FRONTEND|P4_PROBE_STAGE_UPLINK;
        row.valid=(row.end_cursor-row.start_cursor==samples &&
            row.linear.samples==samples && row.frontend.samples==samples &&
            row.uplink.samples==samples && row.sent_clock_us!=0);
        probe_bound[probe_bound_used++]=row;
    }
    xSemaphoreGive(probe_lock);
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
    bool watch=!rolling && sscanf(command,"linear-watch %u %c",&ms,&extra)==1;
    bool long_mode=watch || (!rolling && sscanf(command,"linear-long %u %c",&ms,&extra)==1);
    bool linear=rolling || long_mode || sscanf(command,"linear-start %u %c",&ms,&extra)==1;
    if((long_mode && ms==(watch?P4_PROBE_WATCH_MS:P4_PROBE_LONG_MS)) || (!long_mode &&
       (linear || sscanf(command,"start %u %c",&ms,&extra)==1) && ms>=1000 && ms<=8000)) {
        if(atomic_load(&probe_raw_active))error=3;
        else {probe_free();unsigned count=ms*16;if(rolling)count-=count%512;
            unsigned raw_count=long_mode?P4_PROBE_LONG_RAW_MS*16:count;
            probe_pcm=heap_caps_malloc(raw_count*(linear?8:6),MALLOC_CAP_SPIRAM);
            probe_near=heap_caps_malloc(((count+511)/512)*sizeof(*probe_near),MALLOC_CAP_SPIRAM);
            probe_bound_limit=(count+319)/320;
            probe_bound=heap_caps_calloc(probe_bound_limit,sizeof(*probe_bound),MALLOC_CAP_SPIRAM);
            if(watch)probe_watch_ring=heap_caps_malloc(P4_PROBE_WATCH_RING*4096,MALLOC_CAP_SPIRAM);
            probe_sent_clock=heap_caps_malloc(((count+319)/320)*sizeof(*probe_sent_clock),MALLOC_CAP_SPIRAM);
            if(!long_mode)probe_sent_pcm=heap_caps_malloc(count*2,MALLOC_CAP_SPIRAM);
            if(!probe_pcm || !probe_near || !probe_bound || !probe_sent_clock || (watch && !probe_watch_ring) ||
               (!long_mode && !probe_sent_pcm)){probe_free();error=19;}
            else {probe_limit=count;probe_requested_us=(uint64_t)esp_timer_get_time();atomic_store(&probe_linear,linear);
                probe_long_mode=long_mode;
                if(long_mode)p4_probe_mark(&probe_windows,P4_PROBE_SPAN_MS,0,count/512);
                atomic_store(&probe_rolling,rolling);
                atomic_store(&probe_raw_skips,0);atomic_store(&probe_sent_skips,0);
                atomic_store(&probe_raw_active,true);atomic_store(&probe_sent_active,!long_mode);}}
    } else if(!strcmp(command,"watch-arm")){
        if(!probe_watch_ring || !atomic_load(&probe_raw_active))error=3;
        else if(!probe_watch_armed_ms){
            probe_watch_armed_ms=(uint32_t)(esp_timer_get_time()/1000);
            atomic_store(&probe_watch_armed,true);
        }
    } else if(sscanf(command,"linear-mark %u %c",&ms,&extra)==1){
        if(!probe_long_mode || probe_watch_ring || !atomic_load(&probe_raw_active) ||
           !p4_probe_mark(&probe_windows,ms,probe_near_used,probe_limit/512))error=3;
    } else if(!strcmp(command,"long-ranges")){
        if(!probe_long_mode || !probe_stopped || cap<1024)error=3;
        else {
            int used=snprintf(out,cap,"{\"ranges\":[");
            for(unsigned i=0;i<probe_windows.used;++i){
                const p4_probe_span_t *span=&probe_windows.spans[i];
                used+=snprintf(out+used,cap-used,"%s{\"start_frame\":%u,\"frames\":%u,\"raw_offset\":%u",
                    i?",":"",span->start_frame,span->frames,span->raw_frame*4096);
                if(span->trigger_ms)used+=snprintf(out+used,cap-used,
                    ",\"trigger_ms\":%u,\"candidate_ms\":%u,\"candidate_cursor\":%u,\"pre_frames\":%u",
                    (unsigned)span->trigger_ms,(unsigned)span->candidate_ms,(unsigned)span->candidate_cursor,span->pre_frames);
                out[used++]='}';
            }
            snprintf(out+used,cap-used,"]}");
        }
    } else if(!strcmp(command,"free"))probe_free();
    else if(!strcmp(command,"status"))snprintf(out,cap,"{\"samples\":%u,\"target\":%u,\"channels\":%u,\"rate\":16000,\"order\":\"%s\",\"requested_us\":%llu,\"near_frames\":%u,\"sent_samples\":%u,\"sent_frames\":%u,\"raw_skips\":%u,\"sent_skips\":%u,\"stopped\":%s,\"rolling\":%s,\"triggered\":%s,\"raw_total_samples\":%u,\"long_mode\":%s,\"raw_retained_samples\":%u,\"raw_spans\":%u,\"watch_mode\":%s,\"watch_armed\":%s,\"watch_armed_ms\":%u,\"watch_dropped\":%u,\"stage_records\":%u,\"stage_metadata\":%s}",
        probe_used,probe_limit,atomic_load(&probe_linear)?4u:3u,atomic_load(&probe_linear)?"mic,dac_reference,aec_scaled,linear_scaled":"mic,dac_reference,aec_scaled",
        (unsigned long long)probe_requested_us,probe_near_used,probe_sent_used,probe_sent_frames,atomic_load(&probe_raw_skips),atomic_load(&probe_sent_skips),probe_stopped?"true":"false",
        atomic_load(&probe_rolling)?"true":"false",atomic_load(&probe_trigger_requested)?"true":"false",probe_raw_total,
        probe_long_mode?"true":"false",probe_long_mode?probe_windows.raw_frames*512:probe_used,probe_windows.used,
        probe_watch_ring?"true":"false",atomic_load(&probe_watch_armed)?"true":"false",
        (unsigned)probe_watch_armed_ms,atomic_load(&probe_watch_dropped)+(probe_watch_trigger_ms!=0 && probe_stopped),
        probe_bound_used,probe_bound?"true":"false");
    else if(sscanf(command,"read %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_raw_active))error=3;
        else {unsigned width=atomic_load(&probe_linear)?8:6;
            unsigned start=atomic_load(&probe_rolling) && probe_limit && probe_raw_total>=probe_limit?probe_raw_total%probe_limit:0;
            unsigned samples=probe_long_mode?probe_windows.raw_frames*512:probe_used;
            error=probe_hex((const uint8_t *)probe_pcm,samples*width,offset,out,cap,start*width);}
    } else if(sscanf(command,"sent-read %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_sent_active))error=3;
        else error=probe_hex((const uint8_t *)probe_sent_pcm,probe_sent_used*2,offset,out,cap,0);
    } else if(sscanf(command,"near %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_raw_active) || offset>=probe_near_used || cap<4096)error=3;
        else {unsigned end=offset+(cap>=8192?16:12);if(end>probe_near_used)end=probe_near_used;
            int used=snprintf(out,cap,"{\"offset\":%u,\"columns\":\"ms,mic,ref,aec,scale,flags,votes,strong,mic_envelope,ref_envelope,mic_corr_milli,aec_corr_milli,linear_us,nlp_us,processing_us,wide_votes,wide_strong,input_energy,noise_energy,foreground_energy,adaptive_threshold,witness_corr_milli,witness_residual,witness_trained,witness_us,witness_ratio_milli,witness_ratio_limit_milli,witness_ratio_ready,noise_upper,witness_state,residual_input_milli,anchor_prev_ms,anchor_gap_ms,anchor_age_ms\",\"rows\":[",offset);
            unsigned start=atomic_load(&probe_rolling) && probe_limit && probe_raw_total>=probe_limit?(probe_raw_total/512)%(probe_limit/512):0;
            for(unsigned i=offset;i<end;i++){const near_probe_t *v=&probe_near[(start+i)%probe_near_used];
                used+=snprintf(out+used,cap-used,"%s[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",i==offset?"":",",(unsigned)v->at_ms,v->mic,v->ref,v->out,v->scale,v->flags,v->votes,v->strong,v->last_mic,v->last_ref,v->mic_corr,v->out_corr,v->linear_us,v->nlp_us,v->processing_us,v->wide_votes,v->wide_strong,(unsigned)v->input_energy,(unsigned)v->noise_energy,(unsigned)v->foreground_energy,(unsigned)v->adaptive_threshold,v->witness_corr_milli,(unsigned)v->witness_residual,(unsigned)v->witness_trained,(unsigned)v->witness_us,(unsigned)v->witness_ratio_milli,(unsigned)v->witness_ratio_limit_milli,(unsigned)v->witness_ratio_ready,(unsigned)v->noise_upper,(unsigned)v->witness_state,(unsigned)v->residual_input_milli,(unsigned)v->anchor_prev_ms,(unsigned)v->anchor_gap_ms,(unsigned)v->anchor_age_ms);}
            snprintf(out+used,cap-used,"]}");}
    } else if(sscanf(command,"sent-clock %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_sent_active) || offset>=probe_sent_frames || cap<4096)error=3;
        else {unsigned end=offset+16;if(end>probe_sent_frames)end=probe_sent_frames;
            int used=snprintf(out,cap,"{\"offset\":%u,\"columns\":\"capture_us,accepted_us\",\"rows\":[",offset);
            for(unsigned i=offset;i<end;i++)used+=snprintf(out+used,cap-used,"%s[%llu,%llu]",i==offset?"":",",(unsigned long long)probe_sent_clock[i].capture_us,(unsigned long long)probe_sent_clock[i].sent_us);
            snprintf(out+used,cap-used,"]}");}
    } else if(sscanf(command,"metadata %u %c",&offset,&extra)==1) {
        if(atomic_load(&probe_raw_active) || !probe_bound || offset>=probe_bound_used || cap<16400)error=3;
        else error=probe_hex((const uint8_t *)probe_bound,probe_bound_used*sizeof(*probe_bound),
            offset*sizeof(*probe_bound),out,cap,0);
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
{
    atomic_store(&confirmed_at,0);atomic_store(&confirmed_onset,0);atomic_fetch_add(&near_epoch,1);atomic_store(&near_monitor,enable);
#if defined(AGENT_P4_AOQ_DUPLEX)
    if(!enable)atomic_store_explicit(&admission_calibrated,false,memory_order_release);
#endif
}
uint32_t p4_audio_confirmed_at(void){return atomic_load(&capture_discontinuous)?0:atomic_load(&confirmed_at);}
uint32_t p4_audio_confirmed_onset(void){return atomic_load(&capture_discontinuous)?0:atomic_load(&confirmed_onset);}
p4_idle_voice_evidence_t p4_audio_idle_voice_evidence(uint32_t from_ms,uint32_t through_ms)
{
#if defined(AGENT_P4_AOQ_DUPLEX)
    uint32_t duration=through_ms-from_ms;
    if(!duration || duration>5000u || !atomic_load(&near_monitor) || atomic_load(&capture_discontinuous))
        return P4_IDLE_VOICE_UNKNOWN;
    unsigned epoch=atomic_load(&near_epoch);
    unsigned end=atomic_load_explicit(&idle_voice_write,memory_order_acquire);
    p4_idle_voice_window_t window={.from_ms=from_ms,.through_ms=through_ms};
    for(unsigned i=0;i<end && i<IDLE_VOICE_HISTORY-2u;++i){
        unsigned slot=(end-1-i)%IDLE_VOICE_HISTORY;
        unsigned packed=atomic_load_explicit(&idle_voice_flags[slot],memory_order_relaxed);
        if((packed>>12)!=(epoch&0xfffffu))break;
        if(!p4_idle_voice_frame_weighted(&window,atomic_load_explicit(&idle_voice_times[slot],memory_order_relaxed),
                packed&15u,(packed>>4)&255u))break;
    }
    /* Do not scan either slot that a completed write and the next pending
     * write could have overwritten during this observation. */
    unsigned after=atomic_load_explicit(&idle_voice_write,memory_order_acquire);
    if(atomic_load(&near_epoch)!=epoch || atomic_load(&capture_discontinuous) || after-end>1)
        return P4_IDLE_VOICE_UNKNOWN;
    return p4_idle_voice_finish(&window);
#else
    (void)from_ms;(void)through_ms;return P4_IDLE_VOICE_UNKNOWN;
#endif
}
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
void p4_audio_accept_wake_energy(void)
{
#if defined(AGENT_P4_AOQ_DUPLEX)
    unsigned count=atomic_load_explicit(&wake_energy_count,memory_order_acquire),n=count<64?count:64;
    uint32_t age=(uint32_t)(esp_timer_get_time()/1000)-atomic_load(&wake_energy_at);
    uint32_t values[64];
    for(unsigned i=0;i<n;++i)values[i]=atomic_load(&wake_energy_history[i]);
    uint32_t noise=0,foreground=0;
    p4_energy_wake_snapshot(values,n,age<=200 && !atomic_load(&output_active),&noise,&foreground);
    /* Odd/even publication prevents a reader combining different wakes. */
    atomic_fetch_add_explicit(&wake_energy_sequence,1,memory_order_acq_rel);
    atomic_store(&wake_energy_noise,noise);atomic_store(&wake_energy_foreground,foreground);
    atomic_fetch_add_explicit(&wake_energy_sequence,1,memory_order_release);
#endif
}
bool p4_audio_wake_energy(uint32_t *noise,uint32_t *foreground,unsigned *sequence)
{
    *noise=*foreground=*sequence=0;
#if defined(AGENT_P4_AOQ_DUPLEX)
    for(unsigned attempt=0;attempt<3;++attempt){
        unsigned before=atomic_load_explicit(&wake_energy_sequence,memory_order_acquire);
        if(!before || (before&1))continue;
        uint32_t n=atomic_load(&wake_energy_noise),f=atomic_load(&wake_energy_foreground);
        unsigned after=atomic_load_explicit(&wake_energy_sequence,memory_order_acquire);
        if(before==after){*noise=n;*foreground=f;*sequence=after;return f>n;}
    }
#endif
    return false;
}
void p4_audio_energy_reference(uint32_t noise,uint32_t foreground,uint32_t upper,bool calibrated)
{
#if defined(AGENT_P4_AOQ_DUPLEX)
    if(atomic_load(&admission_noise)==noise && atomic_load(&admission_foreground)==foreground &&
       atomic_load(&admission_upper)==upper && atomic_load(&admission_calibrated)==calibrated)return;
    atomic_fetch_add_explicit(&admission_sequence,1,memory_order_acq_rel);
    atomic_store(&admission_noise,noise);atomic_store(&admission_foreground,foreground);atomic_store(&admission_upper,upper);
    atomic_store(&admission_calibrated,calibrated);
    atomic_fetch_add_explicit(&admission_sequence,1,memory_order_release);
#else
    (void)noise;(void)foreground;(void)upper;(void)calibrated;
#endif
}
#if defined(AGENT_P4_AOQ_DUPLEX)
static bool admission_reference(uint32_t *noise,uint32_t *foreground,uint32_t *upper)
{
    *noise=*foreground=*upper=0;
    for(unsigned attempt=0;attempt<3;++attempt){
        unsigned before=atomic_load_explicit(&admission_sequence,memory_order_acquire);
        if(before&1)continue;
        uint32_t n=atomic_load(&admission_noise),f=atomic_load(&admission_foreground);
        uint32_t u=atomic_load(&admission_upper);
        bool calibrated=atomic_load(&admission_calibrated);
        unsigned after=atomic_load_explicit(&admission_sequence,memory_order_acquire);
        if(before==after){*noise=n;*foreground=f;*upper=u;return calibrated;}
    }
    return false;
}
#endif
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
    static p4_near_residual_onset_t residual_onset;
    static p4_near_output_tail_t output_tail;
    static unsigned structure_history,independent_history,provisional_history,residual_history,reference_gap_history;
    static uint32_t last_anchor_ms;
    static unsigned coherent_residual_history;
    static unsigned reference_gap_frames;
    static p4_voice_pitch_history_t pitch_history;
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
        residual_onset=(p4_near_residual_onset_t){0};
        output_tail=(p4_near_output_tail_t){0};
        structure_history=independent_history=provisional_history=residual_history=reference_gap_history=pitch_history.linked=0;
        last_anchor_ms=0;
        reference_gap_frames=0;
        coherent_residual_history=0;
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
    bool calibrated=linear && admission_reference(&near_probe.noise_energy,&near_probe.foreground_energy,&near_probe.noise_upper);
    uint32_t noise_upper=near_probe.noise_upper;
    near_probe.input_energy=p4_energy_mean_square(signal,512);
    near_probe.adaptive_threshold=p4_energy_barge_threshold(near_probe.noise_energy,near_probe.foreground_energy,noise_upper);
    if(calibrated)near_probe.flags|=NEAR_ADAPTIVE_READY;
    bool adaptive=p4_energy_barge_admit(near_probe.input_energy,near_probe.noise_energy,near_probe.foreground_energy,noise_upper,calibrated);
    if(adaptive)near_probe.flags|=NEAR_ADAPTIVE_PASS;
    float witness_scaled=echo_witness_previous.residual_power*scale*scale;
    near_probe.witness_corr_milli=isfinite(echo_witness_previous.correlation)?(int)(echo_witness_previous.correlation*1000):0;
    near_probe.witness_residual=isfinite(witness_scaled) && witness_scaled>=0 && witness_scaled<4294967295.f?(uint32_t)witness_scaled:UINT32_MAX;
    near_probe.witness_trained=echo_witness_previous.trained;
    near_probe.witness_ratio_milli=isfinite(echo_witness_previous.normalized_residual) &&
        echo_witness_previous.normalized_residual>=0 &&
        echo_witness_previous.normalized_residual<4294967.295f?
        (uint32_t)(echo_witness_previous.normalized_residual*1000.f):UINT32_MAX;
    near_probe.witness_ratio_limit_milli=isfinite(echo_witness_previous.ratio_limit) &&
        echo_witness_previous.ratio_limit>=0 &&
        echo_witness_previous.ratio_limit<4294967.295f?
        (uint32_t)(echo_witness_previous.ratio_limit*1000.f):UINT32_MAX;
    near_probe.witness_ratio_ready=echo_witness_previous.ratio_count>=8;
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
    bool dual=p4_near_output_duplex(&output_tail,near_probe.at_ms,
        atomic_load(&output_active),ref>96);
#else
    bool dual=scale>1 || ref>96;
#endif
    if(dual)near_probe.flags|=NEAR_DUAL;
    /* The FD filter has overlap. Compare against the recent acoustic
     * envelope even during a syllable gap in this particular DAC frame. */
    last_mic=mic>last_mic*4/5?mic:last_mic*4/5;last_ref=ref>last_ref*4/5?ref:last_ref*4/5;
    near_probe.last_mic=last_mic;near_probe.last_ref=last_ref;
#if defined(AGENT_P4_AOQ_DUPLEX)
    if(p4_near_saturation_guard(linear && alignment.ready && dual,
        aligned_mic,echo_out,signal,&votes,&strong,&onset,&hinted_onset)){
        near_probe.flags|=NEAR_CLIPPED;
        structure_history=independent_history=provisional_history=residual_history=reference_gap_history=pitch_history.linked=0;
        reference_gap_frames=0;
        coherent_residual_history=0;
        atomic_store(&confirmed_at,0);atomic_store(&confirmed_onset,0);
        residual_onset=(p4_near_residual_onset_t){0};
        vad_stream.used=linear_vad_stream.used=0;
        /* Preserve the sample clock and echo history even when this frame
         * is unusable as evidence. Raw capture and uplink are unchanged. */
        p4_near_alignment_push(&alignment,echo_mic,echo_ref);
        return;
    }
    bool voiced=p4_vad_stream_feed(&vad_stream,linear?echo_out:signal,512,near_vad_frame,near_vad);
    const bool nlp_voice=voiced;
    if(nlp_voice)near_probe.flags|=NEAR_NLP_VOICE;
    bool residual_voice=p4_vad_stream_feed(&linear_vad_stream,signal,512,near_vad_frame,near_linear_vad);
    if(residual_voice)near_probe.flags|=NEAR_LINEAR_VOICE;
    if(linear && !voiced && p4_near_speech(false,residual_voice,adaptive)){
        voiced=true;near_probe.flags|=NEAR_VAD_BRIDGE;
    }
#endif
    if(voiced)near_probe.flags|=NEAR_VOICE;
    /* Native AOQ compares current linear speech with learned background and
     * foreground power. Legacy WSS keeps its original hardware energy path. */
#if defined(AGENT_P4_AOQ_DUPLEX)
    bool energy=adaptive;
#else
    bool energy=dual?(linear?out>620:(out>620 && out>100+last_mic*9/10 && out>120+last_ref/3)):out>280;
#endif
    if(energy)near_probe.flags|=NEAR_ENERGY;
    bool near=voiced && energy;
#if defined(AGENT_P4_AOQ_DUPLEX)
    near=near && alignment.ready;
    /* Retain a possible early syllable independently of the NLP vote.
     * It must still pass pitch, spectrum and the same echo rejection.
     * This hint affects replay only after the normal votes confirm a cut. */
    bool onset_hint=linear && alignment.ready && residual_voice &&
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
    /* Frozen prediction belongs to the same previous raw frame as alignment.
     * Only explained echo with residual below the existing N/F/MAD threshold
     * can remove a vote. Neither speech PCM nor replay margins are altered. */
    unsigned witness=p4_echo_witness_reasons(echo_witness_previous,dual,calibrated,
        scale,near_probe.adaptive_threshold);
    near_probe.witness_state=witness;
    near_probe.residual_input_milli=near_probe.input_energy?
        (uint32_t)(((uint64_t)near_probe.witness_residual*1000u)/near_probe.input_energy):UINT32_MAX;
    near_probe.anchor_prev_ms=last_anchor_ms;
    near_probe.anchor_gap_ms=0;
    near_probe.anchor_age_ms=last_anchor_ms?near_probe.at_ms-last_anchor_ms:UINT32_MAX;
    /* Correlation has already vetoed raw and linear matches. Require voiced
     * structure on the linear channel and independent transfer residual
     * power together before letting it corroborate speech during playback. */
    bool independent_power=near && residual_voice &&
        p4_echo_witness_independent_power(echo_witness_previous,dual,calibrated,
            scale,near_probe.adaptive_threshold,near_probe.input_energy);
    bool independent_linear=independent_power &&
        p4_voice_duplex_structure(echo_out,signal,false,true);
    /* Diagnostic-only anchor timing. This does not alter near, votes, or the
     * witness veto. A clear or relative witness is safe for timing; an
     * absolute witness is never recorded as an anchor. */
    if(independent_linear && !(witness&P4_ECHO_WITNESS_ABSOLUTE)){
        near_probe.anchor_gap_ms=last_anchor_ms?near_probe.at_ms-last_anchor_ms:0;
        near_probe.anchor_age_ms=0;
        last_anchor_ms=near_probe.at_ms;
    }
    bool provisional_gap=!independent_linear &&
        p4_echo_witness_gap(witness,residual_voice,independent_power);
    if(provisional_gap)near_probe.flags|=NEAR_PROVISIONAL_GAP;
    bool residual_candidate=provisional_gap && p4_echo_witness_residual_fraction(echo_witness_previous) &&
        p4_voice_broadband(signal) && p4_voice_broadband(echo_out);
    if(residual_candidate)near_probe.flags|=NEAR_RESIDUAL_CANDIDATE;
    bool coherent_residual=residual_candidate && p4_voice_residual_onset_coherent(echo_out,signal);
    coherent_residual_history=((coherent_residual_history<<1)|(unsigned)coherent_residual)&15u;
    p4_near_residual_onset_update(&residual_onset,near_probe.at_ms,coherent_residual);
    if(linear && alignment.ready && witness){
        if(witness&P4_ECHO_WITNESS_ABSOLUTE)near_probe.flags|=NEAR_WITNESS_ABSOLUTE;
        if(witness&P4_ECHO_WITNESS_RELATIVE)near_probe.flags|=NEAR_WITNESS_RELATIVE;
        if(!independent_linear || (witness&P4_ECHO_WITNESS_ABSOLUTE)){
            near_probe.flags|=NEAR_TRANSFER_ECHO;near=false;onset_hint=false;
        }
    }
    if(independent_linear)near_probe.flags|=NEAR_INDEPENDENT_LINEAR;
    bool reference_gap_shape=reference_gap_frames==3 && near && dual && !nlp_voice && residual_voice &&
        p4_voice_reference_gap(aligned_ref-1024,scale,near_probe.adaptive_threshold) &&
        p4_voice_reference_gap_shape(aligned_mic,signal,echo_out);
    reference_gap_history=((reference_gap_history<<1)|(unsigned)reference_gap_shape)&0xffffu;
    if(reference_gap_frames<3)++reference_gap_frames;
    p4_near_alignment_push(&alignment,echo_mic,echo_ref);
#else
    memmove(previous_ref,previous_ref+512,1024*sizeof(int16_t));
    memcpy(previous_ref+1024,echo_ref,512*sizeof(int16_t));
#endif
#if defined(AGENT_P4_AOQ_DUPLEX)
    unsigned recovered=0;
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
#if defined(AGENT_P4_AOQ_DUPLEX)
    /* NLP may corroborate an aperiodic syllable only if its own power clears
     * the unchanged dynamic linear-domain threshold. No level rescaling or
     * fixed speech floor; all echo admission and confirmation still apply. */
    bool nlp_adaptive=near && linear && calibrated &&
        p4_energy_mean_square(echo_out,512)>near_probe.adaptive_threshold;
    bool solid=near && linear && (independent_linear ||
        p4_voice_duplex_evidence(echo_out,signal,nlp_voice,residual_voice,suppressed/512,nlp_adaptive));
    bool structure=near && linear && (independent_linear ||
        p4_voice_duplex_structure(echo_out,signal,nlp_voice,residual_voice));
    if(structure)near_probe.flags|=NEAR_STRUCTURE;
    structure_history=((structure_history<<1)|(unsigned)structure)&0xffffu;
    bool independent_solid=solid && independent_linear;
    if(p4_voice_pitch_history_update(&pitch_history,signal,independent_history,
            provisional_history,independent_solid,provisional_gap))near_probe.flags|=NEAR_PITCH_EDGE;
    independent_history=((independent_history<<1)|(unsigned)independent_solid)&0xfffu;
    provisional_history=((provisional_history<<1)|(unsigned)provisional_gap)&0xfffu;
    residual_history=((residual_history<<1)|(unsigned)residual_candidate)&0xffffu;
#else
    bool solid=near && (linear?suppressed/512>620:out>200+last_mic*scale/2);
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
    if(dual){
        confirmed=p4_near_duplex_corroborated(near,votes,strong,structure_history);
        if(!confirmed && p4_near_duplex_close_gap(votes,strong,structure_history,
                independent_history,provisional_history)){
            near_probe.flags|=NEAR_GAP_CONFIRMED;
            confirmed=true;
        }
        if(!confirmed && p4_near_duplex_pitch_pair(votes,strong,structure_history,
                independent_history,provisional_history,pitch_history.linked)){
            near_probe.flags|=NEAR_PITCH_CONFIRMED;
            near_probe.votes+=2;
            near_probe.strong+=2;
            near_probe.wide_votes+=2;
            near_probe.wide_strong+=2;
            confirmed=true;
        }
        if(!confirmed){
            recovered=p4_near_duplex_residual_burst(votes,strong,structure_history,residual_history);
            unsigned recovered_strong=recovered;
            if(!recovered){
                recovered=p4_near_duplex_residual_continuation(votes,strong,structure_history,
                    independent_history,coherent_residual_history);
                recovered_strong=recovered&0xcu;
            }
            if(recovered){
                near_probe.flags|=NEAR_RESIDUAL_CONFIRMED;
                near_probe.votes=__builtin_popcount((votes|recovered)&0xfffu);
                near_probe.strong=__builtin_popcount((strong|recovered_strong)&0xfffu);
                near_probe.wide_votes=__builtin_popcount((votes|recovered)&0xffffu);
                near_probe.wide_strong=__builtin_popcount((strong|recovered_strong)&0xffffu);
                confirmed=true;
            }
        }
        if(!confirmed){
            unsigned gap=p4_near_duplex_reference_gap(near,votes,strong,structure_history,reference_gap_history);
            if(gap){
                near_probe.flags|=NEAR_REFERENCE_GAP_CONFIRMED;
                near_probe.strong=__builtin_popcount((strong|gap)&0xfffu);
                near_probe.wide_strong=__builtin_popcount((strong|gap)&0xffffu);
                confirmed=true;
            }
        }
    }
#endif
    if(confirmed) {
        uint32_t retained_onset=onset.first;
#if defined(AGENT_P4_AOQ_DUPLEX)
        retained_onset=p4_near_onset_select(&onset,&hinted_onset,near_probe.at_ms);
        uint32_t earlier=p4_near_residual_onset_select(&residual_onset,recovered,near_probe.at_ms,retained_onset);
        if(earlier!=retained_onset)near_probe.flags|=NEAR_EARLY_REPLAY_ONSET;
        retained_onset=earlier;
#endif
        near_probe.flags|=NEAR_CONFIRMED;
        atomic_store(&confirmed_onset,p4_near_replay_anchor(retained_onset,
            (near_probe.flags&NEAR_REFERENCE_GAP_CONFIRMED)!=0));
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
        .pa_pin=BSP_POWER_AMP_IO,.use_mclk=true,.no_dac_ref=false,
        .hw_gain={.pa_voltage=5.0,.codec_dac_voltage=3.3}};
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
#if defined(AGENT_P4_AOQ_DUPLEX)
        /* Keep a wake reference in exactly the native input PCM domain,
         * even before monitoring begins. NLP still feeds the existing KWS. */
        need_linear=true;
#endif
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
#if defined(AGENT_P4_AOQ_DUPLEX)
        /* Never learn the device's DAC, cue or 640 ms acoustic tail. This
         * history is only trusted when WakeNet subsequently accepts a wake. */
        if(!atomic_load(&output_active) && !tail && r<512*4 && !atomic_load(&capture_discontinuous)){
            unsigned at=atomic_load_explicit(&wake_energy_count,memory_order_relaxed);
            atomic_store(&wake_energy_history[at%64],p4_energy_mean_square(echo_linear,512));
            atomic_store(&wake_energy_at,(uint32_t)(esp_timer_get_time()/1000));
            atomic_store_explicit(&wake_energy_count,at+1,memory_order_release);
        } else atomic_store_explicit(&wake_energy_count,0,memory_order_release);
#endif
        atomic_store(&echo_mic_level,m/512);atomic_store(&echo_ref_level,r/512);atomic_store(&echo_out_level,o/512);
        int64_t near_at=esp_timer_get_time();
#if defined(AGENT_P4_AOQ_DUPLEX)
        unsigned idle_epoch=atomic_load(&near_epoch);
        unsigned overflows=atomic_load(&rx_overflows);
        if(overflows!=echo_witness_overflows){
            memset(echo_witness,0,sizeof(*echo_witness));
            echo_witness_previous=(p4_echo_witness_result_t){0};echo_witness_overflows=overflows;
        }
#endif
        near_frame(m/512,r/512,o/512,scale,delivered,native_duplex);
        unsigned near_us=(unsigned)(esp_timer_get_time()-near_at);
#if defined(AGENT_P4_AOQ_DUPLEX)
        if(native_duplex && idle_epoch==atomic_load(&near_epoch)){
            unsigned idle_at=atomic_load_explicit(&idle_voice_write,memory_order_relaxed);
            unsigned slot=idle_at%IDLE_VOICE_HISTORY;
            unsigned flags=near_probe.flags;
            unsigned sample=(idle_epoch&0xfffffu)<<12;
            if((flags&(NEAR_MONITOR|NEAR_ALIGNED|NEAR_ADAPTIVE_READY))==
                    (NEAR_MONITOR|NEAR_ALIGNED|NEAR_ADAPTIVE_READY) &&
               !(flags&(NEAR_DUAL|NEAR_CLIPPED)) && !r && !atomic_load(&capture_discontinuous)){
                sample|=P4_IDLE_VOICE_FRAME_VALID;
                if(flags&NEAR_STRUCTURE)sample|=P4_IDLE_VOICE_FRAME_STRUCTURE;
                if(flags&NEAR_ELIGIBLE){
                    sample|=P4_IDLE_VOICE_FRAME_ELIGIBLE;
                    sample|=p4_idle_voice_energy_weight(near_probe.input_energy,near_probe.adaptive_threshold)<<4;
                    if(p4_idle_voice_pcm_rough(delivered))sample|=P4_IDLE_VOICE_FRAME_ROUGH;
                }
            }
            atomic_store_explicit(&idle_voice_times[slot],near_probe.at_ms,memory_order_relaxed);
            atomic_store_explicit(&idle_voice_flags[slot],sample,memory_order_relaxed);
            atomic_store_explicit(&idle_voice_write,idle_at+1,memory_order_release);
        }
        int64_t witness_at=esp_timer_get_time();
        echo_witness_previous=p4_echo_witness_process(echo_witness,echo_mic,echo_ref);
        near_probe.witness_us=(unsigned)(esp_timer_get_time()-witness_at);
#endif
        unsigned processing_us=(unsigned)(esp_timer_get_time()-processing_at);
        near_probe.linear_us=linear_us;near_probe.nlp_us=nlp_us;near_probe.processing_us=processing_us;
        if(near_us>atomic_load(&near_max_us))atomic_store(&near_max_us,near_us);
        if(processing_us>atomic_load(&capture_max_us))atomic_store(&capture_max_us,processing_us);
        if(processing_us>32000)atomic_fetch_add(&capture_overruns,1);
        atomic_fetch_add(&capture_frames,1);
        if(atomic_load(&probe_raw_active) && xSemaphoreTake(probe_lock,0)==pdTRUE) {
            bool rolling=atomic_load(&probe_rolling);
            if(probe_long_mode && atomic_load(&probe_raw_active)){
                if(!need_linear)atomic_fetch_add(&probe_raw_skips,1);
                else {
                    const p4_probe_span_t *span=probe_windows.pending?&probe_windows.spans[probe_windows.used-1]:NULL;
                    unsigned raw_frame;
                    if(span && !span->trigger_ms && p4_probe_take(&probe_windows,&raw_frame))
                        probe_copy_pcm(probe_pcm,raw_frame*512,512,4,delivered);
                    if(probe_watch_ring)
                        probe_copy_pcm(probe_watch_ring,(probe_near_used%P4_PROBE_WATCH_RING)*512,512,4,delivered);
                    probe_near[probe_near_used++]=near_probe;
                    if(probe_watch_ring)probe_watch_frame();
                    probe_used+=512;probe_raw_total=probe_used;
                    if(probe_used==probe_limit){atomic_store(&probe_raw_active,false);probe_stopped=true;}
                }
            } else if(!probe_long_mode && atomic_load(&probe_raw_active) && probe_pcm && (probe_used<probe_limit || rolling)) {
                unsigned write_at=rolling?probe_raw_total%probe_limit:probe_used;
                unsigned n=probe_limit-write_at;if(n>512)n=512;
                unsigned channels=atomic_load(&probe_linear)?4:3;
                /* A capture started mid-frame has no corresponding linear
                 * residual yet. Skip that diagnostic copy, never substitute
                 * stale linear audio or delay the real-time capture owner. */
                if(channels==4 && !need_linear){atomic_fetch_add(&probe_raw_skips,1);n=0;}
                if(n)probe_copy_pcm(probe_pcm,write_at,n,channels,delivered);
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
    echo_witness=heap_caps_calloc(1,sizeof(*echo_witness),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!near_linear_vad || !echo_witness)return ESP_ERR_NO_MEM;
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
