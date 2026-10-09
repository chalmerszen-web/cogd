#ifndef AGENT_QIANWEN_SPEECH_H
#define AGENT_QIANWEN_SPEECH_H
#include "speech.h"

#define AGENT_QWEN_SCRATCH 8192u
#define AGENT_QWEN_EVENT_MAX 6144u
#define AGENT_QWEN_ASR_MODEL "fun-asr-realtime"
#define AGENT_QWEN_TTS_MODEL "qwen-audio-3.0-tts-flash"
#define AGENT_QWEN_VOICE "longanhuan_v3.6"

/* One worker owns a WSS connection. first marks a new WebSocket frame;
 * final marks the last chunk of a FIN frame. Control frames stay in adapter. */
typedef struct { size_t length; unsigned opcode; bool first,final; } agent_ws_chunk_t;
typedef struct {
    agent_err_t (*open)(void *,const atomic_bool *);
    /* BUSY guarantees zero frame bytes sent; caller must pump incoming data.
     * Any other failure is ambiguous and must never replay that frame. */
    agent_err_t (*send)(void *,bool binary,char *,size_t,const atomic_bool *);
    agent_err_t (*read)(void *,char *,size_t,agent_ws_chunk_t *,unsigned timeout_ms,const atomic_bool *);
    void (*close)(void *);
    void *ctx;
} agent_ws_ops_t;
typedef struct {
    const agent_ws_ops_t *ws;
    agent_speech_t *speech;
    agent_pcm_sink_t sink;
    char *transcript;
    size_t transcript_cap,transcript_used,used,text_sent,pcm_bytes;
    uint64_t deadline;
    uint32_t sentence;
    unsigned opcode;
    uint8_t low_byte;
    bool asr,opened,started,ending,done,message,odd,pcm_open,partial,rx_pcm;
} agent_qwen_t;
typedef struct {
    /* Nonblocking producer. count=0,end=false means no published PCM yet.
     * EOF is valid only after capture/VAD/commit have succeeded. */
    agent_err_t (*next)(void *,int16_t *,size_t,size_t *count,bool *end);
    void *ctx;
} agent_speech_live_t;

void agent_qwen_init(agent_qwen_t *,agent_speech_t *,const agent_ws_ops_t *);
/* TTS-only, before begin: loan the first512 RX bytes as typed PCM storage.
 * scratch must be aligned raw allocated storage (or suitably typed storage),
 * not a declared char array. Binary input must be disjoint from that slot;
 * the synchronous sink consumes it before return. RX and unsent TX never
 * overlap. init revokes the loan; begin preserves it for this session. */
agent_err_t agent_qwen_use_rx_pcm(agent_qwen_t *);
/* Direct ASR begin/feed/finish users bind transcript/cap on the initialized
 * session before begin. live()/transcribe() bind that caller-owned output. */
agent_err_t agent_qwen_receive(agent_qwen_t *,const char *,const agent_ws_chunk_t *);
/* Pump downlink while a duplex text producer is waiting for its next phrase. */
agent_err_t agent_qwen_poll(agent_qwen_t *,unsigned timeout_ms);
agent_err_t agent_qwen_live(agent_speech_t *,const agent_speech_live_t *,char *,size_t);
extern const agent_asr_ops_t agent_qianwen_asr;
extern const agent_tts_ops_t agent_qianwen_tts;
#endif
