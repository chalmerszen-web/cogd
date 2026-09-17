#define _POSIX_C_SOURCE 200809L
#include "file_flash.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static agent_err_t read_flash(void *ctx,size_t off,void *data,size_t n)
{
    agent_posix_flash_t *f=ctx; if(off>f->size || n>f->size-off) return AGENT_ERR_ARGUMENT;
    char *p=data;
    while(n) {
        ssize_t count=pread(f->fd,p,n,(off_t)off);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) return AGENT_ERR_STORAGE;
        off+=(size_t)count; p+=count; n-=(size_t)count;
    }
    return AGENT_OK;
}
static agent_err_t write_bytes(agent_posix_flash_t *f,size_t off,const void *data,size_t n)
{
    const char *p=data;
    while(n) {
        ssize_t count=pwrite(f->fd,p,n,(off_t)off);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) return AGENT_ERR_STORAGE;
        off+=(size_t)count; p+=count; n-=(size_t)count;
    }
    return fsync(f->fd)==0 ? AGENT_OK : AGENT_ERR_STORAGE;
}
static agent_err_t write_flash(void *ctx,size_t off,const void *data,size_t n)
{
    agent_posix_flash_t *f=ctx; const unsigned char *p=data;
    unsigned char previous[256];
    if(off>f->size || n>f->size-off) return AGENT_ERR_ARGUMENT;
    for(size_t i=0;i<n;) {
        size_t count=n-i; if(count>sizeof(previous)) count=sizeof(previous);
        agent_err_t e=read_flash(ctx,off+i,previous,count); if(e) return e;
        for(size_t j=0;j<count;++j) if((previous[j]&p[i+j])!=p[i+j]) return AGENT_ERR_STORAGE;
        i+=count;
    }
    return write_bytes(f,off,data,n);
}
static agent_err_t erase_flash(void *ctx,size_t off,size_t n)
{
    agent_posix_flash_t *f=ctx; unsigned char blank[4096]; memset(blank,255,sizeof(blank));
    if(off%4096 || n%4096 || off>f->size || n>f->size-off) return AGENT_ERR_ARGUMENT;
    while(n) { agent_err_t e=write_bytes(f,off,blank,sizeof(blank)); if(e) return e; off+=sizeof(blank); n-=sizeof(blank); }
    return AGENT_OK;
}
agent_err_t agent_posix_flash_open(agent_posix_flash_t *f,const char *path,size_t size,agent_flash_ops_t *ops)
{
    if(!size || size%8192) return AGENT_ERR_ARGUMENT;
    f->fd=open(path,O_RDWR|O_CREAT,0600); f->size=size; if(f->fd<0) return AGENT_ERR_STORAGE;
    struct stat info;
    if(fstat(f->fd,&info)<0 || (info.st_size && (uint64_t)info.st_size!=size)) { agent_posix_flash_close(f); return AGENT_ERR_CORRUPT; }
    if(!info.st_size) {
        if(ftruncate(f->fd,(off_t)size)<0 || erase_flash(f,0,size)) { agent_posix_flash_close(f); return AGENT_ERR_STORAGE; }
    }
    *ops=(agent_flash_ops_t){read_flash,write_flash,erase_flash,f,size,4096}; return AGENT_OK;
}
void agent_posix_flash_close(agent_posix_flash_t *f) { if(f->fd>=0) close(f->fd); f->fd=-1; }
