#ifndef AGENT_WS_READER_H
#define AGENT_WS_READER_H
#include "qianwen.h"

/* Server frames only: no negotiated compression, no masking. The reader keeps
 * incomplete headers/control payloads across calls. Data uses caller memory.
 * read_bytes returns bytes, zero for would-block, negative for terminal error;
 * it MUST preserve a partial TLS record on would-block. One owner, no heap. */
typedef int (*agent_ws_read_bytes_fn)(void *,void *,size_t,unsigned);
typedef struct {
    size_t remaining,length;
    uint8_t header[10],control[125];
    unsigned header_used,header_size,opcode,control_used;
    bool payload,fin,fragmented;
    agent_err_t error;
} agent_ws_reader_t;

agent_err_t agent_ws_reader_next(agent_ws_reader_t *,agent_ws_read_bytes_fn,void *,
    char *,size_t,agent_ws_chunk_t *,unsigned timeout_ms);
#endif
