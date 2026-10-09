/* Offline reader: use the production parser without permitting Flash writes. */
#include "clip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t image[0x70000];
static agent_err_t read_image(void *ctx,size_t at,void *out,size_t n)
{
    (void)ctx;
    if(at>sizeof(image) || n>sizeof(image)-at)return AGENT_ERR_LIMIT;
    memcpy(out,image+at,n);return AGENT_OK;
}
static agent_err_t reject_write(void *ctx,size_t at,const void *in,size_t n)
{ (void)ctx;(void)at;(void)in;(void)n;return AGENT_ERR_STORAGE; }
static agent_err_t reject_erase(void *ctx,size_t at,size_t n)
{ (void)ctx;(void)at;(void)n;return AGENT_ERR_STORAGE; }

int main(int argc,char **argv)
{
    if((argc!=3 && argc!=4) || !strcmp(argv[1],argv[2]))return 2;
    unsigned block=256;
    if(argc==4) {
        char *end;unsigned long value=strtoul(argv[3],&end,10);
        if(!*argv[3] || *end || !value || value>512)return 2;
        block=(unsigned)value;
    }
    FILE *in=fopen(argv[1],"rb");if(!in)return 3;
    size_t bytes=fread(image,1,sizeof(image),in);int extra=fgetc(in);
    bool failed=ferror(in)!=0;
    if(fclose(in) || failed || bytes!=sizeof(image) || extra!=EOF)return 3;
    agent_flash_ops_t flash={read_image,reject_write,reject_erase,NULL,sizeof(image),4096};
    agent_clip_t clip;agent_err_t error=agent_clip_open(&clip,&flash);
    if(error) {fprintf(stderr,"clip_error=%d\n",error);return 4;}
    size_t encoded_samples=clip.reader.sample;
    FILE *out=fopen(argv[2],"wb");if(!out)return 3;
    int16_t pcm[512];
    for(size_t at=0;at<clip.samples;) {
        size_t n=clip.samples-at;if(n>block)n=block;
        error=agent_clip_read(&clip,at,pcm,n);
        if(error) {fclose(out);return 5;}
        for(size_t i=0;i<n;++i) {
            if(fputc((uint8_t)pcm[i],out)==EOF || fputc((uint16_t)pcm[i]>>8,out)==EOF) {
                fclose(out);return 3;
            }
        }
        at+=n;
    }
    if(fclose(out))return 3;
    printf("{\"samples\":%zu,\"encoded_samples\":%zu,\"storage_bytes\":%zu,"
           "\"packed_version\":%u,\"block\":%u,\"committed\":true}\n",
           clip.samples,encoded_samples,clip.storage_bytes,clip.packed_version,block);
    return 0;
}
