#ifndef AGENT_WS_FRAME_H
#define AGENT_WS_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AGENT_WS_FRAME_PAYLOAD_MAX 2048u
#define AGENT_WS_FRAME_MAX (AGENT_WS_FRAME_PAYLOAD_MAX+8u)

/* One final, masked RFC6455 client data frame. Caller provides a fresh random
 * mask and non-overlapping output; no partial frame is produced on failure. */
static inline size_t agent_ws_frame_pack(uint8_t *out,size_t capacity,bool binary,
    const void *payload,size_t length,const uint8_t mask[4])
{
    size_t header=length<=125?6:8;
    if(!out || !mask || (!payload && length) || length>AGENT_WS_FRAME_PAYLOAD_MAX ||
       capacity<header+length)return 0;
    out[0]=(uint8_t)(binary?0x82:0x81);
    out[1]=(uint8_t)(length<=125?0x80|length:0xfe);
    if(header==8) {out[2]=(uint8_t)(length>>8);out[3]=(uint8_t)length;}
    for(size_t i=0;i<4;++i)out[header-4+i]=mask[i];
    const uint8_t *source=payload;
    for(size_t i=0;i<length;++i)out[header+i]=source[i]^mask[i&3];
    return header+length;
}
#endif
