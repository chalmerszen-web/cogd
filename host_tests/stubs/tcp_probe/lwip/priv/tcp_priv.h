#ifndef TEST_TCP_PRIV_H
#define TEST_TCP_PRIV_H
#include <stdint.h>
#define TCP_QUEUE_OOSEQ 1
#define LWIP_IPV6 1
struct pbuf {struct pbuf *next;unsigned tot_len;};
struct tcp_seg {struct tcp_seg *next;struct pbuf *p;unsigned len;};
struct tcp_pcb {
    struct tcp_pcb *next;void *callback_arg;
    unsigned local_port,remote_port,state,rcv_wnd,rcv_ann_wnd,rcv_nxt,nrtx;
    struct pbuf *refused_data;
    struct tcp_seg *ooseq,*unsent,*unacked;
};
extern struct tcp_pcb *tcp_active_pcbs;
#endif
