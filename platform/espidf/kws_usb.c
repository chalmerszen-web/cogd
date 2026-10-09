#include "kws_usb.h"
#include "kws_runtime.h"
#include "kws_pcen.h"
#include "crc.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdalign.h>
#if defined(AGENT_KWS_FUSION) || defined(AGENT_KWS_VERIFIED)
#include <math.h>
#endif

enum { KWS_USB_OUTPUT_BYTES=KWS_CHANNELS==24?1536:4*(KWS_TRACE_VALUES+40)+80+128 };
typedef struct {
    kws_runtime_t *handle;
    unsigned sequence;
    int16_t pcm[256];
    kws_trace_t trace;
    char output[KWS_USB_OUTPUT_BYTES];
    alignas(max_align_t) unsigned char workspace[];
} session_t;
static session_t *session;
bool kws_usb_begin(void)
{
    if(session) return false;
    session=heap_caps_aligned_calloc(alignof(session_t),1,sizeof(*session)+kws_runtime_size(),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!session) return false;
    if(kws_runtime_init(session->workspace,kws_runtime_size(),&session->handle)) {
        kws_usb_end(); return false;
    }
#if defined(AGENT_KWS_FUSION) || defined(AGENT_KWS_VERIFIED)
    float probability=(float)AGENT_WAKE_DEFAULT_THRESHOLD/1000.0f;
    kws_runtime_threshold(session->handle,(int16_t)lroundf(logf(probability/(1-probability))*256));
#endif
    return true;
}
void kws_usb_end(void) { free(session); session=NULL; }
static int digit(char c)
{ return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1; }
static void put_hex(char **out,uint16_t value,unsigned bytes)
{
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<bytes;++i) { *(*out)++=digits[(value>>4)&15]; *(*out)++=digits[value&15]; value>>=8; }
}
const char *kws_usb_frame(unsigned sequence,unsigned crc,const char *hex)
{
    if(!session || sequence!=session->sequence || sequence>=4096 || strlen(hex)!=1024) return NULL;
    unsigned char *raw=(unsigned char *)session->pcm;
    for(unsigned i=0;i<512;++i) {
        int hi=digit(hex[i*2]),lo=digit(hex[i*2+1]);
        if(hi<0 || lo<0) return NULL;
        raw[i]=(unsigned char)((hi<<4)|lo);
    }
    if(agent_crc32(raw,512)!=crc) return NULL;
    int64_t start=esp_timer_get_time();
#ifdef AGENT_KWS_GRU_RESOURCE_ONLY
    kws_event_t event;
    if(kws_gru_pcm_step(session->handle,session->pcm,(uint64_t)(sequence+1)*256,
        &event,&session->trace) || event.detected) return NULL;
#elif defined(AGENT_KWS_VERIFIED)
    kws_event_t event;
    if(kws_verified_step_pcm_armed(session->handle,session->pcm,(uint64_t)(sequence+1)*256,
        true,&event,&session->trace)) return NULL;
#elif defined(AGENT_KWS_FUSION)
    kws_event_t event;
    if(kws_fusion_step_pcm(session->handle,session->pcm,(uint64_t)(sequence+1)*256,&event,&session->trace)) return NULL;
#else
    kws_step_pcm(session->handle,session->pcm,&session->trace);
#endif
    unsigned us=(unsigned)(esp_timer_get_time()-start);
    int n=snprintf(session->output,sizeof(session->output),"{\"seq\":%u,\"crc\":%u,\"us\":%u,\"trace\":\"",sequence,crc,us);
    char *out=session->output+n;
    for(unsigned i=0;i<40;++i) put_hex(&out,(uint16_t)session->trace.logmel[i],2);
    for(unsigned i=0;i<40;++i) put_hex(&out,(uint8_t)session->trace.input[i],1);
#ifdef AGENT_KWS_GRU_RESOURCE_ONLY
    int16_t hidden[KWS_GRU_PCM_HIDDEN],logits[2];kws_gru_pcm_values(session->handle,hidden,logits);
    for(unsigned i=0;i<KWS_GRU_PCM_HIDDEN;++i) put_hex(&out,(uint16_t)hidden[i],2);
    for(unsigned i=0;i<2;++i) put_hex(&out,(uint16_t)logits[i],2);
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
    unsigned matrix_q=8;
#ifdef AGENT_KWS_GRU_Q6_RESOURCE_ONLY
    matrix_q=6;
#endif
    snprintf(out,sizeof(session->output)-(size_t)(out-session->output),"\",\"gru\":2,\"hidden\":64,\"matrix_q\":%u,\"trained\":false,\"resource_only\":true}",matrix_q);
#elif defined(AGENT_KWS_GRU_Q6_RESOURCE_ONLY)
    snprintf(out,sizeof(session->output)-(size_t)(out-session->output),"\",\"gru\":1,\"matrix_q\":6,\"trained\":false,\"resource_only\":true}");
#else
    snprintf(out,sizeof(session->output)-(size_t)(out-session->output),"\",\"gru\":1,\"matrix_q\":8,\"trained\":false,\"resource_only\":true}");
#endif
#else
    for(unsigned i=0;i<KWS_TRACE_VALUES;++i) put_hex(&out,(uint16_t)session->trace.layer[i],2);
#ifdef AGENT_KWS_VERIFIED
    int16_t heads[3],scores[2];kws_verified_scores(session->handle,heads,scores);
    snprintf(out,sizeof(session->output)-(size_t)(out-session->output),"\",\"verified\":[%d,%d,%d,%d,%d,%s]}",
        heads[0],heads[1],heads[2],scores[0],scores[1],event.detected?"true":"false");
#elif defined(AGENT_KWS_FUSION)
    int16_t scores[2];kws_fusion_scores(session->handle,scores);
    snprintf(out,sizeof(session->output)-(size_t)(out-session->output),"\",\"fusion\":[%d,%d,%d,%s]}",
        scores[0],scores[1],event.score_q8,event.detected?"true":"false");
#else
    strcpy(out,"\"}");
#endif
#endif
    ++session->sequence;
    return session->output;
}

const char *kws_usb_pcen_check(void)
{
    if(!session) return NULL;
    kws_pcen_t state;
    uint32_t power[40],random=17,crc=UINT32_MAX;
    int16_t values[40];
    unsigned maximum=0,total=0;
    size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    kws_pcen_reset(&state);
    for(unsigned frame=0;frame<256;++frame) {
        for(unsigned j=0;j<40;++j) {
            random=random*1664525u+1013904223u;
            power[j]=frame==0?0:frame==1?UINT32_MAX:frame==2?0:random;
        }
        int64_t start=esp_timer_get_time();
        kws_pcen_step(&state,power,values);
        unsigned elapsed=(unsigned)(esp_timer_get_time()-start);
        if(elapsed>maximum) maximum=elapsed;
        total+=elapsed;
        crc=agent_crc32_update(crc,values,sizeof(values));
    }
    size_t after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    snprintf(session->output,sizeof(session->output),
        "{\"frames\":256,\"state_bytes\":%u,\"crc\":%u,\"max_us\":%u,\"total_us\":%u,\"heap_before\":%u,\"heap_after\":%u}",
        (unsigned)sizeof(state),(unsigned)~crc,maximum,total,(unsigned)before,(unsigned)after);
    return session->output;
}
