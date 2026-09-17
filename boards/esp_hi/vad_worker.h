#ifndef ESP_HI_VAD_WORKER_H
#define ESP_HI_VAD_WORKER_H
#include "clip.h"
#include "endpoint.h"

#define ESP_HI_CONFIRM_WALL_MS 8000u
typedef struct {
    unsigned frames,processed_samples,backlog_samples,confirmed_ms,confirmed_wall_ms;
    unsigned peak,sum5,max_us,stack_bytes,model_bytes,heap_min;
    unsigned heap_bytes,arena_bytes,cpu_us,producer_cpu_us,nn_cpu_us,nn_wall_us,nn_cpu_max_us;
    unsigned elapsed_ms,speech_ms,noise,level;
    unsigned source_samples,source_frames,bound_ms,target_samples,source_stop_wall_ms;
    unsigned tail_possible_ms,tail_started_ms,tail_steps;
    unsigned tonal_frames,tonal_vetoes,tonal_clean;
    bool confirmed,done,deadline,cpu_timing;
    agent_err_t error;
} esp_hi_confirmation_stats_t;
typedef struct {
    agent_err_t (*acquire)(void *ctx,void **memory,size_t *capacity);
    void (*release)(void *ctx);
    void *ctx;
} esp_hi_confirmation_memory_t;
/* Bound before audio starts. Acquire/release both run on the audio owner. */
void esp_hi_confirmation_memory(const esp_hi_confirmation_memory_t *);

/* Worker owns raw256; audio producer owns filtered320 and source preprocessing.
 * Metadata borrows3000B from the engine tail, outside the TEN arena until join. */
agent_err_t esp_hi_confirmation_open(agent_clip_t *,unsigned noise,int16_t *raw,int16_t *filtered);
void esp_hi_confirmation_start(void);
agent_err_t esp_hi_confirmation_publish(size_t written_samples,size_t stored_bytes);
/* Audio-owner calls only. Metadata completes before associated raw publication. */
agent_err_t esp_hi_confirmation_source(int16_t sample);
bool esp_hi_confirmation_capture_done(void);
void esp_hi_confirmation_capture_stopped(void);
bool esp_hi_confirmation_done(void);
void esp_hi_confirmation_cancel(void);
/* Always joins before releasing buffers, model, VAD ownership or clip storage. */
agent_err_t esp_hi_confirmation_join(agent_endpoint_t *);
void esp_hi_confirmation_stats(esp_hi_confirmation_stats_t *);
#endif
