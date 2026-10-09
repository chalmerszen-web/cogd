#ifndef ESP_HI_VAD_WORKER_H
#define ESP_HI_VAD_WORKER_H
#include "clip.h"
#include "endpoint.h"

#define ESP_HI_CONFIRM_WALL_MS 8000u
#define ESP_HI_CONFIRMED_SILENCE_MS 800u
#define ESP_HI_FAST_SILENCE_MS AGENT_EP_FAST_END_MS
#define ESP_HI_FAST_CAPTURE_WALL_MS (AGENT_CLIP_MAX_MS+1500u)
typedef struct {
    unsigned frames,processed_samples,backlog_samples,confirmed_ms,confirmed_wall_ms;
    unsigned peak,sum5,max_us,stack_bytes,model_bytes,heap_min;
    unsigned heap_bytes,arena_bytes,cpu_us,producer_cpu_us,nn_cpu_us,nn_wall_us,nn_cpu_max_us;
    unsigned elapsed_ms,speech_ms,quiet_ms,end_silence_ms,resume_frames,transcribed_ms,pending_hold_ms,asr_floor_ms,noise,level;
    unsigned source_samples,source_frames,bound_ms,target_samples,source_stop_wall_ms;
    unsigned tail_possible_ms,tail_started_ms,tail_steps;
    unsigned tonal_frames,tonal_vetoes,tonal_clean;
    bool confirmed,done,deadline,cpu_timing,fast,local_end;
    agent_err_t error;
} esp_hi_confirmation_stats_t;
typedef struct {
    /* fast=true selects the lightweight endpoint before backend construction;
     * it remains frozen for this capture and permits a smaller arena. */
    agent_err_t (*acquire)(void *ctx,void **memory,size_t *capacity,bool *fast);
    void (*release)(void *ctx);
    void *ctx;
    /* Optional bounded observation after producer/worker stop, before release.
     * The callback may read metadata only for the duration of this call. */
    void (*observe)(void *ctx,const uint8_t *records,unsigned frames,unsigned noise,
                    const agent_endpoint_t *endpoint,bool fast);
} esp_hi_confirmation_memory_t;
/* Bound before audio starts. Acquire/release both run on the audio owner. */
void esp_hi_confirmation_memory(const esp_hi_confirmation_memory_t *);

/* Worker owns raw256; audio producer owns filtered320 and source preprocessing.
 * Metadata borrows3000B from the engine tail, outside the TEN arena until join. */
agent_err_t esp_hi_confirmation_open(agent_clip_t *,unsigned noise,int16_t *raw,int16_t *filtered);
/* Audio owner, after open and before start only. Fast closes this cycle's TEN
 * backend and uses a local spectral/energy endpoint; each open restores classic. */
agent_err_t esp_hi_confirmation_fast(bool fast);
/* Audio owner only, after fast() closed TEN and before start. This prefix is
 * disjoint from source metadata. Runtime retains it for the queued live turn. */
agent_err_t esp_hi_confirmation_fast_workspace(void **,size_t *);
/* Creates the mode-sized task. A failed start still requires join to release
 * the prepared workspace; cancel/join also work before start. */
agent_err_t esp_hi_confirmation_start(void);
agent_err_t esp_hi_confirmation_publish(size_t written_samples,size_t stored_bytes);
/* Audio-owner calls only. Metadata completes before associated raw publication. */
agent_err_t esp_hi_confirmation_source(int16_t sample);
/* Audio owner only, just after a complete 20-ms source frame. This reads the
 * fresh producer metadata, independent of the neural worker's backlog. */
agent_err_t esp_hi_confirmation_source_frame(unsigned *end_ms,bool *speech);
bool esp_hi_confirmation_capture_done(void);
void esp_hi_confirmation_capture_stopped(void);
bool esp_hi_confirmation_done(void);
void esp_hi_confirmation_cancel(void);
/* Audio owner has stopped/flushed classic capture after neural confirmation.
 * Fast callers only post a revocable proposal; the worker owns every ending. */
void esp_hi_confirmation_finish(void);
/* Current-session ASR evidence, handed atomically to the fast endpoint owner. */
void esp_hi_confirmation_hint(bool meaningful,bool pending,bool empty,bool settled,bool phrase);
/* Audio owner serializes cloud proposals/revocation with ASR metadata updates.
 * Zero retracts. A proposal cannot apply before its source frame is consumed. */
void esp_hi_confirmation_cloud_end(unsigned source_ms);
/* Always joins before releasing buffers, model, VAD ownership or clip storage. */
agent_err_t esp_hi_confirmation_join(agent_endpoint_t *);
void esp_hi_confirmation_stats(esp_hi_confirmation_stats_t *);
#if AGENT_ENDPOINT_TRACE
#include "../../diagnostics/endpoint_notice.h"
/* Only valid inside the post-join metadata observer. */
const endpoint_notice_trace_t *esp_hi_confirmation_notice_trace(void);
#endif
#endif
