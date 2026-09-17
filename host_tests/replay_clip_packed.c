#include "clip.h"
#include <stdio.h>
#include <string.h>

static uint8_t memory[0x70000];
static int16_t pcm[160000],decoded[256];
static unsigned programs,erases;
static agent_err_t read_flash(void *x,size_t at,void *out,size_t n)
{ (void)x;if(at>sizeof(memory) || n>sizeof(memory)-at)return AGENT_ERR_LIMIT;memcpy(out,memory+at,n);return AGENT_OK; }
static agent_err_t write_flash(void *x,size_t at,const void *in,size_t n)
{
    (void)x;if(at>sizeof(memory) || n>sizeof(memory)-at)return AGENT_ERR_LIMIT;
    programs+=(unsigned)((at%256+n+255)/256);const uint8_t *p=in;
    for(size_t i=0;i<n;++i) { if((memory[at+i]&p[i])!=p[i])return AGENT_ERR_STORAGE;memory[at+i]&=p[i]; }
    return AGENT_OK;
}
static agent_err_t erase_flash(void *x,size_t at,size_t n)
{ (void)x;if(at>sizeof(memory) || n>sizeof(memory)-at || at%4096 || n!=4096)return AGENT_ERR_LIMIT;memset(memory+at,255,n);++erases;return AGENT_OK; }
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    FILE *in=fopen(argv[1],"rb");if(!in)return 3;
    if(fseek(in,0,SEEK_END))return 3;long bytes=ftell(in);
    if(bytes<=0 || bytes>320000 || bytes%2 || fseek(in,0,SEEK_SET))return 3;
    size_t count=(size_t)bytes/2;if(fread(pcm,2,count,in)!=count || fclose(in))return 3;
    size_t total=count<1600?1600:count;
    agent_flash_ops_t flash={read_flash,write_flash,erase_flash,NULL,sizeof(memory),4096};agent_clip_t clip;
    memset(memory,255,sizeof(memory));if(agent_clip_open(&clip,&flash)!=AGENT_ERR_NOT_FOUND)return 4;
    if(agent_clip_begin_packed(&clip,(unsigned)((total+15)/16)))return 4;
    size_t consumed=0;
    for(size_t at=0;at<total;) {
        size_t n=at?256:112;if(n>total-at)n=total-at;
        if(agent_clip_write(&clip,pcm+at,n))return 4;at+=n;
        size_t available,stored;agent_clip_progress(&clip,&available,&stored);
        while(available-consumed>=256) {
            if(agent_clip_read_pending(&clip,available,stored,consumed,decoded,256) || memcmp(decoded,pcm+consumed,512))return 5;
            consumed+=256;
        }
    }
    if(agent_clip_finish(&clip))return 6;
    size_t encoded=clip.storage_bytes;
    if(agent_clip_open(&clip,&flash))return 7;
    FILE *out=fopen(argv[2],"wb");if(!out)return 3;
    for(size_t at=0;at<count;) {
        size_t n=count-at;if(n>256)n=256;
        if(agent_clip_read(&clip,at,decoded,n) || memcmp(decoded,pcm+at,n*2) || fwrite(decoded,2,n,out)!=n)return 8;
        at+=n;
    }
    if(fclose(out))return 3;
    printf("{\"samples\":%zu,\"padding\":%zu,\"encoded\":%zu,\"erases\":%u,\"page_programs\":%u,\"exact\":true}\n",count,total-count,encoded,erases,programs);
    return 0;
}
