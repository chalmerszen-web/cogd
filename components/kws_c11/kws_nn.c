#include "kws_internal.h"
#include <string.h>
#include <stdalign.h>
#include <limits.h>

#if defined(KWS_ALLOW_MIXED24) && KWS_BUILD_CHANNELS != 48
#error "KWS_ALLOW_MIXED24 requires compiled48 capacity"
#endif

size_t kws_workspace_size(void) { return sizeof(kws_handle_t); }
size_t kws_workspace_alignment(void) { return alignof(kws_handle_t); }
unsigned kws_channels(void) { return KWS_CHANNELS; }
unsigned kws_trace_values(void) { return KWS_TRACE_VALUES; }
bool kws_model_valid(const kws_model_t *model)
{
    if(!model || !model->layers || !model->mean_q8 || !model->inverse_std_q12) return false;
    /* Capacity remains part of the ABI. Only an explicit48 diagnostic build
     * may additionally hold the complete registered24 topology. */
    unsigned channels=KWS_CHANNELS;
#ifdef KWS_ALLOW_MIXED24
    channels=model->layers[0].outputs;
    if(channels!=24 && channels!=48) return false;
#endif
    for(unsigned i=0;i<KWS_LAYERS;++i) {
        const kws_layer_t *l=&model->layers[i];
        unsigned dw=i && i<11 && (i&1);
        unsigned kernel=i==0?3:dw?5:1;
        unsigned dilation=dw?(1u<<((i-1)/2)):1;
        if(!l->weights || (!dw && !l->bias) || !l->shift || l->kernel!=kernel ||
           l->inputs!=(i?channels:KWS_BANDS) || l->outputs!=(i==11?1:channels) ||
           l->dilation!=dilation || l->depthwise!=dw ||
           l->relu!=(i<11 && !dw)) return false;
        /* Bound every partial int32 sum, even for extreme INT8 operands. */
        int32_t margin=(int32_t)(kernel*(dw?1:l->inputs)*16384u);
        if(l->bias) for(unsigned c=0;c<l->outputs;++c)
            if(l->bias[c]>INT32_MAX-margin || l->bias[c]<INT32_MIN+margin) return false;
    }
    return true;
}
int kws_init(void *memory,size_t bytes,const kws_model_t *model,kws_handle_t **out)
{
    if(!out) return -1;
    *out=NULL;
    if(!memory || bytes<sizeof(kws_handle_t) || !kws_model_valid(model) ||
       (uintptr_t)memory%alignof(kws_handle_t)) return -1;
    kws_handle_t *k=memory;
    k->model=model; kws_reset(k); *out=k; return 0;
}
void kws_reset(kws_handle_t *k)
{
    const kws_model_t *model=k->model;
    memset(k,0,sizeof(*k)); k->model=model;
    kws_detector_reset(&k->detector,0);
}
void kws_set_threshold(kws_handle_t *k,int16_t logit_q8)
{ k->detector.threshold_q8=logit_q8; }
void kws_set_stability(kws_handle_t *k,bool stable)
{ kws_detector_stability(&k->detector,stable); }

int16_t kws_neural_buffers_step(const kws_neural_buffers_t *k,const kws_model_t *model,const int8_t features[40],kws_trace_t *trace)
{
    memcpy(k->current,features,40);
    if(trace && trace->input!=features) memcpy(trace->input,features,40);
    unsigned history_offset=0,slot=0,trace_offset=0;
    int16_t score=0;
    for(unsigned index=0;index<KWS_LAYERS;++index) {
        const kws_layer_t *l=&model->layers[index];
        unsigned past=(l->kernel-1)*l->dilation;
        int8_t *history=k->history+history_offset;
        unsigned pos=past?k->position[slot]:0;
        for(unsigned oc=0;oc<l->outputs;++oc) {
            int32_t acc=l->bias?l->bias[oc]:0;
            for(unsigned tap=0;tap<l->kernel;++tap) {
                unsigned delay=(l->kernel-1-tap)*l->dilation;
                const int8_t *input=delay?history+((pos+past-delay)%past)*l->inputs:k->current;
                if(l->depthwise) acc+=(int32_t)input[oc]*l->weights[oc*l->kernel+tap];
                else for(unsigned ic=0;ic<l->inputs;++ic)
                    acc+=(int32_t)input[ic]*l->weights[(oc*l->kernel+tap)*l->inputs+ic];
            }
            int32_t value=kws_requantize(acc,l->shift[oc],l->relu?0:index==11?-32768:-128,index==11?32767:127);
            if(trace) trace->layer[trace_offset+oc]=(int16_t)value;
            if(index==11) score=(int16_t)value;
            else k->next[oc]=(int8_t)value;
        }
        if(past) {
            memcpy(history+pos*l->inputs,k->current,l->inputs);
            k->position[slot++]=(uint8_t)((pos+1)%past);
            history_offset+=past*l->inputs;
        }
        if(index<11) {
#ifdef KWS_ALLOW_MIXED24
            memcpy(k->current,k->next,l->outputs);
#else
            memcpy(k->current,k->next,KWS_CHANNELS);
#endif
        }
        trace_offset+=l->outputs;
    }
    return score;
}

int16_t kws_neural_step(kws_neural_t *k,const kws_model_t *model,const int8_t features[40],kws_trace_t *trace)
{
    const kws_neural_buffers_t buffers={k->history,k->current,k->next,k->position};
    return kws_neural_buffers_step(&buffers,model,features,trace);
}

int16_t kws_step_features(kws_handle_t *k,const int8_t features[40],kws_trace_t *trace)
{ return kws_neural_step(&k->neural,k->model,features,trace); }

void kws_step_pcm(kws_handle_t *k,const int16_t pcm[256],kws_trace_t *trace)
{
    kws_trace_t *out=trace?trace:&k->trace;
    kws_frontend(k,pcm,out);
    kws_step_features(k,out->input,out);
}
int kws_feed_512_armed(kws_handle_t *k,const int16_t pcm[512],uint64_t end,bool armed,kws_event_t *event)
{
    if(!k || !pcm || !event || end<512) return -1;
    if(k->detector.blocks && end-k->detector.last_sample!=512) {
        int16_t threshold=k->detector.threshold_q8;
        bool stable=k->detector.stable;
        kws_reset(k); kws_set_threshold(k,threshold);
        kws_set_stability(k,stable);
    }
    kws_step_pcm(k,pcm,NULL); kws_step_pcm(k,pcm+256,NULL);
#ifdef KWS_ALLOW_MIXED24
    int16_t score=k->trace.layer[11*k->model->layers[0].outputs];
#else
    int16_t score=k->trace.layer[KWS_TRACE_VALUES-1];
#endif
    bool detected=kws_detector_step_armed(&k->detector,score,end,armed);
    *event=(kws_event_t){score,detected && k->model->trained,end};
    return 0;
}
int kws_feed_512(kws_handle_t *k,const int16_t pcm[512],uint64_t end,kws_event_t *event)
{ return kws_feed_512_armed(k,pcm,end,true,event); }
