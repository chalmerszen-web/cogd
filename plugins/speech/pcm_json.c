#include "pcm_json.h"

void agent_pcm_json(agent_json_writer_t *w,const int16_t *pcm,size_t count)
{
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if(w->error)return;
    if((!pcm && count) || count>512) {w->error=AGENT_ERR_ARGUMENT;return;}
    for(size_t i=0;i<count*2;i+=3) {
        unsigned value=0;size_t left=count*2-i;unsigned bytes=left<3?(unsigned)left:3;
        for(unsigned j=0;j<bytes;++j) {
            size_t at=i+j;
            value|=((uint32_t)(uint16_t)pcm[at/2]>>((at&1)*8)&255u)<<(16-8*j);
        }
        char encoded[4]={alphabet[value>>18],alphabet[(value>>12)&63],
            bytes>1?alphabet[(value>>6)&63]:'=',bytes>2?alphabet[value&63]:'='};
        if(agent_json_write_bytes(w,encoded,sizeof(encoded)))return;
    }
}
