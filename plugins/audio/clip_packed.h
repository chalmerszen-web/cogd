#ifndef AGENT_CLIP_PACKED_H
#define AGENT_CLIP_PACKED_H
#include "clip.h"
#if AGENT_PACKED_CLIP
agent_err_t agent_packed_write(agent_clip_t *,const int16_t *,size_t);
agent_err_t agent_packed_flush(agent_clip_t *);
agent_err_t agent_packed_read(agent_clip_t *,size_t bytes,size_t sample,int16_t *,size_t);
agent_err_t agent_packed_checksum(agent_clip_t *,size_t samples,uint32_t *);
agent_err_t agent_packed_checksum_prefix(agent_clip_t *,size_t total,size_t keep,uint32_t *full,uint32_t *prefix);
agent_err_t agent_packed_verify(agent_clip_t *,uint32_t encoded_crc);
#endif
#endif
