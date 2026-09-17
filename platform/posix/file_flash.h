#ifndef AGENT_POSIX_FLASH_H
#define AGENT_POSIX_FLASH_H
#include "wal.h"
typedef struct { int fd; size_t size; } agent_posix_flash_t;
agent_err_t agent_posix_flash_open(agent_posix_flash_t *,const char *,size_t,agent_flash_ops_t *);
void agent_posix_flash_close(agent_posix_flash_t *);
#endif
