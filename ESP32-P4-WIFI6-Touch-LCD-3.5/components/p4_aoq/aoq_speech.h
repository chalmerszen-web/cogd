#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct p4_aoq_speech p4_aoq_speech_t;
typedef struct {size_t length;unsigned opcode;bool first,final;} p4_aoq_speech_chunk_t;
/* Fresh native Inference task. JSON controls use Data; independent ASR PCM
 * uses the documented Data append encoding. TTS media is received on Audio. */
int p4_aoq_speech_open(p4_aoq_speech_t **,bool asr,void *ctx,bool (*cancelled)(void *));
int p4_aoq_speech_send(p4_aoq_speech_t *,bool binary,const void *,size_t);
int p4_aoq_speech_read(p4_aoq_speech_t *,void *,size_t,p4_aoq_speech_chunk_t *,unsigned wait_ms);
void p4_aoq_speech_close(p4_aoq_speech_t *);
#ifdef __cplusplus
}
#endif
