#include "tcp_probe.h"
#include "runtime.h"
#include "lwip/sockets.h"
#include "lwip/api.h"
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

static void socket_values(esp_agent_tcp_side_t *s,int fd)
{
    s->fd=fd;s->available=-1;s->mailbox=-1;
    if(fd<0)return;
    struct sockaddr_storage addr;socklen_t length=sizeof(addr);
    if(getsockname(fd,(struct sockaddr *)&addr,&length))return;
    if(addr.ss_family==AF_INET)s->local=ntohs(((struct sockaddr_in *)&addr)->sin_port);
#if LWIP_IPV6
    else if(addr.ss_family==AF_INET6)s->local=ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
#endif
    length=sizeof(addr);
    if(getpeername(fd,(struct sockaddr *)&addr,&length)) {s->local=0;return;}
    if(addr.ss_family==AF_INET)s->remote=ntohs(((struct sockaddr_in *)&addr)->sin_port);
#if LWIP_IPV6
    else if(addr.ss_family==AF_INET6)s->remote=ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
#endif
    int available=0;
    if(!ioctl(fd,FIONREAD,&available))s->available=available;
}
static unsigned pbufs(const struct pbuf *p)
{ unsigned n=0;for(;p;p=p->next)++n;return n; }
static unsigned segments(const struct tcp_seg *s)
{ unsigned n=0;for(;s;s=s->next)++n;return n; }
static void snapshot_core(void *ctx)
{
    esp_agent_tcp_snapshot_t *snap=ctx;
    /* Pinned IDF/lwIP headers, no copied struct layout. Socket APIs establish
     * the live local/remote ports; only an unambiguous active PCB is accepted.
     * No PCB or netconn pointer escapes the TCPIP core. */
    for(unsigned i=0;i<2;++i) {
        esp_agent_tcp_side_t *s=&snap->side[i];const struct tcp_pcb *selected=NULL;
        if(!s->local || !s->remote)continue;
        for(const struct tcp_pcb *p=tcp_active_pcbs;p;p=p->next)
            if(p->local_port==s->local && p->remote_port==s->remote) {selected=p;++s->matches;}
        if(s->matches!=1)continue;
        s->state=(unsigned)selected->state;s->window=selected->rcv_wnd;
        s->announced=selected->rcv_ann_wnd;s->next=selected->rcv_nxt;
        s->refused_bytes=selected->refused_data?selected->refused_data->tot_len:0;
        s->refused_pbufs=pbufs(selected->refused_data);
#if TCP_QUEUE_OOSEQ
        for(const struct tcp_seg *p=selected->ooseq;p;p=p->next) {
            s->ooo_bytes+=p->len;s->ooo_pbufs+=pbufs(p->p);
        }
#endif
        s->unsent=segments(selected->unsent);s->unacked=segments(selected->unacked);
        s->retries=selected->nrtx;
        /* These two live descriptors were created by the sockets API, whose
         * setup_tcp binds callback_arg to netconn. Check both associations
         * before querying the pinned FreeRTOS port's queue, without peeking
         * or consuming any message. Neither socket owner reads during this
         * callback (WS calls us; HTTP waits for WS to join). */
        const struct netconn *conn=selected->callback_arg;
        if(conn && conn->pcb.tcp==selected && conn->callback_arg.socket==s->fd && conn->recvmbox)
            s->mailbox=(int)uxQueueMessagesWaiting((QueueHandle_t)&conn->recvmbox->os_mbox);
    }
}
void esp_agent_tcp_snapshot(int ws,int http,esp_agent_tcp_snapshot_t *snap)
{
    memset(snap,0,sizeof(*snap));snap->at=(unsigned)esp_agent_now();
    socket_values(&snap->side[0],ws);socket_values(&snap->side[1],http);
    snap->error=tcpip_callback_wait(snapshot_core,snap);
    snap->duration=(unsigned)esp_agent_now()-snap->at;
}
void esp_agent_tcp_format(const esp_agent_tcp_snapshot_t *snap,unsigned side,char *out,size_t capacity)
{
    if(!out || !capacity)return;
    out[0]=0;if(side>1)return;
    const esp_agent_tcp_side_t *s=&snap->side[side];
    int n=snprintf(out,capacity,"{\"at\":%u,\"ms\":%u,\"err\":%d,\"side\":%u,\"fd\":%d,"
        "\"available\":%d,\"mailbox\":%d,\"ports\":[%u,%u],\"matches\":%u,\"state\":%u,"
        "\"window\":%u,\"announced\":%u,\"next\":%u,\"refused\":[%u,%u],\"ooo\":[%u,%u],"
        "\"unsent\":%u,\"unacked\":%u,\"retries\":%u}",snap->at,snap->duration,snap->error,
        side,s->fd,s->available,s->mailbox,s->local,s->remote,s->matches,s->state,s->window,
        s->announced,s->next,s->refused_bytes,s->refused_pbufs,s->ooo_bytes,s->ooo_pbufs,
        s->unsent,s->unacked,s->retries);
    if(n<0 || (size_t)n>=capacity)out[0]=0;
}
