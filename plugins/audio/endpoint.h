#ifndef AGENT_ENDPOINT_H
#define AGENT_ENDPOINT_H
#include "agent.h"

/* The detector supplies one speech decision per 20 ms of PCM. Time is counted
 * in samples, never USB poll times, so scheduling jitter cannot shorten a word. */
#define AGENT_VAD_FRAME_MS 20u
#define AGENT_EP_FAST_END_MS 700u
#define AGENT_EP_RESUME_MS 60u
#define AGENT_EP_SUPPORT_MS 320u
#define AGENT_EP_WEAK_SUPPORT_MS 320u
#define AGENT_EP_PENDING_MS 1000u
#define AGENT_EP_PHRASE_MS 400u
/* One coherent mailbox value: current flags, proposed source frame, then a
 * persistent withdrawal generation. Publishers serialize read/modify/write;
 * the source owner consumes one atomic snapshot before each20ms frame. */
enum { AGENT_EP_TEXT=1u,AGENT_EP_PENDING=2u,AGENT_EP_REMOTE_SHIFT=2u,
    AGENT_EP_REMOTE_MASK=511u<<AGENT_EP_REMOTE_SHIFT,
    AGENT_EP_PHRASE=1u<<11,AGENT_EP_GENERATION=1u<<12 };
static inline unsigned agent_endpoint_notice(unsigned previous,bool meaningful,bool pending,bool empty,bool settled)
{
    /* A meaningful current draft replaces pending intent. The source still
     * requires local silence; ASR stage alone cannot end capture. Final input
     * validation, rather than this hint, controls playback and tool effects. */
    (void)settled;
    unsigned inherited=previous & ((unsigned)!(meaningful|empty)*AGENT_EP_PENDING);
    return ((previous+(empty?AGENT_EP_GENERATION:0u))&~(AGENT_EP_GENERATION-1u)) |
        (meaningful?AGENT_EP_TEXT:0u)|(pending?AGENT_EP_PENDING:0u)|inherited;
}
static inline unsigned agent_endpoint_notice_with_phrase(unsigned previous,bool meaningful,
    bool pending,bool empty,bool settled,bool phrase)
{
    unsigned notice=agent_endpoint_notice(previous,meaningful,pending,empty,settled);
    if(!empty && (phrase || (!meaningful && (previous&AGENT_EP_PHRASE))))notice|=AGENT_EP_PHRASE;
    return notice;
}
static inline unsigned agent_endpoint_proposal(unsigned previous,unsigned source_ms)
{
    unsigned frame=source_ms<=10000 && !(source_ms%20)?source_ms/20:0;
    return (previous&~AGENT_EP_REMOTE_MASK)|(frame<<AGENT_EP_REMOTE_SHIFT);
}
typedef enum { AGENT_EP_WAIT, AGENT_EP_SPEECH, AGENT_EP_DONE,
    AGENT_EP_NO_SPEECH, AGENT_EP_LIMIT, AGENT_EP_CANCELLED } agent_endpoint_state_t;
typedef struct {
    agent_endpoint_state_t state;
    unsigned elapsed_ms, speech_ms, quiet_ms;
    unsigned end_ms, wait_ms, maximum_ms;
    unsigned onset_bits, support_at_ms, weak_bits, dense_at_ms;
    unsigned resume_frames, transcribed_ms;
    unsigned held_frames,remote_ms;
    unsigned asr_notice;
    uint16_t asr_floor_ms;
    bool pending,local_onset;
} agent_endpoint_t;
typedef struct { unsigned levels[32],at,count,noise,level; } agent_activity_t;
/* Learn a low percentile of recent room levels before wake; freeze during the
 * utterance. The energy guard complements the WebRTC spectral classifier. */
bool agent_activity_feed(agent_activity_t *,const int16_t *,size_t,bool learn,bool spectral_voice,bool speaking);
agent_err_t agent_endpoint_init(agent_endpoint_t *,unsigned end_ms,unsigned wait_ms,unsigned maximum_ms);
agent_endpoint_state_t agent_endpoint_feed(agent_endpoint_t *,bool speech);
/* Preserve a new syllable beginning at the silence boundary: after accepted
 * speech only, recent positive frames may defer ending by at most 60 ms while
 * the unchanged four-vote continuation guard fills. Three votes anywhere in
 * its160ms window can use the same grace across a gap. Silence is never reset
 * by this grace. At exhausted pending quota, only a new contiguous strong run
 * gets up to three frames; a gap ends it and four votes resume normally.
 * Classic confirmation uses the strict feed above. */
agent_endpoint_state_t agent_endpoint_feed_resume(agent_endpoint_t *,bool speech);
/* Once speech is admitted, four weak votes may support two strong votes in
 * 160ms. Only a CURRENT strong frame can reset silence; weak-only noise and
 * isolated strong impulses cannot keep the turn alive. Support expires320ms
 * after the last strict continuation and cannot renew itself. A current ASR
 * transcript or an ASR-admitted pending phrase can corroborate the same two
 * strong votes inside that window; text alone, weak-only input and classic
 * feeds cannot use this shortcut.
 * Onset, end-silence duration and the cumulative hold quota are unchanged. */
agent_endpoint_state_t agent_endpoint_feed_supported(agent_endpoint_t *,bool strong,bool weak);
/* Optional independent speech confirmation. Unconfirmed spectral bursts may
 * neither commit a clip nor extend the original no-speech waiting deadline. */
void agent_endpoint_verify(agent_endpoint_t *,bool confirmed);
/* Strong independent evidence within 320 ms may support a fragmented onset. This
 * cannot reopen a terminal endpoint, extend waiting, or alter the end timer. */
bool agent_endpoint_support(agent_endpoint_t *,bool strong);
/* Same-turn ASR text may corroborate fragmented local onset after120ms of
 * positive source frames. Never resets quiet, renews the waiting deadline or
 * reopens a terminal capture. Fast feeds may spend the shared continuation
 * quota during the first end_ms after ASR-only admission; repeated text cannot
 * refill it. Strict onset and an independently guarded cloud end bypass it. */
bool agent_endpoint_transcribed(agent_endpoint_t *);
/* Empty updates revoke ASR-only admission; a later draft needs120ms of new
 * positive evidence. Insufficient nonempty drafts preserve pending intent;
 * meaningful updates replace it without resetting spent quota. This owner-only
 * operation preserves clocks, votes, strict onset and spent continuation.
 * PHRASE offers up to400ms for sentence pauses, sharing the SAME spent quota
 * as pending/fresh hints. It never refills on a new sentence or new text. */
void agent_endpoint_observe(agent_endpoint_t *,unsigned notice);
/* Only the source-frame owner sets these hints. A cloud proposal must already
 * satisfy its independent source/ASR guard. Local and cloud endings share one
 * per-capture budget; calls without a new frame cannot spend or refill it. */
void agent_endpoint_hint(agent_endpoint_t *,bool pending,unsigned remote_source_ms);
void agent_endpoint_cancel(agent_endpoint_t *);
#endif
