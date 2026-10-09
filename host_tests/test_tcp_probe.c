/* Value-only diagnostics; mock PCB ABI is NOT an IDF integration proof.
 * Real loopback sockets verify FIONREAD does not consume queued bytes. */
#include "tcp_probe.h"
#include "runtime.h"
#include "lwip/api.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/tcpip.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
struct tcp_pcb *tcp_active_pcbs;
static bool in_core,callback_failure;
static unsigned calls;
static uint64_t ticks;
uint64_t esp_agent_now(void) {ticks+=3;return ticks;}
err_t tcpip_callback_wait(void (*fn)(void *),void *ctx)
{
    assert(!in_core);++calls;if(callback_failure)return -1;
    in_core=true;fn(ctx);in_core=false;return 0;
}
unsigned uxQueueMessagesWaiting(QueueHandle_t queue)
{assert(in_core);return *(unsigned *)queue;}
int main(void)
{
    int listener=socket(AF_INET,SOCK_STREAM,0),client=socket(AF_INET,SOCK_STREAM,0);
    assert(listener>=0 && client>=0);
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(!bind(listener,(struct sockaddr *)&addr,sizeof(addr)) && !listen(listener,1));
    socklen_t length=sizeof(addr);assert(!getsockname(listener,(struct sockaddr *)&addr,&length));
    assert(!connect(client,(struct sockaddr *)&addr,sizeof(addr)));
    int server=accept(listener,NULL,NULL);assert(server>=0);
    unsigned remote=ntohs(addr.sin_port);
    assert(!getsockname(client,(struct sockaddr *)&addr,&length));unsigned local=ntohs(addr.sin_port);
    assert(send(server,"test",4,0)==4);
    char one;assert(recv(client,&one,1,MSG_PEEK)==1); /* synchronize arrival, keep all4 */
    struct pbuf tail={.tot_len=2},head={.next=&tail,.tot_len=8};
    struct tcp_seg last={.p=&tail,.len=20},first={.next=&last,.p=&head,.len=30};
    struct sys_mbox_s mailbox={.os_mbox=6};
    struct tcp_pcb pcb={.local_port=local,.remote_port=remote,.state=4,.rcv_wnd=512,
        .rcv_ann_wnd=500,.rcv_nxt=UINT32_MAX,.refused_data=&head,.ooseq=&first,
        .unsent=&last,.unacked=&first,.nrtx=2};
    struct netconn conn={.pcb.tcp=&pcb,.callback_arg.socket=client,.recvmbox=&mailbox};
    pcb.callback_arg=&conn;tcp_active_pcbs=&pcb;
    esp_agent_tcp_snapshot_t snap;esp_agent_tcp_snapshot(client,-1,&snap);
    const esp_agent_tcp_side_t *s=&snap.side[0];
    assert(calls==1 && !snap.error && snap.duration==3 && s->available==4 && s->mailbox==6);
    assert(s->matches==1 && s->window==512 && s->announced==500 && s->next==UINT32_MAX);
    assert(s->refused_bytes==8 && s->refused_pbufs==2 && s->ooo_bytes==50 && s->ooo_pbufs==3);
    assert(s->unsent==1 && s->unacked==2 && s->retries==2);
    assert(snap.side[1].fd==-1 && snap.side[1].available==-1 && !snap.side[1].matches);
    char json[480];esp_agent_tcp_format(&snap,0,json,sizeof(json));
    assert(strstr(json,"\"available\":4") && strstr(json,"4294967295"));
    esp_agent_tcp_format(&snap,2,json,sizeof(json));assert(!json[0]);
    esp_agent_tcp_format(&snap,0,json,2);assert(!json[0]);
    esp_agent_tcp_format(&snap,0,NULL,0);
    struct tcp_pcb duplicate=pcb;pcb.next=&duplicate;
    esp_agent_tcp_snapshot(client,server,&snap);
    assert(s->matches==2 && !s->window && s->mailbox==-1);
    pcb.next=NULL;conn.callback_arg.socket=-1;
    esp_agent_tcp_snapshot(client,-1,&snap);assert(s->matches==1 && s->mailbox==-1);
    callback_failure=true;esp_agent_tcp_snapshot(client,-1,&snap);
    assert(snap.error==-1 && !s->matches && s->available==4);
    char data[4];assert(recv(client,data,4,0)==4 && !memcmp(data,"test",4));
    close(client);close(server);close(listener);tcp_active_pcbs=NULL;
    puts("TCP snapshots: core ownership, queue values, ambiguity, invalid fd, failures, formatting and nonconsumption OK");
    return 0;
}
