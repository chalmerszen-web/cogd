#ifndef AGENT_SPEECH_DRAFT_H
#define AGENT_SPEECH_DRAFT_H
#include "agent.h"

/* One network owner. Borrowed RAM, no heap/Flash and no playback authority.
 * Reset invalidates all samples; full cache is a miss, never a clipped answer.
 * IMA stores one seed sample then two samples per byte. Optional pages are
 * caller-owned and must cover each whole write; the codec never allocates. */
enum { AGENT_DRAFT_PAGE_BYTES=1024 };
typedef struct {
    uint8_t *data;
    uint8_t **pages;
    size_t capacity,samples,read;
    int16_t first;
    int encoder,index,decoder,decode_index;
    bool full;
} agent_speech_draft_t;
void agent_speech_draft_init(agent_speech_draft_t *,void *,size_t);
void agent_speech_draft_init_pages(agent_speech_draft_t *,uint8_t **,size_t);
void agent_speech_draft_reset(agent_speech_draft_t *);
agent_err_t agent_speech_draft_write(agent_speech_draft_t *,const int16_t *,size_t);
agent_err_t agent_speech_draft_read(agent_speech_draft_t *,int16_t *,size_t,size_t *);
#endif
