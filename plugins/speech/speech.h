#ifndef AGENT_CLOUD_SPEECH_H
#define AGENT_CLOUD_SPEECH_H
#include "transport.h"
#include "wav.h"

#define AGENT_SPEECH_TEXT_MAX 1800u
typedef struct {
    uint32_t samples;
    agent_err_t (*read)(void *,size_t offset,int16_t *,size_t count);
    void *ctx;
} agent_speech_input_t;
typedef struct {
    const agent_transport_ops_t *transport;
    const atomic_bool *cancelled;
    char *scratch;
    size_t capacity,used;
    uint64_t (*now_ms)(void *);
    void (*wait_ms)(unsigned);
    void *clock_ctx;
    void (*event)(void *,const char *stage,const char *text);
    void *event_ctx;
    /* Synchronous accepted ASR updates; repeated partials are intentional.
     * timed means a valid endpoint in 1..10000 ms, otherwise end_ms is zero. */
    void (*asr_sentence)(void *ctx,uint32_t id,unsigned end_ms,bool final,bool nonempty,bool timed);
    void *asr_sentence_ctx;
    void *provider_state; /* Caller-owned streaming provider session. */
    uint32_t (*random_u32)(void);
    uint32_t nonce;
    char task_id[129];
} agent_speech_t;

/* All contexts are caller-owned. Future streaming providers may keep state
 * between begin/feed/finish, must honor cancellation/backpressure and never
 * retain borrowed chunks. NULL hooks advertise unavailable model streaming. */
typedef struct {
    bool streaming;
    agent_err_t (*transcribe)(agent_speech_t *,const agent_speech_input_t *,char *,size_t);
    agent_err_t (*begin)(agent_speech_t *,unsigned rate);
    agent_err_t (*feed_pcm)(agent_speech_t *,const int16_t *,size_t);
    agent_err_t (*finish)(agent_speech_t *,char *,size_t);
    void (*cancel)(agent_speech_t *);
} agent_asr_ops_t;
typedef struct {
    bool streaming_text;
    agent_err_t (*speak)(agent_speech_t *,const char *voice,const char *text,const agent_pcm_sink_t *);
    agent_err_t (*begin)(agent_speech_t *,const char *voice,const agent_pcm_sink_t *);
    agent_err_t (*feed_text)(agent_speech_t *,const char *,size_t);
    agent_err_t (*finish)(agent_speech_t *);
    void (*cancel)(agent_speech_t *);
} agent_tts_ops_t;
extern const agent_asr_ops_t agent_vocalign_asr;
extern const agent_tts_ops_t agent_vocalign_tts;
agent_err_t agent_speech_transcript(const char *json,size_t length,char *,size_t);
#endif
