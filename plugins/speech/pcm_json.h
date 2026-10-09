#ifndef AGENT_PCM_JSON_H
#define AGENT_PCM_JSON_H
#include "json.h"

/* Append base64 for one <=512-sample PCM16 little-endian upload block,
 * without JSON quotes. Both realtime transports use this identical encoder. */
void agent_pcm_json(agent_json_writer_t *,const int16_t *,size_t count);
#endif
