#ifndef AGENT_PREFETCH_H
#define AGENT_PREFETCH_H
#include "realtime.h"
#include "draft.h"
#include "intent.h"
enum { AGENT_PREFETCH_BYTES=40960, AGENT_PREFETCH_TEXT=512 };
typedef struct {
    agent_realtime_t *session;
    agent_speech_draft_t audio;
    char *input;
    size_t input_capacity;
    char text[AGENT_PREFETCH_TEXT+1],input_id[65];
    size_t text_length;
    unsigned bound_samples;
    agent_speech_guess_t proposed,guess;
    unsigned discarded,first_pcm_at;
    bool attempted,requested,invalid,complete,unusable,incomplete_input;
    bool commit_sent,committed,final_seen,released,text_done;
} agent_prefetch_t;
/* One silent candidate in caller-owned idle capture workspace. No playback,
 * tools or persistence. Preview IDs may change; the commit ack alone binds
 * final ASR identity. Caller sets commit_sent only after a successful send. */
void agent_prefetch_init(agent_prefetch_t *,agent_realtime_t *,void *,size_t,char *,size_t);
/* ASR is advisory until local capture succeeds and final intent is admitted. */
void agent_prefetch_preview(agent_prefetch_t *,const char *);
bool agent_prefetch_prepare(agent_prefetch_t *,unsigned uploaded_samples);
agent_err_t agent_prefetch_event(agent_prefetch_t *,const cJSON *);
agent_err_t agent_prefetch_pcm(agent_prefetch_t *,const int16_t *,size_t);
bool agent_prefetch_input_ready(const agent_prefetch_t *);
/* Output validation only; independent-ASR callers must separately authorize
 * release from complete, validated input. This never grants input authority. */
bool agent_prefetch_output_ready(const agent_prefetch_t *);
/* A complete admitted sentence can be released before audio completion.
 * The fixed THINK ending is sufficient without punctuation; after release
 * only a single terminal mark/whitespace may extend that neutral receipt.
 * The adapter sets released only after joining capture/routing final input;
 * it then drains the cached prefix and owns direct delivery of later PCM. */
bool agent_prefetch_ready(const agent_prefetch_t *);
#endif
