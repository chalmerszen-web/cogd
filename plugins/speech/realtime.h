#ifndef AGENT_REALTIME_H
#define AGENT_REALTIME_H
#include "qianwen.h"
#include "json.h"

#ifndef AGENT_RT_AUTOVAD
#define AGENT_RT_AUTOVAD 1
#endif
#ifndef AGENT_RT_TEXT_REUSE
#define AGENT_RT_TEXT_REUSE 0
#endif

#define AGENT_RT_MODEL "qwen3.5-omni-flash-realtime"
#define AGENT_RT_SCRATCH 8192u
#define AGENT_RT_EVENT_MAX 6144u
#define AGENT_RT_INPUT_MAX 160000u /* Ten seconds at 16 kHz per input turn. */
#define AGENT_RT_OUTPUT_RATE 16000u /* Negotiated PCM16 mono; input remains 16 kHz. */
#define AGENT_RT_PCM_MAX (AGENT_RT_OUTPUT_RATE*2u*90u)
#define AGENT_RT_SEGMENTS 8u
typedef struct {
    char id[65];
    unsigned start_ms,end_ms,text_offset,text_length;
    bool ended,committed,transcribed;
} agent_realtime_input_t;

/* Borrowed, synchronous event. Audio deltas contain an empty delta string;
 * PCM has already gone to sink. Tools are NEVER executed here. Do not reenter
 * send/poll from this callback; process staged actions after poll returns.
 * All calls, the callback and sink run on one owning worker. scratch must
 * remain exclusively borrowed during calls or incomplete messages. At an idle
 * complete-message boundary it may be overwritten under the same owner lock;
 * the session, speech object and scratch allocation must remain alive.
 * sink completion/draining on every exit remains the caller's responsibility. */
typedef agent_err_t (*agent_realtime_event_fn)(void *,const cJSON *);
typedef struct {
    agent_speech_t *speech;
    const agent_ws_ops_t *ws;
    agent_pcm_sink_t sink;
    agent_realtime_event_fn on_event;
    void *event_ctx;
    uint64_t deadline;
    /* Optional caller-owned absolute cap; protocol events never extend it.
     * Zero disables it. The single owner may clear it after accepted PCM. */
    uint64_t deadline_cap;
    size_t used,string_start,pcm_bytes,event_bytes,input_samples;
    agent_err_t error;
    unsigned depth,top,field,b64_used,unicode_digits,unicode_value;
    unsigned input_start_ms,input_end_ms;
    unsigned char b64[4],low_byte;
    char response_id[65],input_item_id[65];
    /* Non-NULL selects a text-only candidate. Borrowed until close or a
     * successful next_text_turn; its receipt grants no final-ASR authority. */
    const char *text_input;
    const char *text_instructions; /* Fixed96-token text-only configuration. */
#if AGENT_RT_TEXT_REUSE
    /* Opt in before begin_text. Maximum three turns on one connection;
     * existing input/response identity storage rejects old-turn receipts. */
    bool text_reuse;
#endif
    bool text_ready;
    bool opened,created,ready,done,active,pending,auto_vad,dispatching,updating;
    bool input_started,input_ended,input_paused;
    /* Manual-turn reuse only. Keep prior input/response identities while
     * clearing uncommitted audio; server conversation history is retained. */
    bool reusable,clearing,input_final;
    /* Opt-in before begin(auto_vad=true). Continues upload/downlink through
     * provisional200ms endpoints; caller MUST silently cache/discard output.
     * A response binds to the committed item, not the latest speech arrival.
     * Input final-text offsets are caller-owned; no partial authorizes tools. */
    bool continuous,response_cancelled;
    /* Opt-in manual speculation: retain response identities and drain a
     * bounded downlink fragment while uploading. No automatic commits. */
    bool manual_draft;
    unsigned input_count,response_revision,pending_revision;
    uint32_t response_hashes[AGENT_RT_SEGMENTS*2];
    unsigned response_count;
    agent_realtime_input_t inputs[AGENT_RT_SEGMENTS];
    /* Opt-in for one provider speech-to-function response-ID transition.
     * Caller must withhold all PCM/effects. The next response item must be a
     * function call; stale IDs and a second transition remain errors. */
    bool allow_tool_transition,tool_transition_pending;
    bool message,in_string,escape,string_key,audio_delta,delta_seen,padded,odd,pcm_open;
} agent_realtime_t;

void agent_realtime_init(agent_realtime_t *,agent_speech_t *,const agent_ws_ops_t *,
                         const agent_pcm_sink_t *,agent_realtime_event_fn,void *);
/* begin opens once and waits up to 10 seconds for session.updated. tools_json
 * is a JSON array; the entire escaped session.update must fit 2048 bytes.
 * auto_vad uses server_vad, threshold .1, silence_duration_ms 800. This is a
 * bounded on-device comparison against semantic_vad at the same settings. Idle/input
 * time is bounded by 120 s, each response by 45 s; no automatic retry occurs. */
agent_err_t agent_realtime_begin(agent_realtime_t *,const char *instructions,const char *tools_json,bool auto_vad);
/* Fresh text candidate: fixed model/Tina/16k, manual, no tools/search, max96.
 * Exact configuration echo is required; instructions remain immutable. */
agent_err_t agent_realtime_begin_text(agent_realtime_t *,const char *instructions);
#if AGENT_RT_TEXT_REUSE
/* After complete output at a message boundary, release the prior borrowed
 * text and accept another item. No I/O, reconnect, allocation or microphone
 * authority; history/identities and the caller's absolute cap are preserved.
 * The owner must finish its sink before calling and keep this session and
 * its transport/scratch alive. Expiry, cancellation and a fourth turn fail. */
agent_err_t agent_realtime_next_text_turn(agent_realtime_t *);
#endif
agent_err_t agent_realtime_feed_pcm(agent_realtime_t *,const int16_t *,size_t count);
/* Accepted counts only fully successful PCM frames, including frames sent
 * before a later read error. Auto endpoint can return BUSY with input_ended
 * and error==OK: remaining samples were NOT sent. Never resend accepted PCM.
 * input_samples remains the total accepted count for this auto input. */
agent_err_t agent_realtime_feed_pcm_some(agent_realtime_t *,const int16_t *,size_t count,size_t *accepted);
/* One auto input per session. A paired speech_stopped pauses all downlink
 * reads at its complete message boundary. After joining capture/reclaiming
 * reply and speaker memory, resume permits the server's automatic response;
 * it sends neither input_audio_buffer.commit nor response.create. */
agent_err_t agent_realtime_resume(agent_realtime_t *);
/* Manual only. Commit the complete input without generating a
 * reply. A prior draft may have used response.create while audio was still
 * uncommitted; committing remains explicit and never replays source samples.
 * manual_draft permits commit during its one pending/active response: final
 * ASR and output progress independently. Caller must have drained the entire
 * flushed input and must join capture validation before output or effects. */
agent_err_t agent_realtime_commit_input(agent_realtime_t *);
agent_err_t agent_realtime_commit(agent_realtime_t *); /* Manual: commit + response.create. */
/* Manual sessions only. Update instructions/tools without committing input or
 * clearing ASR. Returns after send; caller keeps uploading/polling until
 * updating becomes false on session.updated. The event callback must validate
 * the echoed configuration, as for begin. No output, tool
 * result, second update or turn clear may start before acknowledgement.
 * The caller owns all speculative output and final-input validation. */
agent_err_t agent_realtime_update(agent_realtime_t *,const char *instructions,const char *tools_json);
agent_err_t agent_realtime_tool_result(agent_realtime_t *,const char *call_id,const char *json_output);
agent_err_t agent_realtime_create_response(agent_realtime_t *);
/* Fresh manual text turn only, no microphone/audio-reuse/tools. At most512 UTF-8 bytes;
 * escaped JSON must fit the existing TX budget. A single pending item with
 * exactly matching text binds the returned server ID (the provider may ignore
 * client IDs). A session configured with begin_text may send response.create
 * immediately on the same ordered transport; matching acknowledgement is
 * still mandatory before response.created or PCM. Other sessions must wait.
 * Caller keeps text immutable through close and silently caches the PCM;
 * final intent, capture integrity and playback remain the caller's gates. */
agent_err_t agent_realtime_user_text(agent_realtime_t *,const char *text);
/* After completed output AND final ASR, clear the input and wait at most1s
 * for acknowledgement. Up to3 manual inputs share a connection; no replay.
 * Caller must preserve the session/speech objects and restore its turn state. */
agent_err_t agent_realtime_next_turn(agent_realtime_t *);
agent_err_t agent_realtime_poll(agent_realtime_t *,unsigned timeout_ms);
void agent_realtime_cancel(agent_realtime_t *);

/* The provider must put top-level type BEFORE delta. Only after parsing that
 * prefix as valid JSON and identifying response.audio.delta do we stream PCM.
 * A delta before type is a protocol error, never guessed to be audio. The
 * remaining metadata is fully validated at FIN. Malformed/truncated suffixes
 * abort the session; already delivered PCM cannot be retracted or replayed.
 * While input_paused, direct receive returns BUSY without consuming bytes;
 * poll checks cancellation/deadline but does not call the transport read. */
agent_err_t agent_realtime_receive(agent_realtime_t *,const char *,const agent_ws_chunk_t *);
#endif
