#include "speech_backend.h"
#include "esp_wn_models.h"
#include "esp_vad.h"
#include "esp_heap_caps.h"
#include "model_path.h"
#include <stdatomic.h>
#include "wake_model_config.h"

extern const uint8_t model_start[] __asm__("_binary_wake_model_bin_start");
static srmodel_list_t *models;
static const esp_wn_iface_t *wake;
static model_iface_data_t *state;
static vad_handle_t vad;
extern const esp_wn_iface_t esp_sr_wakenet9s_quantized;
static atomic_uint chunk;
static unsigned allocated;
#ifdef AGENT_KEYWORD_VERIFY
#include "gate.h"
#include "templates.h"
#ifdef AGENT_KEYWORD_TRACE
#include "trace.h"
#endif
#include "esp_timer.h"
#ifdef KEYWORD_GATE_ACTIVITY
#include "activity_templates.h"
#endif
static keyword_gate_t keyword;
static atomic_uint keyword_raw,keyword_rejected,keyword_invalid,keyword_incomplete;
static atomic_uint keyword_positive,keyword_negative,keyword_max_us;
const char *esp_hi_speech_keyword_bank(void)
{
#ifdef KEYWORD_GATE_ACTIVITY
    return KG_ACTIVITY_BANK_ID;
#else
    return KG_BANK_ID;
#endif
}
static bool score_keyword(uint32_t *scores)
{
#ifdef KEYWORD_GATE_ACTIVITY
    return keyword_gate_activity_score(&keyword,keyword_templates,keyword_activity_templates,KG_TEMPLATE_COUNT,scores);
#else
    return keyword_gate_score(&keyword,keyword_templates,KG_TEMPLATE_COUNT,scores);
#endif
}
void esp_hi_speech_keyword_stats(esp_hi_keyword_stats_t *out)
{
    if(out) *out=(esp_hi_keyword_stats_t){atomic_load(&keyword_raw),atomic_load(&keyword_rejected),
        atomic_load(&keyword_invalid),atomic_load(&keyword_incomplete),atomic_load(&keyword_positive),
        atomic_load(&keyword_negative),atomic_load(&keyword_max_us)};
}
static bool verify_keyword(bool hit)
{
    int64_t start=esp_timer_get_time();
    bool accepted=false;
    if(hit) atomic_fetch_add(&keyword_raw,1);
    dl_convq_queue_t *q=wake->get_mfcc_data?wake->get_mfcc_data(state):NULL;
    if(!q || !q->itemq || q->n!=3 || q->c!=KG_CHANNELS || q->nch!=1 ||
       q->front<0 || q->front>=q->n || q->exponent!=-10 ||
       !keyword_gate_feed(&keyword,q->itemq+((q->front+q->n-1)%q->n)*q->c)) {
        atomic_fetch_add(&keyword_invalid,1); keyword_gate_reset(&keyword);
    } else if(hit) {
        uint32_t scores[KG_TEMPLATE_COUNT];
        if(keyword.count<KG_FRAMES) atomic_fetch_add(&keyword_incomplete,1);
        else if(!score_keyword(scores))
            atomic_fetch_add(&keyword_invalid,1);
        else {
            uint32_t positive=scores[0],negative=scores[KG_POSITIVE_COUNT];
            for(unsigned i=1;i<KG_POSITIVE_COUNT;++i) if(scores[i]<positive) positive=scores[i];
            for(unsigned i=KG_POSITIVE_COUNT+1;i<KG_TEMPLATE_COUNT;++i) if(scores[i]<negative) negative=scores[i];
            atomic_store(&keyword_positive,positive); atomic_store(&keyword_negative,negative);
            accepted=positive<=negative;
            if(!accepted) atomic_fetch_add(&keyword_rejected,1);
#ifdef AGENT_KEYWORD_TRACE
            if(keyword_trace_pending()) {
                keyword_startpoint_t point={.channel=-1,.samples=-1};
                if(wake->get_triggered_channel) point.channel=wake->get_triggered_channel(state);
                /* The SDK documents this getter only after channel verification.
                 * Preserve its raw result; no gate uses the estimate. */
                if(point.channel==0 && wake->get_start_point) {
                    int64_t before=esp_timer_get_time();
                    point.samples=wake->get_start_point(state);
                    point.us=(unsigned)(esp_timer_get_time()-before); point.called=true;
                }
                keyword_trace_capture_start(&keyword,scores,(uint32_t)(esp_timer_get_time()/1000),accepted,&point);
            }
#endif
        }
    }
    unsigned us=(unsigned)(esp_timer_get_time()-start);
    if(us>atomic_load(&keyword_max_us)) atomic_store(&keyword_max_us,us);
    return accepted;
}
#endif
#ifdef AGENT_SPEECH_DIAGNOSTICS
/* Exercise the pinned vendor's documented 0.4 floor only in the isolated probe. */
enum { threshold_minimum=400 };
static det_mode_t detection_mode=DET_MODE_90;
static vad_mode_t vad_creation_mode=VAD_MODE_3;
bool esp_hi_speech_probe_vad_mode(unsigned mode)
{
    if(state || vad || mode>4) return false;
    vad_creation_mode=(vad_mode_t)mode;
    return true;
}
bool esp_hi_speech_probe_mode(unsigned mode)
{
    if(state || mode>1) return false;
    detection_mode=mode?DET_MODE_95:DET_MODE_90;
    return true;
}
bool esp_hi_speech_probe_features(esp_hi_speech_features_t *out)
{
    if(!out || !state || !wake->get_mfcc_data) return false;
    dl_convq_queue_t *q=wake->get_mfcc_data(state);
    if(!q || !q->itemq || q->n<1 || q->n>128 || q->c<1 || q->c>64 || q->nch!=1 || q->front<0 || q->front>=q->n)
        return false;
    out->rows=q->n; out->columns=q->c; out->front=q->front; out->exponent=q->exponent;
    const int16_t *latest=q->itemq+((q->front+q->n-1)%q->n)*q->c;
    for(int i=0;i<q->c;++i) out->latest[i]=latest[i];
    return true;
}
#else
enum { threshold_minimum=500 };
#define detection_mode DET_MODE_90
#define vad_creation_mode VAD_MODE_3
#endif

void esp_hi_speech_disarm(void)
{
    if(state) wake->destroy(state);
    state=NULL; chunk=0;
#ifdef AGENT_KEYWORD_VERIFY
    keyword_gate_reset(&keyword);
#endif
}
void esp_hi_speech_close(void)
{
    esp_hi_speech_disarm();
    if(vad) vad_destroy(vad);
    if(models) srmodel_host_deinit(models);
    state=NULL; vad=NULL; models=NULL; chunk=0; allocated=0;
}
bool esp_hi_speech_open(void)
{
    if(state) return true;
    esp_hi_speech_close();
    unsigned before=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    /* Upstream create is not an OOM-safe allocator. Serialize with network work
     * and check its measured allocation budget before entering the library. */
    if(before<70000 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<32768) return false;
    models=srmodel_load(model_start);
    if(!models || models->num!=1) goto fail;
    /* This firmware embeds exactly WakeNet9s. The generic factory also pulls
     * in large-chip model loaders that are not used on C3. */
    wake=&esp_sr_wakenet9s_quantized;
    if(!wake) goto fail;
    state=wake->create(models->model_name[0],detection_mode);
    if(!state) goto fail;
    chunk=(unsigned)wake->get_samp_chunksize(state);
    if(!chunk || chunk>512 || wake->get_samp_rate(state)!=16000 || wake->get_channel_num(state)!=1) goto fail;
    vad=vad_create(vad_creation_mode);
    if(!vad) goto fail;
    allocated=before-heap_caps_get_free_size(MALLOC_CAP_8BIT);
    return true;
fail:
    esp_hi_speech_close(); return false;
}
unsigned esp_hi_speech_chunk(void) { return chunk; }
unsigned esp_hi_speech_heap(void) { return allocated; }
bool esp_hi_speech_wake(int16_t *pcm)
{
    if(!state) return false;
    bool hit=wake->detect(state,pcm)>0;
#ifdef AGENT_KEYWORD_VERIFY
    return verify_keyword(hit);
#else
    return hit;
#endif
}
bool esp_hi_speech_vad(int16_t *pcm) { return vad && vad_process(vad,pcm,16000,20)==VAD_SPEECH; }
bool esp_hi_speech_vad_fast(void)
{
    if(!vad)return false;
    vad_destroy(vad);vad=vad_create(VAD_MODE_2);
    return vad!=NULL;
}
bool esp_hi_speech_threshold(unsigned value)
{ return state && value>=threshold_minimum && value<=950 && wake->set_det_threshold(state,(float)value/1000,1)==1; }
const char *esp_hi_speech_word(void) { return AGENT_WAKE_WORD; }
const char *esp_hi_speech_model(void) { return AGENT_WAKE_MODEL_NAME; }
