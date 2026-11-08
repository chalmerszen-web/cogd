#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct p4_aoq_conversation p4_aoq_conversation_t;
typedef struct {
    void *ctx;
    bool (*cancelled)(void *);
    /* Optional metadata sink. The owner supplies its atomic, nonblocking
     * console path; the adapter must not write into command replies. */
    void (*diagnostic)(void *,const char *);
    bool video;
} p4_aoq_conversation_options_t;
typedef struct {
    bool pcm;
    size_t bytes;
    char response[129];
} p4_aoq_conversation_message_t;
/* Sole network-worker owner. No audio hardware, user history or tools live in
 * this adapter. It joins media and Data completion before releasing a turn. */
int p4_aoq_conversation_open(p4_aoq_conversation_t **,const p4_aoq_conversation_options_t *);
int p4_aoq_conversation_send(p4_aoq_conversation_t *,const char *,size_t);
int p4_aoq_conversation_pcm(p4_aoq_conversation_t *,const int16_t *,size_t,uint64_t capture_us);
int p4_aoq_conversation_jpeg(p4_aoq_conversation_t *,const uint8_t *,size_t,uint64_t capture_us);
/* Call every worker iteration, even when playback cannot accept more PCM.
 * Receive-window backpressure bounds media without stopping transport ACKs. */
int p4_aoq_conversation_pump(p4_aoq_conversation_t *);
/* Queue-only read: BUSY means no complete message. Never returns a partial object. */
int p4_aoq_conversation_read(p4_aoq_conversation_t *,void *,size_t,p4_aoq_conversation_message_t *);
bool p4_aoq_conversation_pending(const p4_aoq_conversation_t *);
/* Metadata only; a zero detected-loss count is not model-consumption proof. */
void p4_aoq_conversation_input_stats(const p4_aoq_conversation_t *,char *,size_t);
const char *p4_aoq_conversation_stage(const p4_aoq_conversation_t *);
void p4_aoq_conversation_close(p4_aoq_conversation_t *);
#ifdef __cplusplus
}
#endif
