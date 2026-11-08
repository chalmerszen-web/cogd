#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P4_AOQ_PLAY_PCM_MAX_BYTES (512u * 1024u)
#define P4_AOQ_PLAY_RING_BYTES 12288u

/* Synchronous, borrowed adapters supplied by the reserved runtime audio owner.
 * Keep ctx/ops valid, with no competing stream producer, until play returns.
 * open/space/write/remaining/played/finish map to esp_hi_stream_*; seal maps to
 * existing P4 p4_media_audio_seal(). write is called with <=space() samples.
 * A successful write accepts the entire chunk. finish(error) MUST join the
 * audio task before returning, even on error: borrowed ring ownership ends
 * only then. An arbitrary immediate stop callback is not a valid substitute.
 * cancelled/now_ms/wait_ms adapt the current job without storing kernel state.
 * No callback may free/reconfigure the borrowed ring or replace the owner. */
typedef struct {
    void* ctx;
    int (*open)(void*,unsigned rate,void* ring,size_t bytes);
    unsigned (*space)(void*);
    int (*write)(void*,const int16_t*,size_t samples);
    unsigned (*remaining)(void*);
    unsigned (*played)(void*);
    void (*seal)(void*);
    int (*finish)(void*,int error);
    bool (*cancelled)(void*);
    uint64_t (*now_ms)(void*);
    void (*wait_ms)(void*,unsigned milliseconds);
} p4_aoq_play_ops_t;

typedef struct {
    uint64_t generation;
    const char* stage; /* fixed strings only */
    int error,copy_error,open_error,finish_error;
    size_t accepted_samples; /* independent, exact-generation cache copy */
    size_t written_samples;  /* complete successful stream writes */
    unsigned played_samples; /* existing DAC-clock-based esp_hi estimate */
    unsigned remaining_samples; /* ring unread count; not DAC completion */
    bool cache_copied,stream_opened,sealed,joined,complete;
    bool cancelled,timed_out;
    uint64_t playback_ms,join_ms,elapsed_ms;
} p4_aoq_play_report_t;

/* Explicit diagnostic playback only. Does not change voice/listening/volume,
 * acquire another speaker path, record a mic, make a cloud call or use Flash.
 * Copies only the requested successful TTS generation into <=512 KiB PSRAM;
 * writes mono PCM16 at 24k through a separate 12 KiB borrowed stream ring.
 * Returns core error numbers. A busy stream is not interrupted/retried.
 * The progress deadline is audio duration + 4s, at most 14923ms. After a cancel,
 * timeout or error finish(error) still joins before either buffer is released.
 * That existing hardware cleanup may exceed the progress deadline: this API
 * never frees memory still owned by audio to impose an unsafe hard timeout.
 * Complete requires all samples written, remaining==0, played>=accepted, and
 * a successful finish/join. DAC counters do not prove a person heard speech. */
int p4_aoq_tts_play(uint64_t generation,const p4_aoq_play_ops_t*,p4_aoq_play_report_t*);

/* Same playback ownership and cleanup for another explicitly retained PCM
 * generation. copy_pcm must copy 24k mono PCM16 synchronously, must validate
 * the exact generation, and must not keep the destination pointer. */
typedef int (*p4_aoq_pcm_copy_fn)(uint64_t,void*,size_t,size_t*);
int p4_aoq_cached_play(p4_aoq_pcm_copy_fn copy_pcm,uint64_t generation,
                       const p4_aoq_play_ops_t*,p4_aoq_play_report_t*);

/* Cross-thread-safe request; affects calls already entered, not a later play.
 * Runtime may also use its existing cancellation flag via ops.cancelled. */
void p4_aoq_tts_play_cancel(void);

#ifdef __cplusplus
}
#endif
