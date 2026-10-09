#include "speech_backend.h"
#include "kws_runtime.h"
#include "esp_vad.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>

static kws_runtime_t *state;
static void *memory;
static vad_handle_t vad;
static atomic_uint chunk,allocated;
static atomic_uint primed_samples,prime_us;
static uint64_t sample_end;
static atomic_uint histogram[64],blocks,maximum,last_score;
#if defined(AGENT_KWS_VERIFIED_RESOURCE_ONLY) || defined(AGENT_KWS_GRU_RESOURCE_ONLY)
static atomic_uint verified_detections;
#endif
#ifdef AGENT_KEYWORD_PCM
static esp_hi_kws_input_t input_trace;
void esp_hi_kws_input(esp_hi_kws_input_t *out) { *out=input_trace; }
#endif
void esp_hi_speech_disarm(void)
{ free(memory); memory=NULL; state=NULL; chunk=0; sample_end=0; primed_samples=0; prime_us=0; }
void esp_hi_speech_close(void)
{
    esp_hi_speech_disarm();
    if(vad) vad_destroy(vad);
    vad=NULL; allocated=0;
}
bool esp_hi_speech_open(void)
{
    if(state) return true;
    esp_hi_speech_close();
    unsigned before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    /* Keep the measured minimum free-heap gate, including vendor VAD setup. */
    if(before<32768+kws_runtime_size()+8192) return false;
    /* IDF's ordinary allocator only guarantees four-byte alignment on C3;
     * the network's uint64 sample clock requires eight on this ABI. */
    memory=heap_caps_aligned_alloc(kws_runtime_alignment(),kws_runtime_size(),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!memory || kws_runtime_init(memory,kws_runtime_size(),&state)) goto fail;
    vad=vad_create(VAD_MODE_3);
    if(!vad) goto fail;
    /* The fixed E/L seed represents 2.048 s of actual zero-PCM inference.
     * Carry that virtual clock into feed_512; starting again at zero would
     * correctly trigger the discontinuity reset and discard the warm state. */
    int64_t prime_start=esp_timer_get_time();
    sample_end=kws_runtime_prime(state);
#ifdef AGENT_KWS_STABLE_WAKE
    kws_runtime_stability(state,true);
#endif
    atomic_store(&prime_us,(unsigned)(esp_timer_get_time()-prime_start));
    atomic_store(&primed_samples,(unsigned)sample_end);chunk=512;
    allocated=before-heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    return true;
fail:
    esp_hi_speech_close(); return false;
}
unsigned esp_hi_speech_chunk(void) { return atomic_load(&chunk); }
unsigned esp_hi_speech_heap(void) { return atomic_load(&allocated); }
bool esp_hi_speech_wake_armed(int16_t *pcm,bool armed)
{
    if(!state) return false;
    int64_t start=esp_timer_get_time();
    kws_event_t event={0};
    sample_end+=512;
    int error=kws_runtime_feed_armed(state,pcm,sample_end,armed,&event);
#ifdef AGENT_KEYWORD_PCM
    input_trace=(esp_hi_kws_input_t){.sample_end=sample_end,.score_q8=event.score_q8,.error=error};
#ifdef AGENT_KWS_VERIFIED
    kws_verified_scores(state,input_trace.heads_q8,input_trace.scores_q8);
#elif defined(AGENT_KWS_FUSION)
    kws_fusion_scores(state,input_trace.scores_q8);
#else
    input_trace.scores_q8[0]=event.score_q8;
#endif
#endif
    unsigned us=(unsigned)(esp_timer_get_time()-start);
    atomic_fetch_add(&histogram[us/500<64?us/500:63],1);
    if(us>atomic_load(&maximum)) atomic_store(&maximum,us);
    atomic_fetch_add(&blocks,1);
    if(!error) atomic_store(&last_score,(uint16_t)event.score_q8);
#if defined(AGENT_KWS_VERIFIED_RESOURCE_ONLY) || defined(AGENT_KWS_GRU_RESOURCE_ONLY)
    if(!error && event.detected) atomic_fetch_add(&verified_detections,1);
    return false;
#else
    return !error && event.detected;
#endif
}
bool esp_hi_speech_wake(int16_t *pcm)
{ return esp_hi_speech_wake_armed(pcm,true); }
bool esp_hi_speech_vad(int16_t *pcm)
{ return vad && vad_process(vad,pcm,16000,20)==VAD_SPEECH; }
bool esp_hi_speech_vad_fast(void)
{
    if(!vad)return false;
    vad_destroy(vad);vad=vad_create(VAD_MODE_2);
    return vad!=NULL;
}
bool esp_hi_speech_threshold(unsigned value)
{
    if(!state || value<500 || value>950) return false;
    /* Run only when the setting changes, outside the frame processing path. */
    float probability=(float)value/1000;
    kws_runtime_threshold(state,(int16_t)lroundf(logf(probability/(1-probability))*256));
    return true;
}
const char *esp_hi_speech_word(void)
{ return kws_active_model.trained?"你好，小言":"你好，小言 (untrained probe)"; }
const char *esp_hi_speech_model(void) { return kws_runtime_name(); }
void esp_hi_kws_profile_reset(void)
{
    for(unsigned i=0;i<64;++i) atomic_store(&histogram[i],0);
    atomic_store(&blocks,0); atomic_store(&maximum,0); atomic_store(&last_score,0);
#if defined(AGENT_KWS_VERIFIED_RESOURCE_ONLY) || defined(AGENT_KWS_GRU_RESOURCE_ONLY)
    atomic_store(&verified_detections,0);
#endif
}
bool esp_hi_kws_profile(char *out,size_t capacity)
{
    unsigned confirmation_ms=0;
#ifdef AGENT_KWS_STABLE_WAKE
    confirmation_ms=128;
#endif
    int n=snprintf(out,capacity,"{\"trained\":%s,\"workspace\":%u,\"prime_samples\":%u,\"prime_us\":%u,\"blocks\":%u,\"max_us\":%u,\"score_q8\":%d,\"confirmation_ms\":%u,\"bin_us\":500,\"histogram\":[",
        kws_active_model.trained?"true":"false",(unsigned)kws_runtime_size(),atomic_load(&primed_samples),atomic_load(&prime_us),atomic_load(&blocks),atomic_load(&maximum),(int)(int16_t)atomic_load(&last_score),confirmation_ms);
    if(n<0 || (size_t)n>=capacity) return false;
    size_t used=(size_t)n;
    for(unsigned i=0;i<64;++i) {
        n=snprintf(out+used,capacity-used,"%s%u",i?",":"",atomic_load(&histogram[i]));
        if(n<0 || (size_t)n>=capacity-used) return false;
        used+=(size_t)n;
    }
#ifdef AGENT_KWS_GRU_RESOURCE_ONLY
    unsigned matrix_q=8;
#ifdef AGENT_KWS_GRU_Q6_RESOURCE_ONLY
    matrix_q=6;
#endif
    n=snprintf(out+used,capacity-used,"],\"models\":1,\"resource_only\":true,\"network\":\"gru%u\",\"matrix_q\":%u,\"threshold_ignored\":true,\"detections\":%u}",
        (unsigned)KWS_GRU_PCM_HIDDEN,matrix_q,atomic_load(&verified_detections));
    return n>0 && (size_t)n<capacity-used;
#elif defined(AGENT_KWS_VERIFIED_RESOURCE_ONLY)
    n=snprintf(out+used,capacity-used,"],\"verification_ms\":256,\"models\":3,\"resource_only\":true,\"detections\":%u}",
        atomic_load(&verified_detections));
    return n>0 && (size_t)n<capacity-used;
#else
    return snprintf(out+used,capacity-used,"]}")>0 && capacity-used>=3;
#endif
}
