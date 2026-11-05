#ifndef KWS_RUNTIME_H
#define KWS_RUNTIME_H
#include "kws_fusion.h"
#ifdef AGENT_KWS_GRU_RESOURCE_ONLY
#include "kws_gru_pcm.h"
typedef kws_gru_pcm_t kws_runtime_t;
static inline size_t kws_runtime_size(void) { return kws_gru_pcm_size(); }
static inline size_t kws_runtime_alignment(void) { return kws_gru_pcm_alignment(); }
static inline int kws_runtime_init(void *p,size_t n,kws_runtime_t **s)
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
#ifdef AGENT_KWS_GRU_Q6_RESOURCE_ONLY
{ return kws_gru_pcm_init64_q6(p,n,&kws_gru64_q6_probe,&kws_active_model,s); }
#else
{ return kws_gru_pcm_init64(p,n,&kws_gru64_probe,&kws_active_model,s); }
#endif
#elif defined(AGENT_KWS_GRU_Q6_RESOURCE_ONLY)
{ return kws_gru_pcm_init_q6(p,n,&kws_gru32_q6_probe,&kws_active_model,s); }
#else
{ return kws_gru_pcm_init(p,n,&kws_gru32_probe,&kws_active_model,s); }
#endif
static inline uint64_t kws_runtime_prime(kws_runtime_t *s) { (void)s;return 0; }
static inline int kws_runtime_feed(kws_runtime_t *s,const int16_t *p,uint64_t end,kws_event_t *e)
{ return kws_gru_pcm_feed(s,p,end,e); }
static inline int kws_runtime_feed_armed(kws_runtime_t *s,const int16_t *p,uint64_t end,bool armed,kws_event_t *e)
{ (void)armed;return kws_gru_pcm_feed(s,p,end,e); }
static inline void kws_runtime_threshold(kws_runtime_t *s,int16_t q) { (void)s;(void)q; }
static inline void kws_runtime_stability(kws_runtime_t *s,bool stable) { (void)s;(void)stable; }
static inline const char *kws_runtime_name(void) {
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
#ifdef AGENT_KWS_GRU_Q6_RESOURCE_ONLY
    return "xiaoyan_gru64_q6_resource_untrained";
#else
    return "xiaoyan_gru64_resource_untrained";
#endif
#elif defined(AGENT_KWS_GRU_Q6_RESOURCE_ONLY)
    return "xiaoyan_gru32_q6_resource_untrained";
#else
    return "xiaoyan_gru32_resource_untrained";
#endif
}
#elif defined(AGENT_KWS_VERIFIED)
#include "kws_verified.h"
extern const kws_model_t kws_verified_primary, kws_verified_secondary;
typedef kws_verified_t kws_runtime_t;
static inline size_t kws_runtime_size(void) { return kws_verified_size(); }
static inline size_t kws_runtime_alignment(void) { return kws_verified_alignment(); }
static inline int kws_runtime_init(void *p,size_t n,kws_runtime_t **s)
{ return kws_verified_init(p,n,&kws_verified_primary,&kws_verified_secondary,&kws_active_model,s); }
static inline uint64_t kws_runtime_prime(kws_runtime_t *s) { return kws_verified_prime(s); }
static inline int kws_runtime_feed(kws_runtime_t *s,const int16_t *p,uint64_t end,kws_event_t *e)
{ return kws_verified_feed_512_armed(s,p,end,true,e); }
static inline int kws_runtime_feed_armed(kws_runtime_t *s,const int16_t *p,uint64_t end,bool armed,kws_event_t *e)
{ return kws_verified_feed_512_armed(s,p,end,armed,e); }
static inline void kws_runtime_threshold(kws_runtime_t *s,int16_t q) { kws_verified_threshold(s,q); }
static inline void kws_runtime_stability(kws_runtime_t *s,bool stable) { kws_verified_stability(s,stable); }
static inline const char *kws_runtime_name(void) { return "xiaoyan_el24_verify48_256ms"; }
#elif defined(AGENT_KWS_FUSION)
typedef kws_fusion_t kws_runtime_t;
static inline size_t kws_runtime_size(void) { return kws_fusion_size(); }
static inline size_t kws_runtime_alignment(void) { return kws_fusion_alignment(); }
static inline int kws_runtime_init(void *p,size_t n,kws_runtime_t **s)
{ return kws_fusion_init(p,n,&kws_active_model,&kws_secondary_model,s); }
static inline uint64_t kws_runtime_prime(kws_runtime_t *s) { return kws_fusion_prime(s); }
static inline int kws_runtime_feed(kws_runtime_t *s,const int16_t *p,uint64_t end,kws_event_t *e)
{ return kws_fusion_feed_512(s,p,end,e); }
static inline int kws_runtime_feed_armed(kws_runtime_t *s,const int16_t *p,uint64_t end,bool armed,kws_event_t *e)
{ return kws_fusion_feed_512_armed(s,p,end,armed,e); }
static inline void kws_runtime_threshold(kws_runtime_t *s,int16_t q) { kws_fusion_threshold(s,q); }
static inline void kws_runtime_stability(kws_runtime_t *s,bool stable) { kws_fusion_stability(s,stable); }
static inline const char *kws_runtime_name(void) {
#ifdef AGENT_KWS_FUSION_L
    return "xiaoyan_ds_tcn24_el3";
#elif defined(AGENT_KWS_FUSION_K)
    return "xiaoyan_ds_tcn24_ek3";
#else
    return "xiaoyan_ds_tcn24_ef3";
#endif
}
#else
typedef kws_handle_t kws_runtime_t;
static inline size_t kws_runtime_size(void) { return kws_workspace_size(); }
static inline size_t kws_runtime_alignment(void) { return kws_workspace_alignment(); }
static inline int kws_runtime_init(void *p,size_t n,kws_runtime_t **s)
{ return kws_init(p,n,&kws_active_model,s); }
static inline uint64_t kws_runtime_prime(kws_runtime_t *s) { (void)s;return 0; }
static inline int kws_runtime_feed(kws_runtime_t *s,const int16_t *p,uint64_t end,kws_event_t *e)
{ return kws_feed_512(s,p,end,e); }
static inline int kws_runtime_feed_armed(kws_runtime_t *s,const int16_t *p,uint64_t end,bool armed,kws_event_t *e)
{ return kws_feed_512_armed(s,p,end,armed,e); }
static inline void kws_runtime_threshold(kws_runtime_t *s,int16_t q) { kws_set_threshold(s,q); }
static inline void kws_runtime_stability(kws_runtime_t *s,bool stable) { kws_set_stability(s,stable); }
static inline const char *kws_runtime_name(void) { return kws_active_model.name; }
#endif
#endif
