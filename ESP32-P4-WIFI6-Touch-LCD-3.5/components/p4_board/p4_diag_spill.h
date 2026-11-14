#pragma once
/* Bounded critical overflow storage; callers provide the producer lock. */
#include "p4_diag_format.h"
#include <stdbool.h>
enum { P4_DIAG_CRITICAL_SLOTS=16 };
typedef struct {
    uint64_t at;
    uint16_t size;
    uint8_t flags;
    char text[P4_DIAG_TEXT];
} p4_diag_critical_record_t;
typedef struct {
    p4_diag_critical_record_t records[P4_DIAG_CRITICAL_SLOTS];
    unsigned head,count,peak;
} p4_diag_critical_queue_t;
static inline bool p4_diag_critical_push(p4_diag_critical_queue_t *q,
    uint64_t at,uint8_t flags,const char *text,uint16_t size)
{
    if(size>P4_DIAG_TEXT || q->count>=P4_DIAG_CRITICAL_SLOTS)return false;
    p4_diag_critical_record_t *r=&q->records[(q->head+q->count)%P4_DIAG_CRITICAL_SLOTS];
    r->at=at;r->flags=flags;r->size=size;memcpy(r->text,text,size);
    if(++q->count>q->peak)q->peak=q->count;
    return true;
}
static inline unsigned p4_diag_critical_drain(p4_diag_critical_queue_t *q,
    uint8_t page[P4_DIAG_PAGE],uint32_t *used)
{
    unsigned n=0;
    while(q->count){p4_diag_critical_record_t *r=&q->records[q->head];
        if(!p4_diag_append(page,used,r->at,6,r->flags,r->text,r->size))break;
        q->head=(q->head+1)%P4_DIAG_CRITICAL_SLOTS;--q->count;++n;
    }
    return n;
}
