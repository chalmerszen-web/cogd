#define _POSIX_C_SOURCE 200809L
#include "file_flash.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void)
{
    char path[]="/tmp/agent-flash-XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd);
    agent_posix_flash_t file; agent_flash_ops_t ops; agent_wal_t wal;
    assert(!agent_posix_flash_open(&file,path,16384,&ops));
    assert(!agent_wal_format(&wal,&ops)); assert(!agent_wal_snapshot_put(&wal,"snapshot",8)); agent_posix_flash_close(&file);
    assert(!agent_posix_flash_open(&file,path,16384,&ops)); assert(!agent_wal_open(&wal,&ops));
    char data[32]; size_t n; assert(!agent_wal_snapshot_get(&wal,data,sizeof(data),&n)); assert(n==8 && !strcmp(data,"snapshot"));
    agent_posix_flash_close(&file); assert(!unlink(path));
}
