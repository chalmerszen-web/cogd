#ifndef KWS_H
#define KWS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Width belongs to the compiled ABI. Default24 keeps existing firmware state;
 * the48 build is an independent binary, never a runtime buffer expansion. */
#ifndef KWS_BUILD_CHANNELS
#define KWS_BUILD_CHANNELS 24
#endif
#if KWS_BUILD_CHANNELS != 24 && KWS_BUILD_CHANNELS != 48
#error "KWS supports only the registered24/48 channel topologies"
#endif
enum { KWS_BANDS=40, KWS_CHANNELS=KWS_BUILD_CHANNELS, KWS_LAYERS=12, KWS_BLOCK=512,
       KWS_TRACE_VALUES=11*KWS_CHANNELS+1 };
typedef struct kws_handle kws_handle_t;
typedef struct {
    const int8_t *weights;
    const int32_t *bias;
    const int8_t *shift;
    uint8_t inputs, outputs, kernel, dilation, depthwise, relu;
} kws_layer_t;
typedef struct {
    const char *name;
    const kws_layer_t *layers;
    const int16_t *mean_q8;
    const uint16_t *inverse_std_q12;
    bool trained;
} kws_model_t;
typedef struct { int16_t score_q8; bool detected; uint64_t sample_end; } kws_event_t;
typedef struct {
    int16_t logmel[40];
    int8_t input[40];
    /* Stem, DW/PW pairs, then Q8.8 head; matches the compiled width. */
    int16_t layer[KWS_TRACE_VALUES];
} kws_trace_t;
typedef struct {
    uint64_t last_sample, cooldown_until;
    uint32_t blocks;
    uint8_t votes;
    bool stable; /* Uses former padding; require four consecutive blocks. */
    int16_t threshold_q8;
} kws_detector_t;

extern const kws_model_t kws_probe_model;
#ifdef AGENT_KWS_TRAINED
extern const kws_model_t kws_trained_model;
#define kws_active_model kws_trained_model
#else
#define kws_active_model kws_probe_model
#endif
size_t kws_workspace_size(void);
size_t kws_workspace_alignment(void);
unsigned kws_channels(void);
unsigned kws_trace_values(void);
int kws_init(void *memory,size_t bytes,const kws_model_t *model,kws_handle_t **out);
void kws_reset(kws_handle_t *kws);
void kws_set_threshold(kws_handle_t *kws,int16_t logit_q8);
void kws_set_stability(kws_handle_t *kws,bool stable);
int kws_feed_512(kws_handle_t *kws,const int16_t pcm[512],uint64_t sample_end,kws_event_t *event);
/* Guarded blocks retain inference history but cannot create a decision or
 * accumulate votes. An already accepted event's cooldown is preserved. */
int kws_feed_512_armed(kws_handle_t *kws,const int16_t pcm[512],uint64_t sample_end,bool armed,kws_event_t *event);
/* Explicit stepping and trace are also used by the host parity harness. */
int16_t kws_step_features(kws_handle_t *kws,const int8_t features[40],kws_trace_t *trace);
void kws_step_pcm(kws_handle_t *kws,const int16_t pcm[256],kws_trace_t *trace);
int32_t kws_requantize(int32_t value,int shift,int32_t low,int32_t high);
void kws_detector_reset(kws_detector_t *detector,int16_t threshold_q8);
/* false: legacy two-of-three; true: four consecutive 32-ms blocks. */
void kws_detector_stability(kws_detector_t *detector,bool stable);
bool kws_detector_step(kws_detector_t *detector,int16_t score,uint64_t sample_end);
bool kws_detector_step_armed(kws_detector_t *detector,int16_t score,uint64_t sample_end,bool armed);
#endif
