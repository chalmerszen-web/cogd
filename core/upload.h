#ifndef AGENT_UPLOAD_H
#define AGENT_UPLOAD_H
#include "agent.h"
typedef struct {
    char *data;
    size_t length,used;
    uint32_t crc;
    uint64_t deadline;
    bool active;
} agent_upload_t;
agent_err_t agent_upload_begin(agent_upload_t *,char *,size_t,size_t,uint32_t,uint64_t);
agent_err_t agent_upload_hex(agent_upload_t *,size_t,const char *,uint64_t);
agent_err_t agent_upload_finish(agent_upload_t *,uint64_t);
void agent_upload_abort(agent_upload_t *);
#endif
