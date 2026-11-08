#ifndef COGD_P4_REPLY_REPAIR_H
#define COGD_P4_REPLY_REPAIR_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Retry only an invalid plain answer, after its media terminator and the
 * exact assistant-item deletion ACK. A real new user turn resets this actor;
 * a late ACK cannot spend the new turn's response ticket. */
typedef enum { P4_REPLY_VALID, P4_REPLY_WAIT_DONE, P4_REPLY_DELETE_DUE,
    P4_REPLY_DELETE_WAIT, P4_REPLY_RETRY_DUE } p4_reply_repair_phase_t;
typedef struct {
    p4_reply_repair_phase_t phase;
    bool attempted;
    unsigned ticket;
    uint64_t at;
    char response[129],item[129];
} p4_reply_repair_t;
static inline bool p4_reply_repair_begin(p4_reply_repair_t *s,unsigned ticket,
    const char *response,const char *item,bool eligible,uint64_t now)
{
    if(!eligible || s->attempted || !response || !*response || strlen(response)>=sizeof(s->response) ||
       !item || !*item || strlen(item)>=sizeof(s->item))return false;
    s->attempted=true;s->ticket=ticket;s->at=now;s->phase=P4_REPLY_WAIT_DONE;
    strcpy(s->response,response);strcpy(s->item,item);return true;
}
static inline void p4_reply_repair_done(p4_reply_repair_t *s,const char *response)
{
    if(s->phase==P4_REPLY_WAIT_DONE && response && !strcmp(response,s->response))s->phase=P4_REPLY_DELETE_DUE;
}
static inline bool p4_reply_repair_deleted(p4_reply_repair_t *s,const char *item)
{
    if(s->phase!=P4_REPLY_DELETE_WAIT || !item || strcmp(item,s->item))return false;
    s->phase=P4_REPLY_RETRY_DUE;return true;
}
static inline bool p4_reply_repair_current(const p4_reply_repair_t *s,unsigned ticket)
{return s->phase!=P4_REPLY_VALID && s->ticket==ticket;}
static inline bool p4_reply_repair_timeout(const p4_reply_repair_t *s,uint64_t now)
{return s->phase!=P4_REPLY_VALID && now-s->at>=10000;}
#endif
