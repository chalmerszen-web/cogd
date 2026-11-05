/* Official P4 WakeNet10 behind the existing audio-owner interface.
 * No custom classifier, verifier, thresholds or dialog state machine. */
#include "speech_backend.h"
#include "esp_wn_models.h"
#include "esp_vad.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "model_path.h"
#include <stdatomic.h>
#include <stdio.h>

extern const uint8_t model_start[] __asm__("_binary_wake_model_bin_start");
extern const uint8_t model_end[] __asm__("_binary_wake_model_bin_end");
/* Select only WakeNet10; the generic factory also links unused model engines. */
extern const esp_wn_iface_t esp_sr_wakenet10_quantized;
static const esp_wn_iface_t *const wake=&esp_sr_wakenet10_quantized;
static srmodel_list_t *models;
static model_iface_data_t *state;
static vad_handle_t vad;
static bool was_armed;
static atomic_uint chunk,allocated,internal_delta,threshold_ppm;
static atomic_uint blocks,maximum,detections,guard_hits,opens,open_us;

void esp_hi_speech_disarm(void)
{
    if(state)wake->destroy(state);
    state=NULL;chunk=0;was_armed=false;allocated=0;internal_delta=0;
}

void esp_hi_speech_close(void)
{
    esp_hi_speech_disarm();
    if(vad)vad_destroy(vad);
    if(models)srmodel_host_deinit(models);
    vad=NULL;models=NULL;
}

bool esp_hi_speech_open(void)
{
    if(state)return true;
    esp_hi_speech_close();
    unsigned before=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    unsigned internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    /* Vendor create is not an OOM-safe interface. Leave room for its arena. */
    if(internal<32768 || heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<2*1024*1024)
        return false;
    int64_t started=esp_timer_get_time();
    models=srmodel_load(model_start);
    if(!models || models->num!=1)goto fail;
    state=wake->create(models->model_name[0],DET_MODE_90);
    if(!state)goto fail;
    unsigned samples=(unsigned)wake->get_samp_chunksize(state);
    if(!samples || samples>512 || wake->get_samp_rate(state)!=16000 || wake->get_channel_num(state)!=1)
        goto fail;
    vad=vad_create(VAD_MODE_3);
    if(!vad)goto fail;
    threshold_ppm=(unsigned)(wake->get_det_threshold(state,1)*1000000.f+.5f);
    unsigned after=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    unsigned after_internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    allocated=before>after?before-after:0;
    internal_delta=internal>after_internal?internal-after_internal:0;
    chunk=samples;open_us=(unsigned)(esp_timer_get_time()-started);
    atomic_fetch_add(&opens,1);
    return true;
fail:
    esp_hi_speech_close();return false;
}

unsigned esp_hi_speech_chunk(void) {return atomic_load(&chunk);}
unsigned esp_hi_speech_heap(void) {return atomic_load(&allocated);}
bool esp_hi_speech_wake_armed(int16_t *pcm,bool armed)
{
    if(!state)return false;
    int64_t started=esp_timer_get_time();
    /* Do not carry a closed-gate hit/cooldown into a new listening period. */
    if(armed && !was_armed)wake->clean(state);
    was_armed=armed;
    bool hit=wake->detect(state,pcm)>0;
    unsigned us=(unsigned)(esp_timer_get_time()-started);
    if(us>atomic_load(&maximum))maximum=us;
    atomic_fetch_add(&blocks,1);
    if(hit)atomic_fetch_add(armed?&detections:&guard_hits,1);
    return armed && hit;
}
bool esp_hi_speech_wake(int16_t *pcm) {return esp_hi_speech_wake_armed(pcm,true);}
bool esp_hi_speech_vad(int16_t *pcm)
{return vad && vad_process(vad,pcm,16000,20)==VAD_SPEECH;}
bool esp_hi_speech_vad_fast(void)
{
    if(!vad)return false;
    vad_destroy(vad);vad=vad_create(VAD_MODE_1);
    return vad!=NULL;
}
bool esp_hi_speech_threshold(unsigned value)
{
    if(!state || (value && (value<500 || value>950)))return false;
    int ok=value?wake->set_det_threshold(state,(float)value/1000.f,1):wake->reset_det_threshold(state);
    if(ok)threshold_ppm=(unsigned)(wake->get_det_threshold(state,1)*1000000.f+.5f);
    return ok==1;
}
const char *esp_hi_speech_word(void) {return "你好小智";}
const char *esp_hi_speech_model(void) {return "wn10_nihaoxiaozhi";}
void esp_hi_kws_profile_reset(void)
{blocks=0;maximum=0;detections=0;guard_hits=0;opens=0;open_us=0;}
bool esp_hi_kws_profile(char *out,size_t capacity)
{
    int n=snprintf(out,capacity,
        "{\"model\":\"wn10_nihaoxiaozhi\",\"trained\":true,\"custom_wake\":false,"
        "\"model_bytes\":%u,\"workspace\":%u,\"heap_measurement\":\"open_delta_includes_vad\","
        "\"internal_delta\":%u,\"chunk\":%u,\"threshold_ppm\":%u,\"blocks\":%u,"
        "\"max_us\":%u,\"detections\":%u,\"guard_hits\":%u,\"opens\":%u,\"open_us\":%u}",
        (unsigned)(model_end-model_start),atomic_load(&allocated),atomic_load(&internal_delta),
        atomic_load(&chunk),atomic_load(&threshold_ppm),atomic_load(&blocks),atomic_load(&maximum),
        atomic_load(&detections),atomic_load(&guard_hits),atomic_load(&opens),atomic_load(&open_us));
    return n>0 && (size_t)n<capacity;
}
