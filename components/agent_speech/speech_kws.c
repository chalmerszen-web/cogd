#include "speech_backend.h"
#include "kws_runtime.h"
#include "esp_vad.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef AGENT_P4_KWS_NS
#include "esp_ns.h"
static ns_handle_t p4_ns;
static int16_t p4_ns_input[160],p4_ns_output[160];
static unsigned p4_ns_position;
static atomic_uint p4_ns_internal,p4_ns_total;
static void p4_denoise(int16_t *pcm)
{
    for(unsigned i=0;i<512;++i) {
        int16_t value=pcm[i];pcm[i]=p4_ns_output[p4_ns_position];
        p4_ns_input[p4_ns_position]=value;
        if(++p4_ns_position==160) {
            ns_process(p4_ns,p4_ns_input,p4_ns_output);
            p4_ns_position=0;
        }
    }
}
#endif

static kws_runtime_t *state;
static void *memory;
static vad_handle_t vad;
static atomic_uint chunk,allocated;
static atomic_uint primed_samples,prime_us;
static uint64_t sample_end;
static atomic_uint histogram[64],blocks,maximum,last_score;
#if defined(AGENT_KWS_VERIFIED) && !defined(AGENT_KWS_VERIFIED_RESOURCE_ONLY)
#define WAKE_OBSERVATION 1
enum { OBS_OPEN=1,OBS_CLOSE,OBS_ARM,OBS_DISARM,OBS_GUARD_HIGH,
       OBS_CANDIDATE,OBS_EXPIRED,OBS_ACCEPTED,OBS_BLOCKED,OBS_KINDS=9,OBS_ROWS=8 };
typedef struct { atomic_uint sequence,ms,scores,detail; } wake_observation_row_t;
static wake_observation_row_t observations[OBS_ROWS];
static atomic_uint observation_sequence,observation_counts[OBS_KINDS];
/* These edge latches belong exclusively to the audio/inference owner. */
static bool observation_armed,observation_guard,observation_blocked;
_Static_assert(sizeof(observations)+sizeof(observation_sequence)+sizeof(observation_counts)+3*sizeof(bool)<=200,
    "Wake observation must remain bounded without borrowing audio/network memory");

static void observe_event(unsigned kind,int16_t primary,int16_t verifier,unsigned cooldown_ms)
{
    unsigned sequence=atomic_load(&observation_sequence)+1;
    if(!sequence)sequence=1;
    wake_observation_row_t *row=&observations[(sequence-1)%OBS_ROWS];
    /* Atomic fields and the sequence guard prevent torn rows in USB snapshots. */
    atomic_store(&row->sequence,0);
    atomic_store(&row->ms,(unsigned)(esp_timer_get_time()/1000));
    atomic_store(&row->scores,(uint16_t)primary|((unsigned)(uint16_t)verifier<<16));
    atomic_store(&row->detail,kind|((cooldown_ms>0xffffff?0xffffff:cooldown_ms)<<8));
    atomic_store(&row->sequence,sequence);
    atomic_fetch_add(&observation_counts[kind-1],1);
    atomic_store(&observation_sequence,sequence);
}
static void observe_step(const kws_verified_observation_t *before,
    const kws_verified_observation_t *after,bool armed,bool detected)
{
    unsigned cooldown=after->cooldown_until>after->sample_end?
        (unsigned)((after->cooldown_until-after->sample_end)/16):0;
    if(armed!=observation_armed)
        observe_event(armed?OBS_ARM:OBS_DISARM,after->primary,after->verifier,cooldown);
    bool guard=!armed && after->primary>=after->threshold;
    bool candidate=armed && ((after->pending && after->pending!=before->pending) ||
        (detected && !before->pending));
    bool blocked=armed && !candidate && cooldown && after->primary_confirmed;
    if(guard && !observation_guard)observe_event(OBS_GUARD_HIGH,after->primary,after->verifier,cooldown);
    if(candidate)observe_event(OBS_CANDIDATE,after->primary,after->verifier,cooldown);
    if(armed && before->pending && !after->pending && !detected &&
       after->sample_end==before->sample_end+512)
        observe_event(OBS_EXPIRED,after->primary,after->verifier,cooldown);
    if(detected)observe_event(OBS_ACCEPTED,after->primary,after->verifier,cooldown);
    if(blocked && !observation_blocked)observe_event(OBS_BLOCKED,after->primary,after->verifier,cooldown);
    observation_armed=armed;observation_guard=guard;observation_blocked=blocked;
}
/* Counts use the enum order above; guard/blocked count rising edges, not all
 * frames. Rows are [sequence, device_ms, kind, primary, verifier, cooldown_ms].
 * Append only complete rows. With an unusually large timing histogram, the
 * array may contain fewer than eight rows, newest first; the base JSON fits
 * exactly as before if there is insufficient room even for the header. */
static void observe_profile(char *out,size_t capacity,size_t closing_brace)
{
    char text[192];unsigned sequence=atomic_load(&observation_sequence);
    int n=snprintf(text,sizeof(text),",\"wake_diag\":{\"v\":1,\"seq\":%u,\"counts\":[%u,%u,%u,%u,%u,%u,%u,%u,%u],\"events\":[",
        sequence,atomic_load(&observation_counts[0]),atomic_load(&observation_counts[1]),
        atomic_load(&observation_counts[2]),atomic_load(&observation_counts[3]),
        atomic_load(&observation_counts[4]),atomic_load(&observation_counts[5]),
        atomic_load(&observation_counts[6]),atomic_load(&observation_counts[7]),atomic_load(&observation_counts[8]));
    if(n<0 || (size_t)n>=sizeof(text) || closing_brace+(size_t)n+4>capacity)return;
    size_t used=closing_brace;memcpy(out+used,text,(size_t)n);used+=(size_t)n;
    unsigned written=0;
    for(unsigned i=0;i<OBS_ROWS && i<sequence;++i) {
        unsigned wanted=sequence-i;wake_observation_row_t *row=&observations[(wanted-1)%OBS_ROWS];
        if(atomic_load(&row->sequence)!=wanted)continue;
        unsigned ms=atomic_load(&row->ms),scores=atomic_load(&row->scores),detail=atomic_load(&row->detail);
        if(atomic_load(&row->sequence)!=wanted)continue;
        n=snprintf(text,sizeof(text),"%s[%u,%u,%u,%d,%d,%u]",written?",":"",wanted,ms,detail&255,
            (int)(int16_t)(scores&65535),(int)(int16_t)(scores>>16),detail>>8);
        if(n<0 || (size_t)n>=sizeof(text) || used+(size_t)n+4>capacity)break;
        memcpy(out+used,text,(size_t)n);used+=(size_t)n;++written;
    }
    memcpy(out+used,"]}}",4);
}
#endif
#if defined(AGENT_KWS_VERIFIED_RESOURCE_ONLY) || defined(AGENT_KWS_GRU_RESOURCE_ONLY)
static atomic_uint verified_detections;
#endif
#ifdef AGENT_KEYWORD_PCM
static esp_hi_kws_input_t input_trace;
void esp_hi_kws_input(esp_hi_kws_input_t *out) { *out=input_trace; }
#endif
void esp_hi_speech_disarm(void)
{
#ifdef AGENT_P4_KWS_NS
    if(p4_ns)ns_destroy(p4_ns);
    p4_ns=NULL;p4_ns_position=0;
    memset(p4_ns_input,0,sizeof(p4_ns_input));memset(p4_ns_output,0,sizeof(p4_ns_output));
#endif
#ifdef WAKE_OBSERVATION
    if(state) {
        if(observation_armed)observe_event(OBS_DISARM,0,0,0);
        observe_event(OBS_CLOSE,0,0,0);
    }
    observation_armed=observation_guard=observation_blocked=false;
#endif
    free(memory); memory=NULL; state=NULL; chunk=0; sample_end=0; primed_samples=0; prime_us=0;
}
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
    unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
#if AGENT_BOARD_P4
    caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
    if(heap_caps_get_free_size(caps)<kws_runtime_size()+8192) return false;
#else
    unsigned before=heap_caps_get_free_size(caps);
    /* Keep the measured minimum free-heap gate, including vendor VAD setup. */
    if(before<32768+kws_runtime_size()+8192) return false;
#endif
    /* IDF's ordinary allocator only guarantees four-byte alignment on C3;
     * the network's uint64 sample clock requires eight on this ABI. */
    memory=heap_caps_aligned_alloc(kws_runtime_alignment(),kws_runtime_size(),caps);
    if(!memory || kws_runtime_init(memory,kws_runtime_size(),&state)) goto fail;
    vad=vad_create(VAD_MODE_3);
    if(!vad) goto fail;
#ifdef AGENT_P4_KWS_NS
    unsigned ns_before_internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    unsigned ns_before_total=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    p4_ns=ns_pro_create(10,1,16000);
    if(!p4_ns)goto fail;
    unsigned ns_after_internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    unsigned ns_after_total=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    p4_ns_internal=ns_before_internal>ns_after_internal?ns_before_internal-ns_after_internal:0;
    p4_ns_total=ns_before_total>ns_after_total?ns_before_total-ns_after_total:0;
#endif
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
#if AGENT_BOARD_P4
    /* Concurrent camera/cloud allocation makes global heap deltas unreliable.
     * Report this model's own PSRAM block; the separate vendor VAD is excluded. */
    allocated=(unsigned)heap_caps_get_allocated_size(memory);
#else
    allocated=before-heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
#endif
#ifdef WAKE_OBSERVATION
    observe_event(OBS_OPEN,0,0,0);
#endif
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
#ifdef AGENT_P4_KWS_NS
    p4_denoise(pcm);
#endif
    kws_event_t event={0};
#ifdef WAKE_OBSERVATION
    kws_verified_observation_t before,after;
    bool observed=kws_verified_observe(state,&before);
#endif
    sample_end+=512;
    int error=kws_runtime_feed_armed(state,pcm,sample_end,armed,&event);
#ifdef WAKE_OBSERVATION
    if(!error && observed && kws_verified_observe(state,&after))observe_step(&before,&after,armed,event.detected);
#endif
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
    vad_destroy(vad);
#if AGENT_BOARD_P4
    // Quiet ES8311 speech is also checked against energy and current ASR.
    // Mode 2 discarded too many voiced frames in the measured weak trials.
    vad=vad_create(VAD_MODE_1);
#else
    vad=vad_create(VAD_MODE_2);
#endif
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
{
#ifdef AGENT_P4_KWS_WIDE
    return "你好，小言 / Hi，小言 (普通话/粤语)";
#else
    if(kws_active_model.trained && !strcmp(kws_active_model.name,"xiaoyan_p4_wake4_c48_v1"))
        return "你好，小言 / Hi，小言 (普通话/粤语)";
    return kws_active_model.trained?"你好，小言":"你好，小言 (untrained probe)";
#endif
}
const char *esp_hi_speech_model(void)
{
    if(kws_active_model.trained && !strcmp(kws_active_model.name,"xiaoyan_p4_wake4_c48_v1"))
        return "xiaoyan_p4_wake4_24_24_48";
#if defined(AGENT_P4_KWS_MEGA)
    return "xiaoyan_p4_wake4_96_96_48_sync";
#elif defined(AGENT_P4_KWS_SYNC_CONFIRM)
    return "xiaoyan_p4_wake4_48_48_48_sync";
#elif defined(AGENT_P4_KWS_WIDE)
    return "xiaoyan_p4_wake4_48_48_48";
#else
    return kws_runtime_name();
#endif
}
void esp_hi_kws_profile_reset(void)
{
    for(unsigned i=0;i<64;++i) atomic_store(&histogram[i],0);
    atomic_store(&blocks,0); atomic_store(&maximum,0); atomic_store(&last_score,0);
#ifdef WAKE_OBSERVATION
    for(unsigned i=0;i<OBS_ROWS;++i)atomic_store(&observations[i].sequence,0);
    for(unsigned i=0;i<OBS_KINDS;++i)atomic_store(&observation_counts[i],0);
    atomic_store(&observation_sequence,0);
#endif
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
#ifdef AGENT_P4_KWS_NS
    n=snprintf(out+used,capacity-used,"],\"frontend_ns_mode\":1,\"ns_adapter_delay_samples\":160,\"ns_heap_internal\":%u,\"ns_heap_total\":%u,\"ns_static_pcm_bytes\":640}",atomic_load(&p4_ns_internal),atomic_load(&p4_ns_total));
    if(n<=0 || (size_t)n>=capacity-used)return false;
    used+=(size_t)n-2;
#else
    if(snprintf(out+used,capacity-used,"]}")<=0 || capacity-used<3)return false;
#endif
#ifdef WAKE_OBSERVATION
    observe_profile(out,capacity,used+1);
#endif
    return true;
#endif
}
