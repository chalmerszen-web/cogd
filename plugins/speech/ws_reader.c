#include "ws_reader.h"
#include <string.h>

static agent_err_t fail(agent_ws_reader_t *r,agent_err_t error)
{ r->error=error;return error; }
static agent_err_t header(agent_ws_reader_t *r)
{
    const uint8_t *h=r->header;
    r->opcode=h[0]&15u;r->fin=(h[0]&128u)!=0;
    if((h[0]&0x70u) || (h[1]&128u) ||
       (r->opcode>2 && r->opcode<8) || r->opcode>10)return AGENT_ERR_PROTOCOL;
    uint32_t n=h[1]&127u;
    if(n>=126) {
        if(n==127 && (h[2] || h[3] || h[4] || h[5]))return AGENT_ERR_LIMIT;
        n=0;
        for(unsigned i=2;i<r->header_size;++i)n=n*256u+h[i];
        if((h[1]==126 && n<126) || (h[1]==127 && n<=65535))return AGENT_ERR_PROTOCOL;
    }
    if(n>24000u*2u*90u)return AGENT_ERR_LIMIT;
    if(r->opcode>=8) {
        if(!r->fin || n>125 || (r->opcode==8 && n==1))return AGENT_ERR_PROTOCOL;
    } else {
        if((r->opcode==0)!=r->fragmented)return AGENT_ERR_PROTOCOL;
        r->fragmented=!r->fin;
    }
    r->length=r->remaining=(size_t)n;r->control_used=0;r->payload=true;
    return AGENT_OK;
}
agent_err_t agent_ws_reader_next(agent_ws_reader_t *r,agent_ws_read_bytes_fn read_bytes,void *ctx,
    char *out,size_t capacity,agent_ws_chunk_t *part,unsigned timeout)
{
    if(!r || !read_bytes || !out || !capacity || !part)return AGENT_ERR_ARGUMENT;
    memset(part,0,sizeof(*part));
    if(r->error)return r->error;
    /* At most two header reads and one payload read. Only the first read may
     * wait; a short header/payload returns immediately with its state intact. */
    while(!r->payload) {
        if(!r->header_size)r->header_size=2;
        size_t want=r->header_size-r->header_used;
        int n=read_bytes(ctx,r->header+r->header_used,want,timeout);timeout=0;
        if(n<0)return fail(r,AGENT_ERR_NETWORK);
        if((size_t)n>want)return fail(r,AGENT_ERR_PROTOCOL);
        r->header_used+=(unsigned)n;
        if(r->header_used<r->header_size)return AGENT_OK;
        if(r->header_size==2 && (r->header[1]&127u)>=126) {
            r->header_size=(r->header[1]&127u)==126?4:10;continue;
        }
        agent_err_t e=header(r);if(e)return fail(r,e);
    }
    bool control=r->opcode>=8,first=r->remaining==r->length;
    if(control && capacity<r->length)return fail(r,AGENT_ERR_LIMIT);
    size_t want=r->remaining;
    if(!control && want>capacity)want=capacity;
    int n=want?read_bytes(ctx,control?(void *)(r->control+r->control_used):(void *)out,want,timeout):0;
    if(n<0)return fail(r,AGENT_ERR_NETWORK);
    if((size_t)n>want)return fail(r,AGENT_ERR_PROTOCOL);
    if(!n && want)return AGENT_OK;
    r->remaining-=(size_t)n;
    if(control) {
        r->control_used+=(unsigned)n;
        if(r->remaining)return AGENT_OK;
        memcpy(out,r->control,r->length);n=(int)r->length;first=true;
    }
    part->first=first;part->opcode=r->opcode;part->length=(size_t)n;
    part->final=r->fin && !r->remaining;
    if(!r->remaining) {r->payload=false;r->header_used=r->header_size=0;}
    return AGENT_OK;
}
