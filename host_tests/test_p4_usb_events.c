#include "p4_usb_events.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    p4_usb_events_t *q=calloc(1,sizeof(*q));assert(q);
    /* Replay a dense admission burst while the console owns USB. These
     * events used to disappear, including the recognized user and job ID. */
    const char *burst[]={"asr_final","energy_accepted","dispersion","expansion",
        "local_tutor_start","job_accepted","local_status","realtime_user"};
    for(unsigned i=0;i<8;i++)assert(p4_usb_events_push(q,burst[i],strlen(burst[i])+1,100+i));
    for(unsigned i=0;i<8;i++) {
        const p4_usb_event_t *e=p4_usb_events_head(q);assert(e);
        assert(!strcmp(e->text,burst[i]));assert(!p4_usb_events_expired(e,150));
        p4_usb_events_pop(q,false);
    }
    assert(!q->count && !q->dropped_records && q->peak==8);
    char max[P4_USB_EVENT_BYTES];memset(max,'x',sizeof(max));
    for(unsigned cycle=0;cycle<100;cycle++) {
        for(unsigned i=0;i<P4_USB_EVENT_SLOTS;i++)assert(p4_usb_events_push(q,max,sizeof(max),200));
        const p4_usb_event_t *head=p4_usb_events_head(q);
        assert(!p4_usb_events_push(q,"overflow",8,201));
        assert(p4_usb_events_head(q)==head && !memcmp(head->text,max,sizeof(max)));
        assert(!p4_usb_events_expired(head,1199));assert(p4_usb_events_expired(head,1200));
        for(unsigned i=0;i<P4_USB_EVENT_SLOTS;i++)p4_usb_events_pop(q,true);
        assert(!p4_usb_events_head(q));
        assert(p4_usb_events_push(q,"reconnected",12,1300));
        assert(!strcmp(p4_usb_events_head(q)->text,"reconnected"));p4_usb_events_pop(q,false);
    }
    assert(q->expired==3200 && q->dropped_records==3300);
    unsigned old=q->dropped_bytes;
    assert(!p4_usb_events_push(q,max,sizeof(max)+1,2000));
    assert(!q->count && q->dropped_records==3301 && q->dropped_bytes==old+sizeof(max)+1);
    free(q);puts("P4 USB burst, blocked-terminal expiry, full-record integrity and reconnect passed");
    return 0;
}
