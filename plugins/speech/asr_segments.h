#ifndef AGENT_ASR_SEGMENTS_H
#define AGENT_ASR_SEGMENTS_H
#include "json.h"

#define AGENT_ASR_SEGMENT_MAX 8u

/* Caller-owned metadata and transcript, no allocation. IDs identify provider
 * segments; count is only a local ordinal within one isolated input session.
 * Settled means every CURRENT segment has a final, not that the user is done.
 * Nonempty final fragments remain in source order, separated by newlines.
 * Empty final segments retain their identity but add no text/end candidate;
 * the session owner must reject an entirely empty completed input. */
typedef struct {
    char id[65];
    uint8_t flags;
    uint16_t start_ms,end_ms,bytes;
} agent_asr_segment_t;
typedef struct {
    char *text;
    uint16_t capacity,used;
    uint8_t count;
    agent_err_t error;
    agent_asr_segment_t items[AGENT_ASR_SEGMENT_MAX];
} agent_asr_segments_t;
_Static_assert(sizeof(agent_asr_segments_t)<=640,"Keep segment metadata bounded");
typedef struct {
    unsigned revision,end_ms;
    /* Contiguous confirmed prefix of text, in UTF-8 bytes. Later finals do
     * not cross a missing earlier segment. This is not an input endpoint. */
    uint16_t confirmed_bytes;
    bool notify,partial,settled;
} agent_asr_segment_update_t;

void agent_asr_segments_init(agent_asr_segments_t *,char *,size_t);
bool agent_asr_segments_settled(const agent_asr_segments_t *);
agent_err_t agent_asr_segments_receive(agent_asr_segments_t *,const cJSON *,size_t uploaded_samples,
                                      agent_asr_segment_update_t *);
#endif
