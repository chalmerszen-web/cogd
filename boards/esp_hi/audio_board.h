#ifndef ESP_HI_AUDIO_BOARD_H
#define ESP_HI_AUDIO_BOARD_H
#include "audio.h"
#include "speech.h"
#include "qianwen.h"
#if AGENT_CAPTURE_PROBE
#include "../../diagnostics/noise_trace.h"
/* Read only during post-capture trace export, before the listener rearms. */
const noise_trace_t *esp_hi_wake_noise_trace(void);
#endif
#ifdef AGENT_KEYWORD_PCM
#include "keyword_pcm.h"
/* USB owner opens/closes only after inspect reports all audio/mic activity off. */
keyword_pcm_t *esp_hi_keyword_pcm(void);
#endif
agent_err_t esp_hi_wake_status(char *,size_t);
agent_err_t esp_hi_wake_threshold(unsigned permille);
agent_err_t esp_hi_wake_gain(unsigned multiplier); /* 1..4, while listening is off. */
/* Release the keyword model before TLS allocations; listening resumes after. */
agent_err_t esp_hi_wake_network(bool active);
agent_err_t esp_hi_clip_export(unsigned offset,unsigned count,char *,size_t);
agent_err_t esp_hi_audio_init(void);
agent_err_t esp_hi_voice_enable(bool);
/* Volatile diagnostic, configurable only with listening/capture stopped. */
agent_err_t esp_hi_voice_capture_output(bool hold);
/* Fixed progress speech, separate from the final answer; join is idempotent. */
agent_err_t esp_hi_progress_begin(bool cantonese,const atomic_bool *cancelled);
agent_err_t esp_hi_progress_search_begin(bool cantonese,const atomic_bool *cancelled);
agent_err_t esp_hi_progress_memory_begin(bool cantonese,const atomic_bool *cancelled);
agent_err_t esp_hi_progress_join(void);
uint64_t esp_hi_progress_started_at(void);
bool esp_hi_voice_pending(void);
agent_err_t esp_hi_voice_input(agent_speech_input_t *);
void esp_hi_voice_release(void);
/* Configure while idle; callback queues the already-reserved live turn. */
void esp_hi_voice_live_configure(bool, bool (*started)(void));
/* Call only inside that audio-owner callback, before queuing the live turn.
 * Per capture: fast uses a local spectral endpoint; its completion cue is
 * rendered by the response stream while the network submits the input. */
/* The capture owner selects endpoint and cue ownership independently before
 * recording. Omni defers its cue to playback; isolated ASR keeps capture cue. */
agent_err_t esp_hi_voice_live_fast(bool fast,bool capture_cue);
void esp_hi_voice_live_ready(void); /* ASR readiness telemetry; capture never waits for it. */
void esp_hi_voice_live_hint(const char *text,bool settled);
/* Sentence identity/time revoke stale cloud endpoints; local VAD still gates acceptance. */
void esp_hi_voice_live_sentence(uint32_t id,unsigned end_ms,bool final,bool nonempty,bool timed);
/* Fast EOF publishes the entire flushed input, possibly before durable clip
 * validation finishes. live_join remains mandatory before effects or output. */
agent_err_t esp_hi_voice_live_input(agent_speech_live_t *);
/* Fast network owner only: idle TEN prefix, retained across live_join until
 * the engine reclaims its reservation. Never aliases source metadata/WS. */
agent_err_t esp_hi_voice_live_workspace(void **,size_t *);
unsigned esp_hi_voice_live_final(void);
/* Source-counted pause, not a semantic endpoint. One locked producer snapshot;
 * last_voice includes every positive frame, including isolated possible words. */
agent_err_t esp_hi_voice_live_join(agent_err_t);
uint64_t esp_hi_stream_started_at(void);
/* Network worker owns borrowed ring memory until finish has joined the audio task. */
agent_err_t esp_hi_stream_open(unsigned rate,void *memory,size_t bytes);
agent_err_t esp_hi_stream_open_cued(unsigned rate,void *memory,size_t bytes);
void esp_hi_stream_cue_times(uint64_t *begin,uint64_t *end);
agent_err_t esp_hi_stream_write(const int16_t *,size_t,const atomic_bool *cancelled);
/* Before any PCM: replace the borrowed ring with an independently owned,
 * bounded Flash spool. seal releases engine memory while audio keeps playing;
 * finish remains mandatory before another audio job or releasing the turn. */
/* deferred keeps unvalidated speech silent until seal; errors discard it. */
agent_err_t esp_hi_stream_cache_begin(const atomic_bool *cancelled,bool deferred);
agent_err_t esp_hi_stream_cache_seal(void);
unsigned esp_hi_stream_remaining(void);
agent_err_t esp_hi_stream_finish(agent_err_t error);
unsigned esp_hi_stream_underruns(void);
extern const agent_audio_ops_t esp_hi_audio_ops;
#endif
