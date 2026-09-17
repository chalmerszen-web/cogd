#ifndef AGENT_FLASH_H
#define AGENT_FLASH_H
#include "agent.h"
/* NOR operations are synchronous; writes only clear bits. */
typedef struct {
    agent_err_t (*read)(void *,size_t,void *,size_t);
    agent_err_t (*write)(void *,size_t,const void *,size_t);
    agent_err_t (*erase)(void *,size_t,size_t);
    void *ctx;
    size_t size,sector;
} agent_flash_ops_t;
#endif
