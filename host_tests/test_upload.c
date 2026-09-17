#include "upload.h"
#include "crc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    agent_upload_t u={0}; char buffer[4097]; uint32_t crc=agent_crc32("abc",3);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),3,crc,100));
    assert(agent_upload_begin(&u,buffer,sizeof(buffer),3,crc,100)==AGENT_ERR_BUSY);
    assert(!agent_upload_hex(&u,0,"61",110));
    assert(!agent_upload_hex(&u,1,"6263",120));
    assert(!agent_upload_finish(&u,130) && !u.active && !strcmp(buffer,"abc"));
    agent_upload_abort(&u); assert(!buffer[0]);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),3,crc,0));
    assert(agent_upload_hex(&u,1,"61",1)==AGENT_ERR_ARGUMENT && !u.active);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),3,crc,0));
    assert(agent_upload_hex(&u,0,"61ff0z",1)==AGENT_ERR_ARGUMENT && !buffer[0]);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),3,crc,0));
    assert(!agent_upload_hex(&u,0,"61",1));
    assert(agent_upload_finish(&u,2)==AGENT_ERR_PROTOCOL && !buffer[0]);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),3,crc,0));
    assert(!agent_upload_hex(&u,0,"616263",1));
    assert(agent_upload_finish(&u,15000)==AGENT_ERR_TIMEOUT && !buffer[0]);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),3,crc+1,0));
    assert(!agent_upload_hex(&u,0,"616263",1));
    assert(agent_upload_finish(&u,2)==AGENT_ERR_CORRUPT && !buffer[0]);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),3,agent_crc32("a\0b",3),0));
    assert(!agent_upload_hex(&u,0,"610062",1));
    assert(agent_upload_finish(&u,2)==AGENT_ERR_JSON && !buffer[0]);
    assert(agent_upload_begin(&u,buffer,sizeof(buffer),4097,0,0)==AGENT_ERR_LIMIT);
    memset(buffer,'x',4096); crc=agent_crc32(buffer,4096);
    assert(!agent_upload_begin(&u,buffer,sizeof(buffer),4096,crc,0));
    char chunk[513]; for(unsigned i=0;i<256;++i) memcpy(chunk+i*2,"78",2); chunk[512]=0;
    for(unsigned i=0;i<4096;i+=256) assert(!agent_upload_hex(&u,i,chunk,i));
    assert(!agent_upload_finish(&u,5000));
    agent_upload_abort(&u); for(unsigned i=0;i<4097;++i) assert(!buffer[i]);
    puts("upload: shared-buffer transaction, limits, offsets, CRC, timeout, NUL and cancellation PASS");
}
