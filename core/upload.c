#include "upload.h"
#include "crc.h"
#include "json.h"
#include <string.h>

agent_err_t agent_upload_begin(agent_upload_t *u,char *data,size_t capacity,size_t length,uint32_t crc,uint64_t now)
{
    if(!u || !data || !length || length>AGENT_ARGS_MAX || capacity<=length) return AGENT_ERR_LIMIT;
    if(u->active) return AGENT_ERR_BUSY;
    *u=(agent_upload_t){.data=data,.length=length,.crc=crc,.deadline=now+15000,.active=true};
    data[0]=0; return AGENT_OK;
}
static int nibble(char ch)
{
    if(ch>='0' && ch<='9') return ch-'0';
    if(ch>='a' && ch<='f') return ch-'a'+10;
    if(ch>='A' && ch<='F') return ch-'A'+10;
    return -1;
}
void agent_upload_abort(agent_upload_t *u)
{
    if(!u) return;
    if(u->data) memset(u->data,0,u->used+1);
    memset(u,0,sizeof(*u));
}
agent_err_t agent_upload_hex(agent_upload_t *u,size_t offset,const char *hex,uint64_t now)
{
    if(!u || !u->active || !hex) return AGENT_ERR_ARGUMENT;
    size_t length=strlen(hex); agent_err_t error=AGENT_OK;
    if(now>=u->deadline) error=AGENT_ERR_TIMEOUT;
    else if(!length || length%2 || length>512 || offset!=u->used || length/2>u->length-u->used) error=AGENT_ERR_ARGUMENT;
    else for(size_t i=0;i<length;++i) if(nibble(hex[i])<0) { error=AGENT_ERR_ARGUMENT; break; }
    if(error) { agent_upload_abort(u); return error; }
    for(size_t i=0;i<length;i+=2) u->data[u->used++]=(char)(nibble(hex[i])*16+nibble(hex[i+1]));
    u->data[u->used]=0; return AGENT_OK;
}
agent_err_t agent_upload_finish(agent_upload_t *u,uint64_t now)
{
    if(!u || !u->active) return AGENT_ERR_ARGUMENT;
    agent_err_t error=now>=u->deadline?AGENT_ERR_TIMEOUT:u->used!=u->length?AGENT_ERR_PROTOCOL:
        agent_crc32(u->data,u->used)!=u->crc?AGENT_ERR_CORRUPT:
        memchr(u->data,0,u->used) || !agent_utf8_valid(u->data,u->used)?AGENT_ERR_JSON:AGENT_OK;
    if(error) agent_upload_abort(u); else u->active=false; /* Caller owns validated bytes until consumed. */
    return error;
}
