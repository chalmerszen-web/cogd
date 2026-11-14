#ifndef P4_USB_EVENTS_H
#define P4_USB_EVENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Callers serialize producers and metadata access. The sole consumer may
 * read the immutable head outside that lock until it pops it. Full queues
 * reject new records; they never overwrite an in-flight record. */
enum { P4_USB_EVENT_SLOTS=32, P4_USB_EVENT_BYTES=2304, P4_USB_EVENT_TTL_MS=1000 };
typedef struct {
    uint64_t at;
    size_t size;
    char text[P4_USB_EVENT_BYTES];
} p4_usb_event_t;
typedef struct {
    unsigned head,count,peak,dropped_records,dropped_bytes,expired;
    p4_usb_event_t slots[P4_USB_EVENT_SLOTS];
} p4_usb_events_t;
static inline bool p4_usb_events_push(p4_usb_events_t *q,const char *text,size_t size,uint64_t at)
{
    if(!size)return true;
    if(size>P4_USB_EVENT_BYTES || q->count==P4_USB_EVENT_SLOTS) {
        ++q->dropped_records;q->dropped_bytes+=(unsigned)size;return false;
    }
    p4_usb_event_t *e=&q->slots[(q->head+q->count)%P4_USB_EVENT_SLOTS];
    e->at=at;e->size=size;memcpy(e->text,text,size);
    if(++q->count>q->peak)q->peak=q->count;
    return true;
}
static inline const p4_usb_event_t *p4_usb_events_head(const p4_usb_events_t *q)
{return q->count?&q->slots[q->head]:NULL;}
static inline bool p4_usb_events_expired(const p4_usb_event_t *e,uint64_t now)
{return now>=e->at && now-e->at>=P4_USB_EVENT_TTL_MS;}
static inline void p4_usb_events_pop(p4_usb_events_t *q,bool expired)
{
    if(!q->count)return;
    if(expired){++q->expired;++q->dropped_records;q->dropped_bytes+=(unsigned)q->slots[q->head].size;}
    q->head=(q->head+1)%P4_USB_EVENT_SLOTS;--q->count;
}
#endif
