#ifndef AGENT_ASR_REALTIME_H
#define AGENT_ASR_REALTIME_H
#include "qianwen.h"
#include "asr_segments.h"

#define AGENT_ASR_RT_MODEL "qwen3-asr-flash-realtime"
#define AGENT_ASR_RT_SCRATCH 8192u
#define AGENT_ASR_RT_EVENT_MAX 6144u
#define AGENT_ASR_RT_INPUT_MAX 160000u
#define AGENT_ASR_RT_CHUNK 464u

/* One owning worker, one input per fresh connection. Manual receipts may
 * omit item IDs: association is the isolated connection and ordered commit /
 * final / session.finished barriers, never an invented ID or a preview.
 * Opt-in server VAD validates the observed IDs and aggregates every segment.
 * scratch and text are disjoint, caller-owned and live through close.
 * asr_partial callbacks borrow text and must not reenter this API or execute
 * tools. text is provisional until finish returns OK AND capture joins with
 * complete-input integrity. Every failure clears it and closes the connection.
 * asr_prefix immediately precedes each partial/segment callback, borrowing the
 * same buffer truncated at the contiguous provider-confirmed prefix. It may be
 * empty or advance while the preview stays equal. Callbacks must consume/copy
 * it synchronously: the full preview is restored on return. Prefix confirmation
 * does not establish a finished command, permit playback, or authorize tools.
 * The transport open hook must select AGENT_ASR_RT_MODEL, not the Omni model. */
typedef struct {
    agent_speech_t *speech;
    const agent_ws_ops_t *ws;
    char *text;
    size_t capacity,used,input_samples;
    uint64_t deadline;
    agent_err_t error;
    agent_asr_segments_t *segments; /* Optional caller-owned server-VAD collector. */
    char session_id[65],input_id[65];
    unsigned events,announcements;
    bool opened,created,updating,ready,message,committing,committed,final,finishing,done;
} agent_asr_realtime_t;

void agent_asr_realtime_init(agent_asr_realtime_t *,agent_speech_t *,const agent_ws_ops_t *,char *,size_t);
/* Before begin only. Same isolated input, multiple identified ASR segments;
 * the caller must keep metadata alive until finish/cancel, and still join
 * capture. Starts revoke endpoint candidates; segment finals are advisory. */
agent_err_t agent_asr_realtime_use_vad(agent_asr_realtime_t *,agent_asr_segments_t *);
agent_err_t agent_asr_realtime_begin(agent_asr_realtime_t *);
agent_err_t agent_asr_realtime_feed_pcm(agent_asr_realtime_t *,const int16_t *,size_t);
agent_err_t agent_asr_realtime_poll(agent_asr_realtime_t *,unsigned timeout_ms);
agent_err_t agent_asr_realtime_receive(agent_asr_realtime_t *,const char *,const agent_ws_chunk_t *);
agent_err_t agent_asr_realtime_finish(agent_asr_realtime_t *);
/* Source EOF must mean the whole intended input is published. Errors never
 * commit partial input. live() owns begin/finish/close, but the capture owner
 * must still be joined by its caller before publishing successful text. */
agent_err_t agent_asr_realtime_live(agent_asr_realtime_t *,const agent_speech_live_t *);
/* Explicitly loan the TX tail as a464-sample PCM input block. scratch must
 * be aligned raw allocated storage (or suitably typed storage), not a declared
 * char array; text/session/source objects stay disjoint. Encoding consumes
 * source bytes before overwriting them, then only TX is borrowed during send.
 * RX polling, including BUSY retries, cannot modify the pending TX frame. */
agent_err_t agent_asr_realtime_live_shared(agent_asr_realtime_t *,const agent_speech_live_t *);
agent_err_t agent_asr_realtime_transcribe(agent_asr_realtime_t *,const agent_speech_input_t *);
void agent_asr_realtime_cancel(agent_asr_realtime_t *);
#endif
